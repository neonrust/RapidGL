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


static float s_spot_inner_angle = 3.f;
static float s_spot_outer_angle = 30.f;


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
		fprintf(stderr, "GL ERROR: type = 0x%x, severity = 0x%x \"%s\"\n", type, severity, message );
}


using namespace RGL;

ClusteredShading::ClusteredShading() :
	m_directional_lights_ssbo(DIRECTIONAL_LIGHTS_SSBO_BINDING_INDEX, GL_DYNAMIC_DRAW),
	m_point_lights_ssbo(POINT_LIGHTS_SSBO_BINDING_INDEX, GL_DYNAMIC_DRAW),
	m_spot_lights_ssbo(SPOT_LIGHTS_SSBO_BINDING_INDEX, GL_DYNAMIC_DRAW),
	m_area_lights_ssbo(AREA_LIGHTS_SSBO_BINDING_INDEX),
	m_point_lights_orbit_ssbo(POINT_LIGHTS_ORBIT_SSBO_BINDING_INDEX),
	m_spot_lights_orbit_ssbo(SPOT_LIGHTS_ORBIT_SSBO_BINDING_INDEX),
	m_exposure            (0.4f),
	m_gamma               (2.2f),
	m_background_lod_level(1.2f),
	m_skybox_vao          (0),
	m_skybox_vbo          (0),
	m_bloom_threshold     (0.8f),
	m_bloom_knee          (0.1f),
	m_bloom_intensity     (1.5f),
	m_bloom_dirt_intensity(0),
	m_bloom_enabled       (false),
	m_fog_density         (0.2f), // [ 0, 0.5 ]   nice-ish value: 0.015
	m_fog_falloff_blend   (0.05f),
	m_ray_march_stride    (0.01f)
{
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

	glDeleteBuffers(1, &m_clusterShading.ssbo.aabb);
    glDeleteBuffers(1, &m_cull_lights_dispatch_args_ssbo);
	glDeleteBuffers(1, &m_nonempty_clusters_ssbo);
    glDeleteBuffers(1, &m_point_light_index_list_ssbo);
    glDeleteBuffers(1, &m_point_light_grid_ssbo);
    glDeleteBuffers(1, &m_spot_light_index_list_ssbo);
    glDeleteBuffers(1, &m_spot_light_grid_ssbo);
    glDeleteBuffers(1, &m_area_light_index_list_ssbo);
    glDeleteBuffers(1, &m_area_light_grid_ssbo);
	glDeleteBuffers(1, &m_active_clusters_ssbo);

	// glDeleteTextures(1, &m_depth_tex2D_id);
	// glDeleteFramebuffers(1, &m_depth_pass_fbo_id);
}

void ClusteredShading::init_app()
{
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(opengl_message_callback, 0);

    /// Initialize all the variables, buffers, etc. here.
	glClearColor(0.05f, 0.05f, 0.05f, 1);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
	glCullFace(GL_BACK);

	// glLineWidth(2.f); // for wireframes (but >1 not commonly supported)

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	glCreateVertexArrays(1, &_dummy_vao_id);

	// Create camera
	m_camera = Camera(m_camera_fov, 0.1f, 200);
	m_camera.setSize(Window::getWidth(), Window::getHeight());
	// m_camera.setPosition(-8.32222f, 4.5269f, -0.768721f);
	// m_camera.setOrientation(glm::quat(0.634325f, 0.0407623f, 0.772209f, 0.0543523f));
	m_camera.setPosition({ -19, 3.3f, -15 });
	m_camera.setOrientationEuler({ 0, 160, 0 });

    /// Randomly initialize lights
	::srand(3281991);
    GeneratePointLights();
    GenerateSpotLights();
    GenerateAreaLights();

	/// Create scene objects
	{
		const auto models_path = FileSystem::getResourcesPath() / "models";

		// auto sponza_model = std::make_shared<StaticModel>();
		// sponza_model->Load(models_path / "sponza2/Sponza2.gltf");

		// auto world_trans  = glm::mat4(1);
		// 	 world_trans  = glm::scale(world_trans, glm::vec3(sponza_model->GetUnitScaleFactor() * 30.0f));
		// // m_sponza_static_object = StaticObject(sponza_model, world_trans);
		// _scene.emplace_back(sponza_model, world_trans);

		auto testroom_model = std::make_shared<StaticModel>();
		testroom_model->Load(models_path / "testroom" / "testroom.gltf");

		_scene.emplace_back(testroom_model, glm::mat4(1));
	}

	// Create depth pre-pass render target
	m_depth_pass_rt.create(size_t(Window::getWidth()), size_t(Window::getHeight()), RGL::RenderTarget::Depth);

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

	// Create a buffer to hold (boolean) flags in the cluster grid that contain samples.
	glCreateBuffers(1, &m_clusterShading.ssbo.aabb);
	glBindBufferBase (GL_SHADER_STORAGE_BUFFER, CLUSTERS_SSBO_BINDING_INDEX, m_clusterShading.ssbo.aabb);
	glCreateBuffers(1, &m_nonempty_clusters_ssbo);
	glBindBufferBase (GL_SHADER_STORAGE_BUFFER, NONEMPTY_CLUSTERS_SSBO_BINDING_INDEX, m_nonempty_clusters_ssbo);

	// A buffer (and internal counter) that holds a list of the active/used clusters (i.e. that contain fragments).
	glCreateBuffers(1, &m_active_clusters_ssbo);
	glBindBufferBase (GL_SHADER_STORAGE_BUFFER, ACTIVE_CLUSTERS_SSBO_BINDING_INDEX, m_active_clusters_ssbo);

	// A buffer that stores number of work groups to be dispatched by cull lights shader
	glCreateBuffers(1, &m_cull_lights_dispatch_args_ssbo);
	glBindBufferBase (GL_SHADER_STORAGE_BUFFER, CULL_LIGHTS_DISPATCH_ARGS_SSBO_BINDING_INDEX, m_cull_lights_dispatch_args_ssbo);

	// A list of indices to the lights that are active and intersect with a cluster
	glCreateBuffers(1, &m_point_light_index_list_ssbo);
	glBindBufferBase (GL_SHADER_STORAGE_BUFFER, POINT_LIGHT_INDEX_LIST_SSBO_BINDING_INDEX, m_point_light_index_list_ssbo);
	glCreateBuffers(1, &m_spot_light_index_list_ssbo);
	glBindBufferBase (GL_SHADER_STORAGE_BUFFER, SPOT_LIGHT_INDEX_LIST_SSBO_BINDING_INDEX, m_spot_light_index_list_ssbo);
	glCreateBuffers(1, &m_area_light_index_list_ssbo);
	glBindBufferBase (GL_SHADER_STORAGE_BUFFER, AREA_LIGHT_INDEX_LIST_SSBO_BINDING_INDEX, m_area_light_index_list_ssbo);
	glCreateBuffers(1, &m_point_light_grid_ssbo);
	glBindBufferBase (GL_SHADER_STORAGE_BUFFER, POINT_LIGHT_GRID_SSBO_BINDING_INDEX, m_point_light_grid_ssbo);
	glCreateBuffers(1, &m_spot_light_grid_ssbo);
	glBindBufferBase (GL_SHADER_STORAGE_BUFFER, SPOT_LIGHT_GRID_SSBO_BINDING_INDEX, m_spot_light_grid_ssbo);
	glCreateBuffers(1, &m_area_light_grid_ssbo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, AREA_LIGHT_GRID_SSBO_BINDING_INDEX, m_area_light_grid_ssbo);


    /// Load LTC look-up-tables for area lights rendering
	const auto ltc_lut_path     = FileSystem::getResourcesPath() / "lut";
	const auto ltc_lut_mat_path = ltc_lut_path / "ltc_mat.dds";
	const auto ltc_lut_amp_path = ltc_lut_path / "ltc_amp.dds";

	m_ltc_mat_lut = std::make_shared<RGL::Texture2D>();
    if (m_ltc_mat_lut->LoadDds(ltc_lut_mat_path))
    {
		m_ltc_mat_lut->SetWrapping (TextureWrappingAxis::S, TextureWrappingParam::CLAMP_TO_EDGE);
		m_ltc_mat_lut->SetWrapping (TextureWrappingAxis::T, TextureWrappingParam::CLAMP_TO_EDGE);
		m_ltc_mat_lut->SetFiltering(TextureFiltering::Minify,     TextureFilteringParam::NEAREST);
		m_ltc_mat_lut->SetFiltering(TextureFiltering::Magnify,    TextureFilteringParam::LINEAR);
    }
    else
    {
        fprintf(stderr, "Error: could not load texture %s\n", ltc_lut_mat_path.string().c_str());
    }

	m_ltc_amp_lut = std::make_shared<RGL::Texture2D>();
    if (m_ltc_amp_lut->LoadDds(ltc_lut_amp_path))
    {
		m_ltc_amp_lut->SetWrapping (TextureWrappingAxis::S, TextureWrappingParam::CLAMP_TO_EDGE);
		m_ltc_amp_lut->SetWrapping (TextureWrappingAxis::T, TextureWrappingParam::CLAMP_TO_EDGE);
		m_ltc_amp_lut->SetFiltering(TextureFiltering::Minify,     TextureFilteringParam::NEAREST);
		m_ltc_amp_lut->SetFiltering(TextureFiltering::Magnify,    TextureFilteringParam::LINEAR);
    }
    else
    {
        fprintf(stderr, "Error: could not load texture %s\n", ltc_lut_amp_path.string().c_str());
    }

    /// Create shaders.
	const std::string dir = "src/demos/27_clustered_shading/";

	const auto T0 = steady_clock::now();

    m_depth_prepass_shader = std::make_shared<Shader>(dir + "depth_pass.vert", dir + "depth_pass.frag");
    m_depth_prepass_shader->link();
	assert(*m_depth_prepass_shader);

    m_generate_clusters_shader = std::make_shared<Shader>(dir + "generate_clusters.comp");
    m_generate_clusters_shader->link();
	assert(*m_generate_clusters_shader);

	m_flag_nonempty_clusters_shader = std::make_shared<Shader>(dir + "flag_nonempty_clusters.comp");
	m_flag_nonempty_clusters_shader->link();
	assert(*m_flag_nonempty_clusters_shader);

	m_collect_active_clusters_shader = std::make_shared<Shader>(dir + "collect_active_clusters.comp");
	m_collect_active_clusters_shader->link();
	assert(*m_collect_active_clusters_shader);

    m_update_cull_lights_indirect_args_shader = std::make_shared<Shader>(dir + "update_cull_lights_indirect_args.comp");
    m_update_cull_lights_indirect_args_shader->link();
	assert(*m_update_cull_lights_indirect_args_shader);

    m_cull_lights_shader = std::make_shared<Shader>(dir + "cull_lights.comp");
    m_cull_lights_shader->link();
	assert(*m_cull_lights_shader);

    m_clustered_pbr_shader = std::make_shared<Shader>(dir + "pbr_lighting.vert", dir + "pbr_clustered.frag");
    m_clustered_pbr_shader->link();
	assert(*m_clustered_pbr_shader);

    m_update_lights_shader = std::make_shared<Shader>(dir + "update_lights.comp");
    m_update_lights_shader->link();
	assert(*m_update_lights_shader);

    m_draw_area_lights_geometry_shader = std::make_shared<Shader>(dir + "area_light_geom.vert", dir + "area_light_geom.frag");
    m_draw_area_lights_geometry_shader->link();
	assert(*m_draw_area_lights_geometry_shader);

    m_equirectangular_to_cubemap_shader = std::make_shared<Shader>(dir + "cubemap.vert", dir + "equirectangular_to_cubemap.frag");
    m_equirectangular_to_cubemap_shader->link();
	assert(*m_equirectangular_to_cubemap_shader);

    m_irradiance_convolution_shader = std::make_shared<Shader>(dir + "cubemap.vert", dir + "irradiance_convolution.frag");
    m_irradiance_convolution_shader->link();
	assert(*m_irradiance_convolution_shader);

    m_prefilter_env_map_shader = std::make_shared<Shader>(dir + "cubemap.vert", dir + "prefilter_cubemap.frag");
    m_prefilter_env_map_shader->link();
	assert(*m_prefilter_env_map_shader);

	m_precompute_brdf = std::make_shared<Shader>(dir + "FSQ.vert", dir + "precompute_brdf.frag");
    m_precompute_brdf->link();
	assert(*m_precompute_brdf);

    m_background_shader = std::make_shared<Shader>(dir + "background.vert", dir + "background.frag");
    m_background_shader->link();
	assert(*m_background_shader);


	// Post-processing steps
	m_tmo_pp.create();
	assert(m_tmo_pp);

	m_bloom_pp.create();
	assert(m_bloom_pp);

	m_scattering_pp.create();
	assert(m_scattering_pp);

	m_line_draw_shader = std::make_shared<Shader>(dir + "line_draw.vert", dir + "line_draw.frag");
	m_line_draw_shader->link();
	assert(*m_line_draw_shader);
	m_fsq_shader = std::make_shared<Shader>(dir + "FSQ.vert", dir + "FSQ.frag");
	m_fsq_shader->link();
	assert(*m_fsq_shader);

	const auto T1 = steady_clock::now();
	const auto shader_init_time = duration_cast<microseconds>(T1 - T0);
	std::printf("Shader init time: %.1f ms\n", float(shader_init_time.count())/1000.f);

	_rt.create(size_t(Window::getWidth()), size_t(Window::getHeight()), RGL::RenderTarget::ColorFloat | RGL::RenderTarget::Depth);
	_rt.SetFiltering(RGL::TextureFiltering::Minify, RGL::TextureFilteringParam::LINEAR_MIP_NEAREST);
	_rt.SetWrapping (RGL::TextureWrappingAxis::S, RGL::TextureWrappingParam::CLAMP_TO_EDGE);
	_rt.SetWrapping (RGL::TextureWrappingAxis::T, RGL::TextureWrappingParam::CLAMP_TO_EDGE);

	_pp_half_rt.create(size_t(Window::getWidth()/2), size_t(Window::getHeight()/2), RGL::RenderTarget::ColorFloat);
	_pp_half_rt.SetFiltering(RGL::TextureFiltering::Minify, RGL::TextureFilteringParam::LINEAR_MIP_NEAREST);
	_pp_half_rt.SetWrapping (RGL::TextureWrappingAxis::S, RGL::TextureWrappingParam::CLAMP_TO_EDGE);
	_pp_half_rt.SetWrapping (RGL::TextureWrappingAxis::T, RGL::TextureWrappingParam::CLAMP_TO_EDGE);

	_pp_full_rt.create(size_t(Window::getWidth()), size_t(Window::getHeight()), RGL::RenderTarget::ColorFloat);
	_pp_full_rt.SetFiltering(RGL::TextureFiltering::Minify, RGL::TextureFilteringParam::LINEAR_MIP_NEAREST);
	_pp_full_rt.SetWrapping (RGL::TextureWrappingAxis::S, RGL::TextureWrappingParam::CLAMP_TO_EDGE);
	_pp_full_rt.SetWrapping (RGL::TextureWrappingAxis::T, RGL::TextureWrappingParam::CLAMP_TO_EDGE);

	// TODO: final_rt.cloneFrom(_rt);
	_final_rt.create(size_t(Window::getWidth()), size_t(Window::getHeight()), RGL::RenderTarget::ColorFloat);
	_final_rt.SetFiltering(RGL::TextureFiltering::Minify, RGL::TextureFilteringParam::LINEAR_MIP_NEAREST);
	_final_rt.SetWrapping (RGL::TextureWrappingAxis::S, RGL::TextureWrappingParam::CLAMP_TO_EDGE);
	_final_rt.SetWrapping (RGL::TextureWrappingAxis::T, RGL::TextureWrappingParam::CLAMP_TO_EDGE);

    // IBL precomputations.
    GenSkyboxGeometry();

	m_env_cubemap_rt = std::make_shared<RenderTargetCube>();
    m_env_cubemap_rt->set_position(glm::vec3(0.0));
	m_env_cubemap_rt->create(2048, 2048, true);

	m_irradiance_cubemap_rt = std::make_shared<RenderTargetCube>();
    m_irradiance_cubemap_rt->set_position(glm::vec3(0.0));
	m_irradiance_cubemap_rt->create(32, 32);

	m_prefiltered_env_map_rt = std::make_shared<RenderTargetCube>();
    m_prefiltered_env_map_rt->set_position(glm::vec3(0.0));
	m_prefiltered_env_map_rt->create(512, 512, true);

	m_brdf_lut_rt = std::make_shared<RGL::RenderTarget::Texture2d>();
    m_brdf_lut_rt->create(512, 512, GL_RG16F);

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

		const glm::uvec2 screen_size { Window::getWidth(), Window::getHeight() };
		glm::uvec2 screen_pos { 0, 0 };//Window::getWidth()/2 + 1, Window::getHeight()/2 };
		glm::vec2 coord = {
			float(screen_pos.x) / float(screen_size.x),
			float(screen_pos.y) / float(screen_size.y)
		};
		coord = coord*2.f - 1.f; // [ -1, 1 ]


		auto target = u_inv_projection * glm::vec4(coord.x, coord.y, 1, 1);
		auto direction = glm::vec3(u_inv_view * glm::vec4(glm::normalize(glm::vec3(target) / target.w), 0)); // World space

		std::printf("        target: %.5f; %.5f; %.5f; %.5f\n", target.x, target.y, target.z, target.w);
		const auto far_depth = target.z / target.w;
		target = glm::normalize(target);
		std::printf("   norm.target: %.5f; %.5f; %.5f   (max depth: %.1f)\n", target.x, target.y, target.z, far_depth);
		std::printf("     direction: %.5f; %.5f; %.5f\n", direction.x, direction.y, direction.z);

		// std::printf("   u_view: %s\n", glm::to_string(u_view).c_str());

		const glm::vec3 light_pos { -10, 2.f, 0 };
		std::printf("  camera[ws]: %.5f; %.5f; %.5f\n", u_cam_pos.x, u_cam_pos.y, u_cam_pos.z);
		std::printf("   light[ws]: %.5f; %.5f; %.5f\n", light_pos.x, light_pos.y, light_pos.z);
		auto light_pos_cs = glm::vec3(u_view * glm::vec4(light_pos, 1));
		std::printf("   light[cs]: %.5f; %.5f; %.5f\n", light_pos_cs.x, light_pos_cs.y, light_pos_cs.z);


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

		std::printf("        X = %.3f; %.3f; %.3f\n", space_x.x, space_x.y, space_x.z);
		std::printf("        Y = %.3f; %.3f; %.3f\n", space_y.x, space_y.y, space_y.z);
		std::printf("        Z = %.3f; %.3f; %.3f\n", space_z.x, space_z.y, space_z.z);

		glm::vec3 ray_direction = glm::normalize(glm::vec3(1, 0, 0));

		auto cone_ray = cone_space * glm::vec4(ray_direction, 0);

		std::printf(" cone ray = %.3f; %.3f; %.3f\n", cone_ray.x, cone_ray.y, cone_ray.z);
		std::exit(EXIT_SUCCESS);
	}

	if(false)
	{
		const auto space = make_common_space_from_direction({ 0, 0, -1 });
		std::printf("        X = %.3f; %.3f; %.3f\n", space[0].x, space[0].y, space[0].z);
		std::printf("        Y = %.3f; %.3f; %.3f\n", space[1].x, space[1].y, space[1].z);
		std::printf("        Z = %.3f; %.3f; %.3f\n", space[2].x, space[2].y, space[2].z);
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

		std::printf("cone center : %.1f; %.1f; %.1f\n", cone.center.x, cone.center.y, cone.center.z);
		std::printf("cone axis   : %.1f; %.1f; %.1f\n", cone.axis.x, cone.axis.y, cone.axis.z);
		std::printf("cone angle  : %.1f\n", glm::degrees(cone.angle));
		std::printf("ray start   : %.1f; %.1f; %.1f\n", ray_start.x, ray_start.y, ray_start.z);
		std::printf("ray dir     : %.1f; %.1f; %.1f\n", ray_dir.x, ray_dir.y, ray_dir.z);

		glm::vec3 center_to_ray = ray_start - cone.center; // aka CO
		float distance_sq = glm::dot(center_to_ray, center_to_ray);

		float cos_theta = std::cos(cone.angle);
		float cos_theta_sq = cos_theta*cos_theta;
		float dir_axis_dot = glm::dot(ray_dir, cone.axis);
		float CO_axis_dot = glm::dot(center_to_ray, cone.axis);

		float A = dir_axis_dot*dir_axis_dot - cos_theta_sq;
		float B = 2 * (dir_axis_dot*CO_axis_dot - glm::dot(ray_dir, center_to_ray)*cos_theta_sq);
		float C = CO_axis_dot*CO_axis_dot - distance_sq*cos_theta_sq;

		std::printf("    A = %.3f\n", A);
		std::printf("    B = %.3f\n", B);
		std::printf("    C = %.3f\n", C);

		float discriminant = B*B - 4*A*C;
		if(discriminant < 0)
			std::printf("no intersection\n");
		else
		{
			std::printf("discriminant = %.3f\n", discriminant);
			float sqrt_discriminant = std::sqrt(discriminant);
			float t1 = (-B - sqrt_discriminant) / (2*A);
			float t2 = (-B + sqrt_discriminant) / (2*A);

			auto ray_point = [&ray_start, &ray_dir](float t) {
				return ray_start + ray_dir*t;
			};
			auto p1 = ray_point(t1);
			std::printf("  t1 = %.3f  ->  %.2f; %.2f; %.2f\n", t1, p1.x, p1.y, p1.z);
			auto p2 = ray_point(t2);
			std::printf("  t2 = %.3f  ->  %.2f; %.2f; %.2f\n", t2, p2.x, p2.y, p2.z);
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

		std::printf("cone center  : %.1f; %.1f; %.1f\n", cone.center.x, cone.center.y, cone.center.z);
		std::printf("cone axis    : %.1f; %.1f; %.1f\n", cone.axis.x, cone.axis.y, cone.axis.z);
		std::printf("cone angle   : %.1f   radius: %.1f\n", glm::degrees(cone.angle), cone.radius);
		std::printf("ray start    : %.1f; %.1f; %.1f\n", ray_start.x, ray_start.y, ray_start.z);
		std::printf("ray dir      : %.1f; %.1f; %.1f\n", ray_dir.x, ray_dir.y, ray_dir.z);
		const auto ray_end = ray_point(50);
		std::printf("ray end @ 50 : %.1f; %.1f; %.1f\n", ray_end.x, ray_end.y, ray_end.z);

		glm::vec3 center_to_ray = ray_start - cone.center; // aka CO

		float A = 1;
		float B = 2 * glm::dot(center_to_ray, ray_dir);
		float C = glm::dot(center_to_ray, center_to_ray) - cone.radius*cone.radius;

		std::printf("    A = %.3f\n", A);
		std::printf("    B = %.3f\n", B);
		std::printf("    C = %.3f\n", C);

		float discriminant = B*B - 4*A*C;
		if(discriminant < 0)
			std::printf("NO INTERSECTION\n");
		else
		{
			std::printf("discriminant = %.3f\n", discriminant);
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
					std::printf("  t1 = %.3f  ->  %.2f; %.2f; %.2f\n", t1, p1.x, p1.y, p1.z);
					got_point = true;
				}
			}
			if(t2 >= 0)
			{
				auto p2 = ray_point(t2);
				if(point_inside_cone(p2))
				{
					std::printf("  t2 = %.3f  ->  %.2f; %.2f; %.2f\n", t2, p2.x, p2.y, p2.z);
					got_point = true;
				}
			}
			if(got_point)
				std::printf("INTERSECTION\n");
			else
				std::printf("NO INTERSECTION\n");
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

		std::printf("depth pos  : %.1f; %.1f; %.1f\n", pos.x, pos.y, pos.z);

		std::printf("world  pos : %.5f; %.5f; %.5f\n", wpos.x, wpos.y, wpos.z);

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


		std::printf("spot  : %.1f; %.1f; %.1f  %.0f° - %.0f°\n", spot_pos.x, spot_pos.y, spot_pos.z, glm::degrees(inner_angle), glm::degrees(outer_angle));

		for(float x = 0.f; x <= 8.f; x += 0.2f)
		{
			point.x = x;
			auto to_point = glm::normalize(point - spot_pos);
			float att = spot_angle_att(to_point, spot_dir, outer_angle, inner_angle);
			std::printf("point : %.1f; %.1f; %.1f  --> %f\n", point.x, point.y, point.z, att);
		}

		std::exit(EXIT_SUCCESS);
	}
}

void ClusteredShading::calculateShadingClusterGrid()
{
	const auto cluster_count_before = m_clusters_count;

	// TODO: these should be properties of the camera  (a component!)

	/// Init clustered shading variables.
	m_cluster_grid_dim.x = uint32_t(glm::ceil(float(Window::getWidth())  / float(m_cluster_grid_block_size)));
	m_cluster_grid_dim.y = uint32_t(glm::ceil(float(Window::getHeight()) / float(m_cluster_grid_block_size)));

	// The depth of the cluster grid during clustered rendering is dependent on the
	// number of clusters subdivisions in the screen Y direction.
	// Source: Clustered Deferred and Forward Shading (2012) (Ola Olsson, Markus Billeter, Ulf Assarsson).
	const float half_fov  = glm::radians(m_camera.verticalFov() * 0.5f);
	const float sD        = 2.0f * glm::tan(half_fov) / float(m_cluster_grid_dim.y);
	m_near_k              = 1.0f + sD;
	m_log_grid_dim_y      = 1.0f / glm::log(m_near_k);

	const float z_near    = m_camera.nearPlane();
	const float z_far     = m_camera.farPlane();
	const float log_depth = glm::log(z_far / z_near);
	m_cluster_grid_dim.z  = uint32_t(glm::floor(log_depth * m_log_grid_dim_y));

	const auto cluster_count = m_cluster_grid_dim.x * m_cluster_grid_dim.y * m_cluster_grid_dim.z;

	if(cluster_count != cluster_count_before)
	{
		m_clusters_count = cluster_count;
		std::printf("shading clusters: %d   (%d x %d x %d)\n", m_clusters_count, m_cluster_grid_dim.x, m_cluster_grid_dim.y, m_cluster_grid_dim.z);
		prepareClusterBuffers();
	}
}

void ClusteredShading::	prepareClusterBuffers()
{
	glNamedBufferData(m_clusterShading.ssbo.aabb, sizeof(ClusterAABB) * m_clusters_count, nullptr, GL_STATIC_READ);
	glNamedBufferData(m_nonempty_clusters_ssbo, sizeof(uint32_t) * m_clusters_count, nullptr, GL_STATIC_READ);
	glNamedBufferData(m_active_clusters_ssbo, sizeof(uint32_t) * m_clusters_count + sizeof(uint32_t), nullptr, GL_STATIC_READ);
	glNamedBufferData(m_cull_lights_dispatch_args_ssbo, sizeof(uint32_t) * 3, nullptr, GL_STATIC_DRAW);
	glNamedBufferData(m_point_light_index_list_ssbo, sizeof(uint32_t) * m_clusters_count * AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER, nullptr, GL_DYNAMIC_DRAW);
	glNamedBufferData(m_spot_light_index_list_ssbo, sizeof(uint32_t) * m_clusters_count * AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER, nullptr, GL_DYNAMIC_DRAW);
	glNamedBufferData(m_area_light_index_list_ssbo, sizeof(uint32_t) * m_clusters_count * AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER, nullptr, GL_DYNAMIC_DRAW);

	// Every tile takes LightGrid struct that has two unsigned ints one to represent the number of lights in that grid
	// Another to represent the offset to the light index list from where to begin reading light indexes from
	// In this SSBO, atomic counter is also being stored (uint global_index_count)
	// This implementation is straight up from Olsson paper

	glNamedBufferData(m_point_light_grid_ssbo, sizeof(uint32_t) + sizeof(LightGrid) * m_clusters_count, nullptr, GL_DYNAMIC_DRAW);
	glNamedBufferData(m_spot_light_grid_ssbo, sizeof(uint32_t) + sizeof(LightGrid) * m_clusters_count, nullptr, GL_DYNAMIC_DRAW);
	glNamedBufferData(m_area_light_grid_ssbo, sizeof(uint32_t) + sizeof(LightGrid) * m_clusters_count, nullptr, GL_DYNAMIC_DRAW);


	// auto proj = m_camera.m_projection;
	// auto inv_proj = glm::inverse(proj);

	/// Generate clusters' AABBs
	// This can be done once as long as the camera parameters doesn't change (projection matrix related variables)
	m_generate_clusters_shader->bind();
	m_generate_clusters_shader->setUniform("u_grid_dim"sv,           m_cluster_grid_dim);
	m_generate_clusters_shader->setUniform("u_cluster_size_ss"sv,    glm::uvec2(m_cluster_grid_block_size));
	m_generate_clusters_shader->setUniform("u_near_k"sv,             m_near_k);
	m_generate_clusters_shader->setUniform("u_near_z"sv,             m_camera.nearPlane());
	m_generate_clusters_shader->setUniform("u_inverse_projection"sv, glm::inverse(m_camera.projectionTransform()));
	m_generate_clusters_shader->setUniform("u_pixel_size"sv,         1.0f / glm::vec2(RGL::Window::getWidth(), RGL::Window::getHeight()));
	glDispatchCompute(GLuint(glm::ceil(float(m_clusters_count) / 1024.0f)), 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClusteredShading::input()
{
    /* Close the application when Esc is released. */
	if (Input::getKeyDown(KeyCode::Escape))
    {
        stop();
    }

	if(Input::getKey(KeyCode::RightArrow))
		s_spot_outer_angle = glm::clamp(s_spot_outer_angle + 0.3f, 0.f, 90.f);
	else if(Input::getKey(KeyCode::LeftArrow))
		s_spot_outer_angle = glm::clamp(s_spot_outer_angle - 0.3f, 0.f, 90.f);

	if(Input::getKey(KeyCode::UpArrow))
		s_spot_inner_angle = glm::clamp(s_spot_inner_angle + 0.3f, 0.f, 90.f);
	else if(Input::getKey(KeyCode::DownArrow))
		s_spot_inner_angle = glm::clamp(s_spot_inner_angle - 0.3f, 0.f, 90.f);

	if(s_spot_inner_angle > s_spot_outer_angle)
		s_spot_inner_angle = s_spot_outer_angle;

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
	if (Input::getKeyUp(KeyCode::F12))
    {
		/* Specify filename of Sthe screenshot. */
        std::string filename = "27_clustered_shading";
		if (take_screenshot_png(filename, size_t(Window::getWidth() / 2), size_t(Window::getHeight() / 2)))
        {
            /* If specified folders in the path are not already created, they'll be created automagically. */
			std::cout << "Saved " << filename << ".png to " << (FileSystem::rootPath() / "screenshots/") << std::endl;
        }
        else
        {
			std::cerr << "Could not save " << filename << ".png to " << (FileSystem::rootPath() / "screenshots/") << std::endl;
        }
    }

    if (Input::getKeyUp(KeyCode::Space))
        m_animate_lights = !m_animate_lights;
}

void ClusteredShading::update(double delta_time)
{
    /* Update variables here. */
	m_camera.update(delta_time);

	// static float     rotation_speed = 1;
	// static float     time_accum     = 0;

	m_spot_lights[0].outer_angle = glm::radians(s_spot_outer_angle);
	m_spot_lights[0].inner_angle = glm::radians(s_spot_inner_angle);

    if (m_animate_lights)
    {
		// time_accum  += float(delta_time * m_animation_speed);
		auto spin_mat  = glm::rotate(glm::mat4(1), glm::radians(60.f * float(delta_time)) * 2.f * m_animation_speed, AXIS_Y);
		auto orbit_mat = glm::rotate(glm::mat4(1), glm::radians(-23.f * float(delta_time)) * 2.f * m_animation_speed, AXIS_Y);

		for(auto &spot: m_spot_lights)
		{
			// spin on its own axis
			spot.direction = spin_mat * glm::vec4(spot.direction, 0);
			// orbit aounr the orgin
			spot.point.position = orbit_mat * glm::vec4(spot.point.position, 1);
		}

		for(auto &point: m_point_lights)
		{
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

		UpdateLightsSSBOs();
	}
	UpdateLightsSSBOs();

}

void ClusteredShading::GenerateAreaLights()
{
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
}

void ClusteredShading::GeneratePointLights()
{
    m_point_lights.clear();

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

	// m_point_lights.push_back({
	// 	.base = {
	// 		.color = { 1.0f, 0.1f, 0.05f },
	// 		.intensity = 100
	// 	},
	// 	.position = { 0, 2.f, 10 },
	// 	.radius = 10,
	// });
	return;

	m_point_lights.resize(m_point_lights_count);

	m_point_lights_orbit.clear();
	m_point_lights_orbit.resize(m_point_lights_count);

    for(uint32_t i = 0; i < m_point_lights.size(); ++i)
    {
        auto& p = m_point_lights[i];

        float rand_x = glm::linearRand(min_lights_bounds.x, max_lights_bounds.x);
        float rand_z = glm::linearRand(min_lights_bounds.z, max_lights_bounds.z);

        p.base.color     = hsv2rgb(glm::linearRand(1.0f, 360.0f), glm::linearRand(0.1f, 1.0f), glm::linearRand(0.1f, 1.0f));
        p.base.intensity = m_point_lights_intensity;
        p.position.y     = glm::linearRand(min_lights_bounds.y, max_lights_bounds.y);
        p.radius         = glm::linearRand(min_max_point_light_radius.x, min_max_point_light_radius.y);

		auto& e = m_point_lights_orbit[i];
		e       = glm::vec4(rand_x, rand_z, glm::linearRand(0.5f, 2.0f), 0.0f); // [x, y, z] => [ellipse a radius, ellipse b radius, light move speed]

        p.position.x = e.x * glm::cos(1.618f * e.z);
        p.position.z = e.y * glm::sin(1.618f * e.z);
    }
}

void ClusteredShading::GenerateSpotLights()
{
    m_spot_lights.clear();

	m_spot_lights.push_back({
		.point = {
			.base = {
				.color = { 1.0f, 0.8f, 0.2f },
				.intensity = 5000
			},
			.position = { 0, 3.f, -8 },
			.radius = 35
		},
		.direction = { 0, 0, 1 },
		.inner_angle = glm::radians(s_spot_inner_angle),//5.f),
		.outer_angle = glm::radians(30.f)
	});
	// m_spot_lights.push_back({
	// 	.point = {
	// 		.base = {
	// 			.color = { 1.0f, 0.2f, 0.2f },
	// 			.intensity = 5000
	// 		},
	// 		.position = { 4, 3.f, -8 },
	// 		.radius = 35
	// 	},
	// 	.direction = { 0, 0, 1 },
	// 	.inner_angle = glm::radians(s_spot_inner_angle),//5.f),
	// 	.outer_angle = glm::radians(30.f)
	// });
	// m_spot_lights.push_back({
	// 	.point = {
	// 		.base = {
	// 			.color = { 1.0f, 0.2f, 0.6f },
	// 			.intensity = 5000
	// 		},
	// 		.position = { 8, 3.f, -8 },
	// 		.radius = 35
	// 	},
	// 	.direction = { 0, 0, 1 },
	// 	.inner_angle = glm::radians(s_spot_inner_angle),//5.f),
	// 	.outer_angle = glm::radians(30.f)
	// });
	// m_spot_lights.push_back({
	// 	.point = {
	// 		.base = {
	// 			.color = { 0.2f, 1.0f, 0.2f },
	// 			.intensity = 5000
	// 		},
	// 		.position = { -4, 3.f, -8 },
	// 		.radius = 35
	// 	},
	// 	.direction = { 0, 0, 1 },
	// 	.inner_angle = glm::radians(s_spot_inner_angle),//5.f),
	// 	.outer_angle = glm::radians(30.f)
	// });
	// m_spot_lights.push_back({
	// 	.point = {
	// 		.base = {
	// 			.color = { 0.2f, 0.4f, 1.0f },
	// 			.intensity = 5000
	// 		},
	// 		.position = { -8, 3.f, -8 },
	// 		.radius = 35
	// 	},
	// 	.direction = { 0, 0, 1 },
	// 	.inner_angle = glm::radians(s_spot_inner_angle),//5.f),
	// 	.outer_angle = glm::radians(30.f)
	// });
	return;

	m_spot_lights.resize(m_spot_lights_count);

	m_spot_lights_orbit.clear();
	m_spot_lights_orbit.resize(m_spot_lights_count);

    for(uint32_t i = 0; i < m_spot_lights.size(); ++i)
    {
        auto& p = m_spot_lights[i];
		auto& e = m_spot_lights_orbit[i];

        float rand_x = glm::linearRand(min_lights_bounds.x, max_lights_bounds.x);
        float rand_z = glm::linearRand(min_lights_bounds.z, max_lights_bounds.z);

        setLightDirection(p.direction, glm::linearRand(0.0f, 360.0f), glm::linearRand(0.0f, 70.0f));
        p.inner_angle          = glm::radians(min_max_spot_angles.x);
        p.outer_angle          = glm::radians(min_max_spot_angles.y);
        p.point.base.color     = hsv2rgb(glm::linearRand(1.0f, 360.0f), glm::linearRand(0.1f, 1.0f), glm::linearRand(0.1f, 1.0f));
        p.point.base.intensity = m_spot_lights_intensity;
        p.point.position.y     = glm::linearRand(min_lights_bounds.y, max_lights_bounds.y);
        p.point.radius         = glm::linearRand(min_max_spot_light_radius.x, min_max_spot_light_radius.y);
        e                      = glm::vec4(rand_x, rand_z, glm::linearRand(0.5f, 2.0f), 0.0f); // [x, y, z] => [ellipse a radius, ellipse b radius, light move speed]

        p.point.position.x = e.x * glm::cos(1.618f * e.z);
        p.point.position.z = e.y * glm::sin(1.618f * e.z);
    }
}

void ClusteredShading::UpdateLightsSSBOs()
{
	m_directional_lights_ssbo.set(m_directional_lights);
	m_point_lights_ssbo.set(m_point_lights);
	m_spot_lights_ssbo.set(m_spot_lights);
	m_area_lights_ssbo.set(m_area_lights);
	m_point_lights_orbit_ssbo.set(m_point_lights_orbit);
	m_spot_lights_orbit_ssbo.set(m_spot_lights_orbit);
}

void ClusteredShading::HdrEquirectangularToCubemap(const std::shared_ptr<RenderTargetCube>& cubemap_rt, const std::shared_ptr<RGL::Texture2D>& m_equirectangular_map)
{
    /* Update all faces per frame */
    m_equirectangular_to_cubemap_shader->bind();
	m_equirectangular_to_cubemap_shader->setUniform("u_projection"sv, cubemap_rt->projection());

	cubemap_rt->bindRenderTarget();
	// glViewport(0, 0, GLsizei(cubemap_rt->m_width), GLsizei(cubemap_rt->m_height));
 //    glBindFramebuffer(GL_FRAMEBUFFER, cubemap_rt->m_fbo_id);
    m_equirectangular_map->Bind(1);

    glBindVertexArray(m_skybox_vao);
    for (uint8_t side = 0; side < 6; ++side)
    {
		m_equirectangular_to_cubemap_shader->setUniform("u_view"sv, cubemap_rt->view_transform(side));
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + side, cubemap_rt->texture_id(), 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glViewport(0, 0, Window::getWidth(), Window::getHeight());
}

void ClusteredShading::IrradianceConvolution(const std::shared_ptr<RenderTargetCube>& cubemap_rt)
{
    /* Update all faces per frame */
    m_irradiance_convolution_shader->bind();
	m_irradiance_convolution_shader->setUniform("u_projection"sv, cubemap_rt->projection());

	cubemap_rt->bindRenderTarget();
	// glViewport(0, 0, GLsizei(cubemap_rt->m_width), GLsizei(cubemap_rt->m_height));
 //    glBindFramebuffer(GL_FRAMEBUFFER, cubemap_rt->m_fbo_id);
    m_env_cubemap_rt->bindTexture(1);

    for (uint8_t side = 0; side < 6; ++side)
    {
		m_irradiance_convolution_shader->setUniform("u_view"sv, cubemap_rt->view_transform(side));
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + side, cubemap_rt->texture_id(), 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glBindVertexArray(m_skybox_vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glViewport(0, 0, Window::getWidth(), Window::getHeight());
}

void ClusteredShading::PrefilterCubemap(const std::shared_ptr<RenderTargetCube>& cubemap_rt)
{
    m_prefilter_env_map_shader->bind();
	m_prefilter_env_map_shader->setUniform("u_projection"sv, cubemap_rt->projection());

    m_env_cubemap_rt->bindTexture(1);

	cubemap_rt->bindRenderTarget();
	// glBindFramebuffer(GL_FRAMEBUFFER, cubemap_rt->m_fbo_id);

	auto max_mip_levels = uint8_t(glm::log2(float(cubemap_rt->width())));
    for (uint8_t mip = 0; mip < max_mip_levels; ++mip)
    {
        // resize the framebuffer according to mip-level size.
		auto mip_width  = uint32_t(cubemap_rt->width()  * std::pow(0.5, mip));
		auto mip_height = uint32_t(cubemap_rt->height() * std::pow(0.5, mip));

		cubemap_rt->bindRenderBuffer();
        //glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mip_width, mip_height);
		glViewport(0, 0, GLsizei(mip_width), GLsizei(mip_height));

        float roughness = float(mip) / float(max_mip_levels - 1);
		m_prefilter_env_map_shader->setUniform("u_roughness"sv, roughness);

        for (uint8_t side = 0; side < 6; ++side)
        {
			m_prefilter_env_map_shader->setUniform("u_view"sv, cubemap_rt->view_transform(side));
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + side, cubemap_rt->texture_id(), mip);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBindVertexArray(m_skybox_vao);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }
	bindScreenRenderTarget();
}

void ClusteredShading::PrecomputeIndirectLight(const std::filesystem::path& hdri_map_filepath)
{
	auto envmap_hdr = std::make_shared<RGL::Texture2D>();
    envmap_hdr->LoadHdr(hdri_map_filepath);

    HdrEquirectangularToCubemap(m_env_cubemap_rt, envmap_hdr);

	glBindTexture(GL_TEXTURE_CUBE_MAP, m_env_cubemap_rt->texture_id());
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    IrradianceConvolution(m_irradiance_cubemap_rt);
    PrefilterCubemap(m_prefiltered_env_map_rt);
}

void ClusteredShading::PrecomputeBRDF(const std::shared_ptr<RGL::RenderTarget::Texture2d>& rt)
{
    rt->bindRenderTarget();
    m_precompute_brdf->bind();

	glBindVertexArray(_dummy_vao_id);
    glDrawArrays(GL_TRIANGLES, 0, 3);

	bindScreenRenderTarget();
}

void ClusteredShading::bindScreenRenderTarget()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, Window::getWidth(), Window::getHeight());
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



	// TODO: render shadow-maps (if lights/meshes changed)
	//   need to limit to only "a few" lights, basically strongest lights (as perceived by the camera)


	_gl_timer.start();


	// 1. Depth pre-pass  (only if camera/meshes moved, probably always)
	renderDepth(m_camera, m_depth_pass_rt);


	// 2. Blit depth info to our main render target
	m_depth_pass_rt.copyTo(_rt, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

	m_depth_time = _gl_timer.elapsed<microseconds>();
	_gl_timer.start();


	// TODO: generate SSAO (texture)
	//   use as input to pbr lighting calculations


	static const uint32_t clear_val = 0;

	// 3. Find used/visible clusters
	//   (clusters containing rendered fragments, those are the only ones we need to care about)
	glClearNamedBufferData(m_nonempty_clusters_ssbo, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);

	m_flag_nonempty_clusters_shader->bind();
	m_flag_nonempty_clusters_shader->setUniform("u_near_z"sv,          m_camera.nearPlane());
	m_flag_nonempty_clusters_shader->setUniform("u_far_z"sv,           m_camera.farPlane());
	m_flag_nonempty_clusters_shader->setUniform("u_log_grid_dim_y"sv,  m_log_grid_dim_y);
	m_flag_nonempty_clusters_shader->setUniform("u_cluster_size_ss"sv, glm::uvec2(m_cluster_grid_block_size));
	m_flag_nonempty_clusters_shader->setUniform("u_grid_dim"sv,        m_cluster_grid_dim);

	m_depth_pass_rt.bindTextureSampler();
	glDispatchCompute(GLuint(glm::ceil(float(m_depth_pass_rt.width()) / 32.f)),
					  GLuint(glm::ceil(float(m_depth_pass_rt.height()) / 32.f)),
					  1);
    glMemoryBarrier  (GL_SHADER_STORAGE_BARRIER_BIT);

	m_cluster_time1 = _gl_timer.elapsed<microseconds>();
	_gl_timer.start();


	// 4. Find active clusters and update the indirect dispatch arguments buffer
	glClearNamedBufferData(m_active_clusters_ssbo, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);

	m_collect_active_clusters_shader->bind();
	glDispatchCompute(GLuint(glm::ceil(float(m_clusters_count) / 1024.0f)), 1, 1);
    glMemoryBarrier  (GL_SHADER_STORAGE_BARRIER_BIT);

    m_update_cull_lights_indirect_args_shader->bind();
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier  (GL_SHADER_STORAGE_BARRIER_BIT);

	m_cluster_time2 = _gl_timer.elapsed<microseconds>();
	_gl_timer.start();


	// 5. Assign lights to clusters (cull lights)
	glClearNamedBufferData(m_point_light_grid_ssbo,       GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
	glClearNamedBufferData(m_point_light_index_list_ssbo, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
	glClearNamedBufferData(m_spot_light_grid_ssbo,        GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
	glClearNamedBufferData(m_spot_light_index_list_ssbo,  GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
	glClearNamedBufferData(m_area_light_grid_ssbo,        GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
	glClearNamedBufferData(m_area_light_index_list_ssbo,  GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);

    m_cull_lights_shader->bind();
	m_cull_lights_shader->setUniform("u_view_matrix"sv, m_camera.viewTransform());

    glBindBuffer             (GL_DISPATCH_INDIRECT_BUFFER, m_cull_lights_dispatch_args_ssbo);
    glDispatchComputeIndirect(0);
    glMemoryBarrier          (GL_SHADER_STORAGE_BARRIER_BIT);

	m_cluster_time3 = _gl_timer.elapsed<microseconds>();
	_gl_timer.start();


    // 6. Render lighting
	_rt.bindRenderTarget(RGL::RenderTarget::ColorBuffer);
	renderLighting(m_camera);


    // 7. Render area lights geometry
	if(m_area_lights_geometry)
	{
		m_draw_area_lights_geometry_shader->bind();
		m_draw_area_lights_geometry_shader->setUniform("u_view_projection"sv, m_camera.projectionTransform() * m_camera.viewTransform());
		glDrawArrays(GL_TRIANGLES, 0, GLsizei(6 * m_area_lights.size()));
	}

	m_lighting_time = _gl_timer.elapsed<microseconds>();
	_gl_timer.start();


    // 8. Render skybox
    m_background_shader->bind();
	m_background_shader->setUniform("u_projection"sv, m_camera.projectionTransform());
	m_background_shader->setUniform("u_view"sv,       glm::mat4(glm::mat3(m_camera.viewTransform())));
	m_background_shader->setUniform("u_lod_level"sv,  m_background_lod_level);
    m_env_cubemap_rt->bindTexture();

    glBindVertexArray(m_skybox_vao);
    glDrawArrays     (GL_TRIANGLES, 0, 36);

	m_skybox_time = _gl_timer.elapsed<microseconds>();
	_gl_timer.start();

	// TODO: Render atmospheric/fog light scattering (i.e. volumetric lights)
	//    - lights culled into screen tiles (i.e. only 2d)
	//    - (optional) grid/voxel-based atmospheric density; otherwise, just use a constant
	m_scattering_pp.shader().setUniform("u_cam_pos"sv,         m_camera.position());
	m_scattering_pp.shader().setUniform("u_near_z"sv,          m_camera.nearPlane());
	m_scattering_pp.shader().setUniform("u_far_z"sv,           m_camera.farPlane());
	m_scattering_pp.shader().setUniform("u_projection"sv,      m_camera.projectionTransform()); // not used ?
	m_scattering_pp.shader().setUniform("u_view"sv,            m_camera.viewTransform());       // not used ?
	m_scattering_pp.shader().setUniform("u_inv_projection"sv,  glm::inverse(m_camera.projectionTransform()));
	m_scattering_pp.shader().setUniform("u_inv_view"sv,        glm::inverse(m_camera.viewTransform()));
	// TODO: m_camera.setUniforms(m_scattering_pp.shader());  // could even cache the uniform locations (per shader)
	m_scattering_pp.shader().setUniform("u_grid_dim"sv,        m_cluster_grid_dim);
	m_scattering_pp.shader().setUniform("u_cluster_size_ss"sv, glm::uvec2(m_cluster_grid_block_size));
	// m_scattering_pp.shader().setUniform("u_log_grid_dim_y"sv,  m_log_grid_dim_y);
	m_scattering_pp.shader().setUniform("u_fog_color"sv,       glm::vec3(1, 1, 1));
	m_scattering_pp.shader().setUniform("u_fog_density"sv,     m_fog_density);
	m_scattering_pp.shader().setUniform("u_fog_falloff_blend"sv, m_fog_falloff_blend);
	m_scattering_pp.shader().setUniform("u_ray_march_stride"sv, m_ray_march_stride);

	m_depth_pass_rt.bindTextureSampler(2);
	// TODO: bind 2d noise texture (for ray randomization)

	m_scattering_pp.render(_rt, _pp_half_rt);

	// TODO: render '_pp_half_rt' additive on to '_rt' (blurred)
	// m_blur_pp.setSigma(1.5f);
	// m_blur_pp.setRadius(2.f);
	// m_blur_pp.setWeights({ 0.2f, 0.3f, 0.4f, ... }));
	// m_blur_pp.render(_pp_half_rt, _full_rt);

	// TODO: render _pp__full_rt onto _rt in additive mode
	draw2d(_pp_half_rt, _rt, BlendMode::Add);


	m_scatter_time = _gl_timer.elapsed<microseconds>();


	// TODO: compute average luminance of rendered image
	//   and gradually adjust exposure over time (see tone mapping, below)
	// m_detect_brightness_shader.bind();
	// glDispatchCompute(GLuint(glm::ceil(float(_rt.width()) / 8.f)),
	// 				  GLuint(glm::ceil(float(_rt.height()) / 8.f)),
	// 				  1);
	// glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	// float brightness = m_detect_brightness_shader.detectedBrightness();
	// TODO: compute new desired exposure, blend 'm_eposure' over time towards that value


	// 9. Bloom
    if (m_bloom_enabled)
    {
		m_bloom_pp.setThreshold(m_bloom_threshold);
		m_bloom_pp.setIntensity(m_bloom_intensity);
		m_bloom_pp.setKnee(m_bloom_knee);
		m_bloom_pp.setDirtIntensity(m_bloom_dirt_intensity);

		m_bloom_pp.render(_rt, _rt);
	}

    // 10. Apply tone mapping
	// TODO: continuously adjust 'm_exposure' depending on how bright the image is

	m_tmo_pp.setExposure(m_exposure);
	m_tmo_pp.setGamma(m_gamma);
	m_tmo_pp.render(_rt, _final_rt);


	// draw the final result on the screen
	draw2d(_final_rt, BlendMode::Replace);


	if(m_draw_aabb)
		renderSceneBounds();
}

void ClusteredShading::renderSceneBounds()
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


	for(const auto &spot: m_spot_lights)
	{
		const auto color = glm::vec4(glm::normalize(spot.point.base.color), 1);
		debugDrawSpotLight(spot, color);
	}

	// for(const auto &point: m_point_lights)
	// {
	// 	const auto color = glm::vec4(glm::normalize(point.base.color), 1);
	// 	debugDrawSphere(point.position, point.radius, color);
	// }
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

void ClusteredShading::draw2d(const Texture &texture, BlendMode blend)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if(blend != BlendMode::Replace)
		glEnable(GL_BLEND);

	switch(blend)
	{
	case BlendMode::Replace: glDisable(GL_BLEND); break;
	case BlendMode::Add:     glBlendFunc(GL_ONE, GL_ONE); break;
	case BlendMode::Alpha:   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
	}

	m_fsq_shader->bind();
	texture.Bind();

	glBindVertexArray(_dummy_vao_id);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

void ClusteredShading::draw2d(const RGL::Texture &texture, RGL::Texture &target, BlendMode blend)
{
	target.Bind();

	if(blend != BlendMode::Replace)
		glEnable(GL_BLEND);

	switch(blend)
	{
	case BlendMode::Replace: glDisable(GL_BLEND); break;
	case BlendMode::Add:     glBlendFunc(GL_ONE, GL_ONE); break;
	case BlendMode::Alpha:   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
	}

	m_fsq_shader->bind();
	texture.Bind();

	glBindVertexArray(_dummy_vao_id);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

void ClusteredShading::draw2d(const RGL::Texture &texture, const glm::uvec2 &top_left, const glm::uvec2 &bottom_right)
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

		// std::printf("distance to plane");
		// static const char *plane_name[] = { "L", "R", "T", "B", "Fr", "Bk" };
		// for(auto idx = 0u; idx < 6; ++idx)
		// 	std::printf("  %s: %.3f", plane_name[idx], result.distance_to_plane[idx]);
		// std::printf("\n");

		if(result.visible)
			_scenePvs.push_back(obj);
		else
		{
			// TODO: visualize result based on result.culled_by_plane, etc.
			// if(result.culled_by_aabb)
			// 	std::printf("culled by AABB\n");
			// else if(result.culled_by_plane >= 0)
			// 	std::printf("culled by plane: %d\n", result.culled_by_plane);
			// else
			// 	std::printf("culled by corner\n");
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


	m_cull_time = duration_cast<microseconds>(steady_clock::now() - T0);

	return _scenePvs;
}

void ClusteredShading::renderScene(const Camera &camera, RGL::Shader &shader, MaterialCtrl materialCtrl)
{
	// TODO: for each model:
	//   this should frustum cull models (and cache the result for other passes)
	//   this would also include skinned meshes (don't want to do the skinning computations multiple times)
	//   (AnimatedMode::BoneTransform() genereates a list of bone transforms, done once, but the actual skinning is in the shader)

	const auto view_projection = camera.projectionTransform() * camera.viewTransform();

	for(const auto &obj: _scenePvs)
	{
		shader.setUniform("u_mvp"sv,   view_projection * obj.transform);
		shader.setUniform("u_model"sv, obj.transform);

		if(materialCtrl == UseMaterials)
		{
			shader.setUniform("u_normal_matrix"sv, glm::mat3(glm::transpose(glm::inverse(obj.transform))));
			obj.model->Render(shader);
		}
		else
			obj.model->Render();
	}
}

void ClusteredShading::renderDepth(const Camera &camera, RenderTarget::Texture2d &target)
{
	target.bindRenderTarget(RGL::RenderTarget::DepthBuffer);

	glDepthMask(GL_TRUE);
    glColorMask(0, 0, 0, 0);
    glDepthFunc(GL_LESS);

    m_depth_prepass_shader->bind();

	renderScene(camera, *m_depth_prepass_shader, NoMaterials);
}

void ClusteredShading::renderLighting(const Camera &camera)
{
	glDepthMask(GL_FALSE);
    glColorMask(1, 1, 1, 1);
	glDepthFunc(GL_EQUAL);   // only draw pixels which exactly match the depth pre-pass

    m_clustered_pbr_shader->bind();

	m_clustered_pbr_shader->setUniform("u_cam_pos"sv,                               camera.position());
	m_clustered_pbr_shader->setUniform("u_near_z"sv,                                camera.nearPlane());
	m_clustered_pbr_shader->setUniform("u_grid_dim"sv,                              m_cluster_grid_dim);
	m_clustered_pbr_shader->setUniform("u_cluster_size_ss"sv,                       glm::uvec2(m_cluster_grid_block_size));
	m_clustered_pbr_shader->setUniform("u_log_grid_dim_y"sv,                        m_log_grid_dim_y);
	m_clustered_pbr_shader->setUniform("u_debug_slices"sv,                          m_debug_slices);
	m_clustered_pbr_shader->setUniform("u_debug_clusters_occupancy"sv,              m_debug_clusters_occupancy);
	m_clustered_pbr_shader->setUniform("u_debug_clusters_occupancy_blend_factor"sv, m_debug_clusters_occupancy_blend_factor);
	m_clustered_pbr_shader->setUniform("u_view"sv,                                  camera.viewTransform());

    m_irradiance_cubemap_rt->bindTexture(6);
    m_prefiltered_env_map_rt->bindTexture(7);
	m_brdf_lut_rt->bindTextureSampler(8);
    m_ltc_mat_lut->Bind(9);
    m_ltc_amp_lut->Bind(10);

	renderScene(m_camera, *m_clustered_pbr_shader);

	// Enable writing to the depth buffer
	glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
}

void ClusteredShading::render_gui()
{
    /* This method is responsible for rendering GUI using ImGUI. */

    /*
     * It's possible to call render_gui() from the base class.
     * It renders performance info overlay.
     */
    CoreApp::render_gui();

    /* Create your own GUI using ImGUI here. */
	ImVec2 window_pos       = ImVec2(float(Window::getWidth()) - 10.f, 10.f);
	ImVec2 window_pos_pivot = ImVec2(1.0f, 0.0f);

    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    ImGui::SetNextWindowSize({ 400, 0 });

	ImGui::Text("   Culling: %4ld µs", m_cull_time.count());
	ImGui::Text("    Z-pass: %4ld µs", m_depth_time.count());
	ImGui::Text("  Clusters: %4ld µs", m_cluster_time1.count() + m_cluster_time2.count() + m_cluster_time3.count());
	// ImGui::Text("Clusters time 2: %ld us", m_cluster_time2.count());
	// ImGui::Text("Clusters time 3: %ld us", m_cluster_time3.count());
	ImGui::Text("   Shading: %4ld µs", m_lighting_time.count());
	ImGui::Text("    Skybox: %4ld µs", m_skybox_time.count());
	// ImGui::Text("        PP: %3ld µs", m_pp_time.count());
	ImGui::Text("Scattering: %4ld µs", m_scatter_time.count());

    ImGui::Begin("Settings");
    {
		if (ImGui::CollapsingHeader("Camera Info", ImGuiTreeNodeFlags_DefaultOpen))
        {
			const auto cam_pos   = m_camera.position();
			const auto cam_fwd   = m_camera.forwardVector();
			const auto cam_right = m_camera.rightVector();
			const auto cam_up    = m_camera.upVector();

			const auto fwd_xz = glm::normalize(glm::vec3(cam_fwd.x, 0.f, cam_fwd.z));
			const float heading_angle = std::acos(glm::clamp(glm::dot(AXIS_Z, fwd_xz), -1.f, 1.f));

			ImGui::Text("      Yaw: %.1f    Pitch: %.1f\n"
						"Position : %.2f ; %.2f ; %.2f\n"
						"Forward  : %.2f ; %.2f ; %.2f  (%.1f°)\n"
						"Right    : %.2f ; %.2f ; %.2f\n"
						"Up       : %.2f ; %.2f ; %.2f",
						glm::degrees(m_camera.yaw()), glm::degrees(m_camera.pitch()),
						cam_pos.x, cam_pos.y, cam_pos.z,
						cam_fwd.x, cam_fwd.y, cam_fwd.z, glm::degrees(heading_angle),
						cam_right.x, cam_right.y, cam_right.z,
						cam_up.x, cam_up.y, cam_up.z);
			ImGui::Text("PVS size : %lu", _scenePvs.size());

			ImGui::Checkbox("Draw AABB", &m_draw_aabb);
			if(ImGui::SliderFloat("FOV", &m_camera_fov, 25.f, 150.f))
				calculateShadingClusterGrid();
		}

		if (ImGui::CollapsingHeader("Lights"))//, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);

            if (ImGui::Checkbox("Show Debug Z Tiles", &m_debug_slices))
                m_debug_clusters_occupancy = false;

			if (ImGui::Checkbox("Show Clusters Occupancy", &m_debug_clusters_occupancy))
                m_debug_slices = false;

			if (m_debug_clusters_occupancy)
				ImGui::SliderFloat("Cluster Occupancy Blend Factor", &m_debug_clusters_occupancy_blend_factor, 0.0f, 1.0f);

			ImGui::Checkbox   ("Animate Lights",    &m_animate_lights);
			ImGui::SliderFloat("Animation Speed",   &m_animation_speed, 0.0f, 15.0f, "%.1f");

            if (ImGui::DragFloat3("Min Bounds", &min_lights_bounds.x, 0.01f))
                max_lights_bounds = glm::max(min_lights_bounds, max_lights_bounds);

            if (ImGui::DragFloat3("Max Bounds", &max_lights_bounds.x, 0.01f))
                min_lights_bounds = glm::min(min_lights_bounds, max_lights_bounds);

            if (ImGui::Button("Normalize Bounds"))
            {
                float range3      = glm::pow(glm::max(min_max_point_light_radius.y, min_max_spot_light_radius.y), 3.0f);
				float bounds      = glm::pow(float(m_point_lights_count + m_spot_lights_count) * range3, 1.0f / 3.0f) / 2.0f;
                min_lights_bounds = glm::vec3(-bounds, -bounds, -bounds);
                max_lights_bounds = glm::vec3(bounds, bounds, bounds);
            }

            ImGui::Separator();
            ImGui::DragScalar("Point Lights Count", ImGuiDataType_U32, &m_point_lights_count);

            if (ImGui::DragFloat("Min Point Lights Radius", &min_max_point_light_radius.x, 0.01f, 0.0f))
            {
                min_max_point_light_radius.x = glm::max(0.0f, min_max_point_light_radius.x);
                min_max_point_light_radius.y = glm::max(min_max_point_light_radius.x, min_max_point_light_radius.y);
            }

            if (ImGui::DragFloat("Max Point Lights Radius", &min_max_point_light_radius.y, 0.01f, 0.0f))
            {
                min_max_point_light_radius.y = glm::max(0.0f, min_max_point_light_radius.y);
                min_max_point_light_radius.x = glm::min(min_max_point_light_radius.x, min_max_point_light_radius.y);
            }
            if (ImGui::DragFloat("Point Lights Intensity", &m_point_lights_intensity, 0.1f, 0.0f))
                m_point_lights_intensity = glm::max(0.0f, m_point_lights_intensity);

            ImGui::Separator();
            ImGui::DragScalar("Spot Lights Count", ImGuiDataType_U32, &m_spot_lights_count);

            if (ImGui::DragFloat("Min Spot Lights Radius", &min_max_spot_light_radius.x, 0.01f, 0.0f))
            {
                min_max_spot_light_radius.x = glm::max(0.0f, min_max_spot_light_radius.x);
                min_max_spot_light_radius.y = glm::max(min_max_spot_light_radius.x, min_max_spot_light_radius.y);
            }

            if (ImGui::DragFloat("Max Spot Lights Radius", &min_max_spot_light_radius.y, 0.01f, 0.0f))
            {
                min_max_spot_light_radius.y = glm::max(0.0f, min_max_spot_light_radius.y);
                min_max_spot_light_radius.x = glm::min(min_max_spot_light_radius.x, min_max_spot_light_radius.y);
            }

            if (ImGui::DragFloat("Min Spot Lights Angle", &min_max_spot_angles.x, 0.01f, 0.0f))
            {
                min_max_spot_angles.x = glm::clamp(min_max_spot_angles.x, 0.0f, 89.0f);
                min_max_spot_angles.y = glm::max(min_max_spot_angles.x, min_max_spot_angles.y);
            }

            if (ImGui::DragFloat("Max Spot Lights Angle", &min_max_spot_angles.y, 0.01f, 0.0f))
            {
                min_max_spot_angles.y = glm::clamp(min_max_spot_angles.y, 0.0f, 89.0f);
                min_max_spot_angles.x = glm::min(min_max_spot_angles.x, min_max_spot_angles.y);
            }

            if (ImGui::DragFloat("Spot Lights Intensity", &m_spot_lights_intensity, 0.1f, 0.0f))
                m_spot_lights_intensity = glm::max(0.0f, m_spot_lights_intensity);

            ImGui::Separator();
			ImGui::Checkbox  ("Two-Sided Area Lights", &m_area_lights_two_sided);
			ImGui::Checkbox  ("Area Lights Geometry", &m_area_lights_geometry);
			ImGui::DragScalar("Area Lights Count", ImGuiDataType_U32, &m_area_lights_count);

            if (ImGui::DragFloat("Area Lights Intensity", &m_area_lights_intensity, 0.1f, 0.0f))
            {
                m_area_lights_intensity = glm::max(0.0f, m_area_lights_intensity);
            }

            if (ImGui::DragFloat2("Area Lights Size", &m_area_lights_size[0], 0.01f, 0.1f))
            {
                m_area_lights_size = glm::max(glm::vec2(0.1f), m_area_lights_size);
            }

            ImGui::Spacing();

            if (ImGui::Button("Normalize Lights Radii"))
            {
                float bounds3 = (max_lights_bounds.x - min_lights_bounds.x) * (max_lights_bounds.y - min_lights_bounds.y) * (max_lights_bounds.z - min_lights_bounds.z);

                if (m_point_lights_count > 0)
                {
					min_max_point_light_radius.y = glm::pow(bounds3 / float(m_point_lights_count), 1.0f / 3.0f);
                    min_max_point_light_radius.x = min_max_point_light_radius.y - (min_max_point_light_radius.y * 0.1f);
                }

                if (m_spot_lights_count > 0)
                {
					min_max_spot_light_radius.y = glm::pow(bounds3 / float(m_spot_lights_count), 1.0f / 3.0f);
                    min_max_spot_light_radius.x = min_max_spot_light_radius.y - (min_max_spot_light_radius.y * 0.1f);
                }
            }
            if (ImGui::Button("Generate Lights"))
            {
                GeneratePointLights();
                GenerateSpotLights();
                GenerateAreaLights();
                UpdateLightsSSBOs();
            }

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
			ImGui::SliderFloat("Bloom threshold",      &m_bloom_threshold,            0.0f, 15.0f, "%.1f");
			ImGui::SliderFloat("Bloom knee",           &m_bloom_knee,                 0.0f, 1.0f,  "%.1f");
            ImGui::SliderFloat("Bloom intensity",      &m_bloom_intensity,      0.0f, 5.0f,  "%.1f");
            ImGui::SliderFloat("Bloom dirt intensity", &m_bloom_dirt_intensity, 0.0f, 10.0f, "%.1f");
        }
		if(ImGui::CollapsingHeader("Fog / Scattering", ImGuiTreeNodeFlags_DefaultOpen))
		{
			{
				float density = m_fog_density;
				if(ImGui::SliderFloat("Fog density", &density, 0.f, 0.5f))
					m_fog_density = density;
			}
			ImGui::SliderFloat("Fog falloff blend", &m_fog_falloff_blend, 0, 1);
			ImGui::SliderFloat("Ray march stride", &m_ray_march_stride, 0.01f, 1.f);
		}

		// if(ImGui::CollapsingHeader("Framebuffers"))
		// {
		// 	ImGui::Image(ImTextureID(int64_t(_rt.texture_id())), { 256, 256 });
		// }
    }
    ImGui::End();
}
