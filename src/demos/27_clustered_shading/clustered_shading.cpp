#include "clustered_shading.h"
#include "filesystem.h"
#include "input.h"
#include "postprocess.h"
#include "util.h"
#include "gui/gui.h"   // IWYU pragma: keep

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/random.hpp>
// #include <glm/gtx/string_cast.hpp>  // glm::to_string

#include <chrono>
#include <vector>

static constexpr glm::vec3 AXIS_X { 1, 0, 0 };
static constexpr glm::vec3 AXIS_Y { 0, 1, 0 };
static constexpr glm::vec3 AXIS_Z { 0, 0, 1 };


using namespace std::chrono;
using namespace std::literals;

#define IMAGE_UNIT_WRITE 0


static float s_spot_outer_angle = 30.f;
static float s_spot_intensity = 2000.f;

namespace hash
{
struct glmv
{
	inline std::size_t operator() (const glm::uvec2 &v) const noexcept
	{
		return std::hash<uint32_t>()(v.x) ^ std::hash<uint32_t>()(v.y);
	}

	inline bool operator() (const glm::uvec2 &l, const glm::uvec2 &r) const noexcept
	{
		return l.x == r.x and l.y == r.y;
	}
};
} // hash

template<typename T>
using uvec2_map = ankerl::unordered_dense::map<glm::uvec2, T, hash::glmv, hash::glmv>;



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

void opengl_message_callback([[maybe_unused]] GLenum source,
							 GLenum type,
							 [[maybe_unused]] GLuint id,
							 GLenum severity,
							 [[maybe_unused]] GLsizei length,
							 const GLchar *message,
							 [[maybe_unused]] const void *userParam)
{
	if(type == GL_DEBUG_TYPE_ERROR)
		std::print(stderr, "GL ERROR: type = {:#x}, severity = {:#x} \"{}\"\n", type, severity, message );
}


using namespace RGL;

ClusteredShading::ClusteredShading() :
	m_simple_clusters_aabb_ssbo("simple-clusters"sv),
	m_lights_ssbo("lights"sv),
	m_light_counts_ubo("light-counts"sv),
	m_shadow_map_params_ssbo("shadow-map-params"sv),
	m_cluster_discovery_ssbo("cluster-discovery"sv),
	m_cull_lights_args_ssbo("cull-lights"sv),
	m_exposure            (0.4f),
	m_gamma               (2.2f),
	m_background_lod_level(1.2f),
	m_skybox_vao          (0),
	m_skybox_vbo          (0),
	m_bloom_threshold     (0.1f),
	m_bloom_knee          (0.1f),
	m_bloom_intensity     (0.5f),
	m_bloom_dirt_intensity(0),
	m_bloom_enabled       (true),
	m_fog_density         (0.f), // [ 0, 0.5 ]   nice-ish value: 0.015
	_ray_march_noise      (1)
{
	m_simple_clusters_aabb_ssbo.setBindIndex(SSBO_BIND_SIMPLE_CLUSTERS_AABB);
	m_lights_ssbo.setBindIndex(SSBO_BIND_LIGHTS);
	m_light_counts_ubo.setBindIndex(UBO_BIND_LIGHT_COUNTS);
	m_shadow_map_params_ssbo.setBindIndex(SSBO_BIND_SHADOW_PARAMS);
	m_cluster_discovery_ssbo.setBindIndex(SSBO_BIND_CLUSTER_DISCOVERY);
	m_cull_lights_args_ssbo.setBindIndex(SSBO_BIND_CULL_LIGHTS_ARGS);
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

void ClusteredShading::init_app()
{
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(opengl_message_callback, 0);

    /// Initialize all the variables, buffers, etc. here.
	glClearColor(0.05f, 0.05f, 0.05f, 1);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
	// glEnable(GL_CULL_FACE);  // stops skybox from working !?
	std::print(stderr, "-------------------- ENABLE FACE CULLING -------------------\n");
	glCullFace(GL_BACK);

	// glLineWidth(2.f); // for wireframes (but >1 not commonly supported)

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	glCreateVertexArrays(1, &_empty_vao);

	// Create camera
	m_camera = Camera(m_camera_fov, 0.1f, 200);
	m_camera.setSize(Window::width(), Window::height());
	// m_camera.setPosition(-8.32222f, 4.5269f, -0.768721f);
	// m_camera.setOrientation(glm::quat(0.634325f, 0.0407623f, 0.772209f, 0.0543523f));
	m_camera.setPosition({ -8.5, 3.3f, 1.1f });
	m_camera.setOrientationEuler({ 0, 117, -0.8f });

    /// Randomly initialize lights
	::srand(3281991);
	m_light_counts_ubo.clear();
	GeneratePointLights();
    GenerateSpotLights();
    GenerateAreaLights();

	/// Create scene objects
	{
		const auto models_path = FileSystem::getResourcesPath() / "models";

		// auto sponza_model = std::make_shared<StaticModel>();
		// sponza_model->Load(models_path / "sponza2/Sponza2.gltf");

		const auto origin = glm::mat4(1);

		// auto world_trans  = glm::mat4(1);
		// 	 world_trans  = glm::scale(world_trans, glm::vec3(sponza_model->GetUnitScaleFactor() * 30.0f));
		// // m_sponza_static_object = StaticObject(sponza_model, world_trans);
		// _scene.emplace_back(sponza_model, world_trans);

		auto testroom_model = std::make_shared<StaticModel>();
		testroom_model->Load(models_path / "testroom" / "white-room.gltf");
		assert(*testroom_model);
		_scene.emplace_back(testroom_model, origin);

		auto default_cube = std::make_shared<StaticModel>();
		default_cube->Load(models_path / "default-cube.gltf");
		assert(*default_cube);
		_scene.emplace_back(default_cube, origin);

		// auto floor = std::make_shared<StaticModel>();
		// floor->Load(models_path / "floor.gltf");
		// _scene.emplace_back(floor, glm::mat4(1));
	}

    /// Prepare lights' SSBOs.
	UpdateLightsSSBOs();  // initial update will create the GL buffers

    /// Prepare SSBOs related to the clustering (light-culling) algorithm.
	// Stores the screen-space clusters

	// represent all the below stuff into a "render method"
	// init:
	//   m_renderMethod.init(m_clusters_count);
	// render:
	//   m_renderMethod.render(_scenePvs);
	//   howevr, api surface-area would be pretty big; e.g. lights, shaders (& pbr), etc
	// step 1: gather all these ssbo into a struct; clusterRendering.cluster_ssbo


    /// Load LTC look-up-tables for area lights rendering
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
		std::print(stderr, "Error: could not load texture {}\n", ltc_lut_mat_path.string());

	m_ltc_amp_lut = std::make_shared<Texture2D>();
    if (m_ltc_amp_lut->LoadDds(ltc_lut_amp_path))
    {
		m_ltc_amp_lut->SetWrapping (TextureWrappingAxis::U,    TextureWrappingParam::ClampToEdge);
		m_ltc_amp_lut->SetWrapping (TextureWrappingAxis::V,    TextureWrappingParam::ClampToEdge);
		m_ltc_amp_lut->SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::Nearest);
		m_ltc_amp_lut->SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
    }
    else
		std::print(stderr, "Error: could not load texture %s\n", ltc_lut_amp_path.string());

    /// Create shaders.
	const fs::path shaders = "src/demos/27_clustered_shading/";

	const auto T0 = steady_clock::now();

	m_depth_prepass_shader = std::make_shared<Shader>(shaders/"depth_pass.vert", shaders/"depth_pass.frag");
    m_depth_prepass_shader->link();
	assert(*m_depth_prepass_shader);

	m_shadow_depth_shader = std::make_shared<Shader>(shaders/"shadow_depth.vert", shaders/"shadow_depth.frag");
	m_shadow_depth_shader->link();
	assert(*m_shadow_depth_shader);

	m_generate_clusters_shader = std::make_shared<Shader>(shaders/"generate_clusters.comp");
    m_generate_clusters_shader->link();
	assert(*m_generate_clusters_shader);

	m_find_nonempty_clusters_shader = std::make_shared<Shader>(shaders/"find_nonempty_clusters.comp");
	m_find_nonempty_clusters_shader->link();
	assert(*m_find_nonempty_clusters_shader);
	m_find_nonempty_clusters_shader->setPostBarrier(Shader::Barrier::SSBO);  // config, only once

	m_collect_nonempty_clusters_shader = std::make_shared<Shader>(shaders/"collect_nonempty_clusters.comp");
	m_collect_nonempty_clusters_shader->link();
	assert(*m_collect_nonempty_clusters_shader);
	m_collect_nonempty_clusters_shader->setPostBarrier(Shader::Barrier::SSBO);  // config, only once

	m_cull_lights_shader = std::make_shared<Shader>(shaders/"cull_lights.comp");
    m_cull_lights_shader->link();
	assert(*m_cull_lights_shader);
	m_cull_lights_shader->setPostBarrier(Shader::Barrier::SSBO);  // config, only once

	m_clustered_pbr_shader = std::make_shared<Shader>(shaders/"pbr_lighting.vert", shaders/"pbr_clustered.frag");
    m_clustered_pbr_shader->link();
	assert(*m_clustered_pbr_shader);

	// m_shadow_cube_shader = std::make_shared<Shader>(shaders/"shadow_cube.vert", shaders/"shadow_cube.frag", shaders/"shadow_cube.geom");
	// m_shadow_cube_shader->link();
	// assert(*m_shadow_cube_shader);

	m_draw_area_lights_geometry_shader = std::make_shared<Shader>(shaders/"area_light_geom.vert", shaders/"area_light_geom.frag");
    m_draw_area_lights_geometry_shader->link();
	assert(*m_draw_area_lights_geometry_shader);

	m_equirectangular_to_cubemap_shader = std::make_shared<Shader>(shaders/"cubemap.vert", shaders/"equirectangular_to_cubemap.frag");
    m_equirectangular_to_cubemap_shader->link();
	assert(*m_equirectangular_to_cubemap_shader);

	m_irradiance_convolution_shader = std::make_shared<Shader>(shaders/"cubemap.vert", shaders/"irradiance_convolution.frag");
    m_irradiance_convolution_shader->link();
	assert(*m_irradiance_convolution_shader);

	m_prefilter_env_map_shader = std::make_shared<Shader>(shaders/"cubemap.vert", shaders/"prefilter_cubemap.frag");
    m_prefilter_env_map_shader->link();
	assert(*m_prefilter_env_map_shader);

	m_precompute_brdf = std::make_shared<Shader>(shaders/"FSQ.vert", shaders/"precompute_brdf.frag");
    m_precompute_brdf->link();
	assert(*m_precompute_brdf);

	m_background_shader = std::make_shared<Shader>(shaders/"background.vert", shaders/"background.frag");
    m_background_shader->link();
	assert(*m_background_shader);

	// Post-processing steps
	m_tmo_pp.create();
	assert(m_tmo_pp);

	m_bloom_pp.create();
	assert(m_bloom_pp);

	m_scattering_pp.create();
	assert(m_scattering_pp);

	m_blur3_pp.create(Window::width(), Window::height());
	assert(m_blur3_pp);

	m_line_draw_shader = std::make_shared<Shader>(shaders/"line_draw.vert", shaders/"line_draw.frag");
	m_line_draw_shader->link();
	assert(*m_line_draw_shader);

	m_2d_line_shader = std::make_shared<Shader>(shaders/"FSQ.vert", shaders/"draw2d_line.frag");
	m_2d_line_shader->link();
	assert(*m_2d_line_shader);
	m_2d_line_shader->setUniform("u_screen_size"sv, glm::uvec2{ Window::width(), Window::height() });
	m_2d_line_shader->setUniform("u_line_color"sv, glm::vec4(1));
	m_2d_line_shader->setUniform("u_thickness"sv, float(Window::height())/720.f);

	m_2d_rect_shader = std::make_shared<Shader>(shaders/"FSQ.vert", shaders/"draw2d_rectangle.frag");
	m_2d_rect_shader->link();
	assert(*m_2d_rect_shader);
	m_2d_rect_shader->setUniform("u_screen_size"sv, glm::uvec2{ Window::width(), Window::height() });
	m_2d_rect_shader->setUniform("u_line_color"sv, glm::vec4(1));
	m_2d_rect_shader->setUniform("u_thickness"sv, float(Window::height())/720.f);

	m_2d_7segment_shader = std::make_shared<Shader>(shaders/"FSQ.vert", shaders/"seven_segment_number.frag");
	m_2d_7segment_shader->link();
	assert(*m_2d_7segment_shader);
	m_2d_7segment_shader->setUniform("u_screen_size"sv, glm::uvec2{ Window::width(), Window::height() });
	m_2d_7segment_shader->setUniform("u_color"sv, glm::vec4(1));
	m_2d_7segment_shader->setUniform("u_thickness"sv, float(Window::height())/720.f);

	m_imgui_depth_texture_shader = std::make_shared<Shader>(shaders/"imgui_depth_image.vert", shaders/"imgui_depth_image.frag");
	m_imgui_depth_texture_shader->link();
	assert(*m_imgui_depth_texture_shader);

	m_fsq_shader = std::make_shared<Shader>(shaders/"FSQ.vert", shaders/"FSQ.frag");
	m_fsq_shader->link();
	assert(*m_fsq_shader);

	const auto T1 = steady_clock::now();
	const auto shader_init_time = duration_cast<microseconds>(T1 - T0);
	std::print("Shader init time: {:.1f} ms\n", float(shader_init_time.count())/1000.f);

	// Create depth pre-pass render target
	m_depth_pass_rt.create("depth-pass", Window::width(), Window::height(), RenderTarget::Color::None, RenderTarget::Depth::Texture);

	_rt.create("rt", Window::width(), Window::height());
	_rt.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipNearest); // not necessary?

	static constexpr size_t low_scale = 4;
	_pp_low_rt.create("pp_low", Window::width()/low_scale, Window::height()/low_scale, RenderTarget::Color::Default, RenderTarget::Depth::None);
	_pp_low_rt.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipNearest); // not necessary?

	_pp_full_rt.create("pp_full", Window::width(), Window::height(), RenderTarget::Color::Default, RenderTarget::Depth::None);
	_pp_full_rt.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipNearest); // not necessary?

	// TODO: final_rt.cloneFrom(_rt);
	_final_rt.create("final", Window::width(), Window::height(), RenderTarget::Color::Default, RenderTarget::Depth::None);
	_final_rt.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipNearest); // not necessary?

    // IBL precomputations.
    GenSkyboxGeometry();

	m_env_cubemap_rt = std::make_shared<RenderTarget::Cube>();
	m_env_cubemap_rt->create("env", 2048, 2048);

	{
		namespace C = RenderTarget::Color;
		namespace D = RenderTarget::Depth;
		_shadow_atlas.create("shadow atlas", 4096, 4096, C::Texture | C::Float2, D::Texture | D::Float);
		// TODO: if we only use the color attachment (i.e. the normals) for slope comparison,
		//   we really only need a single-channel float (basically the cos(light_to_fragment_angle)).

		m_brdf_lut_rt = std::make_shared<RenderTarget::Texture2d>();
		m_brdf_lut_rt->create("brdf-lut", 512, 512, C::Texture | C::Float2);
	}

	m_irradiance_cubemap_rt = std::make_shared<RenderTarget::Cube>();
    m_irradiance_cubemap_rt->set_position(glm::vec3(0.0));
	m_irradiance_cubemap_rt->create("irradiance", 32, 32);

	m_prefiltered_env_map_rt = std::make_shared<RenderTarget::Cube>();
    m_prefiltered_env_map_rt->set_position(glm::vec3(0.0));
	m_prefiltered_env_map_rt->create("prefiltered_env", 512, 512);

    PrecomputeIndirectLight(FileSystem::getResourcesPath() / "textures/skyboxes/IBL" / m_hdr_maps_names[m_current_hdr_map_idx]);
    PrecomputeBRDF(m_brdf_lut_rt);

	calculateShadingClusterGrid();  // will also call prepareClusterBuffers()

	glGenBuffers(1, &m_debug_draw_vbo);
	glGenQueries(1, &_gl_time_query);


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

		std::print("        target: {:.5f}; {:.5f}; {:.5f}; {:.5f}\n", target.x, target.y, target.z, target.w);
		const auto far_depth = target.z / target.w;
		target = glm::normalize(target);
		std::print("   norm.target: {:.5f}; {:.5f}; {:.5f}   (max depth: {:.1f})\n", target.x, target.y, target.z, far_depth);
		std::print("     direction: {:.5f}; {:.5f}; {:.5f}\n", direction.x, direction.y, direction.z);

		// std::print("   u_view: {}\n", glm::to_string(u_view).c_str());

		const glm::vec3 light_pos { -10, 2.f, 0 };
		std::print("  camera[ws]: {:.5f}; {:.5f}; {:.5f}\n", u_cam_pos.x, u_cam_pos.y, u_cam_pos.z);
		std::print("   light[ws]: {:.5f}; {:.5f}; {:.5f}\n", light_pos.x, light_pos.y, light_pos.z);
		auto light_pos_cs = glm::vec3(u_view * glm::vec4(light_pos, 1));
		std::print("   light[cs]: {:.5f}; {:.5f}; {:.5f}\n", light_pos_cs.x, light_pos_cs.y, light_pos_cs.z);


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

		std::print("        X = {:.3f}; {:.3f}; {:.3f}\n", space_x.x, space_x.y, space_x.z);
		std::print("        Y = {:.3f}; {:.3f}; {:.3f}\n", space_y.x, space_y.y, space_y.z);
		std::print("        Z = {:.3f}; {:.3f}; {:.3f}\n", space_z.x, space_z.y, space_z.z);

		glm::vec3 ray_direction = glm::normalize(glm::vec3(1, 0, 0));

		auto cone_ray = cone_space * glm::vec4(ray_direction, 0);

		std::print(" cone ray = {:.3f}; {:.3f}; {:.3f}\n", cone_ray.x, cone_ray.y, cone_ray.z);
		std::exit(EXIT_SUCCESS);
	}

	if(false)
	{
		const auto space = make_common_space_from_direction({ 0, 0, -1 });
		std::print("        X = {:.3f}; {:.3f}; {:.3f}\n", space[0].x, space[0].y, space[0].z);
		std::print("        Y = {:.3f}; {:.3f}; {:.3f}\n", space[1].x, space[1].y, space[1].z);
		std::print("        Z = {:.3f}; {:.3f}; {:.3f}\n", space[2].x, space[2].y, space[2].z);
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

		std::print("cone center : {:.1f}; {:.1f}; {:.1f}\n", cone.center.x, cone.center.y, cone.center.z);
		std::print("cone axis   : {:.1f}; {:.1f}; {:.1f}\n", cone.axis.x, cone.axis.y, cone.axis.z);
		std::print("cone angle  : {:.1f}\n", glm::degrees(cone.angle));
		std::print("ray start   : {:.1f}; {:.1f}; {:.1f}\n", ray_start.x, ray_start.y, ray_start.z);
		std::print("ray dir     : {:.1f}; {:.1f}; {:.1f}\n", ray_dir.x, ray_dir.y, ray_dir.z);

		glm::vec3 center_to_ray = ray_start - cone.center; // aka CO
		float distance_sq = glm::dot(center_to_ray, center_to_ray);

		float cos_theta = std::cos(cone.angle);
		float cos_theta_sq = cos_theta*cos_theta;
		float dir_axis_dot = glm::dot(ray_dir, cone.axis);
		float CO_axis_dot = glm::dot(center_to_ray, cone.axis);

		float A = dir_axis_dot*dir_axis_dot - cos_theta_sq;
		float B = 2 * (dir_axis_dot*CO_axis_dot - glm::dot(ray_dir, center_to_ray)*cos_theta_sq);
		float C = CO_axis_dot*CO_axis_dot - distance_sq*cos_theta_sq;

		std::print("    A = {:.3f}\n", A);
		std::print("    B = {:.3f}\n", B);
		std::print("    C = {:.3f}\n", C);

		float discriminant = B*B - 4*A*C;
		if(discriminant < 0)
			std::print("no intersection\n");
		else
		{
			std::print("discriminant = {:.3f}\n", discriminant);
			float sqrt_discriminant = std::sqrt(discriminant);
			float t1 = (-B - sqrt_discriminant) / (2*A);
			float t2 = (-B + sqrt_discriminant) / (2*A);

			auto ray_point = [&ray_start, &ray_dir](float t) {
				return ray_start + ray_dir*t;
			};
			auto p1 = ray_point(t1);
			std::print("  t1 = {:.3f}  ->  {:.2f}; {:.2f}; {:.2f}\n", t1, p1.x, p1.y, p1.z);
			auto p2 = ray_point(t2);
			std::print("  t2 = {:.3f}  ->  {:.2f}; {:.2f}; {:.2f}\n", t2, p2.x, p2.y, p2.z);
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

		std::print("cone center  : {:.1f}; {:.1f}; {:.1f}\n", cone.center.x, cone.center.y, cone.center.z);
		std::print("cone axis    : {:.1f}; {:.1f}; {:.1f}\n", cone.axis.x, cone.axis.y, cone.axis.z);
		std::print("cone angle   : {:.1f}   radius: {:.1f}\n", glm::degrees(cone.angle), cone.radius);
		std::print("ray start    : {:.1f}; {:.1f}; {:.1f}\n", ray_start.x, ray_start.y, ray_start.z);
		std::print("ray dir      : {:.1f}; {:.1f}; {:.1f}\n", ray_dir.x, ray_dir.y, ray_dir.z);
		const auto ray_end = ray_point(50);
		std::print("ray end @ 50 : {:.1f}; {:.1f}; {:.1f}\n", ray_end.x, ray_end.y, ray_end.z);

		glm::vec3 center_to_ray = ray_start - cone.center; // aka CO

		float A = 1;
		float B = 2 * glm::dot(center_to_ray, ray_dir);
		float C = glm::dot(center_to_ray, center_to_ray) - cone.radius*cone.radius;

		std::print("    A = {:.3f}\n", A);
		std::print("    B = {:.3f}\n", B);
		std::print("    C = {:.3f}\n", C);

		float discriminant = B*B - 4*A*C;
		if(discriminant < 0)
			std::puts("NO INTERSECTION");
		else
		{
			std::print("discriminant = {:.3f}\n", discriminant);
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
					std::print("  t1 = {:.3f}  ->  {:.2f}; {:.2f}; {:.2f}\n", t1, p1.x, p1.y, p1.z);
					got_point = true;
				}
			}
			if(t2 >= 0)
			{
				auto p2 = ray_point(t2);
				if(point_inside_cone(p2))
				{
					std::print("  t2 = {:.3f}  ->  {:.2f}; {:.2f}; {:.2f}\n", t2, p2.x, p2.y, p2.z);
					got_point = true;
				}
			}
			if(got_point)
				std::puts("INTERSECTION");
			else
				std::puts("NO INTERSECTION");
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

		std::print("depth pos  : {:.1f}; {:.1f}; {:.1f}\n", pos.x, pos.y, pos.z);

		std::print("world  pos : {:.5f}; {:.5f}; {:.5f}\n", wpos.x, wpos.y, wpos.z);

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


		std::print("spot  : {:.1f}; {:.1f}; {:.1f}  {:.0f}° - {:.0f}°\n", spot_pos.x, spot_pos.y, spot_pos.z, glm::degrees(inner_angle), glm::degrees(outer_angle));

		for(float x = 0.f; x <= 8.f; x += 0.2f)
		{
			point.x = x;
			auto to_point = glm::normalize(point - spot_pos);
			float att = spot_angle_att(to_point, spot_dir, outer_angle, inner_angle);
			std::print("point : {:.1f}; {:.1f}; {:.1f}  --> {:f}\n", point.x, point.y, point.z, att);
		}

		std::exit(EXIT_SUCCESS);
	}
}

void ClusteredShading::calculateShadingClusterGrid()
{
	const auto cluster_count_before = m_clusters_count;

	// TODO: these should be properties related to the camera  (a component!)

	/// Init clustered shading variables.


	static constexpr auto screen_division = 20; // around 20 is a fair value
	static constexpr auto depth_scale = 1.f;   // default 1
	// if the cluster resolution is even on X & Y axis, it's possible to partiaion into 4 quadrants (if even divisable)
	//   each cluster on the Z-axis is of course also possible to partition by.
	//   not sure how this could be taken advantage of in the light culling stpe.
	// Remember: for light scattering, the clusters needs to have lights assigned to them
	//   whether they're non-enpty or not.
	// ALT: define the grid centered on the screen, that way it can always be partitioned



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


	// TODO: the depth slices closest to the camera will be quite small
	//   might want to use a fixed spacing the first X units (a few meters in world space)
	// Maybe use the one Doom 2016 uses, see https://www.aortiz.me/2018/12/21/CG.html#building-a-cluster-grid
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
		m_clusters_count = cluster_count;
		std::print("Shading clusters: {}   ({} x {} x {})\n", m_clusters_count, m_cluster_resolution.x, m_cluster_resolution.y, m_cluster_resolution.z);

		auto cluster_depth = [this](size_t slice_n) -> float {
			return -m_camera.nearPlane() * std::pow(std::abs(m_near_k), float(slice_n));
		};

		const float depthN0 = -cluster_depth(0); // this should be camera's near plane
		const float depthN1 = -cluster_depth(1);
		const float depthM0 = -cluster_depth(m_cluster_resolution.z/2 - 1); // this should be camera's near plane
		const float depthM1 = -cluster_depth(m_cluster_resolution.z/2);
		const float depthF0 = -cluster_depth(m_cluster_resolution.z - 1);
		const float depthF1 = -cluster_depth(m_cluster_resolution.z);  // this should be camera's far plane (approximately)

		std::print("    cluster[0]: {:.3f}\n", depthN1 - depthN0);
		std::print("  cluster[N/2]: {:.2f}\n", depthM1 - depthM0);
		std::print("    cluster[N]: {:.1f}\n", depthF1 - depthF0);

		// {
		// 	const float depthN0 = -doom_slice_z(0); // this should be camera's near plane
		// 	const float depthN1 = -doom_slice_z(1);
		// 	const float depthM0 = -doom_slice_z(m_cluster_grid_dim.z/2 - 1);
		// 	const float depthM1 = -doom_slice_z(m_cluster_grid_dim.z/2);  // this should be camera's far plane (approximately)
		// 	const float depthF0 = -doom_slice_z(m_cluster_grid_dim.z - 1);
		// 	const float depthF1 = -doom_slice_z(m_cluster_grid_dim.z);  // this should be camera's far plane (approximately)

		// 	std::print("DOOM    Near: %.3f - %.3f (%.3f)\n", depthN0, depthN1, depthN1 - depthN0);
		// 	std::print("DOOM     MId: %.2f - %.2f (%.2f)\n", depthM0, depthM1, depthM1 - depthM0);
		// 	std::print("DOOM     Far: %.1f - %.1f (%.1f)\n", depthF0, depthF1, depthF1 - depthF0);
		// }
		prepareClusterBuffers();
	}
}

void ClusteredShading::prepareClusterBuffers()
{
	m_simple_clusters_aabb_ssbo.resize(m_clusters_count);
	m_cluster_discovery_ssbo.resize(1 + m_clusters_count*2);  // num_active, nonempty[N], active[N]
	m_cull_lights_args_ssbo.resize(1);

	/// Generate AABBs for clusters
	// This needs to be re-done when the camera projection changes (e.g. fov)
	m_camera.setUniforms(*m_generate_clusters_shader);
	m_generate_clusters_shader->setUniform("u_cluster_resolution"sv, m_cluster_resolution);
	m_generate_clusters_shader->setUniform("u_cluster_size_ss"sv,    glm::uvec2(m_cluster_block_size));
	m_generate_clusters_shader->setUniform("u_near_k"sv,             m_near_k);
	m_generate_clusters_shader->setUniform("u_pixel_size"sv,         1.0f / glm::vec2(Window::width(), Window::height()));
	m_generate_clusters_shader->invoke(size_t(std::ceil(float(m_clusters_count) / 1024.f)));
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

	if(Input::wasKeyPressed(KeyCode::N))
	{
		_ray_march_noise = (_ray_march_noise + 1) % 3;
		std::print("ray_march_noise: {}\n", _ray_march_noise);
	}

    /* Toggle between wireframe and solid rendering */
	// if (Input::getKeyUp(KeyCode::F2))
	// {
	//     static bool toggle_wireframe = false;

	//     toggle_wireframe = !toggle_wireframe;

	//     if (toggle_wireframe)
	//     {
	//         glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	//     }
	//     else
	//     {
	//         glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	//     }
	// }

    /* It's also possible to take a screenshot. */
	if (Input::wasKeyReleased(KeyCode::F12))
    {
		/* Specify filename of Sthe screenshot. */
        std::string filename = "27_clustered_shading";
		if (take_screenshot_png(filename, Window::width() / 2, Window::height() / 2))
        {
            /* If specified folders in the path are not already created, they'll be created automagically. */
			std::cout << "Saved " << filename << ".png to " << (FileSystem::rootPath() / "screenshots/") << std::endl;
        }
        else
        {
			std::cerr << "Could not save " << filename << ".png to " << (FileSystem::rootPath() / "screenshots/") << std::endl;
        }
    }

	if (Input::wasKeyReleased(KeyCode::Space))
        m_animate_lights = !m_animate_lights;
}

void ClusteredShading::update(double delta_time)
{
	_running_time += seconds_f(delta_time);

    /* Update variables here. */
	m_camera.update(delta_time);

	// static float     rotation_speed = 1;
	// static float     time_accum     = 0;

/*
	for(auto &spot: m_spot_lights)
	{
		spot.outer_angle = glm::radians(s_spot_outer_angle);
		spot.point.base.intensity = s_spot_intensity;
	}
*/

    if (m_animate_lights)
    {
		// time_accum  += float(delta_time * m_animation_speed);
		auto orbit_mat = glm::rotate(glm::mat4(1), glm::radians(-23.f * float(delta_time)) * 2.f * m_animation_speed, AXIS_Y);
/*
		auto spin_mat  = glm::rotate(glm::mat4(1), glm::radians(60.f * float(delta_time)) * 2.f * m_animation_speed, AXIS_Y);
		for(auto &spot: m_spot_lights)
		{
			// spin on its own axis
			spot.direction = spin_mat * glm::vec4(spot.direction, 0);
			// orbit aounr the orgin
			spot.point.position = orbit_mat * glm::vec4(spot.point.position, 1);
		}
*/
		for(auto idx = 0u; idx < m_light_counts_ubo->num_point_lights; ++idx)
		{
			PointLight &point = m_lights_ssbo->point_lights[idx];
			// orbit aounr the orgin
			point.position = orbit_mat * glm::vec4(point.position, 1);
		}



  //       m_update_lights_shader->bind();
		// m_update_lights_shader->setUniform("u_time"sv,                 time_accum);
		// m_update_lights_shader->setUniform("u_area_two_sided"sv,       m_area_lights_two_sided);
		// m_update_lights_shader->setUniform("u_area_rotation_matrix"sv, rotation_mat);

		// auto max_lights_count = glm::max(m_point_lights.size(), glm::max(m_spot_lights.size(), glm::max(m_directional_lights.size(), m_area_lights.size())));
		// glDispatchCompute(GLuint(glm::ceil(float(max_lights_count) / 1024.f)), 1, 1);
  //       glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		// UpdateLightsSSBOs();

		UpdateLightsSSBOs();
	}
}

void ClusteredShading::GenerateAreaLights()
{
	m_light_counts_ubo->num_area_lights = 0;

	/*
    auto computeAreaLightPoints = [](glm::vec3 position, glm::vec2 size, glm::vec4 points[4])
    {
		const auto p = glm::vec4(position, 1);
		points[0] = p + glm::vec4(0,  size.y, -size.x, 0);
		points[1] = p + glm::vec4(0, -size.y, -size.x, 0);
		points[2] = p + glm::vec4(0,  size.y,  size.x, 0);
		points[3] = p + glm::vec4(0, -size.y,  size.x, 0);
    };

    auto getPointOnRectPerimeter = [](float width, float height, float x0) -> glm::vec2
    {
		auto x = x0 * (2.f * width + 2.f * height);

        if (x < width)
            return glm::vec2(x, 0);

        x -= width;
        if (x < height)
            return glm::vec2(width, x);

        x -= height;
        if (x < width)
            return glm::vec2(x, height);
        else
            return glm::vec2(0, x - width);
    };

    m_area_lights.clear();
    m_area_lights.resize(m_area_lights_count);

	float step             = 1.f / float(m_area_lights_count >> 1);
    float x0               = 0.0f;
    float rect_width       = 19.0f;
    float rect_height      = 7.0f;
    float area_light_pos_y = 0.3f;

    for (uint32_t i = 0; i < m_area_lights.size(); ++i)
    {
        if (i == m_area_lights_count / 2)
        {
            area_light_pos_y = 3.8f;
            x0               = 0.0f;
        }

        auto& ar = m_area_lights[i];

		ar.base.color     = hsv2rgb(
			float(Util::RandomDouble(1, 360)),
			float(Util::RandomDouble(0.1, 1.0)),
			float(Util::RandomDouble(0.1, 1))
		);
        ar.base.intensity = m_area_lights_intensity;
        ar.two_sided      = m_area_lights_two_sided;

		glm::vec2 p_on_rect = getPointOnRectPerimeter(rect_width, rect_height, x0 + float(Util::RandomDouble(0, step / 2.f)));
        glm::vec3 center    = glm::vec3(p_on_rect.x, area_light_pos_y, p_on_rect.y) + glm::vec3(-10.0f, m_area_lights_size.y * 0.5f, -3.5f);
        computeAreaLightPoints(center, m_area_lights_size, ar.points);

        x0 += step;
    }
*/
	m_light_counts_ubo.flush();
}

void ClusteredShading::GeneratePointLights()
{

#if 0
	m_point_lights.push_back({
		.base = {
			.color = { .2f, 1.f, .5f },
			.intensity = 1000
		},
		.position = { -5, 2.f, 4 },
		.radius = 15,
	});
	m_point_lights.push_back({
		.base = {
			.color = { 1.f, 0.5f, .1f },
			.intensity = 1000
		},
		.position = { -5, 3.f, -4 },
		.radius = 15,
	});
	m_point_lights.push_back({
		.base = {
			.color = { .3f, 0.2f, 1.5f },
			.intensity = 1000
		},
		.position = { 5, 3.f, -4 },
		.radius = 15,
	});
#endif

	// m_point_lights.push_back({
	// 	.base = {
	// 		.uuid = static_cast<uint32_t>(m_point_lights.size()), // TODO: use ECS entity ID
	// 		.color = { 1.0f, 0.8f, 0.5f },
	// 		.intensity = 100,
	// 		.fog = 1.f,
	// 		.feature_flags = LIGHT_SHADOW_CASTER,
	// 	},
	// 	.position = { -10, 2.f, 0 },
	// 	.radius = 10,
	// });

	// m_point_lights.push_back({
	// 	.base = {
	// 		.uuid = static_cast<uint32_t>(m_point_lights.size()), // TODO: use ECS entity ID
	// 		.color = { 0.3f, 0.5f, 1.0 },
	// 		.intensity = 100,
	// 		.fog = 1.f,
	// 		.feature_flags = LIGHT_SHADOW_CASTER,
	// 	},
	// 	.position = { 10, 2.f, 0 },
	// 	.radius = 10,
	// });

	// m_point_lights.push_back({
	// 	.base = {
	// 		.uuid = static_cast<uint32_t>(m_point_lights.size()), // TODO: use ECS entity ID
	// 		.color = { 0.4f, 1.0f, 0.4f },
	// 		.intensity = 100,
	// 		.fog = 1.f,
	// 		.feature_flags = LIGHT_SHADOW_CASTER,
	// 	},
	// 	.position = { 0, 2.f, -10 },
	// 	.radius = 10,
	// });
	// return;

	m_light_counts_ubo->num_point_lights = 0;

	for(auto idx = 0u; idx < 3; ++idx)
	{
		auto rand_color= hsv2rgb(
			float(Util::RandomDouble(1, 360)),
			float(Util::RandomDouble(0, 1)),
			1.f
		);
		auto rand_pos = Util::RandomVec3({ -18, 0.5f, -18 }, { 18, 3.5f, 18 });

		auto rand_intensity = float(Util::RandomDouble(1, 100)) * 3;

		m_lights_ssbo->point_lights[idx] = {
			.base = {
				.color = rand_color,
				.intensity = rand_intensity,
				.fog = 1.f,
				.feature_flags = LIGHT_SHADOW_CASTER,
				.uuid = idx, // TODO: use ECS entity ID (e.g. for associatting to shadow map slot)
				._pad0 = { 0 },
			},
			.position = rand_pos,
			.radius = std::pow(rand_intensity, 0.5f),   // maybe this could be tightened as the total light count goes up?
		};
		m_light_counts_ubo->num_point_lights = idx + 1;

		std::print("light[{:2}] @ {:5.1f}; {:3.1f}; {:5.1f}  {:3},{:3},{:3}  {:4.0f}\n",
					idx,
					rand_pos.x, rand_pos.y, rand_pos.z,
					uint(rand_color.r*255), uint(rand_color.g*255), uint(rand_color.b*255),
					rand_intensity);
	}

#if 0
	m_point_lights.push_back({
		.base = {
			.color = { 1.0f, 0.8f, 0.5f },
			.intensity = 100
		},
		.position = { -10, 2.f, 0 },
		.radius = 10,
	});

	m_point_lights.push_back({
		.base = {
			.color = { 0.3f, 0.5f, 1.0 },
			.intensity = 100
		},
		.position = { 10, 2.f, 0 },
		.radius = 10,
	});

	m_point_lights.push_back({
		.base = {
			.color = { 0.4f, 1.0f, 0.4f },
			.intensity = 100
		},
		.position = { 0, 2.f, -10 },
		.radius = 10,
	});

	m_point_lights.push_back({
		.base = {
			.color = { 1.0f, 0.1f, 0.05f },
			.intensity = 100
		},
		.position = { 0, 2.f, 10 },
		.radius = 10,
	});
#endif
	m_light_counts_ubo.flush();
}

void ClusteredShading::GenerateSpotLights()
{
	// m_spot_lights.clear();
	m_light_counts_ubo->num_spot_lights = 0;

#if 0
	m_spot_lights.push_back({
		.point = {
			.base = {
				// .color = { 10f, 0.1f, 0.5f },
				.color = { 1.f, 1.f, 1.f },
				.intensity = 3000
			},
			.position = { -16, 3, -8 },
			.radius = 35
		},
		.direction = { 0, 0, 1 },
		.inner_angle = 0.f,
		.outer_angle = glm::radians(30.f),
		.bounds_radius = 0,
	});
	m_spot_lights.push_back({
		.point = {
			.base = {
				.color = { 1.f, 0.1f, 0.1f },
				.intensity = 3000
			},
			.position = { -12, 3, -8 },
			.radius = 35
		},
		.direction = { 0, 0, 1 },
		.inner_angle = 0.f,
		.outer_angle = glm::radians(30.f),
		.bounds_radius = 0,
	});
	m_spot_lights.push_back({
		.point = {
			.base = {
				.color = { 1.f, 0.6f, 0.1f },
				.intensity = 3000
			},
			.position = { -8, 3, -8 },
			.radius = 35
		},
		.direction = { 0, 0, 1 },
		.inner_angle = 0.f,
		.outer_angle = glm::radians(30.f),
		.bounds_radius = 0,
	});
	m_spot_lights.push_back({
		.point = {
			.base = {
				.color = { 1.f, 1.f, 0.f },
				.intensity = 3000
			},
			.position = { -4, 3, -8 },
			.radius = 35
		},
		.direction = { 0, 0, 1 },
		.inner_angle = 0.f,
		.outer_angle = glm::radians(30.f),
		.bounds_radius = 0,
	});

	m_spot_lights.push_back({
		.point = {
			.base = {
				.color = { 1.f, 1.f, 1.0f },
				.intensity = 3000
			},
			.position = { 0, 3, -8 },
			.radius = 35
		},
		.direction = { 0, 0, 1 },
		.inner_angle = 0.f,
		.outer_angle = glm::radians(30.f),
		.bounds_radius = 0,
	});

	m_spot_lights.push_back({
		.point = {
			.base = {
				.color = { 0.5f, 1.f, 0.f },
				.intensity = 3000
			},
			.position = { 4, 3, -8 },
			.radius = 35
		},
		.direction = { 0, 0, 1 },
		.inner_angle = 0.f,
		.outer_angle = glm::radians(30.f),
		.bounds_radius = 0,
	});
	m_spot_lights.push_back({
		.point = {
			.base = {
				.color = { 0.3f, 1.f, 0.6f },
				.intensity = 3000
			},
			.position = { 8, 3, -8 },
			.radius = 35
		},
		.direction = { 0, 0, 1 },
		.inner_angle = 0.f,
		.outer_angle = glm::radians(30.f),
		.bounds_radius = 0,
	});
	m_spot_lights.push_back({
		.point = {
			.base = {
				.color = { 0.1f, 1.f, 0.3f },
				.intensity = 3000
			},
			.position = { 12, 3, -8 },
			.radius = 35
		},
		.direction = { 0, 0, 1 },
		.inner_angle = 0.f,
		.outer_angle = glm::radians(30.f),
		.bounds_radius = 0,
	});
	m_spot_lights.push_back({
		.point = {
			.base = {
				.color = { 0.1f, 0.8f, 1.f },
				.intensity = 3000
			},
			.position = { 16, 3, -8 },
			.radius = 35
		},
		.direction = { 0, 0, 1 },
		.inner_angle = 0.f,
		.outer_angle = glm::radians(30.f),
		.bounds_radius = 0,
	});
#endif

	// pre-compute the 'bounds_radius'; the radius (and offset from 'position')
	//   of a minimal bounding sphere
/*
	for(auto &spot: m_spot_lights)
	{
		const float tan_theta = std::tan(2*spot.outer_angle);
		spot.bounds_radius = spot.point.radius * (1 + tan_theta*tan_theta*0.5f);
	}
*/
	m_light_counts_ubo.flush();
}

void ClusteredShading::UpdateLightsSSBOs()
{
	m_lights_ssbo.flush();
	// m_light_counts_ubo.flush();
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

void ClusteredShading::PrefilterCubemap(const std::shared_ptr<RenderTarget::Cube>& cubemap_rt)
{
    m_prefilter_env_map_shader->bind();
	m_prefilter_env_map_shader->setUniform("u_projection"sv, cubemap_rt->projection());

    m_env_cubemap_rt->bindTexture(1);

	auto max_mip_levels = uint8_t(glm::log2(float(cubemap_rt->width())));
    for (uint8_t mip = 0; mip < max_mip_levels; ++mip)
    {
        // resize the framebuffer according to mip-level size.
		auto mip_width  = uint32_t(cubemap_rt->width()) >> mip; // * std::pow(0.5, mip));
		auto mip_height = uint32_t(cubemap_rt->height()) >> mip;// * std::pow(0.5, mip));

		cubemap_rt->resizeDepth(mip_width, mip_height);
		// TODO: want to set viewpoort once
		// glViewport(0, 0, GLsizei(mip_width), GLsizei(mip_height));

        float roughness = float(mip) / float(max_mip_levels - 1);
		m_prefilter_env_map_shader->setUniform("u_roughness"sv, roughness);

		for (uint8_t face = 0; face < 6; ++face)
        {
			m_prefilter_env_map_shader->setUniform("u_view"sv, cubemap_rt->view_transform(face));
			cubemap_rt->bindRenderTarget(face);  // , { 0, 0, w, h } TODO?
			glViewport(0, 0, GLsizei(mip_width), GLsizei(mip_height)); // view port is set in bindRenderTarget(face) ...

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
    PrefilterCubemap(m_prefiltered_env_map_rt);
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

	glm::vec3 skybox_positions[] = {
        // positions
		{ -1, -1, -1 },
		{  1,  1, -1 },
		{  1, -1, -1 },
		{  1,  1, -1 },
		{ -1, -1, -1 },
		{ -1,  1, -1 },
        // front face
		{ -1, -1,  1 },
		{  1, -1,  1 },
		{  1,  1,  1 },
		{  1,  1,  1 },
		{ -1,  1,  1 },
		{ -1, -1,  1 },
        // left face
		{ -1,  1,  1 },
		{ -1,  1, -1 },
		{ -1, -1, -1 },
		{ -1, -1, -1 },
		{ -1, -1,  1 },
		{ -1,  1,  1 },
        // right face
		{ 1,  1,  1 },
		{ 1, -1, -1 },
		{ 1,  1, -1 },
		{ 1, -1, -1 },
		{ 1,  1,  1 },
		{ 1, -1,  1 },
        // bottom face
		{ -1, -1, -1 },
		{  1, -1, -1 },
		{  1, -1,  1 },
		{  1, -1,  1 },
		{ -1, -1,  1 },
		{ -1, -1, -1 },
        // top face
		{ -1,  1, -1 },
		{  1,  1 , 1 },
		{  1,  1, -1 },
		{  1,  1,  1 },
		{ -1,  1, -1 },
		{ -1,  1,  1 },
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

void ClusteredShading::render()
{
	m_camera.setFov(m_camera_fov);



	// determine visible meshes  (only if camera or meshes moved (much))
	cullScene();


	_gl_timer.start();

	renderShadowMaps();

	m_shadow_time.add(_gl_timer.elapsed<microseconds>(true));



	// Depth pre-pass  (only if camera/meshes moved, probably always)
	renderDepth(m_camera.projectionTransform() * m_camera.viewTransform(), m_depth_pass_rt);


	// Blit depth info to our main render target
	m_depth_pass_rt.copyTo(_rt, RenderTarget::DepthBuffer, TextureFilteringParam::Nearest);

	m_depth_time.add(_gl_timer.elapsed<microseconds>(true));


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
	m_collect_nonempty_clusters_shader->setUniform("u_num_clusters"sv, m_clusters_count);
	m_collect_nonempty_clusters_shader->invoke(size_t(std::ceil(float(m_clusters_count) / 1024.f)));

	m_cluster_index_time.add(_gl_timer.elapsed<microseconds>(true));
	// ------------------------------------------------------------------

	// ------------------------------------------------------------------

	// Assign lights to clusters (cull lights)
	m_cull_lights_shader->setUniform("u_view_matrix"sv, m_camera.viewTransform());
	m_cull_lights_shader->setUniform("u_num_clusters"sv, m_clusters_count);
	m_cull_lights_shader->setUniform("u_max_cluster_avg_lights"sv, uint32_t(CLUSTER_AVERAGE_LIGHTS));

	m_cull_lights_shader->invoke(m_cull_lights_args_ssbo);  // reads uint (num_active, 1, 1)

	m_light_cull_time.add(_gl_timer.elapsed<microseconds>(true));
	// ------------------------------------------------------------------

	_rt.bindRenderTarget(RenderTarget::ColorBuffer);

	renderLighting(m_camera);
	m_shading_time.add(_gl_timer.elapsed<microseconds>(true));


	// Render area lights geometry, to '_rt'
	if(m_draw_area_lights_geometry and m_light_counts_ubo->num_area_lights > 0)
	{
		m_draw_area_lights_geometry_shader->bind();
		m_draw_area_lights_geometry_shader->setUniform("u_view_projection"sv, m_camera.projectionTransform() * m_camera.viewTransform());
		glDrawArrays(GL_TRIANGLES, 0, GLsizei(6 * m_light_counts_ubo->num_area_lights));
	}

	renderSkybox(); // to '_rt'

	m_skybox_time.add(_gl_timer.elapsed<microseconds>(true));

	if(m_fog_density > 0)
	{
		m_scattering_pp.setCameraUniforms(m_camera);
		m_scattering_pp.shader().setUniform("u_time"sv,               _running_time.count());
		// TODO: use a multiple of the shading cluster resolution, and we can thus re-use the light culling information
		//   e.g. 8-10 more in XY-axes and maybe 2-3x more on Z-axis
		//   also means that the light culling must process *all* clusters, not only non-empty ones
		m_scattering_pp.shader().setUniform("u_cluster_resolution"sv, m_cluster_resolution);
		m_scattering_pp.shader().setUniform("u_cluster_size_ss"sv,    glm::uvec2(m_cluster_block_size));
		m_scattering_pp.shader().setUniform("u_fog_color"sv,          glm::vec3(1, 1, 1));
		m_scattering_pp.shader().setUniform("u_fog_density"sv,        m_fog_density);
		m_scattering_pp.shader().setUniform("u_ray_march_noise",      _ray_march_noise);

		m_depth_pass_rt.bindDepthTextureSampler(2);

		_pp_low_rt.clear();
		m_scattering_pp.render(_rt, _pp_low_rt);  // '_rt' actually isn't used but the API expects an argument


		// _pp_low_rt.copyTo(_pp_full_rt);  // copy and upscale
		// NOTE: draw b/c copy(blit) doesn't work!?!?
		//   no biggie though, it's often faster in practice
		draw2d(_pp_low_rt.color_texture(), _pp_full_rt);
	}
	else
		_pp_full_rt.clear();

	m_scatter_time.add(_gl_timer.elapsed<microseconds>(true));

#if 0
	// TODO: change to MipmapBlur
	// m_blur3_pp.render(_pp_full_rt, _pp_full_rt);
	// m_blur3_pp.render(_pp_full_rt, _pp_full_rt);
	// m_blur3_pp.render(_pp_full_rt, _pp_full_rt);

	// add the scattering effect on to the final image
	draw2d(_pp_full_rt.color_texture(), _rt, BlendMode::Add);   // why does this (kind of) do "replace" instead?
#endif
	m_pp_blur_time.add(_gl_timer.elapsed<microseconds>());



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
	m_tmo_pp.setExposure(m_exposure);
	m_tmo_pp.setGamma(m_gamma);
	m_tmo_pp.render(_rt, _final_rt);


	// draw the final result on the screen
	draw2d(_final_rt.color_texture(), BlendMode::Replace);


	if(m_debug_draw_aabb)
		debugDrawSceneBounds();
	if(m_debug_draw_cluster_grid)
		debugDrawClusterGrid();
}

static constexpr glm::vec3 s_cube_face_forward[] = {
	AXIS_X, -AXIS_X,
	AXIS_Y, -AXIS_Y,
	AXIS_Z, -AXIS_Z,
};
static constexpr glm::vec3 s_cube_face_up[] = {
	-AXIS_Y, -AXIS_Y,
	 AXIS_Z, -AXIS_Z,
	-AXIS_Y, -AXIS_Y,
};

void ClusteredShading::renderShadowMaps()
{
	glCullFace(GL_FRONT);  // render only back faces   TODO face culling is whack! (see init_app())
	glEnable(GL_CULL_FACE);
	glEnable(GL_SCISSOR_TEST);

	// TODO: render shadow-maps
	// if light or meshes within its radius moved -> implies caching, somehow
	//   cap the number of shadow maps (maybe dynamically, based on fps), e.g. top X closest
	//   pack the shadow maps into a few textures (maybe one texture per light type?)
	//     to simplify packing, one texture per shadow map size?
	//   Scale shadow map size is deduced based on distance from camera (far away, small shadow map),
	//     also light radius.
	//   maybe not update the shadow maps every frame?  (preferably, as little as possible)

#if 0
	for(const auto &light: m_directional_lights)
	{
		if((light.base.feature_flags & LIGHT_SHADOW_CASTER) > 0)
			renderShadowMap(light);
	}
#endif


	// calculate "priority" for all shadow casting lights
	// TODO: this may be done in parallel, or even on the GPU

	struct LightImportance
	{
		float importance;
		uint index;
	};

	static std::vector<LightImportance> importance;
	if(importance.capacity() == 0)
		importance.reserve(MAX_POINT_LIGHTS + MAX_SPOT_LIGHTS);// TODO: also area lights?
	importance.clear();

	static const auto radius_power = 0.7f;

	auto light_importance = [cam_pos=m_camera.position()](const PointLight &light) -> float {
		const float distance = std::max(1.f, glm::length(light.position - cam_pos) - light.radius);
		return std::pow(light.radius, radius_power) / distance; // radius/intensity is basically the same
	};

	for(auto idx = 0u; idx < m_light_counts_ubo->num_point_lights; ++idx)
	{
		const auto &light = m_lights_ssbo->point_lights[idx];

		if(light.base.feature_flags & LIGHT_SHADOW_CASTER)
			importance.push_back({ .importance = light_importance(light), .index = idx });
	}

	uint32_t index_offset = MAX_POINT_LIGHTS;
	for(auto idx = 0u; idx < m_light_counts_ubo->num_spot_lights; ++idx)
	{
		const auto &light = m_lights_ssbo->spot_lights[idx].point;

		if(light.base.feature_flags & LIGHT_SHADOW_CASTER)
			importance.push_back({ .importance = light_importance(light), .index = idx + index_offset });
	}
	// TODO: also area lights?

	std::sort(importance.begin(), importance.end(), [](const auto &A, const auto &B) {
		return A.importance > B.importance;
	});

	// TODO: distribute these priorities into 5-10 "buckets"
	//   these buckets correspond to how large the shadowcast
	// The different buckets is a property of the shadow atlas itself,
	//   essentially, what it supports

	// need to use the scissor during atlas update; to avoid touchting pixels outside each tile


	static constexpr auto aspect = 1.f;  // i.e. square
	const glm::vec2 atlas_size { float(_shadow_atlas.width()), float(_shadow_atlas.height()) };
	auto tile_size = 1024u;

	for(auto light_idx = 0u; light_idx < 1/*m_lights_ssbo->num_point_lights*/; ++light_idx)
	{
		auto &light = m_lights_ssbo->point_lights[light_idx];
		if((light.base.feature_flags & LIGHT_SHADOW_CASTER) == 0)
			continue;

		// TODO: is there any non-static objects inside the lights' radius?
		//   if only static objects, no need to update the shadow map

		glm::uvec2 bottom_left { 0, 0 };

		const auto lightProjection = glm::perspective(glm::radians(90.0f), aspect, 0.1f, light.radius);

		// TODO ideally, these 6 faces should be generated using a single draw call
		//   by using a geometry shader

		LightShadowParams params;

		for(auto face = 0u; face < 6; ++face)
		{
			glm::uvec4 tile_rect = {
				(face % 3)*tile_size + bottom_left.x,
				(face / 3)*tile_size + bottom_left.y,
				tile_size,
				tile_size,
			};

			const auto &view_forward = s_cube_face_forward[face];
			const auto &view_up = s_cube_face_up[face];

			const auto lightView = glm::lookAt(light.position, light.position + view_forward, view_up);
			const auto lightVP = lightProjection * lightView;

			renderShadowDepth(light.position, light.radius, lightVP, _shadow_atlas, tile_rect);

			params.atlas_rect[face] = glm::vec4(tile_rect) / glm::vec4(atlas_size, atlas_size);
			params.view_proj[face] = lightVP;
		}

		m_shadow_map_params_ssbo[light_idx] = params;

		tile_size <<= 1;
	}
	// TODO: maybe use a set(index, value) that also sets a dirty flag
	//   then flush only writes those?
	m_shadow_map_params_ssbo.flush();

#if 0
	// lights farther away than this (squared), will not cast a shadow
	static const float cut_off_distance_sq = 50.f * 50.f;

	const auto cam_pos = m_camera.position();

	struct LightIndex
	{
		uint index;
		float importance;
	};
	static std::vector<LightIndex> light_index;
	light_index.reserve(std::max(m_point_lights.size(), std::max(m_spot_lights.size(), m_area_lights.size())));

	auto sq_distance = [&cam_pos](const auto &pos) {
		const auto v = glm::vec3(pos) - cam_pos;
		return glm::dot(v, v);
	};

	light_index.clear();
	uint index = 0;
	for(const auto &light: m_point_lights)
	{
		if((light.base.feature_flags & LIGHT_SHADOW_CASTER) > 0)
		{
			const auto distance = sq_distance(light.position);
			if(distance < cut_off_distance_sq)
			{
				const auto importance = 1.f/distance;  // TODO: also light radius
				light_index.push_back({ index, importance });
			}
		}
		++index;
	}

	auto by_importance = [](const auto &A, const auto &B) {
		return A.importance > B.importance;
	};

	std::sort(light_index.begin(), light_index.end(), by_importance);
	for(const auto &item: light_index)
		renderShadowMap(m_point_lights[item.index]);
#endif



#if 0
	light_index.clear();
	index = 0;
	for(const auto &light: m_spot_lights)
	{
		if((light.base.feature_flags & LIGHT_SHADOW_CASTER) > 0)
		{
			const auto distance = sq_distance(light.point.position);
			if(distance < cut_off_distance)
			{
				const auto importance = 1.f/distance; // TODO: also radius
				light_index.push_back({ index, importance });
			}
		}
		++index;
	}

	std::sort(light_index.begin(), light_index.end(), by_importance);
	for(const auto &item: light_index)
		renderShadowMap(m_spot_lights[item.index]);


	light_index.clear();
	index = 0;
	for(const auto &light: m_area_lights)
	{
		if((light.base.feature_flags & LIGHT_SHADOW_CASTER) > 0)
		{
			const auto center = (light.points[0] + light.points[1] + light.points[2] + light.points[3]) / 4.f;
			const auto distance = sq_distance(center);
			if(distance < cut_off_distance)
			{
				const auto importance = 1.f/distance; // TODO: also radius
				light_index.push_back({ index, importance });
			}
		}
		++index;
	}

	std::sort(light_index.begin(), light_index.end(), by_importance);
	for(const auto &item: light_index)
		renderShadowMap(m_area_lights[item.index]);
#endif

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glCullFace(GL_BACK);
}

void ClusteredShading::renderSkybox()
{
	m_background_shader->bind();
	m_camera.setUniforms(*m_background_shader);
	m_background_shader->setUniform("u_view_orientation"sv, glm::mat4(glm::mat3(m_camera.viewTransform())));  // only rotational part
	m_background_shader->setUniform("u_lod_level"sv,        m_background_lod_level);
	m_env_cubemap_rt->bindTexture();

	glBindVertexArray(m_skybox_vao);
	glDrawArrays     (GL_TRIANGLES, 0, 36);
}

void ClusteredShading::debugDrawSceneBounds()
{
	const auto view_projection = m_camera.projectionTransform() * m_camera.viewTransform();

	// if using VBO, generate the data into a single VBO then draw using a single call


	glDisable(GL_DEPTH_TEST);
	glEnable(GL_LINE_SMOOTH);
	glDepthMask(GL_FALSE);

	glBindBuffer(GL_ARRAY_BUFFER, m_debug_draw_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);

	// indices are fixed TODO use an element array buffer
	static const std::array<std::uint16_t, 24> indices { // see AABB::corners() for vertex ordering
		// top
		0, 1,
		1, 2,
		2, 3,
		3, 0,
		// bottom
		4, 5,
		5, 6,
		6, 7,
		7, 4,
		// "walls"
		0, 4,
		1, 5,
		2, 6,
		3, 7
	};

	// TODO: also draw AABBs for lights
	//   a tad "laborious" since the light "animation" is currently done in a compute shader


	m_line_draw_shader->bind();
	m_line_draw_shader->setUniform("u_line_color"sv, glm::vec4(0.3, 1.0, 0.7, 1));
	m_line_draw_shader->setUniform("u_mvp"sv, view_projection); // no model transform needed; we'll generate vertices in world-space

	for(const auto &obj: _scene) // _scenePvs
	{
		// TODO: the transformed AABB should be updated by the model itself, when moved that is.
		bounds::AABB tfm_aabb;
		for(const auto &corner: obj.model->aabb().corners())
			tfm_aabb.expand(obj.transform * glm::vec4(corner, 1));


		const auto &vertices = tfm_aabb.corners();
		// TODO: add UV so the shader can draw gradients?

		glNamedBufferData(m_debug_draw_vbo, GLsizeiptr(vertices.size()*sizeof(vertices[0])), &vertices[0], GL_STREAM_DRAW);
		glDrawElements(GL_LINES, indices.size(), GL_UNSIGNED_SHORT, &indices[0]);
	}


	// restore some states
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDisableVertexAttribArray(0);

/*
	for(const auto &spot: m_spot_lights)
	{
		const auto color = glm::vec4(glm::normalize(spot.point.base.color), 1);
		debugDrawSpotLight(spot, color);
	}
*/
	for(auto idx = 0u; idx < m_light_counts_ubo->num_point_lights; ++idx)
	{
		const PointLight &point = m_lights_ssbo->point_lights[idx];
		const auto color = glm::vec4(glm::normalize(point.base.color), 1);
		debugDrawSphere(point.position, point.radius, color);
	}
}

void ClusteredShading::debugDrawLine(const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec4 &color)
{
	const auto view_projection = m_camera.projectionTransform() * m_camera.viewTransform();

	m_line_draw_shader->bind();
	m_line_draw_shader->setUniform("u_line_color"sv, color);
	m_line_draw_shader->setUniform("u_mvp"sv, view_projection); // no model transform needed; we'll generate vertices in world-space

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_LINE_SMOOTH);
	glDepthMask(GL_FALSE);

	glBindBuffer(GL_ARRAY_BUFFER, m_debug_draw_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);


	const std::array<glm::vec3, 2> vertices { p1, p2 };

	glNamedBufferData(m_debug_draw_vbo, GLsizeiptr(vertices.size()*sizeof(vertices[0])), &vertices[0], GL_STREAM_DRAW);
	glDrawArrays(GL_LINES, 0, vertices.size());


	// restore some states
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDisableVertexAttribArray(0);
}

void ClusteredShading::debugDrawLine(const glm::uvec2 &p1, const glm::uvec2 &p2, const glm::vec4 &color, float thickness)
{
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_2d_line_shader->bind();

	m_2d_line_shader->setUniform("u_start"sv, p1);
	m_2d_line_shader->setUniform("u_end"sv, p2);
	m_2d_line_shader->setUniform("u_line_color"sv, color);
	m_2d_line_shader->setUniform("u_thickness", std::max(1.f, thickness));

	glBindVertexArray(_empty_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

void ClusteredShading::debugDrawRect(const glm::uvec2 &top_left, const glm::uvec2 &size, const glm::vec4 &color, float thickness)
{
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_2d_rect_shader->bind();

	m_2d_rect_shader->setUniform("u_rect_min"sv, top_left);
	m_2d_rect_shader->setUniform("u_rect_max"sv, top_left + size);
	m_2d_rect_shader->setUniform("u_line_color"sv, color);
	m_2d_rect_shader->setUniform("u_thickness", thickness); // 0 = filled

	glBindVertexArray(_empty_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

void ClusteredShading::debugDrawNumber(uint32_t number, const glm::uvec2 &bottom_right, float height, const glm::vec4 &color, float thickness)
{
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	m_2d_7segment_shader->bind();

	m_2d_7segment_shader->setUniform("u_number"sv, number);
	m_2d_7segment_shader->setUniform("u_bottom_right"sv, bottom_right);
	m_2d_7segment_shader->setUniform("u_height"sv, height);
	m_2d_7segment_shader->setUniform("u_color"sv, color);
	m_2d_7segment_shader->setUniform("u_thickness"sv, thickness);

	glBindVertexArray(_empty_vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

void ClusteredShading::debugDrawSphere(const glm::vec3 &center, float radius, const glm::vec4 &color)
{
	debugDrawSphere(center, radius, 8, 10, color);
}

void ClusteredShading::debugDrawSphere(const glm::vec3 &center, float radius, size_t rings, size_t slices, const glm::vec4 &color)
{
	auto view_projection = m_camera.projectionTransform() * m_camera.viewTransform();
	auto transform = view_projection * glm::translate(glm::mat4(1), center) * glm::scale(glm::mat4(1), { radius, radius, radius });

	m_line_draw_shader->bind();
	m_line_draw_shader->setUniform("u_line_color"sv, color);
	m_line_draw_shader->setUniform("u_mvp"sv, transform);

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_LINE_SMOOTH);
	glDepthMask(GL_FALSE);

	glBindBuffer(GL_ARRAY_BUFFER, m_debug_draw_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);


	static std::vector<glm::vec3> vertices;
	vertices.reserve(6 * (rings + 1) * slices);
	vertices.clear();

	// this bit was "borrowed" from Raylib's DrawSphereEx()  (c++-ified)

	const auto ring_step = glm::radians(180.f/float(rings + 1));
	const auto slice_step = glm::radians(360.f/float(slices));

	// const auto ring_rot = glm::angleAxis(ring_step, AXIS_Y);

	const auto ring_cos = std::cos(ring_step);
	const auto ring_sin = std::sin(ring_step);
	const auto slice_cos = std::cos(slice_step);
	const auto slice_sin = std::sin(slice_step);

	// rotation matrix around z axis
	// const auto ring_rotate = glm::rotate(glm::mat4(1),  ring_step, AXIS_Z);
	// rotation matrix around y axis
	// const auto slice_rotate = glm::rotate(glm::mat4(1), slice_step, AXIS_Y);

	std::array<glm::vec3, 4> base_verts;
	base_verts[2] = AXIS_Y;
	base_verts[3] = { ring_sin, ring_cos, 0 };

	for(auto ring = 0u; ring < rings + 1; ++ring)
	{
		for(auto slice = 0u; slice < slices; ++slice)
		{
			// rotate around y axis to set up vertices for next face
			base_verts[0] = base_verts[2];
			base_verts[1] = base_verts[3];
			// base_verts[2] = slice_rotate * glm::vec4(base_verts[2], 0);
			// base_verts[3] = slice_rotate * glm::vec4(base_verts[3], 0);
			base_verts[2] = {
				slice_cos*base_verts[2].x - slice_sin*base_verts[2].z,
				base_verts[2].y,
				slice_sin*base_verts[2].x + slice_cos*base_verts[2].z
			};
			base_verts[3] = {
				slice_cos*base_verts[3].x - slice_sin*base_verts[3].z,
				base_verts[3].y,
				slice_sin*base_verts[3].x + slice_cos*base_verts[3].z
			};

			vertices.push_back(base_verts[0]);
			vertices.push_back(base_verts[3]);
			vertices.push_back(base_verts[1]);

			vertices.push_back(base_verts[0]);
			vertices.push_back(base_verts[2]);
			vertices.push_back(base_verts[3]);
		}

		// rotate around z axis to set up  starting vertices for next ring
		base_verts[2] = base_verts[3];
		// base_verts[3] = ring_rotate * glm::vec4(base_verts[3], 0);
		base_verts[3] = { // rotation matrix around z axis
			 ring_cos*base_verts[3].x + ring_sin*base_verts[3].y,
			-ring_sin*base_verts[3].x + ring_cos*base_verts[3].y,
																  base_verts[3].z
		};
	}

	// TODO: cache vertices? (key: rings + slices)

	glNamedBufferData(m_debug_draw_vbo, GLsizeiptr(vertices.size()*sizeof(vertices[0])), &vertices[0], GL_STREAM_DRAW);
	glDrawArrays(GL_LINES, 0, GLsizei(vertices.size()));


	// restore some states
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDisableVertexAttribArray(0);
}

void ClusteredShading::debugDrawSpotLight(const SpotLight &light, const glm::vec4 &color)
{
	const auto &L = light;

	const auto dir_space = make_common_space_from_direction(L.direction);
	const auto line_rot = glm::rotate(glm::mat4(1), -L.outer_angle, dir_space[0]);
	const glm::vec3 dir_line = line_rot * glm::vec4(L.direction, 0)*L.point.radius;

	static constexpr auto num_lines = 24;
	const auto rot_angle = glm::radians(360.f / float(num_lines));
	glm::vec3 first_end;
	glm::vec3 last_end;
	for(int idx = 0; idx < num_lines; ++idx)
	{
		glm::vec3 end_point = L.point.position + glm::vec3(glm::rotate(glm::mat4(1), rot_angle*float(idx), L.direction) * glm::vec4(dir_line, 0));

		debugDrawLine(L.point.position, end_point, color);
		if(idx > 0)
			debugDrawLine(end_point, last_end, color);
		else
			first_end = end_point;
		last_end = end_point;
	}

	// center/axis
	debugDrawLine(first_end, last_end, color);
	debugDrawLine(L.point.position, L.point.position + L.direction*L.point.radius, color);

	// TODO: draw cap
}

void ClusteredShading::debugDrawClusterGrid()
{
	const glm::vec4 grid_color{ 0.1f, 1.f, 0.6f, m_debug_clusters_blend_factor };
	const glm::vec4 cluster_color{ 0.7f, 0.3f, 0.2f, m_debug_clusters_blend_factor };
	const glm::vec4 text_color { 1, 0.9f, 0.2f, m_debug_clusters_blend_factor };
	const glm::vec4 grid2d_color{ 0.1f, 0.3f, 0.9f, 0.8f * m_debug_clusters_blend_factor };

	const auto ww = Window::width();
	const auto wh = Window::height();
	const auto aspect = float(ww)/float(wh);


	// draw screen grid, starting bottom left
	const auto stride = glm::vec2(m_cluster_block_size, m_cluster_block_size);
	for(auto x = 0.f; x < float(Window::width()); x += stride.x)
		debugDrawLine({ uint32_t(x), 0u }, { uint32_t(x), Window::height() - 1 }, grid2d_color);
	for(auto y = float(Window::height() - 1); y > 0.f; y -= stride.y)
		debugDrawLine({ 0u, uint32_t(y) }, { Window::width() - 1, uint32_t(y) }, grid2d_color);



	auto draw_grid = [this, &grid_color](const glm::uvec2 &top_left, const glm::uvec2 &size, const glm::uvec2 &dims) {

		auto x_stride = float(size.x) / float(dims.x);
		auto y_stride = float(size.y) / float(dims.y);

		// vertical lines (keft to right)
		auto x = float(top_left.x) + x_stride;
		while(x < float(top_left.x + size.x))
		{
			debugDrawLine({ uint32_t(x), top_left.y }, { uint32_t(x), top_left.y + size.y }, grid_color);
			x += x_stride;
		}

		// horizontallines (top to bottom)
		auto y = float(top_left.y) + y_stride;
		while(y < float(top_left.y + size.y))
		{
			debugDrawLine({ top_left.x, uint32_t(y) }, { top_left.x + size.x, uint32_t(y) }, grid_color);
			y += y_stride;
		}

		debugDrawRect(top_left, size, grid_color, 4.f);
	};


	const auto base_size = uint32_t(float(Window::width())/6.4f);
	const auto pad = base_size/30u;

	const glm::uvec2 front_rect { base_size, uint32_t(float(base_size)/aspect) };
	const glm::uvec2 side_rect { uint32_t(float(base_size)*1.8f), front_rect.y };
	const glm::uvec2 top_rect   { front_rect.x, side_rect.x };

	const glm::uvec2 front_top_left { pad, wh - pad - front_rect.y };
	const glm::uvec2 side_top_left  { pad + front_rect.x + pad, wh - pad - front_rect.y };
	const glm::uvec2 top_top_left   { pad, wh - pad - front_rect.y - pad - top_rect.y };

	draw_grid(front_top_left, front_rect, { m_cluster_resolution.x, m_cluster_resolution.y });
	draw_grid(side_top_left,  side_rect,  { m_cluster_resolution.z, m_cluster_resolution.y });
	draw_grid(top_top_left,   top_rect,   { m_cluster_resolution.x, m_cluster_resolution.z });

	const auto front_cell_size = glm::uvec2 { front_rect.x / m_cluster_resolution.x, front_rect.y / m_cluster_resolution.y };
	const auto side_cell_size  = glm::uvec2 { side_rect.x  / m_cluster_resolution.z, side_rect.y  / m_cluster_resolution.y };
	const auto top_cell_size   = glm::uvec2 { top_rect.x   / m_cluster_resolution.x, top_rect.y   / m_cluster_resolution.z };


	auto discovery_view = m_cluster_discovery_ssbo.view();
	const auto &discovery = *discovery_view;
	const auto num_nonempty = discovery[0]; // cull_lights_args.x;

	static std::vector<uint32_t> nonempty_clusters;
	nonempty_clusters.reserve(num_nonempty);
	nonempty_clusters.clear();

	static constexpr auto nonempty_offset = 3u; // skipping cull_lights_args, num_active

	for(auto idx = 0u; idx < m_clusters_count; ++idx)
	{
		if(discovery[idx + nonempty_offset] == 1)
			nonempty_clusters.push_back(idx);
	}
	std::sort(nonempty_clusters.begin(), nonempty_clusters.end());
/*
	auto index2coord = [this](auto index) -> glm::uvec3 {
		return glm::uvec3 {
			index % m_cluster_resolution.x,
			index % (m_cluster_resolution.x * m_cluster_resolution.y) / m_cluster_resolution.x,
			index / (m_cluster_resolution.x * m_cluster_resolution.y)
		};
	};

	auto draw_cell = [this, cluster_color](glm::uvec2 top_left, glm::uvec2 coord, glm::uvec2 size) {
		top_left += coord*size + glm::uvec2{ 1, 1 };
		size -= glm::uvec2{ 2, 2 };
		debugDrawRect(top_left, size, cluster_color, 0.f);
	};
*/
	// auto c_lights_view = m_cluster_lights_ssbo.view();
	auto clusters_view = m_simple_clusters_aabb_ssbo.view();
	// auto points_view = m_point_lights_ssbo.view();
	// auto points_idx_view = m_point_lights_index_ssbo.view();

	static uvec2_map<uint32_t> visited_front;
	visited_front.reserve(m_cluster_resolution.x * m_cluster_resolution.y);
	visited_front.clear();
	static uvec2_map<uint32_t> visited_side;
	visited_side.reserve(m_cluster_resolution.z * m_cluster_resolution.y);
	visited_side.clear();
	static uvec2_map<uint32_t> visited_top;
	visited_top.reserve(m_cluster_resolution.x * m_cluster_resolution.z);
	visited_top.clear();

	/*
	auto keep_max = [](auto &target, auto value) {
		if(value > target)
			target = value;
	};

	for(const auto &index: nonempty_clusters)
	{
		// std::print(" {:3}", index);
		const auto coord = index2coord(index);

		// const auto index_range = (*c_lights_view)[index];
		// const auto num_lights = index_range.count;
		const auto num_lights = (*clusters_view)[index].num_point_lights;
		if(true or num_lights)
		{
			const auto front_coord = glm::uvec2{ coord.x, m_cluster_resolution.y - 1 - coord.y };
			const auto side_coord = glm::uvec2{ coord.z, m_cluster_resolution.y - 1 - coord.y };
			const auto top_coord = glm::uvec2{ coord.x, m_cluster_resolution.z - 1 - coord.z };

			if(not visited_front.contains(front_coord))
				draw_cell(front_top_left, front_coord, front_cell_size);
			keep_max(visited_front[front_coord], num_lights);

			if(not visited_side.contains(side_coord))
				draw_cell(side_top_left, side_coord, side_cell_size);
			keep_max(visited_side[side_coord], num_lights);

			if(not visited_top.contains(top_coord))
				draw_cell(top_top_left, top_coord, top_cell_size);
			keep_max(visited_top[top_coord], num_lights);
		}
	}
*/
	// std::puts("");

	auto draw_light_counts = [this, &text_color](auto top_left, auto cell_size, auto visited) {

		float text_size = float(cell_size.y) * 0.4f;
		float thickness = text_size/10.f;

		for(const auto &[coord, num_lights]: visited)
		{
			const auto bottom_right = top_left + cell_size + coord*cell_size - glm::uvec2{8, 8};
			debugDrawNumber(num_lights, bottom_right, text_size, text_color, thickness);
		}
	};

	draw_light_counts(front_top_left, front_cell_size, visited_front);
	draw_light_counts(side_top_left,  side_cell_size,  visited_side);
	draw_light_counts(top_top_left,   top_cell_size,   visited_top);
}

void ClusteredShading::draw2d(const Texture &texture, BlendMode blend)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

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
}

void ClusteredShading::draw2d(const Texture &source, RenderTarget::Texture2d &target, BlendMode blend)
{
	if(blend == BlendMode::Replace)
		glDisable(GL_BLEND);
	else
	{
		glEnable(GL_BLEND);
		if(blend != BlendMode::Subtract)
			glBlendEquation(GL_FUNC_ADD);
	}

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

const std::vector<StaticObject> &ClusteredShading::cullScene()
{
	const auto T0 = steady_clock::now();
	// TODO: in theory, this could be done in multiple threads
	//   however, a space partitioning scheme is probably a better (first) step

	_scenePvs.clear();
	_scenePvs.reserve(256); // a guesstimate how many objects are visible (maybe a % of total count?)

	// perform frustum culling of all objects in the scene (or a partition there of)

	const auto view_pos = m_camera.position();
	const auto &frustum = m_camera.frustum();
	// TODO: _scenePvs = _scene.cull(view_pos, frustum)

	for(const auto &obj: _scene) // TODO _scene.near(view_pos, m_camera.farPlane()) i.e. everything within range of the camera's far plane
	{
		auto result = intersect::check(frustum, obj.model->aabb(), obj.transform);

		// std::print("distance to plane");
		// static const char *plane_name[] = { "L", "R", "T", "B", "Fr", "Bk" };
		// for(auto idx = 0u; idx < 6; ++idx)
		// 	std::print("  {}: {:.3f}", plane_name[idx], result.distance_to_plane[idx]);
		// std::puts("");

		if(result.visible)
			_scenePvs.push_back(obj);
		else
		{
			// TODO: visualize result based on result.culled_by_plane, etc.
			// if(result.culled_by_aabb)
			// 	std::puts("culled by AABB");
			// else if(result.culled_by_plane >= 0)
			// 	std::print("culled by plane: {}\n", result.culled_by_plane);
			// else
			// 	std::puts("culled by corner");
		}
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
	// TODO: for each model:
	//   this should frustum cull models (and cache the result for other passes)
	//   this would also include skinned meshes (don't want to do the skinning computations multiple times)
	//   (AnimatedMode::BoneTransform() genereates a list of bone transforms, done once, but the actual skinning is in the shader)


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
	target.bindRenderTarget(RenderTarget::DepthBuffer, rect);

	glDepthMask(GL_TRUE);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthFunc(GL_LESS);

    m_depth_prepass_shader->bind();

	renderScene(view_projection, *m_depth_prepass_shader, NoMaterials);
}

void ClusteredShading::renderShadowDepth(const glm::vec3 &pos, float far_z, const glm::mat4 &view_projection, RenderTarget::Texture2d &target, const glm::ivec4 &rect)
{
	// TODO: ideally, only render objects whose AABB intersects with the sphere { pos, far_z }

	target.bindRenderTarget(RenderTarget::DepthBuffer, rect);

	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_FALSE, GL_FALSE);  // writing 2-component normals
	glDepthFunc(GL_LESS);

	m_shadow_depth_shader->bind();

	m_shadow_depth_shader->setUniform("u_cam_pos"sv, pos);
	m_shadow_depth_shader->setUniform("u_far_z"sv, far_z);

	renderScene(view_projection, *m_shadow_depth_shader, NoMaterials);
}

void ClusteredShading::renderLighting(const Camera &camera)
{
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthFunc(GL_EQUAL);   // only draw pixels which exactly match the depth pre-pass

    m_clustered_pbr_shader->bind();

	// TODO: camera.setUniforms(*m_clustered_pbr_shader);
	m_clustered_pbr_shader->setUniform("u_cam_pos"sv,                    camera.position());
	m_clustered_pbr_shader->setUniform("u_view"sv,                       camera.viewTransform());
	m_clustered_pbr_shader->setUniform("u_near_z"sv,                     camera.nearPlane());
	m_clustered_pbr_shader->setUniform("u_cluster_resolution"sv,         m_cluster_resolution);
	m_clustered_pbr_shader->setUniform("u_cluster_size_ss"sv,            glm::uvec2(m_cluster_block_size));
	m_clustered_pbr_shader->setUniform("u_log_cluster_res_y"sv,          m_log_cluster_res_y);
	m_clustered_pbr_shader->setUniform("u_num_cluster_avg_lights"sv,     uint32_t(CLUSTER_AVERAGE_LIGHTS));

	m_clustered_pbr_shader->setUniform("u_shadow_bias_constant"sv, m_shadow_bias_constant);
	m_clustered_pbr_shader->setUniform("u_shadow_bias_slope_scale"sv, m_shadow_bias_slope_scale);
	m_clustered_pbr_shader->setUniform("u_shadow_bias_slope_power"sv, m_shadow_bias_slope_power);
	m_clustered_pbr_shader->setUniform("u_shadow_bias_distance_scale"sv, m_shadow_bias_distance_scale);
	m_clustered_pbr_shader->setUniform("u_shadow_bias_scale"sv, m_shadow_bias_scale);

	m_clustered_pbr_shader->setUniform("u_debug_cluster_geom"sv,                    m_debug_cluster_geom);
	m_clustered_pbr_shader->setUniform("u_debug_clusters_occupancy"sv,              m_debug_clusters_occupancy);
	m_clustered_pbr_shader->setUniform("u_debug_clusters_occupancy_blend_factor"sv, m_debug_clusters_blend_factor);

    m_irradiance_cubemap_rt->bindTexture(6);
    m_prefiltered_env_map_rt->bindTexture(7);
	m_brdf_lut_rt->bindTextureSampler(8);
    m_ltc_mat_lut->Bind(9);
    m_ltc_amp_lut->Bind(10);

	_shadow_atlas.bindDepthTextureSampler(20);
	_shadow_atlas.bindTextureSampler(21);   // encoded normals

	const auto view_projection = m_camera.projectionTransform() * m_camera.viewTransform();
	renderScene(view_projection, *m_clustered_pbr_shader);

	// Enable writing to the depth buffer
	glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
}


void ImGui_ImageEx(ImTextureID texture_id, ImVec2 size, ImVec2 uv0, ImVec2 uv1, GLuint shader_id);

void ClusteredShading::render_gui()
{
    /* This method is responsible for rendering GUI using ImGUI. */

    /*
     * It's possible to call render_gui() from the base class.
     * It renders performance info overlay.
     */
    CoreApp::render_gui();

    /* Create your own GUI using ImGUI here. */
	ImVec2 window_pos       = ImVec2(float(Window::width()) - 10.f, 10.f);
	ImVec2 window_pos_pivot = ImVec2(1.0f, 0.0f);

    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
	ImGui::SetNextWindowSize({ 400, 800 });

	ImGui::Text("   Culling: %4ld µs", m_cull_scene_time.average().count());
	ImGui::Text("   Shadows: %4ld µs", m_shadow_time.average().count());
	ImGui::Text("    Z-pass: %4ld µs", m_depth_time.average().count());
	ImGui::Text("Clstr.find: %4ld µs", m_cluster_find_time.average().count());
	ImGui::Text("Clstr.coll: %4ld µs", m_cluster_index_time.average().count());
	ImGui::Text("Light cull: %4ld µs", m_light_cull_time.average().count());
	ImGui::Text("   Shading: %4ld µs", m_shading_time.average().count());
	ImGui::Text("    Skybox: %4ld µs", m_skybox_time.average().count());
	// ImGui::Text("        PP: %3ld µs", m_pp_time.count());
	ImGui::Text("Scattering: %4ld µs", m_scatter_time.average().count());
	ImGui::Text("   PP blur: %4ld µs", m_pp_blur_time.average().count());

	ImGui::Begin("Settings");
    {
		ImGui::Text("T: %6.2f", _running_time.count());

		if (ImGui::CollapsingHeader("Camera Info", ImGuiTreeNodeFlags_DefaultOpen))
        {
			const auto cam_pos   = m_camera.position();
			const auto cam_fwd   = m_camera.forwardVector();
			const auto cam_right = m_camera.rightVector();
			const auto cam_up    = m_camera.upVector();

			const auto fwd_xz = glm::normalize(glm::vec3(cam_fwd.x, 0.f, cam_fwd.z));
			const float heading_angle = std::acos(glm::clamp(glm::dot(AXIS_Z, fwd_xz), -1.f, 1.f));


			ImGui::Text("     Yaw : %6.1f   Pitch : %5.1f\n"
						"Position : %5.1f ; %5.1f ; %5.1f\n"
						"Forward  : %5.2f ; %5.2f ; %5.2f   %5.1f°\n"
						"Right    : %5.2f ; %5.2f ; %5.2f\n"
						"Up       : %5.2f ; %5.2f ; %5.2f",
						glm::degrees(m_camera.yaw()), glm::degrees(m_camera.pitch()),
						cam_pos.x, cam_pos.y, cam_pos.z,
						cam_fwd.x, cam_fwd.y, cam_fwd.z, glm::degrees(heading_angle),
						cam_right.x, cam_right.y, cam_right.z,
						cam_up.x, cam_up.y, cam_up.z);
			ImGui::Text("PVS size : %lu", _scenePvs.size());

			ImGui::Checkbox("Draw AABB", &m_debug_draw_aabb);
			if(ImGui::SliderFloat("FOV", &m_camera_fov, 25.f, 150.f))
				calculateShadingClusterGrid();
		}

		if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);

			ImGui::Text("Cluster  resolution: %u x %u x %u", m_cluster_resolution.x, m_cluster_resolution.y, m_cluster_resolution.z);
			ImGui::Checkbox("Draw cluster grid (slow!)  [c]", &m_debug_draw_cluster_grid);
			if (ImGui::Checkbox("Show cluster geom", &m_debug_cluster_geom))
                m_debug_clusters_occupancy = false;

			if (ImGui::Checkbox("Show cluster occupancy", &m_debug_clusters_occupancy))
				m_debug_cluster_geom = false;

			if (m_debug_cluster_geom or m_debug_clusters_occupancy or m_debug_draw_cluster_grid)
				ImGui::SliderFloat("Cluster debug blending", &m_debug_clusters_blend_factor, 0.0f, 1.0f);

			ImGui::Checkbox   ("Animate Lights",    &m_animate_lights);
			ImGui::SliderFloat("Animation Speed",   &m_animation_speed, 0.0f, 15.0f, "%.1f");

			ImGui::PopItemWidth();
        }

        if (ImGui::CollapsingHeader("Tonemapper"))
        {
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
            ImGui::SliderFloat("Exposure",             &m_exposure,             0.0, 10.0, "%.1f");
            ImGui::SliderFloat("Gamma",                &m_gamma,                0.0, 10.0, "%.1f");
			ImGui::SliderFloat("Background LOD level", &m_background_lod_level, 0.0, glm::log2(float(m_env_cubemap_rt->width())), "%.1f");

			if (ImGui::BeginCombo("HDR map", m_hdr_maps_names[m_current_hdr_map_idx].data()))
            {
				for (uint8_t idx = 0; idx < std::size(m_hdr_maps_names); ++idx)
                {
					const bool is_selected = (m_current_hdr_map_idx == idx);
					if (ImGui::Selectable(m_hdr_maps_names[idx].data(), is_selected))
                    {
						m_current_hdr_map_idx = idx;
                        PrecomputeIndirectLight(FileSystem::getResourcesPath() / "textures/skyboxes/IBL" / m_hdr_maps_names[m_current_hdr_map_idx]);
                    }

                    if (is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
        }

        if (ImGui::CollapsingHeader("Bloom"))
        {
            ImGui::Checkbox   ("Bloom enabled",        &m_bloom_enabled);
			ImGui::SliderFloat("Bloom threshold",      &m_bloom_threshold,      0, 15.f, "%.1f");
			ImGui::SliderFloat("Bloom knee",           &m_bloom_knee,           0,  1.f, "%.1f");
			ImGui::SliderFloat("Bloom intensity",      &m_bloom_intensity,      0,  2.f, "%.1f");
			ImGui::SliderFloat("Bloom dirt intensity", &m_bloom_dirt_intensity, 0, 10.f, "%.1f");
        }
		if(ImGui::CollapsingHeader("Fog / Scattering", ImGuiTreeNodeFlags_DefaultOpen))
		{
			{
				float density = m_fog_density*100.f;
				if(ImGui::SliderFloat("Fog density", &density, 0.f, 1.f))
					m_fog_density = density/100.f;
			}
			ImGui::Text("Ray march noise (N): %d", _ray_march_noise);
		}

		if(ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat("Bias constant",    &m_shadow_bias_constant,      -0.02f,   0.02f, "%.3f");
			ImGui::SliderFloat("Bias slope scale", &m_shadow_bias_slope_scale,    0.f,    5.f,   "%.1f");
			ImGui::SliderFloat("Bias slope power", &m_shadow_bias_slope_power,    0.01f,  5.f,   "%.2f");
			ImGui::SliderFloat("Bias dist. scale", &m_shadow_bias_distance_scale, 0.f,    0.001f,"%.3f");
			ImGui::SliderFloat("Bias scale",       &m_shadow_bias_scale,         -0.2f,    2.f,   "%.1f");
		}

		if(ImGui::CollapsingHeader("Images", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static const char *rt_names[] = {
				"-- No selection",
				// cube
				"env_cubemap_rt  [cube]",
				"irradiance_cubemap_rt  [cube]",
				"prefiltered_env_map_rt  [cube]",

				// 2d
				"depth_pass_rt",
				"rt",
				"pp_low_rt",
				"pp_full_rt",
				"final_rt",
				"shadow_atlas",
			};
			static int current_image = 9;
			ImGui::Combo("Render target", &current_image, rt_names, std::size(rt_names));

			RenderTarget::Texture2d *rt = nullptr;
			RenderTarget::Cube *rtc = nullptr;
			switch(current_image)
			{
			case 1: rtc = m_env_cubemap_rt.get(); break;
			case 2: rtc = m_irradiance_cubemap_rt.get(); break;
			case 3: rtc = m_prefiltered_env_map_rt.get(); break;
			case 4: rt = &m_depth_pass_rt; break;
			case 5: rt = &_rt; break;
			case 6: rt = &_pp_low_rt; break;
			case 7: rt = &_pp_full_rt; break;
			case 8: rt = &_final_rt; break;
			case 9: rt = &_shadow_atlas; break;
			}
			// const bool is_cube = current_image >= 1 and current_image <= 3;
			// const bool is_depth = current_image == 4;

			auto format_str = [](auto f) {
				if(not f)
					return "none";
				switch(f)
				{
				case GL_RGB: return "RGB";
				case GL_RGBA: return "RGBA";
				case GL_R16F: return "R16F";
				case GL_RG16F: return "RG16F";
				case GL_RGBA32F: return "RGBA32F";
				case GL_DEPTH_COMPONENT32F: return "32F";
				}
				return "?";
			};


			static constexpr ImVec2 top_left { 0, 1 };
			static constexpr ImVec2 bottom_right { 1, 0 };

			const auto vMin = ImGui::GetWindowContentRegionMin();
			const auto vMax = ImGui::GetWindowContentRegionMax();
			const auto win_width = std::min(vMax.x - vMin.x, 512.f);

			if(rt)
			{
				float aspect = float(rt->width()) / float(rt->height());

				const ImVec2 img_size { win_width, float(win_width)/aspect };

				if(rt->has_color() and rt->color_texture())
				{
					auto &texture = rt->color_texture();

					ImGui_ImageEx(texture.texture_id(), img_size, top_left, bottom_right, 0);

					const auto color_f = rt->color_format();
					ImGui::Text("Color: %u x %u  %s", rt->width(), rt->height(), format_str(color_f));
				}

				if(rt->has_depth() and rt->depth_texture())
				{
					auto &texture = rt->depth_texture();

					// render with shader to show as gray scale
					ImGui_ImageEx(texture.texture_id(), img_size, top_left, bottom_right, m_imgui_depth_texture_shader->program_id());

					const auto depth_f = rt->depth_format();
					ImGui::Text("Depth: %u x %u  %s", rt->width(), rt->height(), format_str(depth_f));
				}
			}

			if(rtc)
			{
				float aspect = float(rtc->width()) / float(rtc->height());

				const ImVec2 img_size { win_width / 2, float(win_width / 2)/aspect };

				if(rtc->has_color() and rtc->color_texture())
				{
					auto &texture = rtc->color_texture();

					const char *names[] = { "right", "left", "up", "down", "front", "back" };
					for(auto face = 0u; face < 6; ++face)
					{
						ImGui_ImageEx(texture.texture_face_id(CubeFace(face)), img_size, top_left, bottom_right, 0);
						ImGui::Text("%u: %s", face, names[face]);
					}
				}
			}
		}
	}
    ImGui::End();
}

inline ImVec2 operator + (const ImVec2 &A, const ImVec2 &B)
{
	return { A.x + B.x, A.y + B.y };
}

void ImGui_ImageEx(ImTextureID texture_id, ImVec2 size, ImVec2 uv0, ImVec2 uv1, GLuint shader_id)
{
	struct CB_args
	{
		GLuint program_id;
		GLuint texture_id;
	};

	auto *args = new CB_args{
		shader_id,
		GLuint(texture_id),
	};

	static TextureSampler clamp0_sampler;
	if(not clamp0_sampler)
	{
		clamp0_sampler.Create();
		clamp0_sampler.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::Linear);
		clamp0_sampler.SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
	}

	auto dl = ImGui::GetWindowDrawList();

	dl->AddCallback([](const ImDrawList *, const ImDrawCmd *cmd) {
		CB_args *args = static_cast<CB_args *>(cmd->UserCallbackData);
		if(args->program_id)
		{
			const auto *draw_data = ImGui::GetDrawData();
			float left = draw_data->DisplayPos.x;
			float right = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
			float top = draw_data->DisplayPos.y;
			float bottom = draw_data->DisplayPos.y + draw_data->DisplaySize.y;

			const auto ortho_proj = glm::orthoLH(left, right, bottom, top, 1.f, -1.f);

			glUseProgram(args->program_id);
			const auto projLoc = glGetUniformLocation(args->program_id, "u_projection");
			glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(ortho_proj));
		}
		glBindTextureUnit(0, args->texture_id);
		delete args;

		clamp0_sampler.Bind(0);
	}, args);

	dl->AddImage(texture_id, ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + size, uv0, uv1);
	ImGui::Dummy(size);  // Reserve layout space *before* drawing

	dl->AddCallback([](const ImDrawList *, const ImDrawCmd *) {
		glUseProgram(0);
		glBindTextureUnit(0, 0);
		glBindSampler(0, 0);
	}, nullptr);

	// reset all ImGui state
	dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}
