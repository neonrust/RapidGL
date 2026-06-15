#include "clustered_shading.h"
#include "filesystem.h"
#include "gl_lookup.h"
#include "hash_combine.h"
#include "input.h"
#include "instance_attributes.h"
#include "constants.h"
#include "light_wrapper.h"
#include "log.h"
#include "postprocess.h"
#include "util.h"
#include "gui/gui.h"   // IWYU pragma: keep

#include "component_model.h"
#include "component_transform.h"
#include "component_light_general.h"
#include "component_light_spot.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/random.hpp>
// #include <glm/gtx/string_cast.hpp>  // glm::to_string

#include <chrono>
#include <ranges>
#include <vector>

using namespace std::chrono;
using namespace std::literals;

// testing variables
static float s_spot_outer_angle = 30.f;
static float s_spot_intensity = 2000.f;

static constexpr auto s_relevant_lights_update_min_interval = 250ms;

// light/shadow distances as fraction of camera far plane (OR of furthest shading cluster? should be the same though...)
//  must be in this order
static constexpr auto s_light_relevant_fraction      = 0.6f;  // input to cluster light culling (and by extension, everything else)
// TODO: "relevant" per light type?  ordered by computational cost:
//   point, spot, disc, { sphere, rect, tube }
static constexpr auto s_light_affect_fraction        = 0.5f;  // fade shading by distance
static constexpr auto s_light_volumetric_fraction    = 0.2f;  // fade volumetric/scattering by distance
static constexpr auto s_light_shadow_max_fraction    = 0.4f;  // may allocated  sdhadow map
static constexpr auto s_light_shadow_affect_fraction = 0.3f;  // fade shadow by distance
static constexpr auto s_light_specular_fraction      = 0.1f;  // how far from the light speculars are calculated

static_assert(s_light_relevant_fraction > 0.f and s_light_relevant_fraction <= 1.f);
static_assert(s_light_affect_fraction > 0.f and s_light_affect_fraction <= 1.f);
static_assert(s_light_volumetric_fraction > 0.f and s_light_volumetric_fraction <= 1.f);
static_assert(s_light_shadow_max_fraction > 0.f and s_light_shadow_max_fraction <= 1.f);
static_assert(s_light_shadow_affect_fraction > 0.f and s_light_shadow_affect_fraction <= 1.f);
static_assert(s_light_specular_fraction > 0.f and s_light_specular_fraction <= 1.f);

static_assert(s_light_relevant_fraction > s_light_affect_fraction);
static_assert(s_light_affect_fraction > s_light_shadow_max_fraction);
static_assert(s_light_affect_fraction > s_light_volumetric_fraction);
static_assert(s_light_shadow_max_fraction > s_light_shadow_affect_fraction);

glm::mat3 make_common_space_from_direction(const glm::vec3 &direction)
{
	glm::vec3 space_x;
	glm::vec3 space_y;
	glm::vec3 space_z = direction;
	if(space_z == AXIS_Y)
	{
		space_y = glm::cross(AXIS_X, space_z);
		space_x = glm::cross(space_z, space_y);
	}
	else
	{
		space_y = glm::cross(AXIS_Y, space_z);
		space_x = glm::cross(space_z, space_y);
	}

	return glm::mat3{ space_x, space_y, space_z };
}


using namespace RGL;

ClusteredShading::ClusteredShading() :
	_scene(_entities),
	_light_mgr(_entities),
	_shadow_atlas(8192, _light_mgr),
	m_cluster_aabb_ssbo("cluster-aabb"sv),
	m_cluster_discovery_ssbo("cluster-discovery"sv),
	m_cull_lights_args_ssbo("cull-lights"sv),
	m_cluster_light_ranges_ssbo("cluster-lights"sv),
	m_cluster_all_lights_index_ssbo("cluster-all-lights"sv),
	m_affecting_lights_bitfield_ssbo("affecting-lights-bitfield"sv),
	_relevant_lights_index_ssbo("relevant-lights-index"sv),
	m_shadow_map_slots_ssbo("shadow-map-slots"sv),
	m_gamma               (2.2f),
	_ibl_mip_level        (1.2f),
	m_skybox_vao          (0),
	m_skybox_vbo          (0),
	m_bloom_threshold     (1.5f),
	m_bloom_knee          (0.1f),
	m_bloom_intensity     (0.9f),
	m_bloom_dirt_intensity(0),
	_fog_strength         (0.3f),
	_fog_density          (0.1f),    // [ 0, 1 ]
	_fog_blend_weight     (0.95f)    // [ 0, 1 ]
{
	m_cluster_aabb_ssbo.bindAt(SSBO_BIND_CLUSTER_AABB);
	m_shadow_map_slots_ssbo.bindAt(SSBO_BIND_SHADOW_SLOTS_INFO);
	m_cluster_discovery_ssbo.bindAt(SSBO_BIND_CLUSTER_DISCOVERY);
	m_cluster_light_ranges_ssbo.bindAt(SSBO_BIND_CLUSTER_LIGHT_RANGE);
	m_cluster_all_lights_index_ssbo.bindAt(SSBO_BIND_CLUSTER_ALL_LIGHTS);
	m_affecting_lights_bitfield_ssbo.bindAt(SSBO_BIND_AFFECTING_LIGHTS_BITFIELD);
	m_cull_lights_args_ssbo.bindAt(SSBO_BIND_CULL_LIGHTS_ARGS);
	_relevant_lights_index_ssbo.bindAt(SSBO_BIND_RELEVANT_LIGHTS_INDEX);

	_affecting_lights.reserve(256);
	_lightsPvs.reserve(1024);

	m_volumetrics_pp.setEnabled(false);
	m_bloom_pp.setEnabled(false);

	_light_mgr.set_falloff_power(50.f);
	_light_mgr.set_radius_power(0.6f);

	_gl_timers.reserve(16);
}

ClusteredShading::~ClusteredShading()
{
	if(m_skybox_vao)
    {
        glDeleteVertexArrays(1, &m_skybox_vao);
        m_skybox_vao = 0;
    }

	if(m_skybox_vbo)
    {
        glDeleteBuffers(1, &m_skybox_vbo);
        m_skybox_vbo = 0;
    }

	if(m_debug_draw_vbo)
	{
		glDeleteBuffers(1, &m_debug_draw_vbo);
		m_debug_draw_vbo = 0;
	}
}

void opengl_message_callback(GLenum /*source*/, GLenum type, GLuint /*id*/, GLenum severity, GLsizei /*len*/, const GLchar *message, const void *handler)
{
	if(severity == GL_DEBUG_SEVERITY_NOTIFICATION)
		return;
	const auto *app = static_cast<const ClusteredShading *>(handler);
	app->debug_message(type, gl_lookup::enum_name(severity).substr(18), message);
}

void ClusteredShading::init_app()
{
	GLint flags;
	glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
	if(flags & GL_CONTEXT_FLAG_DEBUG_BIT)
	{
		glEnable(GL_DEBUG_OUTPUT);
		glDebugMessageCallback(opengl_message_callback, this);
		glDebugMessageControl(GL_DONT_CARE /*source*/, GL_DONT_CARE /*type*/, GL_DEBUG_SEVERITY_MEDIUM /*severity*/, 0 /*count*/, nullptr /*ids*/, GL_TRUE /*enabled*/);
	}

    /// Initialize all the variables, buffers, etc. here.
	glClearColor(0, 0, 0, 1);
	glClearDepth(1.f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

	// glLineWidth(2.f); // for wireframes (but >1 not commonly supported)

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	glCreateVertexArrays(1, &_empty_vao);

	// Create camera
	m_camera = Camera(m_camera_fov, 0.1f, 200);
	m_camera.setSize(Window::width(), Window::height());
	m_camera.setPosition({ 0.f, 0.f, 0.f });
	m_camera.setOrientationEuler({ 0, 0, 0 });
	// shadow bias study:
	// m_camera.setPosition({ 5.6f, 10.4f, -0.9f });
	// m_camera.setOrientationEuler({ -19.f, 65.8f, 0 });
	// rect light fog artifact study
	// m_camera.setPosition({ -8.6f, 2.4f, 10.5f });
	// m_camera.setOrientationEuler({ 3.f, -90.f, 0 });
	// csm shadows study
	// m_camera.setPosition({ 15.4f, 3.7f, 7.9f });
	// m_camera.setOrientationEuler({ 9.f, -118.f, 0 });
	m_camera.setPosition({ 0.f, 8.f, 0.f });
	m_camera.setOrientationEuler({ 0.f, -90.f, 0 });



	Log::debug("Horizontal FOV: {}", m_camera.horizontalFov());

	/// Randomly initialize lights (predictably)
	::srand(3281533);//3281991);
	// m_light_counts_ubo.clear();
	createLights();

    /// Prepare lights' SSBOs.
	updateLightsSSBOs();  // initial update will create the GL buffers


	// _csm_shadow_maps = std::make_shared<Texture2DArray>();
	// _csm_shadow_maps->Create(4096, 4096, 4, GL_DEPTH_COMPONENT32F, 0);
	// assert(*_csm_shadow_maps);

    /// Create shaders.
	const fs::path core_shaders = "resources/shaders/";
	const fs::path shaders = "src/demos/27_clustered_shading/shaders/";

	Util::AddShaderSearchPath(core_shaders);

	const auto T0 = steady_clock::now();

	m_depth_prepass_shader = std::make_shared<Shader>(core_shaders/"depth_pass.vert", core_shaders/"depth_pass.frag");
    m_depth_prepass_shader->link();
	assert(*m_depth_prepass_shader);

	m_shadow_depth_shader = std::make_shared<Shader>(core_shaders/"shadow_depth.vert", core_shaders/"shadow_depth.frag");
	m_shadow_depth_shader->link();
	assert(*m_shadow_depth_shader);

	m_generate_clusters_shader = std::make_shared<Shader>(core_shaders/"clustered_generate.comp");
	m_generate_clusters_shader->link();
	assert(*m_generate_clusters_shader);

	m_find_nonempty_clusters_shader = std::make_shared<Shader>(core_shaders/"clustered_find_nonempty.comp");
	m_find_nonempty_clusters_shader->link();
	assert(*m_find_nonempty_clusters_shader);
	m_find_nonempty_clusters_shader->setPostBarrier(Shader::Barrier::SSBO);  // config, only once

	m_collect_nonempty_clusters_shader = std::make_shared<Shader>(core_shaders/"clustered_collect_nonempty.comp");
	m_collect_nonempty_clusters_shader->link();
	assert(*m_collect_nonempty_clusters_shader);
	m_collect_nonempty_clusters_shader->setPostBarrier(Shader::Barrier::SSBO);  // config, only once

	m_cull_lights_shader = std::make_shared<Shader>(core_shaders/"clustered_cull.comp");
    m_cull_lights_shader->link();
	assert(*m_cull_lights_shader);
	m_cull_lights_shader->setPostBarrier(Shader::Barrier::SSBO);  // config, only once

	m_clustered_pbr_shader = std::make_shared<Shader>(core_shaders/"pbr_lighting.vert", core_shaders/"pbr_clustered.frag");
    m_clustered_pbr_shader->link();
	assert(*m_clustered_pbr_shader);
	// set some defaults
	m_clustered_pbr_shader->setUniform("u_specular_max_distance"sv, m_camera.farPlane()*s_light_specular_fraction);
	m_clustered_pbr_shader->setUniform("u_debug_unshaded_clusters"sv, false);

	m_light_geometry_shader = std::make_shared<Shader>(core_shaders/"surface_light_geom.vert", core_shaders/"surface_light_geom.frag");
	m_light_geometry_shader->link();
	assert(*m_light_geometry_shader);

	m_equirectangular_to_cubemap_shader = std::make_shared<Shader>(shaders/"cubemap.vert", shaders/"equirectangular_to_cubemap.frag");
    m_equirectangular_to_cubemap_shader->link();
	assert(*m_equirectangular_to_cubemap_shader);

	m_irradiance_convolution_shader = std::make_shared<Shader>(shaders/"cubemap.vert", shaders/"irradiance_convolution.frag");
    m_irradiance_convolution_shader->link();
	assert(*m_irradiance_convolution_shader);

	m_prefilter_env_map_shader = std::make_shared<Shader>(shaders/"cubemap.vert", shaders/"prefilter_cubemap.frag");
    m_prefilter_env_map_shader->link();
	assert(*m_prefilter_env_map_shader);

	m_precompute_brdf = std::make_shared<Shader>(core_shaders/"FSQ.vert", shaders/"precompute_brdf.frag");
    m_precompute_brdf->link();
	assert(*m_precompute_brdf);

	m_background_shader = std::make_shared<Shader>(core_shaders/"background.vert", core_shaders/"background.frag");
    m_background_shader->link();
	assert(*m_background_shader);

	// Post-processing steps
	m_tmo_pp.create();
	assert(m_tmo_pp);

	m_bloom_pp.create();
	assert(m_bloom_pp);

	m_volumetrics_pp.create();
	assert(m_volumetrics_pp);

	m_blur3_pp.create(Window::width(), Window::height());
	assert(m_blur3_pp);

	m_line_draw_shader = std::make_shared<Shader>(core_shaders/"line_draw.vert", core_shaders/"line_draw.frag");
	m_line_draw_shader->link();
	assert(*m_line_draw_shader);

	m_2d_line_shader = std::make_shared<Shader>(core_shaders/"FSQ.vert", core_shaders/"draw2d_line.frag");
	m_2d_line_shader->link();
	assert(*m_2d_line_shader);
	m_2d_line_shader->setUniform("u_screen_size"sv, glm::uvec2{ Window::width(), Window::height() });
	m_2d_line_shader->setUniform("u_line_color"sv, glm::vec4(1));
	m_2d_line_shader->setUniform("u_thickness"sv, float(Window::height())/720.f);

	m_2d_rect_shader = std::make_shared<Shader>(core_shaders/"FSQ.vert", core_shaders/"draw2d_rectangle.frag");
	m_2d_rect_shader->link();
	assert(*m_2d_rect_shader);
	m_2d_rect_shader->setUniform("u_screen_size"sv, glm::uvec2{ Window::width(), Window::height() });
	m_2d_rect_shader->setUniform("u_line_color"sv, glm::vec4(1));
	m_2d_rect_shader->setUniform("u_thickness"sv, float(Window::height())/720.f);

	m_2d_7segment_shader = std::make_shared<Shader>(core_shaders/"FSQ.vert", core_shaders/"seven_segment_number.frag");
	m_2d_7segment_shader->link();
	assert(*m_2d_7segment_shader);
	m_2d_7segment_shader->setUniform("u_screen_size"sv, glm::uvec2{ Window::width(), Window::height() });
	m_2d_7segment_shader->setUniform("u_color"sv, glm::vec4(1));
	m_2d_7segment_shader->setUniform("u_thickness"sv, float(Window::height())/720.f);

	m_icon_shader = std::make_shared<Shader>(core_shaders/"billboard-icon.vert", core_shaders/"billboard-icon.frag");
	m_icon_shader->link();
	assert(*m_icon_shader);

	m_imgui_depth_texture_shader = std::make_shared<Shader>(core_shaders/"imgui_depth_image.vert", core_shaders/"imgui_depth_image.frag");
	m_imgui_depth_texture_shader->link();
	assert(*m_imgui_depth_texture_shader);
	m_imgui_depth_texture_shader->setUniform("u_brightness"sv, 1.f);

	m_imgui_3d_texture_shader = std::make_shared<Shader>(core_shaders/"imgui_3d_texture.vert", core_shaders/"imgui_3d_texture.frag");
	m_imgui_3d_texture_shader->link();
	assert(*m_imgui_3d_texture_shader);

	m_fsq_shader = std::make_shared<Shader>(core_shaders/"FSQ.vert", core_shaders/"FSQ.frag");
	m_fsq_shader->link();
	assert(*m_fsq_shader);

	const auto T1 = steady_clock::now();
	const auto shader_init_time = duration_cast<microseconds>(T1 - T0);
	Log::info("Shader init time: {:.1f} ms", float(shader_init_time.count())/1000.f);


	/// Load LTC look-up-tables for rect lights rendering
	const auto ltc_lut_path     = FileSystem::getResourcesPath() / "lut";
	const auto ltc_lut_mat_path = ltc_lut_path / "ltc_mat.dds";
	const auto ltc_lut_amp_path = ltc_lut_path / "ltc_amp.dds";

	m_ltc_mat_lut = std::make_shared<Texture2D>();
	if (m_ltc_mat_lut->LoadDds(ltc_lut_mat_path))
	{
		m_ltc_mat_lut->SetWrapping (TextureWrappingAxis::U,    TextureWrappingParam::ClampToEdge);
		m_ltc_mat_lut->SetWrapping (TextureWrappingAxis::V,    TextureWrappingParam::ClampToEdge);
		m_ltc_mat_lut->SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::Nearest);
		m_ltc_mat_lut->SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
	}
	else
		Log::error("Could not load texture {}", ltc_lut_mat_path.string());

	m_ltc_amp_lut = std::make_shared<Texture2D>();
	if (m_ltc_amp_lut->LoadDds(ltc_lut_amp_path))
	{
		m_ltc_amp_lut->SetWrapping (TextureWrappingAxis::U,    TextureWrappingParam::ClampToEdge);
		m_ltc_amp_lut->SetWrapping (TextureWrappingAxis::V,    TextureWrappingParam::ClampToEdge);
		m_ltc_amp_lut->SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::Nearest);
		m_ltc_amp_lut->SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
	}
	else
		Log::error("Could not load texture %s", ltc_lut_amp_path.string());

	namespace C = RenderTarget::Color;
	namespace D = RenderTarget::Depth;

	// Create depth pre-pass render target
	m_depth_pass_rt.create("depth-pass", Window::width(), Window::height(), C::None, D::Texture);

	_rt.create("rt", Window::width(), Window::height());
	_rt.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipNearest); // not necessary?

	static constexpr auto pp_downscale = 2;
	_pp_low_rt.create("pp-low", Window::width() / pp_downscale, Window::height() / pp_downscale, C::HalfFloat | C::Texture, D::None);
	_pp_low_rt.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipNearest); // not necessary?

	_pp_full_rt.create("pp-full", Window::width(), Window::height(), C::Default, D::None);
	_pp_full_rt.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipNearest); // not necessary?

	// TODO: final_rt.cloneFrom(_rt);
	_final_rt.create("final", Window::width(), Window::height(), C::Default, D::None);
	_final_rt.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipNearest); // not necessary?

	// IBL precomputations.
    GenSkyboxGeometry();

	m_env_cubemap_rt = std::make_shared<RenderTarget::Cube>();
	m_env_cubemap_rt->create("env", 2048, 2048);

	_shadow_atlas.create();

	m_brdf_lut_rt = std::make_shared<RenderTarget::Texture2d>();
	m_brdf_lut_rt->create("brdf-lut", 512, 512, C::Texture | C::Float2);

	m_irradiance_cubemap_rt = std::make_shared<RenderTarget::Cube>();
    m_irradiance_cubemap_rt->set_position(glm::vec3(0.0));
	m_irradiance_cubemap_rt->create("irradiance", 32, 32);

	m_prefiltered_env_map_rt = std::make_shared<RenderTarget::Cube>();
    m_prefiltered_env_map_rt->set_position(glm::vec3(0.0));
	m_prefiltered_env_map_rt->create("prefiltered-env", 512, 512);

	_light_icons.Load(FileSystem::getResourcesPath() / "icons" / "lights.array");
	assert(_light_icons);

	PrecomputeIndirectLight(FileSystem::getResourcesPath() / "textures" / "skyboxes" / "IBL" / m_hdr_maps_names[m_current_hdr_map_idx]);
    PrecomputeBRDF(m_brdf_lut_rt);

	calculateShadingClusterGrid();  // will also call prepareClusterBuffers()

	generateRandomAngles(_random_angles, 64);

	glGenBuffers(1, &m_debug_draw_vbo);


	const auto models_path = FileSystem::getResourcesPath() / "models";

	{
		const auto origin = glm::mat4(1);

		const auto light_meshes = models_path / "lights";

		for(const auto light_type: { LightType::Rect, LightType::Tube, LightType::Sphere, LightType::Disc })
		{
			auto filename = std::format("{}.gltf", _light_mgr.type_name(uint_fast8_t(light_type)));
			auto model = std::make_shared<StaticModel>();
			model->Load(light_meshes / filename);
			assert(*model);
			_lightModels.emplace_back(model, origin);
		}
		Log::info("Loaded {} light geometries", _lightModels.size());
	}

	loadScene("test");
}

void ClusteredShading::calculateShadingClusterGrid()
{
	const auto cluster_count_before = m_cluster_count;

	// TODO: these should be properties related to the camera  (a component!)

	static constexpr uint32_t screen_division = 16;    // more than 20 might be overkill
	static constexpr float    depth_scale     = 1.f;   // default 1



	m_cluster_resolution.x = screen_division;

	m_cluster_block_size = uint32_t(std::ceil(float(Window::width()) / float(m_cluster_resolution.x)));
	m_cluster_resolution.y = uint32_t(glm::ceil(float(Window::height()) / float(m_cluster_block_size)));

	// The depth of the cluster grid during clustered rendering is dependent on the
	// number of clusters subdivisions in the screen Y direction.
	// Source: Clustered Deferred and Forward Shading (2012) (Ola Olsson, Markus Billeter, Ulf Assarsson).

	const float half_fov   = glm::radians(m_camera.verticalFov() * 0.5f);
	const float sD         = 2.0f * glm::tan(half_fov) / float(m_cluster_resolution.y) * depth_scale;
	m_near_k               = 1.0f + sD;  // used by "generate clusters" shader; apply scale factor to 'sD' to change the number of depth slices
	m_log_cluster_res_y    = 1.0f / glm::log(m_near_k);

	const float z_near     = m_camera.nearPlane();
	const float z_far      = m_camera.farPlane();
	const float log_depth  = glm::log(z_far / z_near);
	m_cluster_resolution.z = uint32_t(glm::floor(log_depth * m_log_cluster_res_y));


	// TODO:
	// Maybe use the grid depth calculation used by Doom 2016
	//   see https://www.aortiz.me/2018/12/21/CG.html#building-a-cluster-grid
	// auto doom_slice_z = [near_z=m_camera.nearPlane(), far_z=m_camera.farPlane(), num_slices=m_cluster_grid_dim.z](size_t slice_n) -> float {
	// 	return -near_z * std::pow(far_z / near_z, float(slice_n)/float(num_slices));
	// };
	/// HOWEVER: seem to result in approx. the same spacing
	///   might be faster though, if that's useful?
	// the reverse:
	//   slice_n = std::log(z_slice) * (num_slices / std::log(cam_far_z/cam_near_z)) - num_slices * std::log(cam_near_z) / std::log(cam_far_z / cam_near_z);
	// auto doom_slide_n = [near_z=m_camera.nearPlane(), far_z=m_camera.farPlane(), num_slices=m_cluster_resolution.z](float z_slice) -> size_t {
	// 	auto far_near = std::log(far_z / near_z);
	// 	return size_t(std::log(z_slice) * (float(num_slices) / far_near) - float(num_slices) * std::log(near_z) / far_near);
	// };

	const auto cluster_count = m_cluster_resolution.x * m_cluster_resolution.y * m_cluster_resolution.z;

	assert(cluster_count < CLUSTER_MAX_COUNT);

	if(cluster_count != cluster_count_before)
	{
		m_cluster_count = cluster_count;
		Log::info("Shading clusters: {}   ({} x {} x {})", m_cluster_count, m_cluster_resolution.x, m_cluster_resolution.y, m_cluster_resolution.z);

		auto cluster_depth = [this](size_t slice_n) -> float {
			return -m_camera.nearPlane() * std::pow(std::abs(m_near_k), float(slice_n));
		};

		const float depthN0 = -cluster_depth(0); // this should be camera's near plane
		const float depthN1 = -cluster_depth(1);
		const float depthM0 = -cluster_depth(m_cluster_resolution.z/2 - 1); // this should be camera's near plane
		const float depthM1 = -cluster_depth(m_cluster_resolution.z/2);
		const float depthF0 = -cluster_depth(m_cluster_resolution.z - 1);
		const float depthF1 = -cluster_depth(m_cluster_resolution.z);  // this should be camera's far plane (approximately)

		Log::info("    cluster[0].depth: {:.3f}", depthN1 - depthN0);
		Log::info("  cluster[N/2].depth: {:.2f}", depthM1 - depthM0);
		Log::info("    cluster[N].depth: {:.1f}   ({:.1f} - {:.1f})", depthF1 - depthF0, depthF0, depthF1);

		// {
		// 	const float depthN0 = -doom_slice_z(0); // this should be camera's near plane
		// 	const float depthN1 = -doom_slice_z(1);
		// 	const float depthM0 = -doom_slice_z(m_cluster_grid_dim.z/2 - 1);
		// 	const float depthM1 = -doom_slice_z(m_cluster_grid_dim.z/2);  // this should be camera's far plane (approximately)
		// 	const float depthF0 = -doom_slice_z(m_cluster_grid_dim.z - 1);
		// 	const float depthF1 = -doom_slice_z(m_cluster_grid_dim.z);  // this should be camera's far plane (approximately)

		// 	Log::info("DOOM    Near: %.3f - %.3f (%.3f)", depthN0, depthN1, depthN1 - depthN0);
		// 	Log::info("DOOM     MId: %.2f - %.2f (%.2f)", depthM0, depthM1, depthM1 - depthM0);
		// 	Log::info("DOOM     Far: %.1f - %.1f (%.1f)", depthF0, depthF1, depthF1 - depthF0);
		// }
		prepareClusterBuffers();
	}
}

void ClusteredShading::prepareClusterBuffers()
{
	m_cluster_aabb_ssbo.resize(m_cluster_count);
	m_cluster_discovery_ssbo.resize(1 + m_cluster_count*2);  // num_active, nonempty[N], active[N]
	m_cluster_light_ranges_ssbo.resize(m_cluster_count);
	m_cluster_all_lights_index_ssbo.resize(1 + m_cluster_count * CLUSTER_AVERAGE_LIGHTS); // all_lights_start_index, all_lights_index[]
	m_cull_lights_args_ssbo.resize(1);

	/// Generate AABBs for clusters
	// This needs to be re-done when the camera projection changes (e.g. fov)
	m_camera.setUniforms(*m_generate_clusters_shader);
	m_generate_clusters_shader->setUniform("u_cluster_resolution"sv, m_cluster_resolution);
	m_generate_clusters_shader->setUniform("u_cluster_size_ss"sv,    glm::uvec2(m_cluster_block_size));
	m_generate_clusters_shader->setUniform("u_near_k"sv,             m_near_k);
	m_generate_clusters_shader->setUniform("u_pixel_size"sv,         1.0f / glm::vec2(Window::width(), Window::height()));
	m_generate_clusters_shader->invoke(size_t(std::ceil(float(m_cluster_count) / 1024.f)));

	m_affecting_lights_bitfield_ssbo.clear();
}

void ClusteredShading::input()
{
    /* Close the application when Esc is released. */
	if (Input::wasKeyPressed(KeyCode::Escape))
        stop();

	if(Input::wasKeyPressed(KeyCode::Backtick))
		_debug_ui_enabled = not _debug_ui_enabled;

	if(Input::isKeyDown(KeyCode::RightArrow))
		s_spot_outer_angle = std::min(s_spot_outer_angle + 0.3f, 89.9f);
	else if(Input::isKeyDown(KeyCode::LeftArrow))
		s_spot_outer_angle = std::max(s_spot_outer_angle - 0.3f, 0.1f);

	if(Input::isKeyDown(KeyCode::UpArrow))
		s_spot_intensity = std::min(s_spot_intensity + 5.f, 5000.0f);
	else if(Input::isKeyDown(KeyCode::DownArrow))
		s_spot_intensity = std::max(s_spot_intensity - 5.f, 10.f);

	if(Input::isKeyDown(KeyCode::Equals))
		m_camera_fov = std::min(m_camera_fov + 0.5f, 140.f);
	else if(Input::isKeyDown(KeyCode::Minus))
		m_camera_fov = std::max(m_camera_fov - 0.5f, 3.f);

	if(_pov_light_id != NO_LIGHT_ID and Input::wasKeyPressed(KeyCode::F3))
		_light_mgr.set_enabled(_pov_light_id, not _light_mgr.is_enabled(_pov_light_id));

	if(_light_mgr.sun_id() != NO_LIGHT_ID and Input::wasKeyPressed(KeyCode::F4))
		_light_mgr.set_enabled(_light_mgr.sun_id(), not _light_mgr.is_enabled(_light_mgr.sun_id()));

	if (Input::wasKeyReleased(KeyCode::F12))
    {
		// TODO: add "slot numer" suffix
        std::string filename = "27_clustered_shading";
		if (take_screenshot_png(filename, Window::width(), Window::height()))
			Log::info("Screenshot: {}.png", filename);
        else
			Log::error("Failed screenshot [{}]", filename);
    }

	// if (Input::wasKeyReleased(KeyCode::Space))
	// 	m_animate_lights = !m_animate_lights;
}

void ClusteredShading::update(double delta_time)
{
	_running_time += seconds_f(delta_time);

	m_camera.update(delta_time);

	if(_pov_light_id != NO_LIGHT_ID)
	{
		const auto position = m_camera.position() + m_camera.forwardVector() * _pov_light_distance;
		const auto direction = m_camera.forwardVector();
		_entities.patch<component::Transform>(entt::entity(_pov_light_id), [position, direction](auto &transform) {
			transform.set_position(position);
			transform.set_direction(direction);
		});
	}

	const float energy_multiplier = 1.01f;
	float adjust_energy = 0.f;
	if(Input::isKeyDown(KeyCode::UpArrow))
		adjust_energy = energy_multiplier;
	else if(Input::isKeyDown(KeyCode::DownArrow))
		adjust_energy  = -energy_multiplier;

	const float move_amount = float(1.0f * delta_time);
	float adjust_position = 0.f;
	if(Input::isKeyDown(KeyCode::LeftArrow))
		adjust_position = -move_amount;
	else if(Input::isKeyDown(KeyCode::RightArrow))
		adjust_position = +move_amount;

	const float angle_amount = float(glm::radians(10.f)*delta_time);
	float adjust_angle  = 0.f;
	if(Input::isKeyDown(KeyCode::RightBracket))
		adjust_angle = angle_amount;
	else if(Input::isKeyDown(KeyCode::LeftBracket))
		adjust_angle  = -angle_amount;

	if(Input::wasKeyPressed(KeyCode::F))
		m_volumetrics_pp.setEnabled(not m_volumetrics_pp.enabled());
	if(Input::wasKeyPressed(KeyCode::B))
		m_bloom_pp.setEnabled(not m_bloom_pp.enabled());

	if(adjust_position != 0 or adjust_angle != 0 or adjust_energy != 0)
	{
		auto light_view = _entities.view<component::LightGeneral, component::Transform>();
		for(const auto &[light_ent, general, transform]: light_view.each())
		{
			const auto light_id = LightID(light_ent);
			if(light_id == _pov_light_id)
				continue;

			if(adjust_position != 0)
			{
				_entities.patch<component::Transform>(light_ent, [adjust_position](auto &transform) {
					transform.set_position(transform.position() + glm::vec3(0, 0, adjust_position));
				});
			}

			if(adjust_angle  != 0 and general.light_type == LightType::Spot)
			{
				const auto &spot = _entities.get<component::SpotLight>(light_ent);
				float new_angle = std::max(spot.outer_angle + adjust_angle, glm::radians(3.f));  // noise becomes apparent at smaller degrees
				_light_mgr.set_spot_angle(light_id, new_angle);
				Log::info("  {{{}}} spot angle: {:.1f}  {:.1f}   P:{:.0f}",
						  light_id,
						  glm::degrees(spot.outer_angle),
						  glm::degrees(spot.inner_angle),
						  general.intensity);
			}

			if(adjust_energy != 0)
			{
				const auto &general = _entities.get<component::LightGeneral>(entt::entity(light_id));
				float intensity = general.intensity;
				if(adjust_energy > 0)
					intensity *= adjust_energy;
				else
					intensity /= -adjust_energy;
				_light_mgr.set_intensity(light_id, intensity);
			}
		}
	}
	else if (m_animate_lights)
    {
		// time_accum  += float(delta_time * m_animation_speed);
		const auto orbit_mat = glm::rotate(glm::mat4(1), glm::radians(float(delta_time)) * 10.f * m_animation_speed, AXIS_Y);
		const auto spin_mat = glm::angleAxis(glm::radians(float(15*delta_time * m_animation_speed)), AXIS_Y);

		// auto spin_mat  = glm::rotate(glm::mat4(1), glm::radians(60.f * float(delta_time)) * 2.f * m_animation_speed, AXIS_Y);

		// TODO: need API to update a specific light OR all lights (by iteration)

		auto view = _entities.view<component::LightGeneral>();
		for(const auto &[light_ent, general]: view.each())
		{
			const auto light_id = LightID(light_ent);
			if(light_id == _pov_light_id)
				continue;

			_entities.patch<component::Transform>(light_ent, [light_type=general.light_type, &orbit_mat, &spin_mat](auto &transform) {
				if(light_type == LightType::Point or light_type == LightType::Sphere)
					transform.set_position(orbit_mat * glm::vec4(transform.position(), 1)); // orbit around the world origin
				else
					transform.set_orientation(spin_mat * transform.orientation());
			});
		}
	}

	if(m_animate_lights or adjust_position != 0)
		updateLightsSSBOs();
}

void ClusteredShading::createLights()
{
	[[maybe_unused]] static const glm::vec3 room_min { -18, 0.5f, -18 };
	// [[maybe_unused]] static const glm::vec3 room_max { 178, 3.5f, 18 };
	[[maybe_unused]] static const glm::vec3 room_max {  18, 3.5f,  18 };

	static constexpr auto ident_quat = glm::quat_identity<float, glm::defaultp>();

	_light_mgr.add(DirectionalLightParams{
		.color = { 1.f, 0.97f, 0.9f },
		.intensity = 20.f,
		.fog = 1.f,
		.shadow_caster = true,
		.contact_shadows = true,
		.direction = glm::normalize(glm::vec3(5, -3, 5)),
	});

	// auto l = _light_mgr.add(SpotLightParams{
	// 	.color = { 1.f, 0.85f, 0.7f },
	// 	.intensity = 30.f,
	// 	.fog = .4f,
	// 	.shadow_caster = true,
	// 	.contact_shadows = true,
	// 	.position = m_camera.position(),  // will be kept up to date in update()
	// 	.direction = AXIS_X,              // will be kept up to date in update()
	// 	.outer_angle = glm::radians(45.f),
	// 	.inner_angle = glm::radians(25.f),
	// });
	// if(l)
	// 	_pov_light_id = *l;


	glm::vec3 offset { 0.f, 0.f, 0.f };
	const float step_z = 12.f;
	const float step_x = 22.f;

	auto light_pos = [&offset, step_z, step_x](auto idx) -> glm::vec3 {
		glm::vec3 pos { -13.f + offset.x, 2.5f, 12.f - offset.z };
		offset.z += step_z;
		if(offset.z > step_z*5 + 1)
		{
			offset.z = 0;
			offset.x += step_x;
		}
		return pos;
	};

	for(auto idx = 0u; idx < 0; ++idx)
	{
		const auto rand_color= hsv2rgb(
			Util::RandomFloat(1, 360),   // hue
			Util::RandomFloat(0.2f, 0.8f),   // saturation
			1.f                                      // value (brightness)
		);
		// const auto rand_pos = Util::RandomVec3(room_min, room_max);
		const auto rand_pos = light_pos(idx);

		const auto rand_intensity = 100.f;//Util::RandomFloat(10, 100);

		auto light_type = LightType(uint_fast8_t(LightType::Rect) + (idx % 4));
		light_type = LightType::Point;

		auto light_id { NO_LIGHT_ID };
		std::string_view type_name;
		switch(light_type)
		{
		case LightType::Point:
		case LightType::Directional:
		{
			auto l = _light_mgr.add(PointLightParams{
				.color = rand_color,
				.intensity = rand_intensity,
				.fog = 1.f,
				.shadow_caster = true,
				.contact_shadows = true,
				.position = rand_pos,
			});
			if(l)
			{
				type_name = _light_mgr.type_name(light_type);
				light_id = *l;
			}
		}
		break;
		case LightType::Spot:
		{
			auto l = _light_mgr.add(SpotLightParams{
				.color = rand_color,
				.intensity = rand_intensity,
				.fog = 1.f,
				.shadow_caster = true,
				.contact_shadows = true,
				.position = rand_pos,
				.direction = AXIS_X, //glm::normalize(Util::RandomVec3(0, 1)),
				.outer_angle = glm::radians(25.f),
				.inner_angle = glm::radians(15.f),
			});
			assert(l);
			if(l)
			{
				type_name = _light_mgr.type_name(light_type);
				light_id = *l;
			}
		}
		break;
		case LightType::Rect:
		{
			auto l = _light_mgr.add(RectLightParams{
				.color = rand_color,
				.intensity = rand_intensity,
				.fog = 1.f,
				.shadow_caster = false,   // probably never
				.position = rand_pos,
				.size = glm::vec2(0.6f, 0.4f),
				.orientation = glm::rotate(ident_quat, glm::radians(-90.f), glm::vec3(0, 1, 0)),
				.double_sided = false,
				.visible_surface = true,
			});
			assert(l);
			if(l)
			{
				type_name = _light_mgr.type_name(light_type);
				light_id = *l;
			}
		}
		break;
		case LightType::Tube:
		{
			auto l = _light_mgr.add(TubeLightParams{
				.color = rand_color,
				.intensity = rand_intensity,
				.fog = 1.f,
				.shadow_caster = false,   // probably never
				.position = rand_pos,
				.half_extent = { 0.f, 0.f, 0.5f },
				.thickness = 0.02f,
				.visible_surface = true,
			});
			assert(l);
			if(l)
			{
				type_name = _light_mgr.type_name(light_type);
				light_id = *l;
			}
		}
		break;
		case LightType::Sphere:
		{
			auto l = _light_mgr.add(SphereLightParams{
				.color = rand_color,
				.intensity = rand_intensity,
				.fog = 1.f,
				.shadow_caster = false,  // probably never
				.position = rand_pos,
				.radius = 0.2f,
				.visible_surface = true,
			});
			assert(l);
			if(l)
			{
				type_name = _light_mgr.type_name(light_type);
				light_id = *l;
			}
		}
		break;
		case LightType::Disc:
		{
			auto l = _light_mgr.add(DiscLightParams{
				.color = rand_color,
				.intensity = rand_intensity,
				.fog = 1.f,
				.shadow_caster = false,   // probably never
				.position = rand_pos,
				.direction = AXIS_X,
				.radius = 0.25f,
				.double_sided = false,
				.visible_surface = true,
			});
			assert(l);
			if(l)
			{
				type_name = _light_mgr.type_name(light_type);
				light_id = *l;
			}
		}
		break;
		default:
			assert(false);
		}

		// const auto &L = _light_mgr.gpu_get_by_id(l_id);

		Log::info("light{{{:2}}} {:5} @ {:5.1f}; {:3.1f}; {:5.1f}  {:3},{:3},{:3}  {:4.0f}",
				  light_id,
				  type_name,
				  rand_pos.x, rand_pos.y, rand_pos.z,
				  uint(rand_color.r*255), uint(rand_color.g*255), uint(rand_color.b*255),
				  rand_intensity);
				   // L.affect_radius);
	}
}

void ClusteredShading::updateLightsSSBOs()
{
	_light_mgr.flush();
}

void ClusteredShading::HdrEquirectangularToCubemap(const std::shared_ptr<RenderTarget::Cube>& cubemap_rt, const std::shared_ptr<Texture2D>& equirectangular_map)
{
    /* Update all faces per frame */
    m_equirectangular_to_cubemap_shader->bind();
	m_equirectangular_to_cubemap_shader->setUniform("u_projection"sv, cubemap_rt->projection());

	equirectangular_map->Bind(1);

    glBindVertexArray(m_skybox_vao);
    for (uint8_t side = 0; side < 6; ++side)
    {
		m_equirectangular_to_cubemap_shader->setUniform("u_view"sv, cubemap_rt->view_transform(side));
		cubemap_rt->bindRenderTarget(side);

        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
	glViewport(0, 0, GLsizei(Window::width()), GLsizei(Window::height()));
}

void ClusteredShading::IrradianceConvolution(const std::shared_ptr<RenderTarget::Cube>& cubemap_rt)
{
    /* Update all faces per frame */
    m_irradiance_convolution_shader->bind();
	m_irradiance_convolution_shader->setUniform("u_projection"sv, cubemap_rt->projection());

    m_env_cubemap_rt->bindTexture(1);

    for (uint8_t side = 0; side < 6; ++side)
    {
		m_irradiance_convolution_shader->setUniform("u_view"sv, cubemap_rt->view_transform(side));
		cubemap_rt->bindRenderTarget(side);

        glBindVertexArray(m_skybox_vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
	glViewport(0, 0, GLsizei(Window::width()), GLsizei(Window::height()));
}

void ClusteredShading::PrefilterEnvCubemap(const std::shared_ptr<RenderTarget::Cube>& cubemap_rt)
{
    m_prefilter_env_map_shader->bind();
	m_prefilter_env_map_shader->setUniform("u_projection"sv, cubemap_rt->projection());

    m_env_cubemap_rt->bindTexture(1);

	const auto max_mip_levels = uint8_t(glm::log2(float(cubemap_rt->width())));

	for(auto mip = 0u; mip < max_mip_levels; ++mip)
    {
		const auto mip_width  = std::max(1u, uint32_t(cubemap_rt->width())  >> mip);
		const auto mip_height = std::max(1u, uint32_t(cubemap_rt->height()) >> mip);

		cubemap_rt->resizeDepth(mip_width, mip_height);
		// TODO: ideally, set viewpoort only once (per mip level):
		// glViewport(0, 0, GLsizei(mip_width), GLsizei(mip_height));

		const auto roughness = float(mip) / std::max(1.f, float(max_mip_levels - 1));
		m_prefilter_env_map_shader->setUniform("u_roughness"sv, roughness);

		for(auto face = 0u; face < 6; ++face)
        {
			m_prefilter_env_map_shader->setUniform("u_view"sv, cubemap_rt->view_transform(face));
			cubemap_rt->bindRenderTargetMip(face, mip);

			glBindVertexArray(m_skybox_vao);
			glDrawArrays(GL_TRIANGLES, 0, 36);
        }

	}
	bindScreenRenderTarget();
}

void ClusteredShading::PrecomputeIndirectLight(const std::filesystem::path& hdri_map_filepath)
{
	auto envmap_hdr = std::make_shared<Texture2D>();
    envmap_hdr->LoadHdr(hdri_map_filepath);

    HdrEquirectangularToCubemap(m_env_cubemap_rt, envmap_hdr);

	m_env_cubemap_rt->color_texture().SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipLinear);
	m_env_cubemap_rt->color_texture().GenerateMipMaps();

	IrradianceConvolution(m_irradiance_cubemap_rt);
	PrefilterEnvCubemap(m_prefiltered_env_map_rt);
}

void ClusteredShading::PrecomputeBRDF(const std::shared_ptr<RenderTarget::Texture2d>& rt)
{
    rt->bindRenderTarget();
    m_precompute_brdf->bind();

	glBindVertexArray(_empty_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

	bindScreenRenderTarget();
}

void ClusteredShading::bindScreenRenderTarget()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, GLsizei(Window::width()), GLsizei(Window::height()));
}

void ClusteredShading::GenSkyboxGeometry()
{
    m_skybox_vao = 0;
    m_skybox_vbo = 0;

    glCreateVertexArrays(1, &m_skybox_vao);
    glCreateBuffers(1, &m_skybox_vbo);

	// vertex positions for cube faces
	glm::vec3 skybox_positions[] = {
		// back face
		{ -1, -1, -1 },
		{  1, -1, -1 },
		{  1,  1, -1 },
		{  1,  1, -1 },
		{ -1,  1, -1 },
		{ -1, -1, -1 },
        // front face
		{ -1, -1,  1 },
		{  1,  1,  1 },
		{  1, -1,  1 },
		{  1,  1,  1 },
		{ -1, -1,  1 },
		{ -1,  1,  1 },
        // left face
		{ -1,  1,  1 },
		{ -1, -1, -1 },
		{ -1,  1, -1 },
		{ -1, -1, -1 },
		{ -1,  1,  1 },
		{ -1, -1,  1 },
        // right face
		{ 1,  1,  1 },
		{ 1,  1, -1 },
		{ 1, -1, -1 },
		{ 1, -1, -1 },
		{ 1, -1,  1 },
		{ 1,  1,  1 },
        // bottom face
		{ -1, -1, -1 },
		{  1, -1,  1 },
		{  1, -1, -1 },
		{  1, -1,  1 },
		{ -1, -1, -1 },
		{ -1, -1,  1 },
        // top face
		{ -1,  1, -1 },
		{  1,  1, -1 },
		{  1,  1 , 1 },
		{  1,  1,  1 },
		{ -1,  1,  1 },
		{ -1,  1, -1 },
    };

    /* Set up buffer objects */
	glNamedBufferStorage(m_skybox_vbo, sizeof(skybox_positions), &skybox_positions[0], 0 /*flags*/);

    /* Set up VAO */
    glEnableVertexArrayAttrib(m_skybox_vao, 0 /*index*/);

    /* Separate attribute format */
    glVertexArrayAttribFormat(m_skybox_vao, 0 /*index*/, 3 /*size*/, GL_FLOAT, GL_FALSE, 0 /*relativeoffset*/);
    glVertexArrayAttribBinding(m_skybox_vao, 0 /*index*/, 0 /*bindingindex*/);
    glVertexArrayVertexBuffer(m_skybox_vao, 0 /*bindingindex*/, m_skybox_vbo, 0 /*offset*/, sizeof(glm::vec3) /*stride*/);
}

void ClusteredShading::downloadAffectingLightSet()
{
	_affecting_lights.clear();

	// Log::info("  affecting lights:");

	// "decode" the bitfield into actual light indices
	for(const auto &[bucket, bucket_bits]: std::views::enumerate(m_affecting_lights_bitfield_ssbo))
	{
		auto bits = bucket_bits;
		while(bits)
		{
			const auto bit_index = uint_fast32_t(std::countr_zero(bits));
			const auto light_index = (uint32_t(bucket) << 5u) + bit_index;

			_affecting_lights.insert(light_index);
			bits &= bits - 1u; // clear lowest set bit

			// Log::info(" {}", light_index);

#if defined(DEBUG)
			assert(light_index < _light_mgr.num_lights());
#endif
		}
	}

	// std::puts("");
}


void ClusteredShading::render()
{
	// TODO: move (parts of) this stuff to a "renderer" ?
	//   maybe have a renderer for each "step", and then a "compositor" to combine it all?

	const auto now = steady_clock::now();

	downloadAffectingLightSet();

	m_camera.setFov(m_camera_fov);


	collectRelevantLights(m_camera);

	// determine visible meshes  (only if camera or meshes moved (much))
	cullScene(m_camera, _cameraPvs);


	_gl_timers["shadows"].start();

	renderShadowMaps();

	if(auto d = _gl_timers["shadows"].elapsed<microseconds>(); d)
		m_shadow_time.add(*d);
	// ------------------------------------------------------------------
	_gl_timers["z-prepass"].start();
	// Depth pre-pass  (only if camera/meshes moved, probably always)
	renderDepth(m_camera.projectionTransform() * m_camera.viewTransform(), m_depth_pass_rt);

	// Blit depth info to our main render target
	m_depth_pass_rt.copyTo(_rt, RenderTarget::DepthBuffer, TextureFilteringParam::Nearest);

	if(auto d = _gl_timers["z-prepass"].elapsed<microseconds>(); d)
		m_depth_time.add(*d);
	// ------------------------------------------------------------------
	_gl_timers["cluster-find"].start();

	// this is an attempt at avoiding performing cluster discovery and light culling each frame,
	//   instead, only do it when the camera moves or after a max interval time.
	static auto prev_cam_pos = m_camera.position();
	static auto prev_cam_fwd = m_camera.forwardVector();
	static auto last_discovery_T = steady_clock::now();

	// TODO: is it possible to not do this every frame?
	//   some threshold for camera movement and if dynamic objects is in the frustum?
	prev_cam_pos = m_camera.position();
	prev_cam_fwd = m_camera.forwardVector();
	last_discovery_T = now;


	// find clusters with fragments in them (the only ones we need to process in the light culling step)
	m_find_nonempty_clusters_shader->setUniform("u_near_z"sv,             m_camera.nearPlane());
	m_find_nonempty_clusters_shader->setUniform("u_far_z"sv,              m_camera.farPlane());
	m_find_nonempty_clusters_shader->setUniform("u_log_cluster_res_y"sv,  m_log_cluster_res_y);
	m_find_nonempty_clusters_shader->setUniform("u_cluster_size_ss"sv,    glm::uvec2(m_cluster_block_size));
	m_find_nonempty_clusters_shader->setUniform("u_cluster_resolution"sv, m_cluster_resolution);

	m_cluster_discovery_ssbo.clear();
	m_depth_pass_rt.bindDepthTextureSampler(0);
	m_find_nonempty_clusters_shader->invoke(size_t(glm::ceil(float(m_depth_pass_rt.width()) / 32.f)),
											size_t(glm::ceil(float(m_depth_pass_rt.height()) / 32.f)));


	if(auto d = _gl_timers["cluster-find"].elapsed<microseconds>(); d)
		m_cluster_find_time.add(*d);
	// ------------------------------------------------------------------
	_gl_timers["cluster-index"].start();

	m_cull_lights_args_ssbo.clear();
	m_collect_nonempty_clusters_shader->setUniform("u_num_clusters"sv, m_cluster_count);
	m_collect_nonempty_clusters_shader->invoke(size_t(std::ceil(float(m_cluster_count) / 1024.f)));

	if(auto d = _gl_timers["cluster-index"].elapsed<microseconds>(); d)
		m_cluster_index_time.add(*d);
	// ------------------------------------------------------------------
	_gl_timers["cluster-cull"].start();

	// Assign lights to clusters (cull lights)
	m_cluster_light_ranges_ssbo.clear();
	m_cluster_all_lights_index_ssbo.clear();
	m_affecting_lights_bitfield_ssbo.clear();
	m_cull_lights_shader->setUniform("u_cam_pos"sv, m_camera.position());
	m_cull_lights_shader->setUniform("u_light_max_distance"sv, std::min(100.f, m_camera.farPlane()));
	m_cull_lights_shader->setUniform("u_view_matrix"sv, m_camera.viewTransform());
	m_cull_lights_shader->setUniform("u_num_clusters"sv, m_cluster_count);
	m_cull_lights_shader->setUniform("u_max_cluster_avg_lights"sv, uint32_t(CLUSTER_AVERAGE_LIGHTS));
	m_cull_lights_shader->invoke(m_cull_lights_args_ssbo);

	if(auto d = _gl_timers["cluster-cull"].elapsed<microseconds>(); d)
		m_light_cull_time.add(*d);
	// ------------------------------------------------------------------
	_gl_timers["shading"].start();

	_rt.bindRenderTarget(RenderTarget::ColorBuffer);

	renderShading(m_camera);

	if(auto d = _gl_timers["shading"].elapsed<microseconds>(); d)
		m_shading_time.add(*d);
	// ------------------------------------------------------------------
	_gl_timers["skybox"].start();

	if(m_draw_surface_lights_geometry)
		renderLightGeometry(); // to '_rt'

	renderSkybox(); // to '_rt'

	if(auto d = _gl_timers["skybox"].elapsed<microseconds>(); d)
		m_skybox_time.add(*d);
	// ------------------------------------------------------------------

	if(m_volumetrics_pp.enabled() and _fog_density > 0)
	{
		_gl_timers["volumetrics-cull"].start();

		m_volumetrics_pp.setViewParams(m_camera, m_camera.farPlane() * s_light_volumetric_fraction);
		m_volumetrics_pp.cull_lights();

		if(auto d = _gl_timers["volumetrics-cull"].elapsed<microseconds>(); d)
			m_volumetrics_cull_time.add(*d);
		// ------------------------------------------------------------------
		_gl_timers["volumetrics-inject"].start();

		m_volumetrics_pp.setStrength(_fog_strength);
		m_volumetrics_pp.setDensity(_fog_density);
		m_volumetrics_pp.setTemporalBlendWeight(_fog_blend_weight);  // if blending is enabled

		auto &shader = m_volumetrics_pp.shader();
		shader.setUniform("u_light_max_distance"sv,  m_camera.farPlane() * s_light_affect_fraction);
		shader.setUniform("u_shadow_max_distance"sv, m_camera.farPlane() * s_light_shadow_affect_fraction);
		shader.setUniform("u_falloff_power"sv, _light_mgr.falloff_power());


		if(const auto &csm = _shadow_atlas.csm_params(); csm)
		{
			shader.setUniform("u_csm_num_cascades"sv,     uint32_t(csm.num_cascades));
			shader.setUniform("u_csm_split_depth"sv,      csm.split_depth);
			// shader.setUniform("u_csm_cascade_near_far"sv, csm.near_far_plane); // PCSS
			shader.setUniform("u_csm_light_radius_uv"sv,  csm.light_radius_uv); // PCSS
			// needed in vertex shader
			shader.setUniform("u_csm_light_view_space"sv, csm.light_view);
			shader.setUniform("u_csm_light_clip_space"sv, csm.light_view_projection); // also in SSBO, but we'd like to avoid accessing that from the vertex shader
		}
		else
			shader.setUniform("u_csm_num_cascades"sv,     0u);


		_shadow_atlas.bindDepthTextureSampler(22); // just using single-sample, no PCF
		m_depth_pass_rt.bindDepthTextureSampler(2); // HUH?!?

		m_volumetrics_pp.inject();

		if(auto d = _gl_timers["volumetrics-inject"].elapsed<microseconds>(); d)
			m_volumetrics_inject_time.add(*d);
		// ------------------------------------------------------------------
		_gl_timers["volumetrics-accum"].start();

		m_volumetrics_pp.accumulate();

		if(auto d = _gl_timers["volumetrics-accum"].elapsed<microseconds>(); d)
			m_volumetrics_accum_time.add(*d);
		// ------------------------------------------------------------------
		_gl_timers["volumetrics-render"].start();

		_pp_low_rt.clear();
		m_volumetrics_pp.render(_rt, _pp_low_rt);  // '_rt' actually isn't used but the API expects an argument


		// _pp_low_rt.copyTo(_pp_full_rt);  // copy and upscale
		// NOTE: draw b/c copy(blit) doesn't work!?!?
		//   no biggie though, it's often faster in practice
#if 0
		// blur low-res target
		m_blur3_pp.render(_pp_low_rt, _pp_low_rt);
#endif
		// upscale to full-size
		draw2d(_pp_low_rt.color_texture(), _pp_full_rt);

#if 0
		// TODO: change to MipmapBlur
		// m_blur3_pp.render(_pp_full_rt, _pp_full_rt);
		// m_blur3_pp.render(_pp_full_rt, _pp_full_rt);
		// m_blur3_pp.render(_pp_full_rt, _pp_full_rt);
#endif

		// add the scattering effect on to the final image
		draw2d(_pp_full_rt.color_texture(), _rt, BlendMode::Add);  // TODO: should be Alpha bland! no scatter should be just sample fog color
		// m_pp_blur_time.add(_gl_timer.elapsed<microseconds>());

		if(auto d = _gl_timers["volumetrics-render"].elapsed<microseconds>(); d)
			m_volumetrics_render_time.add(*d);
	}
	else
	{
		m_volumetrics_cull_time.clear();
		m_volumetrics_inject_time.clear();
		m_volumetrics_accum_time.clear();
		m_volumetrics_render_time.clear();
	}


	// TODO: compute average luminance of rendered image
	//   and gradually adjust exposure over time (see tone mapping, below)
	// m_detect_brightness_shader.bind();
	// m_detect_brightness_shader.invoke(GLuint(glm::ceil(float(_rt.width()) / 8.f)),
	// 				  GLuint(glm::ceil(float(_rt.height()) / 8.f)));
	// write the result to some SSBO, so tonemapping can pick it up
	// TODO: compute new desired exposure, blend 'm_eposure' over time towards that value

	// ------------------------------------------------------------------
	// Bloom
	_gl_timers["bloom-tonemap"].start();

	if (m_bloom_pp.enabled())
    {
		m_bloom_pp.setThreshold(m_bloom_threshold);
		m_bloom_pp.setIntensity(m_bloom_intensity);
		m_bloom_pp.setKnee(m_bloom_knee);
		m_bloom_pp.setDirtIntensity(m_bloom_dirt_intensity);

		m_bloom_pp.render(_rt, _rt);
	}

	// Apply tone mapping
	// TODO: continuously adjust 'm_exposure' depending on how bright the image is (see above)
	m_tmo_pp.setExposure(m_camera.exposure());
	m_tmo_pp.setGamma(m_gamma);
	m_tmo_pp.render(_rt, _final_rt);

	// draw the final result to the screen
	draw2d(_final_rt.color_texture(), BlendMode::Replace);

	if(auto d = _gl_timers["bloom-tonemap"].elapsed<microseconds>(); d)
		m_tonemap_time.add(*d);
	// ------------------------------------------------------------------
	_gl_timers["debug-draw"].start();

	if(m_debug_draw_aabb)
		debugDrawSceneBounds();
	if(m_debug_draw_light_markers)
		debugDrawLightMarkers();
	if(m_debug_draw_cluster_grid)
		debugDrawClusterGrid();

	if(auto d = _gl_timers["debug-draw"].elapsed<microseconds>(); d)
		m_debug_draw_time.add(*d);
}

void ClusteredShading::renderShadowMaps()
{
	// render shadow-maps if light or meshes within its radius/frustum moved (the latter is TODO)
	// TODO: move this stuff to a "shadow map renderer" ?

	glCullFace(GL_FRONT);       // render only back faces
	glEnable(GL_SCISSOR_TEST);  // for slot slicing
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_FALSE, GL_FALSE);  // writing 2-component normals
	glDepthFunc(GL_LESS);

	assert(glIsEnabled(GL_CULL_FACE));

	const auto now = steady_clock::now();

	static steady_clock::time_point last_eval_time;

	// if there's a sun allocated, update its shadow parameters
	if(const auto &sun_id = _light_mgr.sun_id(); sun_id != NO_LIGHT_ID)
	{
		// technically needed only if/when light's direction or camera changed, or objects moved,
		//    but it's nearly always
		_shadow_atlas.update_csm_params(sun_id, m_camera);//, _sun_radius_uv);
	}

	if(now - last_eval_time > 100ms)
	{
		last_eval_time = now;

		_shadow_atlas.set_max_distance(m_camera.farPlane() * s_light_shadow_max_fraction);
		const auto T0 = steady_clock::now();
		_shadow_atlas.update_allocations(_lightsPvs, m_camera.position(), m_camera.forwardVector());
		m_shadow_alloc_time.add(duration_cast<microseconds>(steady_clock::now() - T0));
	}
	else
		m_shadow_alloc_time.add(microseconds(0));

	// light projections needs to be updated more often than the atlas allocations.
	//   it also needs to be updated when the light has moved (new view-projection),
	// for simplicity, all allocated light are updated every frame
	_shadow_atlas.update_slots_ssbo();


	// before the first shadow map is rendered, the SSBO content must be in synch,
	//   but in the case of no shadow maps rendered, no barrier is required (here)
	bool did_barrier = false;
	auto barrier = []() {
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	};

	_light_shadow_maps_rendered = 0;
	_shadow_atlas_slots_rendered = 0;

	// TODO: limit the number of shadow maps to render in one frame, e.g. top N most important lights
	//    the remaining will still by "dirty" so they will be rendered eventually.
	//    this implies a 2-pass algo; "allocated_lights() is an unordered mapping.

#if defined(_DEBUG)
	static dense_set<uint_fast16_t> seen_shadow_idx;
	seen_shadow_idx.reserve(16);
	seen_shadow_idx.clear();
#endif

	for(auto &[light_id, atlas_light]: _shadow_atlas.allocated_lights())
	{
		const auto light_index = _light_mgr.light_index(light_id);

		// if this light did not contribute to the (previous) frame, no need to render its shadow map (yet)
		if(not _affecting_lights.contains(light_index))
			continue;

		const auto light_ent = entt::entity(light_id);

		const auto &[general, transform] = _entities.get<component::LightGeneral, component::Transform>(light_ent);
		if(not general.enabled) // disabled lights will remain in the "allocated lights" set for a while (until it's updated)
		{
			Log::debug("shmap| {{{}}} not enabled", light_id);
			continue;
		}

		auto light_hash = _light_mgr.hash(light_id, general, transform);
		if(general.light_type == LightType::Directional)  // also affected by the camera's frustum
			light_hash = hash_combine(light_hash, m_camera.hash());

		// TODO: keep TWO shadow maps, one static-object only (cache) and one active (all objects)
		//   1. render static objects first, to both maps (copy to active afterwards?)
		//   2. render dynamic objects to active map only
		//   next frame:
		//   - if dynamic objects moved: copy static to active & render dynamic objects
		//   - naturally, if light moved/changed, go to step 1.

		const auto need_render = _shadow_atlas.need_render(atlas_light, now, light_hash, _scene);
		if(need_render)
		{
			// render shadow map(s) for this light

			const auto shadow_idx = general.shadow_index;
			if(shadow_idx == LIGHT_NO_SHADOW)
			{
				Log::warning("{{{}}} no shadow idx set", light_id);
				continue;
			}
			// Log::debug("[{}] -> shadow idx: {}", light_index, shadow_idx);
#if defined(_DEBUG)
			assert(not seen_shadow_idx.contains(shadow_idx));
			seen_shadow_idx.insert(shadow_idx);
#endif

			uint_fast8_t slots_rendered { 0 };

			for(uint_fast8_t slot_idx = 0u; slot_idx < atlas_light.num_slots; ++slot_idx)
			{
				const auto dynamic_only = false;

				if((need_render & (1u << slot_idx)) > 0)
				{
					const auto &slot_rect = atlas_light.slots[slot_idx].rect;
					_shadow_atlas.bindRenderTarget(slot_rect);
					if(not did_barrier)
					{
						barrier();
						did_barrier = true;
					}

					const auto &pvs = _shadow_atlas.pvs(_scene, light_id, slot_idx);

					renderSceneShadow(pvs, shadow_idx, slot_idx, dynamic_only);
					++_shadow_atlas_slots_rendered;
					++slots_rendered;
				}
			}
			assert(slots_rendered == atlas_light.num_slots);

			atlas_light.on_rendered(now, light_hash);
			++_light_shadow_maps_rendered;
		}
	}

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);  // back to default; write all color channels
	glDisable(GL_SCISSOR_TEST);
	glCullFace(GL_BACK);
}

void ClusteredShading::renderSkybox()
{
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDepthFunc(GL_LEQUAL);

	m_background_shader->bind();
	m_camera.setUniforms(*m_background_shader);
	m_background_shader->setUniform("u_view_orientation"sv, glm::mat4(glm::mat3(m_camera.viewTransform())));  // only rotational part
	m_background_shader->setUniform("u_mip_level"sv,        _ibl_mip_level);
	m_env_cubemap_rt->bindTexture();

	glBindVertexArray(m_skybox_vao);
	glDrawArrays     (GL_TRIANGLES, 0, 36);
}

void ClusteredShading::renderLightGeometry()
{
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LEQUAL);

	struct SurfaceLightAttrs
	{
		glm::mat4 transform;
		glm::vec3 color_intensity;
		uint32_t double_sided;
	};

	m_light_geometry_shader->bind();
	m_light_geometry_shader->setUniform("u_view_projection"sv, m_camera.projectionTransform() * m_camera.viewTransform());

	static std::vector<SurfaceLightAttrs> surf_attrs;
	surf_attrs.reserve(_lightsPvs.size());
	surf_attrs.clear();

	auto config_attrs = [](InstanceAttributes &inst_attrs) {
		inst_attrs.skip(4);  // TODO: query the mesh to find how many locations are used. Also, this must match the shader
		inst_attrs.add<glm::mat4>("transform"sv);
		inst_attrs.add<glm::vec3>("color-intensity"sv);
		inst_attrs.add<uint32_t>("double-sided");
	};

	// TODO: (optionally) render point/spots in some way? e.g. a circular glare?

	static_assert(LIGHT_TYPE__COUNT == 7);

	// all the light types with surface
	for(const auto &light_type: { LightType::Rect, LightType::Tube, LightType::Sphere, LightType::Disc })
	{
		// collect lights geometries for this light type
		surf_attrs.clear();
		for(auto light_index: _lightsPvs)
		{
			const auto light_id = _light_mgr.light_id(light_index);

			const auto light_ = _light_mgr.get_light(light_id);
			if(not light_)
				continue;

			const auto &light = *light_;

			if(light.general.light_type == light_type)
			{
				switch(light.general.light_type)
				{
				case LightType::Rect:
				{
					const auto &rect = std::get<component::RectLight>(light.light);
					const auto size = glm::vec3{rect.size, 1.f};
					const auto tfm = glm::scale(light.transform.transform(), size);

					surf_attrs.emplace_back(tfm, light.general.color*light.general.intensity, rect.double_sided);
				}
				break;
				case LightType::Tube:
				{
					const auto &tube = std::get<component::TubeLight>(light.light);
					const auto size = glm::vec3(tube.thickness, tube.thickness, glm::length(tube.half_extent)*2);
					const auto tfm = glm::scale(light.transform.transform(), size);

					surf_attrs.emplace_back(tfm, light.general.color*light.general.intensity, true);
				}
				break;
				case LightType::Sphere:
				{
					const auto &sphere = std::get<component::SphereLight>(light.light);
					const auto size = glm::vec3(sphere.radius);
					const auto tfm = glm::scale(light.transform.transform(), size);

					surf_attrs.emplace_back(tfm, light.general.color*light.general.intensity, true);
				}
				break;
				case LightType::Disc:
				{
					const auto &disc = std::get<component::DiscLight>(light.light);
					const auto size = glm::vec3(1, disc.radius, disc.radius);
					const auto tfm = glm::scale(light.transform.transform(), size);

					surf_attrs.emplace_back(tfm, light.general.color*light.general.intensity, false);
				}
				break;
				default:
					break;
				}
			}
		}
		if(surf_attrs.empty())
			continue;

		uint_fast8_t model_index = uint_fast8_t(light_type) - uint_fast8_t(LightType::Rect);

		auto &model = _lightModels[model_index].model;

		auto &inst_attrs = model->instance_attributes(sizeof(SurfaceLightAttrs));
		if(not inst_attrs)
			config_attrs(inst_attrs);

		inst_attrs.load<SurfaceLightAttrs>(surf_attrs);

		model->Render(*m_light_geometry_shader, uint32_t(surf_attrs.size()));
	}

	// draw "sun"   (or just draw all directional lights?)
	if(auto sun_id = _light_mgr.sun_id(); sun_id != NO_LIGHT_ID)
	{
		// draw a "sun" disc (or moon, I suppose); only supports one
		// const auto &L = _light_mgr.gpu_get_by_id(sun_id);
		const auto &[general, transform] = _entities.get<component::LightGeneral, component::Transform>(entt::entity(sun_id));
		if(general.enabled)
		{
			const auto cam_pos = m_camera.position();
			const auto cam_up = m_camera.upVector();           // e.g. AXIS_Y
			// const auto sun_dir_world = L.direction; // direction light -> scene?
			const glm::vec3 sun_dir_world = transform.orientation() * glm::vec4(-AXIS_Z, 1);
			// Make sure sun_dir points from camera toward the sun position. If sun_dir is light direction
			// (pointing from sun -> scene) use -sun_dir when computing position below.

			const float distance = m_camera.farPlane() - _sun_size - 1.f;
			const auto sun_pos_ws = cam_pos -sun_dir_world * distance;

				   // Choose sun angular half-angle (physical ~0.00465 rad) or artistic scale
			const float sun_half_angle = 0.03f * _sun_size; // radians
			float radius_ws = std::tan(sun_half_angle) * distance;

				   // Build camera-facing basis
			glm::vec3 forward = glm::normalize(cam_pos - sun_pos_ws); // points from sun -> camera
			glm::vec3 right = glm::normalize(glm::cross(forward, cam_up));
			if(std::abs(glm::dot(forward, cam_up)) > 0.999f)
				right = glm::normalize(glm::cross(forward, AXIS_X));
			glm::vec3 up = glm::cross(right, forward);

				   // Log::debug("sun pos: {:.0f}; {:.0f}; {:.0f}   R: {:.1f}  F: {:.2f}; {:.2f}; {:.2f}", sun_pos_ws.x, sun_pos_ws.y, sun_pos_ws.z, radius_ws, forward.x, forward.y, forward.z);

			glm::mat4 sun_model;
			sun_model[0] = glm::vec4(right * radius_ws, 0); // local +X -> world right * scale
			sun_model[1] = glm::vec4(up    * radius_ws, 0); // local +Y -> world up * scale
			sun_model[2] = glm::vec4(forward,           0); // local +Z -> world forward (keeps normal facing camera)
			sun_model[3] = glm::vec4(sun_pos_ws,        1); // translation

			surf_attrs.clear();
			surf_attrs.emplace_back(sun_model, general.color*general.intensity * 100.f, false);

			auto &model = _lightModels[2].model;  // render as a disc  (but currently as a sphere, for testing)
			auto &inst_attrs = model->instance_attributes(sizeof(SurfaceLightAttrs));
			if(not inst_attrs)
				config_attrs(inst_attrs);

			inst_attrs.load<SurfaceLightAttrs>(surf_attrs);

			model->Render(*m_light_geometry_shader, uint32_t(surf_attrs.size()));
		}
	}
}



void ClusteredShading::draw2d(const Texture &texture, BlendMode blend)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// completely ignore the depth buffer
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	if(blend == BlendMode::Replace)
		glDisable(GL_BLEND);
	else
	{
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
	}

	switch(blend)
	{
	case BlendMode::Replace: glDisable(GL_BLEND); break;
	case BlendMode::Subtract: glBlendEquation(GL_FUNC_SUBTRACT);  // fallthrough
	case BlendMode::Add:     glBlendFunc(GL_ONE, GL_ONE); break;
	case BlendMode::Alpha:   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
	}

	m_fsq_shader->bind();
	texture.Bind();

	glBindVertexArray(_empty_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	// restore states
	if(blend != BlendMode::Replace)
	{
		glDisable(GL_BLEND);
		if(blend == BlendMode::Subtract)
			glBlendEquation(GL_FUNC_ADD);
	}

	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

void ClusteredShading::draw2d(const Texture &source, RenderTarget::Texture2d &target, BlendMode blend)
{
	// TODO: this setting blend mode should be a separate function

	if(blend == BlendMode::Replace)
		glDisable(GL_BLEND);
	else
	{
		glEnable(GL_BLEND);
		if(blend != BlendMode::Subtract)
			glBlendEquation(GL_FUNC_ADD);
	}

	// completely ignore the depth buffer
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	switch(blend)
	{
	case BlendMode::Replace:  break;
	case BlendMode::Subtract: glBlendEquation(GL_FUNC_SUBTRACT);  // fallthrough
	case BlendMode::Add:      glBlendFunc(GL_ONE, GL_ONE); break;
	case BlendMode::Alpha:    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
	}

	source.Bind(0);
	target.bindRenderTarget(RenderTarget::NoBuffer);

	m_fsq_shader->bind();
	glBindVertexArray(_empty_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	// restore states
	if(blend != BlendMode::Replace)
	{
		glDisable(GL_BLEND);
		if(blend == BlendMode::Subtract)
			glBlendEquation(GL_FUNC_ADD);
	}

	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

void ClusteredShading::draw2d(const Texture &texture, const glm::uvec2 &top_left, const glm::uvec2 &bottom_right)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// m_2d_shader->bind();
	texture.Bind();

	(void)top_left;
	(void)bottom_right;
	// m_2d_shader.setUniform("u_source_rect"sv, glm::vec4(top_left, bottom_right));

	// glBindVertexArray(_rect_vao_id);
	// glDrawArrays(GL_TRIANGLES, 0, 6);
}

void ClusteredShading::cullScene(const Camera &view, QueryResult &pvs)
{
	const auto T0 = steady_clock::now();

	// perform frustum culling of all objects in the scene (or a partition thereof)
	_scene.query(view.frustum(), pvs);


	m_cull_scene_time.add(duration_cast<microseconds>(steady_clock::now() - T0));
}

void ClusteredShading::collectRelevantLights(const Camera &view)
{
	const auto T0 = steady_clock::now();

	const auto view_pos = view.position();
	const auto max_view_distance = view.farPlane()* s_light_relevant_fraction;

	// this probably doesn't need to be done every frame
	//   if no lights nor the view moves then only once
	{
		static auto last_update = T0 - 1h;
		if(T0 - last_update > s_relevant_lights_update_min_interval)
		{
			last_update = T0;

			// static dense_set<uint> previous_pvs;
			// previous_pvs.insert(_lightsPvs.begin(), _lightsPvs.end());
			_lightsPvs.clear();

			for(const auto &[l_index, L]: std::views::enumerate(_light_mgr))
			{
				const auto light_index = LightIndex(l_index);

				if(not IS_ENABLED(L))
					continue;

				if(IS_DIR_LIGHT(L))
					_lightsPvs.push_back(light_index);
				else
				{
					const auto edge_distance = std::max(0.f, glm::distance(L.position, view_pos) - L.affect_radius);
					const auto relevant = edge_distance < max_view_distance;
					// doing a frustum check means that quick camera pans might show unlit areas
					//	and intersect::check(m_camera.frustum(), _light_mgr.light_bounds(L));

					if(relevant)
						_lightsPvs.push_back(light_index);
					else
					{
						if(IS_SHADOW_CASTER(L) /* and was in the light pvs before? */)
						{
							const auto light_id = _light_mgr.light_id(light_index);
							_shadow_atlas.remove_allocation(light_id);
						}
					}
				}
			}
			// TODO: ideally these should be sorted by distance from camera
			_relevant_lights_index_ssbo.set(_lightsPvs);
		}
	}
}

void ClusteredShading::renderScene(const glm::mat4 &view_projection, Shader &shader, MaterialCtrl materialCtrl)
{
	// TODO: in c++26: std::views::concat(_cameraPvs.static_ids, _cameraPvs.dynamic_ids)
	for(const auto &entity_id: _cameraPvs.dynamic_entities)
	{
		const auto &[model, transform] = _entities.get<component::Model, component::Transform>(entity_id);
		const auto &tfm = transform.transform();

		shader.setUniform("u_mvp"sv,           view_projection * tfm);
		shader.setUniform("u_model"sv,         tfm);
		shader.setUniform("u_normal_matrix"sv, transform.normal_matrix());

		if(materialCtrl == UseMaterials)
			model.Render(shader);
		else
			model.Render();
	}

	for(const auto &entity_id: _cameraPvs.static_entities)
	{
		const auto &[model, transform] = _entities.get<component::Model, component::Transform>(entity_id);
		const auto &tfm = transform.transform();

		shader.setUniform("u_mvp"sv,           view_projection * tfm);
		shader.setUniform("u_model"sv,         tfm);
		shader.setUniform("u_normal_matrix"sv, transform.normal_matrix());

		if(materialCtrl == UseMaterials)
			model.Render(shader);
		else
			model.Render();
	}
}


void ClusteredShading::renderDepth(const glm::mat4 &view_projection, RenderTarget::Texture2d &target, const glm::ivec4 &rect)
{
	target.bindRenderTarget(rect, RenderTarget::DepthBuffer);

	glDepthMask(GL_TRUE);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthFunc(GL_LESS);
	glCullFace(GL_BACK);

    m_depth_prepass_shader->bind();

	if(_polygon_offset_factor != 0 and _polygon_offset_unit != 0)
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(_polygon_offset_factor, _polygon_offset_unit);
	}
	glDisable(GL_POLYGON_OFFSET_FILL);

	renderScene(view_projection, *m_depth_prepass_shader, NoMaterials);
}

void ClusteredShading::renderSceneShadow(const QueryResult &objects, uint16_t shadow_idx, uint_fast8_t slot_idx, bool dynamic_only)
{
	m_shadow_depth_shader->bind();

	// TODO: probably, it would be better (for perf) to pass the 'light_vp' as a uniform...
	m_shadow_depth_shader->setUniform("u_light_shadow_index"sv, uint32_t(shadow_idx)); // for 'mvp'
	m_shadow_depth_shader->setUniform("u_shadow_slot_index"sv, uint32_t(slot_idx));

	if(not dynamic_only)
	{
		for(const auto &entity_id: objects.static_entities)
		{
			const auto &[transform, model] = _entities.get<component::Transform, component::Model>(entity_id);
			const auto &tfm = transform.transform();

			m_shadow_depth_shader->setUniform("u_model"sv, tfm);
			m_shadow_depth_shader->setUniform("u_normal_matrix"sv, transform.normal_matrix());
			model.Render();
		}
	}
	for(const auto &entity_id: objects.dynamic_entities)
	{
		const auto &[transform, model] = _entities.get<component::Transform, component::Model>(entity_id);
		const auto &tfm = transform.transform();

		m_shadow_depth_shader->setUniform("u_model"sv, tfm);
		m_shadow_depth_shader->setUniform("u_normal_matrix"sv, transform.normal_matrix());
		model.Render();
	}

}

void ClusteredShading::renderShading(const Camera &camera)
{
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthFunc(GL_LEQUAL);   // only draw pixels which exactly match the depth pre-pass

	auto &shader = *m_clustered_pbr_shader;

	shader.bind();

	camera.setUniforms(shader);
	shader.setUniform("u_cluster_resolution"sv,         m_cluster_resolution);
	shader.setUniform("u_cluster_size_ss"sv,            glm::uvec2(m_cluster_block_size));
	shader.setUniform("u_log_cluster_res_y"sv,          m_log_cluster_res_y);
	shader.setUniform("u_light_max_distance"sv,         m_camera.farPlane() * s_light_affect_fraction);
	shader.setUniform("u_shadow_max_distance"sv,        m_camera.farPlane() * s_light_shadow_affect_fraction);
	shader.setUniform("u_shadow_contact_max_distance"sv, 10.f);
	//shader.setUniform("u_specular_max_distance"sv,      m_camera.farPlane() * s_light_specular_fraction);
	shader.setUniform("u_ambient_radiance"sv,           _ambient_radiance);
	shader.setUniform("u_ibl_strength"sv,               _ibl_strength);
	shader.setUniform("u_falloff_power"sv,              _light_mgr.falloff_power());

	shader.setUniform("u_shadow_bias_constant"sv,       m_shadow_bias_constant);
	shader.setUniform("u_shadow_bias_slope_scale"sv,    m_shadow_bias_slope_scale);
	shader.setUniform("u_shadow_bias_slope_power"sv,    m_shadow_bias_slope_power);
	shader.setUniform("u_shadow_bias_distance_scale"sv, m_shadow_bias_distance_scale);
	shader.setUniform("u_shadow_bias_texel_size_mix"sv, m_shadow_bias_texel_size_mix);
	shader.setUniform("u_shadow_bias_scale"sv,          m_shadow_bias_scale);
	shader.setUniform("u_shadow_occlusion"sv,           m_shadow_occlusion);
	shader.setUniform("u_shadow_colorize"sv,            _debug_colorize_shadows);
	shader.setUniform("u_shadow_contacts"sv,            m_shadow_contacts);
	shader.setUniform("u_shadow_contact_max_ray_length"sv, m_shadow_contact_max_ray_length);
	shader.setUniform("u_shadow_colorize_contact"sv,    _debug_colorize_contact_shadows);
	shader.setUniform("u_shadow_contact_offset"sv,      0.05f);

	if(const auto &csm = _shadow_atlas.csm_params(); csm)
	{
		shader.setUniform("u_csm_num_cascades"sv,     uint32_t(csm.num_cascades));
		shader.setUniform("u_csm_split_depth"sv,      csm.split_depth);
		shader.setUniform("u_csm_cascade_near_far"sv, csm.near_far_plane);  // PCSS
		shader.setUniform("u_csm_light_radius_uv"sv,  csm.light_radius_uv); // PCSS
		// needed in vertex shader
		shader.setUniform("u_csm_light_view_space"sv, csm.light_view);
		shader.setUniform("u_csm_light_clip_space"sv, csm.light_view_projection); // also in SSBO, but we'd like to avoid accessing that from the vertex shader
	}
	else
		shader.setUniform("u_csm_num_cascades"sv,     0u);

	shader.setUniform("u_debug_cluster_geom"sv,       m_debug_cluster_geom);
	shader.setUniform("u_debug_clusters_occupancy"sv, m_debug_clusters_occupancy);
	shader.setUniform("u_debug_tile_occupancy"sv,     m_debug_tile_occupancy);
	shader.setUniform("u_debug_overlay_blend"sv,      m_debug_coverlay_blend);

    m_irradiance_cubemap_rt->bindTexture(6);
    m_prefiltered_env_map_rt->bindTexture(7);
	m_brdf_lut_rt->bindTextureSampler(8);
    m_ltc_mat_lut->Bind(9);
    m_ltc_amp_lut->Bind(10);

	_shadow_atlas.bindShadowSampler(20);
	_shadow_atlas.bindTextureSampler(21);   // encoded normals
	_shadow_atlas.bindDepthTextureSampler(22);
	m_depth_pass_rt.bindDepthTextureSampler(23); // for screen-space/contact shadows


	// we need updated textures (shadow maps) and SSBO data
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

	if(_polygon_offset_factor != 0 and _polygon_offset_unit != 0)
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(_polygon_offset_factor, _polygon_offset_unit);
	}

	glViewport(0, 0, GLsizei(Window::width()), GLsizei(Window::height()));

	const auto view_projection = m_camera.projectionTransform() * m_camera.viewTransform();
	renderScene(view_projection, shader);

	glDisable(GL_POLYGON_OFFSET_FILL);
	// Enable writing to the depth buffer
	// glDepthMask(GL_TRUE);
	// glDepthFunc(GL_LESS);
}

void ClusteredShading::generateRandomAngles(Texture3D &texture, uint32_t size)
{
	const auto T0 = steady_clock::now();

	// create a 3d texture with size^3 random angles: { cos, sin }   (2 floats)

	const auto buffer_size = size * size * size; // 3d cube with side 'size'

	std::vector<glm::vec2> angles;
	angles.reserve(buffer_size);

	for(auto idx = 0u; idx < buffer_size; ++idx)
	{
		const auto angle = Util::RandomFloat(0.f, 2*std::numbers::pi_v<float>);
		angles.emplace_back(std::cos(angle), std::sin(angle));
	}

	texture.Create(size, size, size, GL_RG32F);  // 2 float channels
	// texture.Create(size, size, size, 2, Texture::Float);
	texture.SetWrapping(TextureWrappingAxis::U, TextureWrappingParam::Repeat);
	texture.SetWrapping(TextureWrappingAxis::V, TextureWrappingParam::Repeat);
	texture.SetWrapping(TextureWrappingAxis::W, TextureWrappingParam::Repeat);
	texture.SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Nearest);
	texture.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::Nearest);

	texture.Upload(angles.data(), size*size*size*sizeof(glm::vec2));

	const auto T1 = steady_clock::now();
	Log::debug("Generated {}k random angles ({}^3), in {}", buffer_size >> 10, size, duration_cast<microseconds>(T1 - T0));
}

void ClusteredShading::debug_message(GLenum type, std::string_view severity, std::string_view message) const
{
	switch(type)
	{
	case GL_DEBUG_TYPE_ERROR:
		Log::error("GL ERROR: {} {}", severity, message);
		assert(false);
		break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
		Log::error("GL DEPRECATED / {}: {}", severity, message);
		assert(false);
		break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
		Log::error("GL U.B. / {}: {}", severity, message);
		assert(false);
		break;
	case GL_DEBUG_TYPE_PORTABILITY:
		Log::warning("GL PORTING / {}: {}", severity, message);
		break;
	case GL_DEBUG_TYPE_PERFORMANCE:
		Log::warning("GL PERF. / {}: {}", severity, message);
		break;
	case GL_DEBUG_TYPE_OTHER:
		Log::warning("GL mOTHER / {}: {}", severity, message);
		break;
	}

	GLuint program_id = 0;
	GLenum shader_type = 0;

	// "(Vertex|Fragment) shader in program <id>"
	if(auto found = message.find(" shader in program "); found != std::string_view::npos)
	{
		if(found >= 8 and message.substr(found - 8, 8) == "Fragment"sv)
			shader_type = GL_FRAGMENT_SHADER;
		else if(found >= 6 and message.substr(found - 6, 6) == "Vertex"sv)
			shader_type = GL_VERTEX_SHADER;
		if(shader_type != 0)
			program_id = GLuint(std::atol(message.substr(found + 19).data()));

		if(program_id)
		{
			const auto *shader = ShaderRegistry::the().get(program_id);
			if(shader)
			{
				auto name = shader->name();
				//auto file_name = shader->sourceFile(shader_type);
				Log::info("   program {} -> \"{}\"", program_id, name);//, file_name);
			}
		}
	}
}

void ClusteredShading::loadScene([[maybe_unused]] std::string_view name)
{
	// Create scene objects
	const auto origin = component::Transform(glm::mat4(1));

	// auto sponza_model = std::make_shared<StaticModel>();
	// sponza_model->Load(models_path / "sponza2" / "Sponza2.gltf");
	// assert(*sponza_model);
	// _scene.emplace_back(sponza_model, origin);

	// auto testroom_model = std::make_shared<StaticModel>();
	// testroom_model->Load(models_path / "testroom" / "testroom.gltf");
	// assert(*testroom_model);
	// _scene.emplace_back(testroom_model, origin);

	// TODO: where should the actual object be stored?
	//   the "scene" only stores the entity and bounds?  (it's just a "lookup")
	//   I guess the ECS?
	//     - model component
	//     - transform component

	StaticModel cathedral_model;
	cathedral_model.Load("/dl/necropolisfantasygraveyardkit/cathedral_jxl.gltf");
	assert(cathedral_model);
	_scene.add(std::move(cathedral_model), origin);

	StaticModel floor_model;
	floor_model.Load(FileSystem::getResourcesPath() / "models" / "floor.gltf");
	assert(floor_model);
	_scene.add(std::move(floor_model), origin);

	// auto shadow_model = std::make_shared<StaticModel>();
	// shadow_model->Load(FileSystem::getResourcesPath() / "models" / "shadowtest.gltf");
	// assert(*shadow_model);
	// _scene.emplace_back(shadow_model, origin);

	_entities.compact();
}
