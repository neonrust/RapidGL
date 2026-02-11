#include "clustered_shading.h"
#include "filesystem.h"
#include "gl_lookup.h"
#include "hash_combine.h"
#include "input.h"
#include "instance_attributes.h"
#include "log.h"
#include "postprocess.h"
#include "util.h"
#include "gui/gui.h"   // IWYU pragma: keep

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/random.hpp>
// #include <glm/gtx/string_cast.hpp>  // glm::to_string

#include <chrono>
#include <ranges>
#include <vector>
#include "constants.h"

using namespace std::chrono;
using namespace std::literals;

#define IMAGE_UNIT_WRITE 0


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
	_ibl_mip_level(1.2f),
	m_skybox_vao          (0),
	m_skybox_vbo          (0),
	m_bloom_threshold     (1.5f),
	m_bloom_knee          (0.1f),
	m_bloom_intensity     (0.9f),
	m_bloom_dirt_intensity(0),
	m_bloom_enabled       (true),
	_fog_enabled          (false),
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

	_light_mgr.set_falloff_power(50.f);
	_light_mgr.set_radius_power(0.6f);
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
	glClearColor(0.05f, 0.05f, 0.05f, 1);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
	glCullFace(GL_BACK);

	// glLineWidth(2.f); // for wireframes (but >1 not commonly supported)

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	glCreateVertexArrays(1, &_empty_vao);

	// Create camera
	m_camera = Camera(m_camera_fov, 0.1f, 200);
	m_camera.setSize(Window::width(), Window::height());
	m_camera.setPosition({ 0.f, 3.2f, -19.5f });
	m_camera.setOrientationEuler({ 0, 180, 0 });
	// rect light fog stuff:
	// m_camera.setPosition({ 37.5f, 1.7f, 12.4f });
	// m_camera.setOrientationEuler({ 2.5f, 139.8f, 0 });
	// shadow bias study:
	// m_camera.setPosition({ 5.f, 1.0f, 9.8f });
	// m_camera.setOrientationEuler({ 37.f, 60.f, 0 });
	// spot light shadow study
	// m_camera.setPosition({ 10.3f, 2.1f, -3.6f });
	// m_camera.setOrientationEuler({ 1.6f, -30.4f, 0 });
	// light falloff study
	m_camera.setPosition({ 18.f, 3.4f, -12.4f });
	m_camera.setOrientationEuler({ 3.f, -90.f, 0 });


	Log::debug("Horizontal FOV: {}", m_camera.horizontalFov());

	/// Randomly initialize lights (predictably)
	::srand(3281533);//3281991);
	// m_light_counts_ubo.clear();
	createLights();

	const auto models_path = FileSystem::getResourcesPath() / "models";

   /// Create scene objects
	{
		const auto origin = glm::mat4(1);

		// auto sponza_model = std::make_shared<StaticModel>();
		// sponza_model->Load(models_path / "sponza2/Sponza2.gltf");
		// _scene.emplace_back(sponza_model, glm::scale(origin, glm::vec3(sponza_model->GetUnitScaleFactor() * 30.0f)));

		auto testroom_model = std::make_shared<StaticModel>();
		testroom_model->Load(models_path / "testroom" / "testroom.gltf");
		assert(*testroom_model);
		_scene.emplace_back(testroom_model, origin);

		// auto default_cube = std::make_shared<StaticModel>();
		// default_cube->Load(models_path / "default-cube.gltf");
		// assert(*default_cube);
		// _scene.emplace_back(default_cube, origin);

		// auto floor = std::make_shared<StaticModel>();
		// floor->Load(models_path / "floor.gltf");
		// _scene.emplace_back(floor, glm::mat4(1));
	}

	{
		const auto origin = glm::mat4(1);

		const auto light_meshes = models_path / "lights";

		for(const auto &light_type: { LIGHT_TYPE_RECT, LIGHT_TYPE_TUBE, LIGHT_TYPE_SPHERE, LIGHT_TYPE_DISC })
		{
			auto filename = std::format("{}.gltf", _light_mgr.type_name(uint_fast8_t(light_type)));
			auto model = std::make_shared<StaticModel>();
			model->Load(light_meshes / filename);
			assert(*model);
			_lightModels.emplace_back(model, origin);
		}
		Log::info("Loaded {} light geometries", _lightModels.size());
	}

    /// Prepare lights' SSBOs.
	updateLightsSSBOs();  // initial update will create the GL buffers

    /// Prepare SSBOs related to the clustering (light-culling) algorithm.
	// Stores the screen-space clusters

	// represent all the below stuff into a "render method"
	// init:
	//   m_renderMethod.init(m_clusters_count);
	// render:
	//   m_renderMethod.render(_scenePvs);
	//   howevr, api surface-area would be pretty big; e.g. lights, shaders (& pbr), etc
	// step 1: gather all these ssbo into a struct; clusterRendering.cluster_ssbo


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

    PrecomputeIndirectLight(FileSystem::getResourcesPath() / "textures/skyboxes/IBL" / m_hdr_maps_names[m_current_hdr_map_idx]);
    PrecomputeBRDF(m_brdf_lut_rt);

	calculateShadingClusterGrid();  // will also call prepareClusterBuffers()

	glGenBuffers(1, &m_debug_draw_vbo);


	if(false) // transforming into camera/view space
	{
		m_camera.update(0);  // update the internal transforms

		const auto &u_view = m_camera.viewTransform();
		const auto &u_projection = m_camera.projectionTransform();
		const auto u_inv_projection = glm::inverse(u_projection);
		const auto u_inv_view = glm::inverse(u_view);
		const auto u_cam_pos = m_camera.position();

		const glm::uvec2 screen_size { Window::width(), Window::height() };
		glm::uvec2 screen_pos { 0, 0 };//Window::getWidth()/2 + 1, Window::getHeight()/2 };
		glm::vec2 coord = {
			float(screen_pos.x) / float(screen_size.x),
			float(screen_pos.y) / float(screen_size.y)
		};
		coord = coord*2.f - 1.f; // [ -1, 1 ]


		auto target = u_inv_projection * glm::vec4(coord.x, coord.y, 1, 1);
		auto direction = glm::vec3(u_inv_view * glm::vec4(glm::normalize(glm::vec3(target) / target.w), 0)); // World space

		Log::info("        target: {:.5f}; {:.5f}; {:.5f}; {:.5f}", target.x, target.y, target.z, target.w);
		const auto far_depth = target.z / target.w;
		target = glm::normalize(target);
		Log::info("   norm.target: {:.5f}; {:.5f}; {:.5f}   (max depth: {:.1f})", target.x, target.y, target.z, far_depth);
		Log::info("     direction: {:.5f}; {:.5f}; {:.5f}", direction.x, direction.y, direction.z);

		// Log::info("   u_view: {}", glm::to_string(u_view).c_str());

		const glm::vec3 light_pos { -10, 2.f, 0 };
		Log::info("  camera[ws]: {:.5f}; {:.5f}; {:.5f}", u_cam_pos.x, u_cam_pos.y, u_cam_pos.z);
		Log::info("   light[ws]: {:.5f}; {:.5f}; {:.5f}", light_pos.x, light_pos.y, light_pos.z);
		auto light_pos_cs = glm::vec3(u_view * glm::vec4(light_pos, 1));
		Log::info("   light[cs]: {:.5f}; {:.5f}; {:.5f}", light_pos_cs.x, light_pos_cs.y, light_pos_cs.z);


		std::exit(EXIT_SUCCESS);
	}

	if(false)  // create space vectors to define transform test
	{
		glm::vec3 light_center { 1, 2, 3 };
		glm::vec3 light_direction { 1, 0, 0 };

		glm::vec3 space_x;
		glm::vec3 space_y;
		glm::vec3 space_z = light_direction;
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
		glm::mat4 cone_space{
			glm::vec4(space_x, 0),
			glm::vec4(space_y, 0),
			glm::vec4(space_z, 0),
			glm::vec4(light_center, 1)
		};

		Log::info("        X = {:.3f}; {:.3f}; {:.3f}", space_x.x, space_x.y, space_x.z);
		Log::info("        Y = {:.3f}; {:.3f}; {:.3f}", space_y.x, space_y.y, space_y.z);
		Log::info("        Z = {:.3f}; {:.3f}; {:.3f}", space_z.x, space_z.y, space_z.z);

		glm::vec3 ray_direction = glm::normalize(glm::vec3(1, 0, 0));

		auto cone_ray = cone_space * glm::vec4(ray_direction, 0);

		Log::info(" cone ray = {:.3f}; {:.3f}; {:.3f}", cone_ray.x, cone_ray.y, cone_ray.z);
		std::exit(EXIT_SUCCESS);
	}

	if(false)
	{
		const auto space = make_common_space_from_direction({ 0, 0, -1 });
		Log::info("        X = {:.3f}; {:.3f}; {:.3f}", space[0].x, space[0].y, space[0].z);
		Log::info("        Y = {:.3f}; {:.3f}; {:.3f}", space[1].x, space[1].y, space[1].z);
		Log::info("        Z = {:.3f}; {:.3f}; {:.3f}", space[2].x, space[2].y, space[2].z);
		std::exit(EXIT_SUCCESS);
	}

	if(false)  // cone intersection test
	{
		struct Cone
		{
			glm::vec3 center;
			float radius;
			glm::vec3 axis;
			float angle;
		};
		const Cone cone {
			.center = { 0, 0, 0 },
			.radius = 10,   // not used in this test
			.axis = glm::vec3{0, 0, 1},
			.angle = glm::radians(45.f),
		};
		const glm::vec3 ray_start { 2, 0, -5 };
		const auto ray_dir = glm::normalize(glm::vec3{ 0, 0, -1 });

		std::puts("-----------------------------------------------------");

		Log::info("cone center : {:.1f}; {:.1f}; {:.1f}", cone.center.x, cone.center.y, cone.center.z);
		Log::info("cone axis   : {:.1f}; {:.1f}; {:.1f}", cone.axis.x, cone.axis.y, cone.axis.z);
		Log::info("cone angle  : {:.1f}", glm::degrees(cone.angle));
		Log::info("ray start   : {:.1f}; {:.1f}; {:.1f}", ray_start.x, ray_start.y, ray_start.z);
		Log::info("ray dir     : {:.1f}; {:.1f}; {:.1f}", ray_dir.x, ray_dir.y, ray_dir.z);

		glm::vec3 center_to_ray = ray_start - cone.center; // aka CO
		float distance_sq = glm::dot(center_to_ray, center_to_ray);

		float cos_theta = std::cos(cone.angle);
		float cos_theta_sq = cos_theta*cos_theta;
		float dir_axis_dot = glm::dot(ray_dir, cone.axis);
		float CO_axis_dot = glm::dot(center_to_ray, cone.axis);

		float A = dir_axis_dot*dir_axis_dot - cos_theta_sq;
		float B = 2 * (dir_axis_dot*CO_axis_dot - glm::dot(ray_dir, center_to_ray)*cos_theta_sq);
		float C = CO_axis_dot*CO_axis_dot - distance_sq*cos_theta_sq;

		Log::info("    A = {:.3f}", A);
		Log::info("    B = {:.3f}", B);
		Log::info("    C = {:.3f}", C);

		float discriminant = B*B - 4*A*C;
		if(discriminant < 0)
			Log::warning("no intersection");
		else
		{
			Log::info("discriminant = {:.3f}", discriminant);
			float sqrt_discriminant = std::sqrt(discriminant);
			float t1 = (-B - sqrt_discriminant) / (2*A);
			float t2 = (-B + sqrt_discriminant) / (2*A);

			auto ray_point = [&ray_start, &ray_dir](float t) {
				return ray_start + ray_dir*t;
			};
			auto p1 = ray_point(t1);
			Log::info("  t1 = {:.3f}  ->  {:.2f}; {:.2f}; {:.2f}", t1, p1.x, p1.y, p1.z);
			auto p2 = ray_point(t2);
			Log::info("  t2 = {:.3f}  ->  {:.2f}; {:.2f}; {:.2f}", t2, p2.x, p2.y, p2.z);
		}

		std::exit(EXIT_SUCCESS);
	}

	if(false)  // cone spherical cap intersection test
	{
		struct Cone
		{
			glm::vec3 center;
			float radius;
			glm::vec3 axis;
			float angle;
		};
		const Cone cone {
			.center = { 0, 0, 0 },
			.radius = 30,
			.axis = glm::vec3{0, 0, 1},
			.angle = glm::radians(30.f),
		};
		const glm::vec3 ray_start { -12, 0, -10 };
		const auto ray_dir = glm::normalize(glm::vec3{ 0, 0, 1 }) * glm::angleAxis(glm::radians(-20.f), AXIS_Y);

		auto ray_point = [&ray_start, &ray_dir](float t)
		{
			return ray_start + ray_dir*t;
		};

		std::puts("-----------------------------------------------------");

		Log::info("cone center  : {:.1f}; {:.1f}; {:.1f}", cone.center.x, cone.center.y, cone.center.z);
		Log::info("cone axis    : {:.1f}; {:.1f}; {:.1f}", cone.axis.x, cone.axis.y, cone.axis.z);
		Log::info("cone angle   : {:.1f}   radius: {:.1f}", glm::degrees(cone.angle), cone.radius);
		Log::info("ray start    : {:.1f}; {:.1f}; {:.1f}", ray_start.x, ray_start.y, ray_start.z);
		Log::info("ray dir      : {:.1f}; {:.1f}; {:.1f}", ray_dir.x, ray_dir.y, ray_dir.z);
		const auto ray_end = ray_point(50);
		Log::info("ray end @ 50 : {:.1f}; {:.1f}; {:.1f}", ray_end.x, ray_end.y, ray_end.z);

		glm::vec3 center_to_ray = ray_start - cone.center; // aka CO

		float A = 1;
		float B = 2 * glm::dot(center_to_ray, ray_dir);
		float C = glm::dot(center_to_ray, center_to_ray) - cone.radius*cone.radius;

		Log::info("    A = {:.3f}", A);
		Log::info("    B = {:.3f}", B);
		Log::info("    C = {:.3f}", C);

		float discriminant = B*B - 4*A*C;
		if(discriminant < 0)
			Log::warning("NO INTERSECTION");
		else
		{
			Log::info("discriminant = {:.3f}", discriminant);
			float sqrt_discriminant = std::sqrt(discriminant);
			float t1 = (-B - sqrt_discriminant) / (2*A);
			float t2 = (-B + sqrt_discriminant) / (2*A);

			auto point_inside_cone = [&cone](const glm::vec3 &point)
			{
				//    point
				//   /
				//  C--------| axis
				//           ^ radius

				glm::vec3 to_center = point - cone.center;
				float len = length(to_center);

					   // outside the entire sphere?
				if(len > cone.radius)
					return false;

					   // cos of the angle between the vector and the cone's axis
				float cos_theta = glm::dot(to_center, cone.axis) / len;

					   // compare with the cosine of the cone's half-angle (i.e. must be less than 90 degrees)
					   // (larger cos value means sharper angle)
				return cos_theta >= std::cos(cone.angle);
			};

			bool got_point = false;

			if(t1 >= 0)
			{
				auto p1 = ray_point(t1);
				if(point_inside_cone(p1))
				{
					Log::info("  t1 = {:.3f}  ->  {:.2f}; {:.2f}; {:.2f}", t1, p1.x, p1.y, p1.z);
					got_point = true;
				}
			}
			if(t2 >= 0)
			{
				auto p2 = ray_point(t2);
				if(point_inside_cone(p2))
				{
					Log::info("  t2 = {:.3f}  ->  {:.2f}; {:.2f}; {:.2f}", t2, p2.x, p2.y, p2.z);
					got_point = true;
				}
			}
			if(got_point)
				Log::warning("INTERSECTION");
			else
				Log::warning("NO INTERSECTION");
		}

		std::exit(EXIT_SUCCESS);
	}

	if(false)
	{
		const float u_near_z = 0.1f;
		const float u_far_z = 200.f;
		auto linear_depth = [u_near_z, u_far_z](float depth) -> float {
			// convert a depth texture sample in range (-1, 1) to linear depth, ranged (near_z, far_z).
			float ndc          = depth * 2.f - 1.f;
			float linear_depth = 2.f * u_near_z * u_far_z / (u_far_z + u_near_z - ndc * (u_far_z - u_near_z));

			return linear_depth;
		};

		const auto inv_projection = glm::inverse(m_camera.projectionTransform());
		float linearDepth = linear_depth(-1.f);
		glm::vec2 texCoord { 0.25f, 0.25f };
		auto pos = glm::vec4(texCoord * 2.f - 1.f, linearDepth * 2.f - 1.f, 1);
		glm::vec4 wpos = inv_projection * pos;
		wpos /= wpos.w;

		Log::info("depth pos  : {:.1f}; {:.1f}; {:.1f}", pos.x, pos.y, pos.z);
		Log::info("world  pos : {:.5f}; {:.5f}; {:.5f}", wpos.x, wpos.y, wpos.z);

		std::exit(EXIT_SUCCESS);
	}

	if(false)
	{
		glm::vec3 spot_pos { 0, 0, 0 };
		glm::vec3 spot_dir { 0, 0, 1 };
		glm::vec3 point { 0.f, 0, 5.f };
		float outer_angle = glm::radians(45.f);
		float inner_angle = glm::radians(22.5f);

		auto spot_angle_att = [](glm::vec3 to_point, glm::vec3 spot_dir, float outer_angle, float inner_angle) {
			float cos_outer   = std::cos(outer_angle);
			float spot_scale  = 1.f / std::max(std::cos(inner_angle) - cos_outer, 1e-5f);
			float spot_offset = -cos_outer * spot_scale;

			float cd          = glm::dot(spot_dir, to_point);
			float attenuation = glm::clamp(cd * spot_scale + spot_offset, 0.f, 1.f);

			return attenuation * attenuation;
		};


		Log::info("spot  : {:.1f}; {:.1f}; {:.1f}  {:.0f}° - {:.0f}°", spot_pos.x, spot_pos.y, spot_pos.z, glm::degrees(inner_angle), glm::degrees(outer_angle));

		for(auto idx = 0; idx <= 8; ++idx)
		{
			point.x = float(idx) * 0.2f;
			auto to_point = glm::normalize(point - spot_pos);
			float att = spot_angle_att(to_point, spot_dir, outer_angle, inner_angle);
			Log::info("point : {:.1f}; {:.1f}; {:.1f}  --> {:f}", point.x, point.y, point.z, att);
		}

		std::exit(EXIT_SUCCESS);
	}
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

	if(Input::wasKeyPressed(KeyCode::C))
		m_debug_draw_cluster_grid = not m_debug_draw_cluster_grid;

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

	if(_pov_light_id != NO_LIGHT_ID and Input::wasKeyPressed(KeyCode::P))
		_light_mgr.set_enabled(_pov_light_id, not _light_mgr.is_enabled(_pov_light_id));


	if (Input::wasKeyReleased(KeyCode::F12))
    {
		// TODO: add "slot numer" suffix
        std::string filename = "27_clustered_shading";
		if (take_screenshot_png(filename, Window::width(), Window::height()))
			Log::info("Screenshot: {}.png", filename);
        else
			Log::error("Failed screenshot [{}]", filename);
    }

	if (Input::wasKeyReleased(KeyCode::Space))
        m_animate_lights = !m_animate_lights;
}

void ClusteredShading::update(double delta_time)
{
	_running_time += seconds_f(delta_time);

	m_camera.update(delta_time);

	if(_pov_light_id != NO_LIGHT_ID)
	{
		auto L = _light_mgr.get_by_id(_pov_light_id);
		L.position = m_camera.position() + m_camera.forwardVector() * _pov_light_distance;
		L.direction = m_camera.forwardVector();
		_light_mgr.set(_pov_light_id, L);
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
		_fog_enabled = not _fog_enabled;
	if(Input::wasKeyPressed(KeyCode::B))
		m_bloom_enabled = not m_bloom_enabled;

	if(adjust_position != 0 or adjust_angle  != 0 or adjust_energy != 0)
	{
		for(LightIndex light_index = 0; light_index <  _light_mgr.size(); ++light_index)
		{
			const auto &[light_id, L] =_light_mgr.at(light_index);
			if(light_id == _pov_light_id)
				continue;

			auto Lmut = L;

			Lmut.position.z += adjust_position;

			if(adjust_angle  != 0 and IS_SPOT_LIGHT(Lmut))
			{
				float new_angle = std::max(Lmut.outer_angle + adjust_angle, glm::radians(3.f));  // noise becomes apparent at smaller degrees
				_light_mgr.set_spot_angle(Lmut, new_angle);
				Log::info("  [{}] spot angle: {:.1f}  {:.1f}   P:{:.0f}   R:{:.0f}", light_id,
						   glm::degrees(Lmut.outer_angle),
						   glm::degrees(Lmut.inner_angle),
						   Lmut.intensity,
						   Lmut.affect_radius);
			}

			if(adjust_energy != 0)
			{
				if(adjust_energy > 0)
					_light_mgr.set_intensity(Lmut, Lmut.intensity * adjust_energy);
				else
					_light_mgr.set_intensity(Lmut, Lmut.intensity / -adjust_energy);
			}

			_light_mgr.set(light_id, Lmut);
		}
	}
	else if (m_animate_lights)
    {
		// time_accum  += float(delta_time * m_animation_speed);
		const auto orbit_mat = glm::rotate(glm::mat4(1), glm::radians(float(delta_time)) * 2.f * m_animation_speed, AXIS_Y);
		const auto spin_mat = glm::angleAxis(glm::radians(float(15*delta_time * m_animation_speed)), AXIS_Y);

		// auto spin_mat  = glm::rotate(glm::mat4(1), glm::radians(60.f * float(delta_time)) * 2.f * m_animation_speed, AXIS_Y);

		// TODO: need API to update a specific light OR all lights (by iteration)

		for(LightIndex light_index = 0; light_index <  _light_mgr.size(); ++light_index)
		{
			const auto &[light_id, L] =_light_mgr.at(light_index);
			if(light_id == _pov_light_id)
				continue;
			auto Lmut = L;

			if(IS_POINT_LIGHT(Lmut) or IS_SPHERE_LIGHT(Lmut))
				Lmut.position = orbit_mat * glm::vec4(L.position, 1); // orbit around the world origin
			else
				_light_mgr.transform(Lmut, spin_mat);

			_light_mgr.set(light_id, Lmut);
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

	auto z_offset = 0.f;
	static constexpr auto z_step = 12.f;
	auto x_offset = 0.f;

	// _light_mgr.add(DirectionalLightParams{
	// 	.color = { 1.f, 1.f, 1.f },
	// 	.intensity = 50.f,
	// 	.fog = 1.f,
	// 	.shadow_caster = true,
	// 	.direction = glm::normalize(glm::vec3(20, -50, 20)),
	// });

	auto l = _light_mgr.add(SpotLightParams{
		.color = { 1.f, 0.85f, 0.7f },
		.intensity = 100.f,
		.fog = 1.f,
		.shadow_caster = true,
		.position = m_camera.position(),  // will be kept up to date in update()
		.direction = AXIS_X,              // will be kept up to date in update()
		.outer_angle = glm::radians(45.f),
		.inner_angle = glm::radians(35.f),
	});
	_pov_light_id = l.id();


	for(auto idx = 0u; idx < 0; ++idx)
	{
		const auto rand_color= hsv2rgb(
			float(Util::RandomDouble(1, 360)),       // hue
			float(Util::RandomDouble(0.2f, 0.8f)),   // saturation
			1.f                                      // value (brightness)
		);
		// const auto rand_pos = Util::RandomVec3(room_min, room_max);
		const auto rand_pos = glm::vec3(-13.f + x_offset, 2.5f, 12.f - z_offset );
		z_offset += z_step;

		if(z_offset > z_step*5 + 1)
		{
			z_offset = 0;
			x_offset += 22.f;
		}

		const auto rand_intensity = 100.f;//float(Util::RandomDouble(10, 100));

		auto light_type = 3 + (idx % 4);
		// light_type = LIGHT_TYPE_DISC;

		LightID l_id;
		std::string_view type_name;
		switch(light_type)
		{
		case LIGHT_TYPE_POINT:
		case LIGHT_TYPE_DIRECTIONAL:
		{
			auto l = _light_mgr.add(PointLightParams{
				.color = rand_color,
				.intensity = rand_intensity,
				.fog = 1.f,
				.shadow_caster = true,
				.position = rand_pos,
			});
			type_name = _light_mgr.type_name<decltype(l)>();
			l_id = l.id();
		}
		break;
		case LIGHT_TYPE_SPOT:
		{
			auto l = _light_mgr.add(SpotLightParams{
				.color = rand_color,
				.intensity = rand_intensity,
				.fog = 1.f,
				.shadow_caster = true,
				.position = rand_pos,
				.direction = AXIS_X, //glm::normalize(Util::RandomVec3(0, 1)),
				.outer_angle = glm::radians(25.f),
				.inner_angle = glm::radians(15.f),
			});
			type_name = _light_mgr.type_name<decltype(l)>();
			l_id = l.id();
		}
		break;
		case LIGHT_TYPE_RECT:
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
			type_name = _light_mgr.type_name<decltype(l)>();
			l_id = l.id();
		}
		break;
		case LIGHT_TYPE_TUBE:
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
			type_name = _light_mgr.type_name<decltype(l)>();
			l_id = l.id();
		}
		break;
		case LIGHT_TYPE_SPHERE:
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
			type_name = _light_mgr.type_name<decltype(l)>();
			l_id = l.id();
		}
		break;
		case LIGHT_TYPE_DISC:
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
			type_name = _light_mgr.type_name<decltype(l)>();
			l_id = l.id();
		}
		break;
		default:
			assert(false);
		}

		const auto &L = _light_mgr.get_by_id(l_id);

		Log::info("light[{:2}] {:5} @ {:5.1f}; {:3.1f}; {:5.1f}  {:3},{:3},{:3}  {:4.0f} (R:{:.1f})",
				   l_id,
				   type_name,
				   rand_pos.x, rand_pos.y, rand_pos.z,
				   uint(rand_color.r*255), uint(rand_color.g*255), uint(rand_color.b*255),
				   rand_intensity,
				   L.affect_radius);
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
	const auto now = steady_clock::now();

	downloadAffectingLightSet();

	m_camera.setFov(m_camera_fov);



	// determine visible meshes  (only if camera or meshes moved (much))
	cullScene(m_camera);
	// TODO: to make it more general, the culling result (_scenePvs)
	//   could be stored in the 'view' (e.g. camera or a point light shadow map cube face)
	//   or "in relation to" the view, e.g. a map of ID -> PVS
	//     _scene_cull_sets[m_camera.entity_id()] = _scenePvs;
	//     _scene_cull_sets[(light.entity_id() << 3) + face] = _scenePvs;


	_gl_timer.start();

	renderShadowMaps();

	m_shadow_time.add(_gl_timer.elapsed<microseconds>(true));


	// Depth pre-pass  (only if camera/meshes moved, probably always)
	renderDepth(m_camera.projectionTransform() * m_camera.viewTransform(), m_depth_pass_rt);


	// Blit depth info to our main render target
	m_depth_pass_rt.copyTo(_rt, RenderTarget::DepthBuffer, TextureFilteringParam::Nearest);

	m_depth_time.add(_gl_timer.elapsed<microseconds>(true));


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

	m_cluster_find_time.add(_gl_timer.elapsed<microseconds>(true));
	// ------------------------------------------------------------------
	m_cull_lights_args_ssbo.clear();
	m_collect_nonempty_clusters_shader->setUniform("u_num_clusters"sv, m_cluster_count);
	m_collect_nonempty_clusters_shader->invoke(size_t(std::ceil(float(m_cluster_count) / 1024.f)));

	m_cluster_index_time.add(_gl_timer.elapsed<microseconds>(true));
	// ------------------------------------------------------------------

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

	m_light_cull_time.add(_gl_timer.elapsed<microseconds>(true));
	// ------------------------------------------------------------------

	_rt.bindRenderTarget(RenderTarget::ColorBuffer);

	renderSceneShading(m_camera);
	m_shading_time.add(_gl_timer.elapsed<microseconds>(true));

	if(m_draw_surface_lights_geometry)
		renderLightGeometry(); // to '_rt'

	renderSkybox(); // to '_rt'

	m_skybox_time.add(_gl_timer.elapsed<microseconds>(true));

	if(_fog_enabled and _fog_density > 0)
	{
		m_volumetrics_pp.setViewParams(m_camera, m_camera.farPlane() * s_light_volumetric_fraction);
		m_volumetrics_pp.cull_lights();
		m_volumetrics_cull_time.add(_gl_timer.elapsed<microseconds>(true));

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
			shader.setUniform("u_csm_cascade_near_far"sv, csm.near_far_plane);
			shader.setUniform("u_csm_light_radius_uv"sv,  csm.light_radius_uv); // PCSS
			// needed in vertex shader
			shader.setUniform("u_csm_light_view_space"sv, csm.light_view);
			shader.setUniform("u_csm_light_clip_space"sv, csm.light_view_projection); // also in SSBO, but we'd like to avoid accessing that from the vertex shader
		}
		else
			shader.setUniform("u_csm_num_cascades"sv,     0);


		_shadow_atlas.bindDepthTextureSampler(22); // just using single-sample, no PCF
		m_depth_pass_rt.bindDepthTextureSampler(2);

		m_volumetrics_pp.inject();

		m_volumetrics_inject_time.add(_gl_timer.elapsed<microseconds>(true));

		m_volumetrics_pp.accumulate();
		m_volumetrics_accum_time.add(_gl_timer.elapsed<microseconds>(true));

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

		m_volumetrics_render_time.add(_gl_timer.elapsed<microseconds>(true));
	}
	else
	{
		_pp_full_rt.clear();

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

	// Bloom
    if (m_bloom_enabled)
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

	m_tonemap_time.add(_gl_timer.elapsed<microseconds>(true));


	// draw the final result to the screen
	draw2d(_final_rt.color_texture(), BlendMode::Replace);


	_gl_timer.start();

	if(m_debug_draw_aabb)
		debugDrawSceneBounds();
	if(m_debug_draw_light_markers)
		debugDrawLightMarkers();
	if(m_debug_draw_cluster_grid)
		debugDrawClusterGrid();

	m_debug_draw_time.add(_gl_timer.elapsed<microseconds>(true));
}

void ClusteredShading::renderShadowMaps()
{
	// render shadow-maps if light or meshes within its radius/frustum moved (the latter is TODO)

	glCullFace(GL_FRONT);  // render only back faces
	glEnable(GL_SCISSOR_TEST);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_FALSE, GL_FALSE);  // writing 2-component normals
	glDepthFunc(GL_LESS);

	const auto now = steady_clock::now();

	static steady_clock::time_point last_eval_time;

	// if there's a sun allocated, update its shadow parameters
	if(const auto &sun_id = _shadow_atlas.sun_id(); sun_id != NO_LIGHT_ID)
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
		_shadow_atlas.eval_lights(_lightsPvs, m_camera.position(), m_camera.forwardVector());
		m_shadow_alloc_time.add(duration_cast<microseconds>(steady_clock::now() - T0));
	}
	m_shadow_alloc_time.add(microseconds(0));

	// light projections needs to be updated more often than the atlas allocations.
	//   it also needs to be updated when the light has moved (new view-projection),
	// but for simplicity, we're updating all the allocated lights in one go.
	_shadow_atlas.update_shadow_params();


	bool did_barrier = false;
	auto barrier = []() {
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	};

	_light_shadow_maps_rendered = 0;
	_shadow_atlas_slots_rendered = 0;

	// TODO: limit the number of shadow maps to render in one frame, e.g. top N most important lights
	//    the remaining will still by "dirty" so they will be rendered eventually.
	//    this implies a 2-pass algo; "allocated_lights() is an unordered mapping.

	for(auto &[light_id, atlas_light]: _shadow_atlas.allocated_lights())
	{
		const auto &light = _light_mgr.get_by_id(light_id);

		// if this light did not contribute to the (previous) frame, no need to render its shadow map
		const auto light_index = _light_mgr.light_index(light_id);
		if(not _affecting_lights.contains(light_index))
			continue;

		auto light_hash = _light_mgr.hash(light);
		if(IS_DIR_LIGHT(light))  // cascades are also affected by the camera's frustum
			light_hash = hash_combine(light_hash, m_camera.hash());

		// TODO: check whether scene objects inside the light's sphere or frustum are dynamic (as opposed to static)
		//   preferably, this should be per slot (cube face for point lights)
		const auto has_dynamic = false; //_scene_culler.pvs(light_id).has(SceneObjectType::Dynamic);

		// TODO: keep TWO shadow maps, one static-object only (cache) and one active (all objects)
		//   1. render static objects first, to both maps (copy to active afterwards?)
		//   2. render dynamic objects to active map only
		//   next frame:
		//   - if dynamic objects moved: copy static to active & render dynamic objects
		//   - naturally, if light moved/changed, go to step 1.

		if(_shadow_atlas.should_render(atlas_light, now, light_hash, has_dynamic))
		{
			// render shadow map(s) for this light

			const auto shadow_index = GET_SHADOW_IDX(light);

			for(auto slot_idx = 0u; slot_idx < atlas_light.num_slots; ++slot_idx)
			{
				const auto &slot_rect = atlas_light.slots[slot_idx].rect;

				if(true)//_scene_culler.pvs(light_id, idx).has(SceneObjectType::Dynamic))
				{
					_shadow_atlas.bindRenderTarget(slot_rect);
					if(not did_barrier)
					{
						barrier();
						did_barrier = true;
					}

					renderSceneShadow(shadow_index, slot_idx);
					++_shadow_atlas_slots_rendered;
				}
			}

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

	// TODO: (optionally) render point/spots in some way? e.g. a circular glare?

	static_assert(LIGHT_TYPE__COUNT == 7);
	for(const auto &light_type: { LIGHT_TYPE_RECT, LIGHT_TYPE_TUBE, LIGHT_TYPE_SPHERE, LIGHT_TYPE_DISC })
	{
		// collect lights geometries for this light type
		surf_attrs.clear();
		for(auto light_index: _lightsPvs)
		{
			const auto &L = _light_mgr[light_index];
			if(IS_ENABLED(L) and IS_VISIBLE_SURFACE(L) and IS_LIGHT_TYPE(L, light_type))
			{
				switch(GET_LIGHT_TYPE(L))
				{
				case LIGHT_TYPE_RECT:
				{
					// reconstruct the orientation from direction + outer_angle
					const auto orientation = glm::mat4_cast(glm::quat{ L.shape_data[4].w, L.shape_data[4].x, L.shape_data[4].y, L.shape_data[4].z });
					const auto size = glm::vec3{ L.outer_angle, L.inner_angle, 1.f };

					auto tfm = glm::translate(glm::mat4(1), L.position);
					tfm *= orientation;
					tfm = glm::scale(tfm, size);

					surf_attrs.emplace_back(tfm, L.color*L.intensity, IS_DOUBLE_SIDED(L));
				}
				break;
				case LIGHT_TYPE_TUBE:
				{
					const auto orientation = glm::mat4_cast(glm::quat{ L.shape_data[4].w, L.shape_data[4].x, L.shape_data[4].y, L.shape_data[4].z });
					const auto thickness = L.shape_data[2].x;
					const auto size = glm::vec3(thickness, thickness, L.outer_angle);

					auto tfm = glm::translate(glm::mat4(1), L.position);
					tfm *= orientation;
					tfm = glm::scale(tfm, size);

					surf_attrs.emplace_back(tfm, L.color*L.intensity, true);
				}
				break;
				case LIGHT_TYPE_SPHERE:
				{
					auto tfm = glm::translate(glm::mat4(1), L.position);
					const auto radius = L.shape_data[0].x;
					const auto size = glm::vec3(radius);
					tfm = glm::scale(tfm, size);

					surf_attrs.emplace_back(tfm, L.color*L.intensity, true);
				}
				break;
				case LIGHT_TYPE_DISC:
				{
					const auto orientation = glm::mat4_cast(glm::quat{ L.shape_data[4].w, L.shape_data[4].x, L.shape_data[4].y, L.shape_data[4].z });
					const auto radius = L.shape_data[0].x;
					const auto size = glm::vec3(1, radius, radius);

					auto tfm = glm::translate(glm::mat4(1), L.position);
					tfm *= orientation;
					tfm = glm::scale(tfm, size);

					surf_attrs.emplace_back(tfm, L.color*L.intensity, false);
				}
				break;
				}
			}
		}
		if(surf_attrs.empty())
			continue;

		auto model_index = light_type - LIGHT_TYPE_RECT;

		auto &model = _lightModels[model_index].model;

		auto &inst_attrs = model->instance_attributes(sizeof(SurfaceLightAttrs));
		if(not inst_attrs)
		{
			inst_attrs.skip(4);  // TODO: query the mesh to find how many locations are used. Also, this must match the shader
			inst_attrs.add<glm::mat4>("transform"sv);
			inst_attrs.add<glm::vec3>("color-intensity"sv);
			inst_attrs.add<uint32_t>("double-sided");
		}

		inst_attrs.load<SurfaceLightAttrs>(surf_attrs);

		model->Render(*m_light_geometry_shader, uint32_t(surf_attrs.size()));
	}

	// draw "sun"   (or just draw all directional lights?)
	if(auto sun_id = _shadow_atlas.sun_id(); sun_id != NO_LIGHT_ID and _light_mgr.is_enabled(sun_id))  // TODO: LightManager should probably keep track of this
	{
		// draw a "sun" disc (or moon, I suppose); only supports one
		const auto &L = _light_mgr.get_by_id(sun_id);
		const auto cam_pos = m_camera.position();
		const auto cam_up = m_camera.upVector();           // e.g. AXIS_Y
		const auto sun_dir_world = L.direction; // direction light -> scene?
		// Make sure sun_dir points from camera toward the sun position. If sun_dir is light direction
		// (pointing from sun -> scene) use -sun_dir when computing position below.

		const float distance = m_camera.farPlane() * 0.99f;
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
		surf_attrs.emplace_back(sun_model, L.color*L.intensity, false);

		auto &model = _lightModels[2].model;  // render as a disc  (but currently as a sphere, for testing)
		auto &inst_attrs = model->instance_attributes(sizeof(SurfaceLightAttrs));
		if(not inst_attrs)
		{
			inst_attrs.skip(4);  // TODO: query the mesh to find how many locations are used. Also, this must match the shader
			inst_attrs.add<glm::mat4>("transform"sv);
			inst_attrs.add<glm::vec3>("color-intensity"sv);
			inst_attrs.add<uint32_t>("double-sided");
		}

		inst_attrs.load<SurfaceLightAttrs>(surf_attrs);

		model->Render(*m_light_geometry_shader, uint32_t(surf_attrs.size()));
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

const std::vector<StaticObject> &ClusteredShading::cullScene(const Camera &view)
{
	const auto T0 = steady_clock::now();
	// TODO: in theory, this could be done in multiple threads
	//   however, a space partitioning scheme is probably a better (first) step

	_scenePvs.clear();
	_scenePvs.reserve(256); // a guesstimate how many objects are visible (maybe a % of total count?)

	// perform frustum culling of all objects in the scene (or a partition thereof)

	const auto view_pos = view.position();
	const auto &frustum = view.frustum();

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

				if(GET_LIGHT_TYPE(L) == LIGHT_TYPE_DIRECTIONAL)
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

	// TODO do something like:
	//    view.near(_scene)  i.e. everything in range of the view
	for(const auto &obj: _scene)
	{
		auto visible = intersect::check(frustum, obj.model->aabb(), obj.transform);
		if(visible)
			_scenePvs.push_back(obj);
	}

	// TODO: cull invisible objects in the scene, using any method available
	//   e.g. frustum and/or occlusion culling

	std::sort(_scenePvs.begin(), _scenePvs.end(), [view_pos](const StaticObject &A, const StaticObject &B) {
		// TODO: sort back-to-front
		//   e.g. by closest part of AABB/OBB/bounding sphere
		//   for now, just use AABB center for simplicity
		const auto offsetA = view_pos - A.model->aabb().center();
		const auto sqDistanceA = glm::dot(offsetA, offsetA);
		const auto offsetB = view_pos - B.model->aabb().center();
		const auto sqDistanceB = glm::dot(offsetB, offsetB);
		return sqDistanceA < sqDistanceB;
	});


	m_cull_scene_time.add(duration_cast<microseconds>(steady_clock::now() - T0));

	return _scenePvs;
}

void ClusteredShading::renderScene(const glm::mat4 &view_projection, Shader &shader, MaterialCtrl materialCtrl)
{
	for(const auto &obj: _scenePvs)
	{
		shader.setUniform("u_mvp"sv,   view_projection * obj.transform);
		shader.setUniform("u_model"sv, obj.transform);
		shader.setUniform("u_normal_matrix"sv, glm::transpose(glm::inverse(glm::mat3(obj.transform))));

		if(materialCtrl == UseMaterials)
			obj.model->Render(shader);
		else
			obj.model->Render();
	}
}


void ClusteredShading::renderDepth(const glm::mat4 &view_projection, RenderTarget::Texture2d &target, const glm::ivec4 &rect)
{
	target.bindRenderTarget(rect, RenderTarget::DepthBuffer);

	glDepthMask(GL_TRUE);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthFunc(GL_LESS);

    m_depth_prepass_shader->bind();

	renderScene(view_projection, *m_depth_prepass_shader, NoMaterials);
}

void ClusteredShading::renderSceneShadow(uint_fast16_t shadow_index, uint32_t shadow_map_index, bool dynamic_only)
{
	// TODO: ideally, only render objects whose AABB intersects with the light's projection (frustum)

	m_shadow_depth_shader->bind();

	m_shadow_depth_shader->setUniform("u_shadow_slot_index"sv, uint32_t(shadow_index)); // for 'mvp'
	m_shadow_depth_shader->setUniform("u_shadow_map_index"sv, shadow_map_index);

	for(const auto &obj: _scenePvs)
	{
		(void)dynamic_only;
		// if(dynamic_only and not obj.is_dynamic)
		// 	continue;
		m_shadow_depth_shader->setUniform("u_model"sv, obj.transform);
		m_shadow_depth_shader->setUniform("u_normal_matrix"sv, glm::transpose(glm::inverse(glm::mat3(obj.transform))));

		obj.model->Render();
	}
}

void ClusteredShading::renderSceneShading(const Camera &camera)
{
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthFunc(GL_EQUAL);   // only draw pixels which exactly match the depth pre-pass

	auto &shader = *m_clustered_pbr_shader;

	shader.bind();

	camera.setUniforms(shader);
	shader.setUniform("u_cluster_resolution"sv,         m_cluster_resolution);
	shader.setUniform("u_cluster_size_ss"sv,            glm::uvec2(m_cluster_block_size));
	shader.setUniform("u_log_cluster_res_y"sv,          m_log_cluster_res_y);
	shader.setUniform("u_light_max_distance"sv,         m_camera.farPlane() * s_light_affect_fraction);
	shader.setUniform("u_shadow_max_distance"sv,        m_camera.farPlane() * s_light_shadow_affect_fraction);
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

	if(const auto &csm = _shadow_atlas.csm_params(); csm)
	{
		shader.setUniform("u_csm_num_cascades"sv,     uint32_t(csm.num_cascades));
		shader.setUniform("u_csm_split_depth"sv,      csm.split_depth);
		shader.setUniform("u_csm_cascade_near_far"sv, csm.near_far_plane);
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

	// we need updated textures (shadow maps) and SSBO data
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

	const auto view_projection = m_camera.projectionTransform() * m_camera.viewTransform();
	renderScene(view_projection, *m_clustered_pbr_shader);

	// Enable writing to the depth buffer
	// glDepthMask(GL_TRUE);
	// glDepthFunc(GL_LESS);
}


void ClusteredShading::debug_message(GLenum type, std::string_view severity, std::string_view message) const
{
	switch(type)
	{
	case GL_DEBUG_TYPE_ERROR:
		Log::error("GL ERROR: {}", severity, message);
		break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
		Log::error("GL DEPRECATED / {}: {}", severity, message);
		break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
		Log::error("GL U.B. / {}: {}", severity, message);
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
