#include "clustered_shading.h"
#include "filesystem.h"
#include "input.h"
#include "util.h"
#include "gui/gui.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtx/string_cast.hpp>

#include <chrono>
#include <vector>
using namespace std::chrono;
using namespace std::literals;

#define IMAGE_UNIT_WRITE 0

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
	m_threshold           (1.5),
	m_knee                (0.1f),
	m_bloom_intensity     (1),
	m_bloom_dirt_intensity(0),
	m_bloom_enabled       (true)
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

    glDeleteBuffers(1, &m_clusters_ssbo);
    glDeleteBuffers(1, &m_cull_lights_dispatch_args_ssbo);
    glDeleteBuffers(1, &m_clusters_flags_ssbo);
    glDeleteBuffers(1, &m_point_light_index_list_ssbo);
    glDeleteBuffers(1, &m_point_light_grid_ssbo);
    glDeleteBuffers(1, &m_spot_light_index_list_ssbo);
    glDeleteBuffers(1, &m_spot_light_grid_ssbo);
    glDeleteBuffers(1, &m_area_light_index_list_ssbo);
    glDeleteBuffers(1, &m_area_light_grid_ssbo);
    glDeleteBuffers(1, &m_unique_active_clusters_ssbo);

    glDeleteTextures(1, &m_depth_tex2D_id);
    glDeleteFramebuffers(1, &m_depth_pass_fbo_id);
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

void ClusteredShading::init_app()
{
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(opengl_message_callback, 0);

    /// Initialize all the variables, buffers, etc. here.
	glClearColor(0.05f, 0.05f, 0.05f, 1);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

	glLineWidth(2.f); // for wireframes (but >1 not commonly supported)

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    /* Create virtual camera. */
	m_camera = std::make_shared<Camera>(80, Window::aspectRatio(), 0.1, 50);
	// m_camera->setPosition(-8.32222f, 4.5269f, -0.768721f);
	// m_camera->setOrientation(glm::quat(0.634325f, 0.0407623f, 0.772209f, 0.0543523f));
	m_camera->setPosition({ -5, 1, -11 });
	m_camera->setOrientationEuler({ 0, 180, 0 });
   
    /// Init clustered shading variables.
	m_cluster_grid_dim.x = uint32_t(glm::ceil(float(Window::getWidth())  / float(m_cluster_grid_block_size)));
	m_cluster_grid_dim.y = uint32_t(glm::ceil(float(Window::getHeight()) / float(m_cluster_grid_block_size)));

    // The depth of the cluster grid during clustered rendering is dependent on the 
    // number of clusters subdivisions in the screen Y direction.
    // Source: Clustered Deferred and Forward Shading (2012) (Ola Olsson, Markus Billeter, Ulf Assarsson).
	const float half_fov     = glm::radians(m_camera->verticalFov() * 0.5f);
	float sD         = 2.0f * glm::tan(half_fov) / float(m_cluster_grid_dim.y);
          m_near_k   = 1.0f + sD;
    m_log_grid_dim_y = 1.0f / glm::log(m_near_k);

	const float z_near       = m_camera->nearPlane();
	const float z_far        = m_camera->farPlane();
	const float log_depth      = glm::log(z_far / z_near);
    m_cluster_grid_dim.z = uint32_t(glm::floor(log_depth * m_log_grid_dim_y));

    m_clusters_count = m_cluster_grid_dim.x * m_cluster_grid_dim.y * m_cluster_grid_dim.z;

	std::printf("clusters count: %d   (%d x %d x %d)\n", m_clusters_count, m_cluster_grid_dim.x, m_cluster_grid_dim.y, m_cluster_grid_dim.z);

    /// Randomly initialize lights
	::srand(3281991);
    GeneratePointLights();
    GenerateSpotLights();
    GenerateAreaLights();

	/// Create scene objects
    /// Create Sponza static object
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

		// const auto box_corners = testbox_model->aabb().corners();

		if(false){
			auto testbox_model = std::make_shared<StaticModel>();
			testbox_model->Load(models_path / "testbox.gltf");

			// create an bunch of them
			for(auto x = -5; x <= 5; x += 1)
			{
				for(auto y = -5; y <= 5; y += 1)
				{
					for(auto z = -5; z <= 5; z += 1)
					{
						const auto position = glm::vec3(x, y, z);
						const auto testbox_matrix = glm::translate(glm::mat4(1), position);
						_scene.emplace_back(testbox_model, testbox_matrix);
					}
				}
			}
		}
	}

    /// Prepare lights' SSBOs.
	UpdateLightsSSBOs();  // initial update will create the GL buffers

    /// Prepare SSBOs related to the clustering (light-culling) algorithm.
    // Stores the screen-space clusters 
    glCreateBuffers  (1, &m_clusters_ssbo);
    glNamedBufferData(m_clusters_ssbo, sizeof(ClusterAABB) * m_clusters_count, nullptr, GL_STATIC_READ);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, CLUSTERS_SSBO_BINDING_INDEX, m_clusters_ssbo);

    // Create a buffer to hold (boolean) flags in the cluster grid that contain samples.
    glCreateBuffers  (1, &m_clusters_flags_ssbo);
    glNamedBufferData(m_clusters_flags_ssbo, sizeof(uint32_t) * m_clusters_count, nullptr, GL_STATIC_READ);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, CLUSTERS_FLAGS_SSBO_BINDING_INDEX, m_clusters_flags_ssbo);

    // A buffer (and internal counter) that holds a list of the unique clusters (the clusters that are visible and actually contain a sample).
    glCreateBuffers  (1, &m_unique_active_clusters_ssbo);
    glNamedBufferData(m_unique_active_clusters_ssbo, sizeof(uint32_t) * m_clusters_count + sizeof(uint32_t), nullptr, GL_STATIC_READ);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, UNIQUE_ACTIVE_CLUSTERS_SSBO_BINDING_INDEX, m_unique_active_clusters_ssbo);

    // A buffer that stores number of work groups to be dispatched by cull lights shader
    glCreateBuffers  (1, &m_cull_lights_dispatch_args_ssbo);
    glNamedBufferData(m_cull_lights_dispatch_args_ssbo, sizeof(uint32_t) * 3, nullptr, GL_STATIC_DRAW);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, CULL_LIGHTS_DISPATCH_ARGS_SSBO_BINDING_INDEX, m_cull_lights_dispatch_args_ssbo);

    // A list of indices to the lights that are active and intersect with a cluster
    glCreateBuffers  (1, &m_point_light_index_list_ssbo);
    glNamedBufferData(m_point_light_index_list_ssbo, sizeof(uint32_t) * m_clusters_count * AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, POINT_LIGHT_INDEX_LIST_SSBO_BINDING_INDEX, m_point_light_index_list_ssbo);

    glCreateBuffers  (1, &m_spot_light_index_list_ssbo);
    glNamedBufferData(m_spot_light_index_list_ssbo, sizeof(uint32_t) * m_clusters_count * AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, SPOT_LIGHT_INDEX_LIST_SSBO_BINDING_INDEX, m_spot_light_index_list_ssbo);

    glCreateBuffers  (1, &m_area_light_index_list_ssbo);
    glNamedBufferData(m_area_light_index_list_ssbo, sizeof(uint32_t) * m_clusters_count * AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, AREA_LIGHT_INDEX_LIST_SSBO_BINDING_INDEX, m_area_light_index_list_ssbo);

    // Every tile takes LightGrid struct that has two unsigned ints one to represent the number of lights in that grid
    // Another to represent the offset to the light index list from where to begin reading light indexes from
    // In this SSBO, atomic counter is also being stored (uint global_index_count)
    // This implementation is straight up from Olsson paper
    glCreateBuffers  (1, &m_point_light_grid_ssbo);
    glNamedBufferData(m_point_light_grid_ssbo, sizeof(uint32_t) + sizeof(LightGrid) * m_clusters_count, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, POINT_LIGHT_GRID_SSBO_BINDING_INDEX, m_point_light_grid_ssbo);

    glCreateBuffers  (1, &m_spot_light_grid_ssbo);
    glNamedBufferData(m_spot_light_grid_ssbo, sizeof(uint32_t) + sizeof(LightGrid) * m_clusters_count, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase (GL_SHADER_STORAGE_BUFFER, SPOT_LIGHT_GRID_SSBO_BINDING_INDEX, m_spot_light_grid_ssbo);

    glCreateBuffers(1, &m_area_light_grid_ssbo);
    glNamedBufferData(m_area_light_grid_ssbo, sizeof(uint32_t) + sizeof(LightGrid) * m_clusters_count, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, AREA_LIGHT_GRID_SSBO_BINDING_INDEX, m_area_light_grid_ssbo);

    // Create depth pre-pass texture and FBO
    glCreateTextures  (GL_TEXTURE_2D, 1, &m_depth_tex2D_id);
    glTextureStorage2D(m_depth_tex2D_id, 1, GL_DEPTH_COMPONENT32F, RGL::Window::getWidth(), RGL::Window::getHeight());

    glTextureParameteri(m_depth_tex2D_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_depth_tex2D_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_depth_tex2D_id, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_depth_tex2D_id, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

    glCreateFramebuffers     (1, &m_depth_pass_fbo_id);
    glNamedFramebufferTexture(m_depth_pass_fbo_id, GL_DEPTH_ATTACHMENT, m_depth_tex2D_id, 0);

    GLenum draw_buffers[] = { GL_NONE };
    glNamedFramebufferDrawBuffers(m_depth_pass_fbo_id, 1, draw_buffers);

    /// Load LTC look-up-tables for area lights rendering
	const auto ltc_lut_path     = FileSystem::getResourcesPath() / "lut";
	const auto ltc_lut_mat_path = ltc_lut_path / "ltc_mat.dds";
	const auto ltc_lut_amp_path = ltc_lut_path / "ltc_amp.dds";

    m_ltc_mat_lut = std::make_shared<Texture2D>();
    if (m_ltc_mat_lut->LoadDds(ltc_lut_mat_path))
    {
		m_ltc_mat_lut->SetWrapping (TextureWrappingCoordinate::S, TextureWrappingParam::CLAMP_TO_EDGE);
		m_ltc_mat_lut->SetWrapping (TextureWrappingCoordinate::T, TextureWrappingParam::CLAMP_TO_EDGE);
		m_ltc_mat_lut->SetFiltering(TextureFiltering::Minify,     TextureFilteringParam::NEAREST);
		m_ltc_mat_lut->SetFiltering(TextureFiltering::Magnify,    TextureFilteringParam::LINEAR);
    }
    else
    {
        fprintf(stderr, "Error: could not load texture %s\n", ltc_lut_mat_path.string().c_str());
    }

    m_ltc_amp_lut = std::make_shared<Texture2D>();
    if (m_ltc_amp_lut->LoadDds(ltc_lut_amp_path))
    {
		m_ltc_amp_lut->SetWrapping (TextureWrappingCoordinate::S, TextureWrappingParam::CLAMP_TO_EDGE);
		m_ltc_amp_lut->SetWrapping (TextureWrappingCoordinate::T, TextureWrappingParam::CLAMP_TO_EDGE);
		m_ltc_amp_lut->SetFiltering(TextureFiltering::Minify,     TextureFilteringParam::NEAREST);
		m_ltc_amp_lut->SetFiltering(TextureFiltering::Magnify,    TextureFilteringParam::LINEAR);
    }
    else
    {
        fprintf(stderr, "Error: could not load texture %s\n", ltc_lut_amp_path.string().c_str());
    }

    /// Create shaders.
	const std::string dir = "src/demos/27_clustered_shading/";

    m_depth_prepass_shader = std::make_shared<Shader>(dir + "depth_pass.vert", dir + "depth_pass.frag");
    m_depth_prepass_shader->link();

    m_generate_clusters_shader = std::make_shared<Shader>(dir + "generate_clusters.comp");
    m_generate_clusters_shader->link();

    m_find_visible_clusters_shader = std::make_shared<Shader>(dir + "find_visible_clusters.comp");
    m_find_visible_clusters_shader->link();

    m_find_unique_clusters_shader = std::make_shared<Shader>(dir + "find_unique_clusters.comp");
    m_find_unique_clusters_shader->link();

    m_update_cull_lights_indirect_args_shader = std::make_shared<Shader>(dir + "update_cull_lights_indirect_args.comp");
    m_update_cull_lights_indirect_args_shader->link();

    m_cull_lights_shader = std::make_shared<Shader>(dir + "cull_lights.comp");
    m_cull_lights_shader->link();

    m_clustered_pbr_shader = std::make_shared<Shader>(dir + "pbr_lighting.vert", dir + "pbr_clustered.frag");
    m_clustered_pbr_shader->link();

    m_update_lights_shader = std::make_shared<Shader>(dir + "update_lights.comp");
    m_update_lights_shader->link();

    m_draw_area_lights_geometry_shader = std::make_shared<Shader>(dir + "area_light_geom.vert", dir + "area_light_geom.frag");
    m_draw_area_lights_geometry_shader->link();

	m_line_draw_shader = std::make_shared<Shader>(dir + "line_draw.vert", dir + "line_draw.frag");
	m_line_draw_shader->link();

    m_equirectangular_to_cubemap_shader = std::make_shared<Shader>(dir + "cubemap.vert", dir + "equirectangular_to_cubemap.frag");
    m_equirectangular_to_cubemap_shader->link();

    m_irradiance_convolution_shader = std::make_shared<Shader>(dir + "cubemap.vert", dir + "irradiance_convolution.frag");
    m_irradiance_convolution_shader->link();
    
    m_prefilter_env_map_shader = std::make_shared<Shader>(dir + "cubemap.vert", dir + "prefilter_cubemap.frag");
    m_prefilter_env_map_shader->link();

	m_precompute_brdf = std::make_shared<Shader>(dir + "FSQ.vert", dir + "precompute_brdf.frag");
    m_precompute_brdf->link();

    m_background_shader = std::make_shared<Shader>(dir + "background.vert", dir + "background.frag");
    m_background_shader->link();

	m_tmo_ps = std::make_shared<TonemappingFilter>(Window::getWidth(), Window::getHeight());

    // Bloom shaders.
    m_downscale_shader = std::make_shared<Shader>(dir + "downscale.comp");
    m_downscale_shader->link();

    m_upscale_shader = std::make_shared<Shader>(dir + "upscale.comp");
    m_upscale_shader->link();

    m_bloom_dirt_texture = std::make_shared<Texture2D>(); 
	m_bloom_dirt_texture->Load(FileSystem::getResourcesPath() / "textures/bloom_dirt_mask.jxl");

    // IBL precomputations.
    GenSkyboxGeometry();

    m_env_cubemap_rt = std::make_shared<CubeMapRenderTarget>();
    m_env_cubemap_rt->set_position(glm::vec3(0.0));
    m_env_cubemap_rt->generate_rt(2048, 2048, true);

    m_irradiance_cubemap_rt = std::make_shared<CubeMapRenderTarget>();
    m_irradiance_cubemap_rt->set_position(glm::vec3(0.0));
    m_irradiance_cubemap_rt->generate_rt(32, 32);

    m_prefiltered_env_map_rt = std::make_shared<CubeMapRenderTarget>();
    m_prefiltered_env_map_rt->set_position(glm::vec3(0.0));
    m_prefiltered_env_map_rt->generate_rt(512, 512, true);

    m_brdf_lut_rt = std::make_shared<Texture2DRenderTarget>();
    m_brdf_lut_rt->create(512, 512, GL_RG16F);

    PrecomputeIndirectLight(FileSystem::getResourcesPath() / "textures/skyboxes/IBL" / m_hdr_maps_names[m_current_hdr_map_idx]);
    PrecomputeBRDF(m_brdf_lut_rt);


	// auto proj = m_camera->m_projection;
	// auto inv_proj = glm::inverse(proj);

    /// Generate clusters' AABBs
    // This can be done once as long as the camera parameters doesn't change (projection matrix related variables)
    m_generate_clusters_shader->bind();
	m_generate_clusters_shader->setUniform("u_grid_dim"sv,           m_cluster_grid_dim);
	m_generate_clusters_shader->setUniform("u_cluster_size_ss"sv,    glm::uvec2(m_cluster_grid_block_size));
	m_generate_clusters_shader->setUniform("u_near_k"sv,             m_near_k);
	m_generate_clusters_shader->setUniform("u_near_z"sv,             m_camera->nearPlane());
	m_generate_clusters_shader->setUniform("u_inverse_projection"sv, glm::inverse(m_camera->m_projection));
	m_generate_clusters_shader->setUniform("u_pixel_size"sv,         1.0f / glm::vec2(RGL::Window::getWidth(), RGL::Window::getHeight()));
	glDispatchCompute(GLuint(glm::ceil(float(m_clusters_count) / 1024.0f)), 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClusteredShading::input()
{
    /* Close the application when Esc is released. */
    if (Input::getKeyUp(KeyCode::Escape))
    {
        stop();
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
	if (Input::getKeyUp(KeyCode::F12))
    {
		/* Specify filename of Sthe screenshot. */
        std::string filename = "27_clustered_shading";
		if (take_screenshot_png(filename, size_t(Window::getWidth() / 2), size_t(Window::getHeight() / 2)))
        {
            /* If specified folders in the path are not already created, they'll be created automagically. */
            std::cout << "Saved " << filename << ".png to " << (FileSystem::getRootPath() / "screenshots/") << std::endl;
        }
        else
        {
            std::cerr << "Could not save " << filename << ".png to " << (FileSystem::getRootPath() / "screenshots/") << std::endl;
        }
    }

    if (Input::getKeyUp(KeyCode::Space))
    {
        m_animate_lights = !m_animate_lights;
    }
}

void ClusteredShading::update(double delta_time)
{
    /* Update variables here. */
    m_camera->update(delta_time);

	// static float     rotation_speed = 1;
	static float     time_accum     = 0;
	static glm::mat4 rotation_mat   = glm::mat4(1);

    if (m_animate_lights)
    {
		time_accum  += float(delta_time * m_animation_speed);
		rotation_mat = glm::rotate(glm::mat4(1), glm::radians(60.0f * float(delta_time)) * 2.0f * m_animation_speed, glm::vec3(0, 1, 0));

        m_update_lights_shader->bind();
		m_update_lights_shader->setUniform("u_time"sv,                 time_accum);
		m_update_lights_shader->setUniform("u_area_two_sided"sv,       m_area_lights_two_sided);
		m_update_lights_shader->setUniform("u_area_rotation_matrix"sv, rotation_mat);

		auto max_lights_count = glm::max(m_point_lights.size(), glm::max(m_spot_lights.size(), glm::max(m_directional_lights.size(), m_area_lights.size())));
		glDispatchCompute(GLuint(glm::ceil(float(max_lights_count) / 1024.f)), 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
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

void ClusteredShading::HdrEquirectangularToCubemap(const std::shared_ptr<CubeMapRenderTarget>& cubemap_rt, const std::shared_ptr<Texture2D>& m_equirectangular_map)
{
    /* Update all faces per frame */
    m_equirectangular_to_cubemap_shader->bind();
	m_equirectangular_to_cubemap_shader->setUniform("u_projection"sv, cubemap_rt->m_projection);

	glViewport(0, 0, GLsizei(cubemap_rt->m_width), GLsizei(cubemap_rt->m_height));
    glBindFramebuffer(GL_FRAMEBUFFER, cubemap_rt->m_fbo_id);
    m_equirectangular_map->Bind(1);

    glBindVertexArray(m_skybox_vao);
    for (uint8_t side = 0; side < 6; ++side)
    {
		m_equirectangular_to_cubemap_shader->setUniform("u_view"sv, cubemap_rt->m_view_transforms[side]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + side, cubemap_rt->m_cubemap_texture_id, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glViewport(0, 0, Window::getWidth(), Window::getHeight());
}

void ClusteredShading::IrradianceConvolution(const std::shared_ptr<CubeMapRenderTarget>& cubemap_rt)
{
    /* Update all faces per frame */
    m_irradiance_convolution_shader->bind();
	m_irradiance_convolution_shader->setUniform("u_projection"sv, cubemap_rt->m_projection);

	glViewport(0, 0, GLsizei(cubemap_rt->m_width), GLsizei(cubemap_rt->m_height));
    glBindFramebuffer(GL_FRAMEBUFFER, cubemap_rt->m_fbo_id);
    m_env_cubemap_rt->bindTexture(1);

    for (uint8_t side = 0; side < 6; ++side)
    {
		m_irradiance_convolution_shader->setUniform("u_view"sv, cubemap_rt->m_view_transforms[side]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + side, cubemap_rt->m_cubemap_texture_id, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        glBindVertexArray(m_skybox_vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
    glViewport(0, 0, Window::getWidth(), Window::getHeight());
}

void ClusteredShading::PrefilterCubemap(const std::shared_ptr<CubeMapRenderTarget>& cubemap_rt)
{
    m_prefilter_env_map_shader->bind();
	m_prefilter_env_map_shader->setUniform("u_projection"sv, cubemap_rt->m_projection);
    
    m_env_cubemap_rt->bindTexture(1);

    glBindFramebuffer(GL_FRAMEBUFFER, cubemap_rt->m_fbo_id);

	auto max_mip_levels = uint8_t(glm::log2(float(cubemap_rt->m_width)));
    for (uint8_t mip = 0; mip < max_mip_levels; ++mip)
    {
        // resize the framebuffer according to mip-level size.
		auto mip_width  = uint32_t(cubemap_rt->m_width  * std::pow(0.5, mip));
		auto mip_height = uint32_t(cubemap_rt->m_height * std::pow(0.5, mip));

        glBindRenderbuffer(GL_RENDERBUFFER, cubemap_rt->m_rbo_id);
        //glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mip_width, mip_height);
		glViewport(0, 0, GLsizei(mip_width), GLsizei(mip_height));

        float roughness = float(mip) / float(max_mip_levels - 1);
		m_prefilter_env_map_shader->setUniform("u_roughness"sv, roughness);

        for (uint8_t side = 0; side < 6; ++side)
        {
			m_prefilter_env_map_shader->setUniform("u_view"sv, cubemap_rt->m_view_transforms[side]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + side, cubemap_rt->m_cubemap_texture_id, mip);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBindVertexArray(m_skybox_vao);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, Window::getWidth(), Window::getHeight());
}

void ClusteredShading::PrecomputeIndirectLight(const std::filesystem::path& hdri_map_filepath)
{
    auto envmap_hdr = std::make_shared<Texture2D>();
    envmap_hdr->LoadHdr(hdri_map_filepath);

    HdrEquirectangularToCubemap(m_env_cubemap_rt, envmap_hdr);

    glBindTexture(GL_TEXTURE_CUBE_MAP, m_env_cubemap_rt->m_cubemap_texture_id);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    IrradianceConvolution(m_irradiance_cubemap_rt);
    PrefilterCubemap(m_prefiltered_env_map_rt);
}

void ClusteredShading::PrecomputeBRDF(const std::shared_ptr<Texture2DRenderTarget>& rt)
{
    GLuint m_dummy_vao_id;
    glCreateVertexArrays(1, &m_dummy_vao_id);

    rt->bindRenderTarget();
    m_precompute_brdf->bind();

    glBindVertexArray(m_dummy_vao_id);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glDeleteVertexArrays(1, &m_dummy_vao_id);

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
	auto T0 = steady_clock::now();


	// TODO: render shadow-maps (if lights/meshes changed)
	//   need to limit to only "a few" lights, basically strongest lights (as perceived by the camera)



	cullScene();


	// 1. Depth(Z) pre-pass (if camera/meshes moved, likely always)
	renderDepthPass();

    // 2. Blit depth info to tmo_ps framebuffer
	glBlitNamedFramebuffer(m_depth_pass_fbo_id,
						   m_tmo_ps->rt->m_fbo_id,
                           0, 0, Window::getWidth(), Window::getHeight(),
						   0, 0, Window::getWidth(), Window::getHeight(),
						   GL_DEPTH_BUFFER_BIT,
						   GL_NEAREST);

	auto T1 = steady_clock::now();
	m_depth_time = duration_cast<microseconds>(T1 - T0);
    
    static const uint32_t clear_val = 0;

	T0 = steady_clock::now();

    // 3. Find visible clusters
    glClearNamedBufferData(m_clusters_flags_ssbo, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);

    m_find_visible_clusters_shader->bind();
	m_find_visible_clusters_shader->setUniform("u_near_z"sv,          m_camera->nearPlane());
	m_find_visible_clusters_shader->setUniform("u_far_z"sv,           m_camera->farPlane());
	m_find_visible_clusters_shader->setUniform("u_log_grid_dim_y"sv,  m_log_grid_dim_y);
	m_find_visible_clusters_shader->setUniform("u_cluster_size_ss"sv, glm::uvec2(m_cluster_grid_block_size));
	m_find_visible_clusters_shader->setUniform("u_grid_dim"sv,        m_cluster_grid_dim);
    
    glBindTextureUnit(0, m_depth_tex2D_id);
	glDispatchCompute(GLuint(glm::ceil(float(RGL::Window::getWidth()) / 32.f)),
					  GLuint(glm::ceil(float(RGL::Window::getHeight()) / 32.f)),
					  1);
    glMemoryBarrier  (GL_SHADER_STORAGE_BARRIER_BIT);

	T1 = steady_clock::now();
	m_cluster_time1 = duration_cast<microseconds>(T1 - T0);
	T0 = T1;

    // 4. Find unique clusters and update the indirect dispatch arguments buffer
    glClearNamedBufferData(m_unique_active_clusters_ssbo, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);

    m_find_unique_clusters_shader->bind();
	glDispatchCompute(GLuint(glm::ceil(float(m_clusters_count) / 1024.0f)), 1, 1);
    glMemoryBarrier  (GL_SHADER_STORAGE_BARRIER_BIT);

    m_update_cull_lights_indirect_args_shader->bind();
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier  (GL_SHADER_STORAGE_BARRIER_BIT);

	T1 = steady_clock::now();
	m_cluster_time2 = duration_cast<microseconds>(T1 - T0);
	T0 = T1;

	// 5. Assign lights to clusters (cull lights)
    glClearNamedBufferData(m_point_light_grid_ssbo,       GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
    glClearNamedBufferData(m_point_light_index_list_ssbo, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
    glClearNamedBufferData(m_spot_light_grid_ssbo,        GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
    glClearNamedBufferData(m_spot_light_index_list_ssbo,  GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
    glClearNamedBufferData(m_area_light_grid_ssbo,        GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);
    glClearNamedBufferData(m_area_light_index_list_ssbo,  GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &clear_val);

    m_cull_lights_shader->bind();
	m_cull_lights_shader->setUniform("u_view_matrix"sv, m_camera->m_view);

    glBindBuffer             (GL_DISPATCH_INDIRECT_BUFFER, m_cull_lights_dispatch_args_ssbo);
    glDispatchComputeIndirect(0);
    glMemoryBarrier          (GL_SHADER_STORAGE_BARRIER_BIT);

	T1 = steady_clock::now();
	m_cluster_time3 = duration_cast<microseconds>(T1 - T0);
	T0 = T1;

    // 6. Render lighting
    m_tmo_ps->bindFilterFBO(GL_COLOR_BUFFER_BIT);
    renderLighting();

    // 7. Render area lights geometry
	if(m_area_lights_geometry)
	{
		m_draw_area_lights_geometry_shader->bind();
		m_draw_area_lights_geometry_shader->setUniform("u_view_projection"sv, m_camera->m_projection * m_camera->m_view);
		glDrawArrays(GL_TRIANGLES, 0, GLsizei(6 * m_area_lights.size()));
	}

	T1 = steady_clock::now();
	m_lighting_time = duration_cast<microseconds>(T1 - T0);
	T0 = T1;

    // 8. Render skybox
    m_background_shader->bind();
	m_background_shader->setUniform("u_projection"sv, m_camera->m_projection);
	m_background_shader->setUniform("u_view"sv,       glm::mat4(glm::mat3(m_camera->m_view)));
	m_background_shader->setUniform("u_lod_level"sv,  m_background_lod_level);
    m_env_cubemap_rt->bindTexture();

    glBindVertexArray(m_skybox_vao);
    glDrawArrays     (GL_TRIANGLES, 0, 36);

	T1 = steady_clock::now();
	m_skybox_time = duration_cast<microseconds>(T1 - T0);
	T0 = T1;

    // 9. Bloom: downscale
    if (m_bloom_enabled)
    {
		// TODO: move this stuff to a separate class (akin to TonemappingFilter
		//   preferrably, generalize both to "postprocess filter"

        m_downscale_shader->bind();
		m_downscale_shader->setUniform("u_threshold"sv, glm::vec4(m_threshold, m_threshold - m_knee, 2.0f * m_knee, 0.25f * m_knee));
        m_tmo_ps->rt->bindTexture();

        glm::uvec2 mip_size = glm::uvec2(m_tmo_ps->rt->m_width / 2, m_tmo_ps->rt->m_height / 2);

		for (auto idx = 0; idx < m_tmo_ps->rt->m_mip_levels - 1; ++idx)
        {
			m_downscale_shader->setUniform("u_texel_size"sv,    1.0f / glm::vec2(mip_size));
			m_downscale_shader->setUniform("u_mip_level"sv,     idx);
			m_downscale_shader->setUniform("u_use_threshold"sv, idx == 0);

			m_tmo_ps->rt->bindImageForWrite(IMAGE_UNIT_WRITE, idx + 1);

			glDispatchCompute(GLuint(glm::ceil(float(mip_size.x) / 8.f)),
							  GLuint(glm::ceil(float(mip_size.y) / 8.f)),
							  1);

            mip_size = mip_size / 2u;

            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        }

		// Bloom: upscale
        m_upscale_shader->bind();
		m_upscale_shader->setUniform("u_bloom_intensity"sv, m_bloom_intensity);
		m_upscale_shader->setUniform("u_dirt_intensity"sv,  m_bloom_dirt_intensity);
        m_tmo_ps->rt->bindTexture();
        m_bloom_dirt_texture->Bind(1);

		for (auto idx = m_tmo_ps->rt->m_mip_levels - 1; idx >= 1; --idx)
        {
			mip_size.x = glm::max(1u, uint32_t(glm::floor(float(m_tmo_ps->rt->m_width)  / glm::pow(2.0, idx - 1))));
			mip_size.y = glm::max(1u, uint32_t(glm::floor(float(m_tmo_ps->rt->m_height) / glm::pow(2.0, idx - 1))));

			m_upscale_shader->setUniform("u_texel_size"sv, 1.0f / glm::vec2(mip_size));
			m_upscale_shader->setUniform("u_mip_level"sv,  idx);

			m_tmo_ps->rt->bindImageForReadWrite(IMAGE_UNIT_WRITE, idx - 1);

			glDispatchCompute(GLuint(glm::ceil(float(mip_size.x) / 8)), GLuint(glm::ceil(float(mip_size.y) / 8)), 1);

            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        }
    }

    // 10. Apply tone mapping
    m_tmo_ps->render(m_exposure, m_gamma);

	T1 = steady_clock::now();
	m_pp_time = duration_cast<microseconds>(T1 - T0);

	if(m_draw_aabb)
		renderSceneAABB();
}

void ClusteredShading::renderSceneAABB()
{
	if(not m_debug_draw_vbo)
		glGenBuffers(1, &m_debug_draw_vbo);

	const auto modelView = m_camera->m_projection * m_camera->m_view;

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


	m_line_draw_shader->bind();
	m_line_draw_shader->setUniform("u_line_color"sv, glm::vec4(0.3, 1.0, 0.7, 1));
	m_line_draw_shader->setUniform("u_mvp"sv, modelView); // no obj.transform needed; we'll transform the AABB vertices

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
}

const std::vector<StaticObject> &ClusteredShading::cullScene()
{
	const auto T0 = steady_clock::now();
	// TODO: in theory, this could be done in multiple threads
	//   however, a space partitioning scheme is probably a better (first) step

	_scenePvs.clear();
	_scenePvs.reserve(256); // a guesstimate how many objects are visible (maybe a % of total count?)

	// perform frustum culling of all objects in the scene (or a partition there of)

	const auto view_pos = m_camera->position();
	const auto &frustum = m_camera->frustum();
	// TODO: _scenePvs = _scene.cull(view_pos, frustum)

	for(const auto &obj: _scene) // TODO _scene.near(view_pos, m_camera->farPlane()) i.e. everything within range of the camera's far plane
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

void ClusteredShading::renderScene(RGL::Shader &shader, bool use_material)
{
	// TODO: for each model:
	//   this should frustum cull models (and cache the result for other passes)
	//   this would also include skinned meshes (don't want to do the skinning computations multiple times)
	//   (AnimatedMode::BoneTransform() genereates a list of bone transforms, done once, but the actual skinning is in the shader)

	const auto modelView = m_camera->m_projection * m_camera->m_view;

	for(const auto &obj: _scenePvs)
	{
		shader.setUniform("u_mvp"sv,   modelView * obj.transform);
		shader.setUniform("u_model"sv, obj.transform);

		if(use_material)
		{
			shader.setUniform("u_normal_matrix"sv, glm::mat3(glm::transpose(glm::inverse(obj.transform))));
			obj.model->Render(shader);
		}
		else
			obj.model->Render();
	}
}

void ClusteredShading::renderDepthPass()
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_depth_pass_fbo_id);
    glClear          (GL_DEPTH_BUFFER_BIT);

	glDepthMask(GL_TRUE);
    glColorMask(0, 0, 0, 0);
    glDepthFunc(GL_LESS);

    m_depth_prepass_shader->bind();

	renderScene(*m_depth_prepass_shader, false);
}

void ClusteredShading::renderLighting()
{
	glDepthMask(GL_FALSE);
    glColorMask(1, 1, 1, 1);
	glDepthFunc(GL_EQUAL);   // only draw pixels which exactly match the depth pre-pass

    m_clustered_pbr_shader->bind();

	m_clustered_pbr_shader->setUniform("u_cam_pos"sv,                               m_camera->position());
	m_clustered_pbr_shader->setUniform("u_near_z"sv,                                m_camera->nearPlane());
	m_clustered_pbr_shader->setUniform("u_grid_dim"sv,                              m_cluster_grid_dim);
	m_clustered_pbr_shader->setUniform("u_cluster_size_ss"sv,                       glm::uvec2(m_cluster_grid_block_size));
	m_clustered_pbr_shader->setUniform("u_log_grid_dim_y"sv,                        m_log_grid_dim_y);
	m_clustered_pbr_shader->setUniform("u_debug_slices"sv,                          m_debug_slices);
	m_clustered_pbr_shader->setUniform("u_debug_clusters_occupancy"sv,              m_debug_clusters_occupancy);
	m_clustered_pbr_shader->setUniform("u_debug_clusters_occupancy_blend_factor"sv, m_debug_clusters_occupancy_blend_factor);
	m_clustered_pbr_shader->setUniform("u_view"sv,                                  m_camera->m_view);

    m_irradiance_cubemap_rt->bindTexture(6);
    m_prefiltered_env_map_rt->bindTexture(7);
    m_brdf_lut_rt->bindTexture(8);
    m_ltc_mat_lut->Bind(9);
    m_ltc_amp_lut->Bind(10);

	renderScene(*m_clustered_pbr_shader);

    /* Enable writing to the depth buffer. */
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

	ImGui::Text("Cull time: %ld us", m_cull_time.count());
	ImGui::Text("Z-pass time: %ld us", m_depth_time.count());
	ImGui::Text("Clusters time 1: %ld us", m_cluster_time1.count());
	ImGui::Text("Clusters time 2: %ld us", m_cluster_time2.count());
	ImGui::Text("Clusters time 3: %ld us", m_cluster_time3.count());
	ImGui::Text("Lighting time: %ld us", m_lighting_time.count());
	ImGui::Text("Skybox time: %ld us", m_skybox_time.count());
	ImGui::Text("PP time: %ld us", m_pp_time.count());

    ImGui::Begin("Settings");
    {
        if (ImGui::CollapsingHeader("Help"))
        {
            ImGui::Text("Controls info: \n\n"
                        "F1     - take a screenshot\n"
                        "F2     - toggle wireframe rendering\n"
                        "WASDQE - control camera movement\n"
                        "RMB    - press to rotate the camera\n"
                        "Esc    - close the app\n\n");
        }

        if (ImGui::CollapsingHeader("Camera Info"))
        {
            glm::vec3 cam_pos = m_camera->position();
            glm::vec3 cam_dir = m_camera->direction();
			float     cam_fov = m_camera->verticalFov();

            ImGui::Text("Position  : [%.2f, %.2f, %.2f]\n"
                        "Direction : [%.2f, %.2f, %.2f]\n"
                        "FoV       : % .2f", 
                         cam_pos.x, cam_pos.y, cam_pos.z, 
                         cam_dir.x, cam_dir.y, cam_dir.z, 
                         cam_fov);
			ImGui::Text("Visible   : %lu", _scenePvs.size());

			ImGui::Checkbox("Draw AABB", &m_draw_aabb);
		}

        if (ImGui::CollapsingHeader("Lights Generator", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);

            if (ImGui::Checkbox("Show Debug Z Tiles", &m_debug_slices))
            {
                m_debug_clusters_occupancy = false;
            }
            if (ImGui::Checkbox("Show Clusters Occupancy", &m_debug_clusters_occupancy))
            {
                m_debug_slices = false;
            }
            if (m_debug_clusters_occupancy)
            {
                ImGui::SliderFloat("Cluster Occupancy Blend Factor", &m_debug_clusters_occupancy_blend_factor, 0.0f, 1.0f);
            }

			ImGui::Checkbox   ("Animate Lights",    &m_animate_lights);
			ImGui::SliderFloat("Animation Speed",   &m_animation_speed, 0.0f, 15.0f, "%.1f");

            if (ImGui::DragFloat3("Min Bounds", &min_lights_bounds.x, 0.01f))
            {
                max_lights_bounds = glm::max(min_lights_bounds, max_lights_bounds);
            }
            
            if (ImGui::DragFloat3("Max Bounds", &max_lights_bounds.x, 0.01f))
            {
                min_lights_bounds = glm::min(min_lights_bounds, max_lights_bounds);
            }
            
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
            {
                m_point_lights_intensity = glm::max(0.0f, m_point_lights_intensity);
            }

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
            {
                m_spot_lights_intensity = glm::max(0.0f, m_spot_lights_intensity);
            }

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
            ImGui::SliderFloat("Background LOD level", &m_background_lod_level, 0.0, glm::log2(float(m_env_cubemap_rt->m_width)), "%.1f");

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
            ImGui::SliderFloat("Bloom threshold",      &m_threshold,            0.0f, 15.0f, "%.1f");
            ImGui::SliderFloat("Bloom knee",           &m_knee,                 0.0f, 1.0f,  "%.1f");
            ImGui::SliderFloat("Bloom intensity",      &m_bloom_intensity,      0.0f, 5.0f,  "%.1f");
            ImGui::SliderFloat("Bloom dirt intensity", &m_bloom_dirt_intensity, 0.0f, 10.0f, "%.1f");
        }

    }
    ImGui::End();
}
