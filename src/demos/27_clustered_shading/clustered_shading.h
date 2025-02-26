#pragma once
#include "core_app.h"

#include "camera.h"
#include "pp_light_scattering.h"
#include "pp_tonemapping.h"
#include "ssbo.h"
#include "static_model.h"
#include "shader.h"
#include "shared.h"
#include "rendertarget_2d.h"
#include "rendertarget_cube.h"
#include "GLTimer.h"
#include "pp_bloom.h"
#include "pp_gaussian_blur.h"

#include <memory>
#include <vector>

using seconds_f = std::chrono::duration<float, std::ratio<1>>;

namespace
{
	[[maybe_unused]] void setLightDirection(glm::vec3& direction, float azimuth, float elevation)
    {
        float az = glm::radians(azimuth);
        float el = glm::radians(elevation);

        direction.x = glm::sin(el) * glm::cos(az);
        direction.y = glm::cos(el);
        direction.z = glm::sin(el) * glm::sin(az);

        direction = glm::normalize(-direction);
    }

    // Convert HSV to RGB:
    // Source: https://en.wikipedia.org/wiki/HSL_and_HSV#From_HSV
    // Retrieved: 28/04/2016
    // @param H Hue in the range [0, 360)
    // @param S Saturation in the range [0, 1]
    // @param V Value in the range [0, 1]
	[[maybe_unused]] glm::vec3 hsv2rgb(float H, float S, float V)
    {
        float C = V * S;
        float m = V - C;
        float H2 = H / 60.0f;
		float X = C * (1.0f - std::abs(std::fmod(H2, 2.f) - 1.f));

        glm::vec3 RGB;

        switch (static_cast<int>(H2))
        {
        case 0:
            RGB = { C, X, 0 };
            break;
        case 1:
            RGB = { X, C, 0 };
            break;
        case 2:
            RGB = { 0, C, X };
            break;
        case 3:
            RGB = { 0, X, C };
            break;
        case 4:
            RGB = { X, 0, C };
            break;
        case 5:
            RGB = { C, 0, X };
            break;
        }

        return RGB + m;
    }
};

struct StaticObject
{
    StaticObject() : StaticObject(nullptr, glm::mat4(1.0)) {}
	StaticObject(const std::shared_ptr<RGL::StaticModel> & model_,
				 const glm::mat4                         & transform)
		: transform(transform),
		  model    (model_) {}

	glm::mat4 transform;
	std::shared_ptr<RGL::StaticModel> model;
};

enum struct BlendMode
{
	Replace,
	Add,
	Alpha,
};

class ClusteredShading : public RGL::CoreApp
{
public:
    ClusteredShading();
    ~ClusteredShading();

    void init_app()                override;
    void input()                   override;
    void update(double delta_time) override;
    void render()                  override;
    void render_gui()              override;

private:
	void calculateShadingClusterGrid();
	void prepareClusterBuffers();
	void GenerateAreaLights();
    void GeneratePointLights();
    void GenerateSpotLights();
    void UpdateLightsSSBOs();

	void bindScreenRenderTarget();
	void HdrEquirectangularToCubemap(const std::shared_ptr<RenderTargetCube> & cubemap_rt, const std::shared_ptr<RGL::Texture2D> & m_equirectangular_map);
	void IrradianceConvolution      (const std::shared_ptr<RenderTargetCube> & cubemap_rt);
	void PrefilterCubemap           (const std::shared_ptr<RenderTargetCube>& cubemap_rt);
    void PrecomputeIndirectLight    (const std::filesystem::path & hdri_map_filepath);
	void PrecomputeBRDF             (const std::shared_ptr<RGL::RenderTarget::Texture2d>& rt);
    void GenSkyboxGeometry();

	const std::vector<StaticObject> &cullScene();
	void renderScene(const RGL::Camera &camera, RGL::Shader &shader, MaterialCtrl matCtrl=UseMaterials);
	void renderDepth(const RGL::Camera &camera, RGL::RenderTarget::Texture2d &target);
	void renderLighting(const RGL::Camera &camera);
	void renderSceneBounds();
	void draw2d(const RGL::Texture &texture, BlendMode mode=BlendMode::Replace); // TODO: move to CoreApp
	void draw2d(const RGL::Texture &texture, RGL::Texture &target, BlendMode blend=BlendMode::Replace); // TODO: move to CoreApp
	void draw2d(const RGL::Texture &texture, const glm::uvec2 &top_left, const glm::uvec2 &bottom_right); // TODO: move to CoreApp

	void debugDrawLine(const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec4 &color={1,1,1,1});
	void debugDrawSphere(const glm::vec3 &center, float radius, const glm::vec4 &color={1,1,1,1});
	void debugDrawSphere(const glm::vec3 &center, float radius, size_t rings, size_t slices, const glm::vec4 &color={1,1,1,1});
	void debugDrawSpotLight(const SpotLight &light, const glm::vec4 &color={1,1,1,1});

	RGL::Camera m_camera;
	float m_camera_fov { 80.f };

	std::shared_ptr<RenderTargetCube>   m_env_cubemap_rt;
	std::shared_ptr<RenderTargetCube>   m_irradiance_cubemap_rt;
	std::shared_ptr<RenderTargetCube>   m_prefiltered_env_map_rt;
	std::shared_ptr<RGL::RenderTarget::Texture2d> m_brdf_lut_rt;

    std::shared_ptr<RGL::Shader> m_equirectangular_to_cubemap_shader;
    std::shared_ptr<RGL::Shader> m_irradiance_convolution_shader;
    std::shared_ptr<RGL::Shader> m_prefilter_env_map_shader;
    std::shared_ptr<RGL::Shader> m_precompute_brdf;
    std::shared_ptr<RGL::Shader> m_background_shader;

    /// Clustered shading variables.
    std::shared_ptr<RGL::Shader> m_depth_prepass_shader;
    std::shared_ptr<RGL::Shader> m_generate_clusters_shader;
	std::shared_ptr<RGL::Shader> m_flag_nonempty_clusters_shader;
	std::shared_ptr<RGL::Shader> m_collect_active_clusters_shader;
    std::shared_ptr<RGL::Shader> m_update_cull_lights_indirect_args_shader;
    std::shared_ptr<RGL::Shader> m_cull_lights_shader;
    std::shared_ptr<RGL::Shader> m_clustered_pbr_shader;
    std::shared_ptr<RGL::Shader> m_update_lights_shader;

    std::shared_ptr<RGL::Shader> m_draw_area_lights_geometry_shader;
	std::shared_ptr<RGL::Shader> m_line_draw_shader;
	std::shared_ptr<RGL::Shader> m_fsq_shader;

	// GLuint m_depth_tex2D_id;
	// GLuint m_depth_pass_fbo_id;
	RGL::RenderTarget::Texture2d m_depth_pass_rt;

	GLuint _dummy_vao_id;

	// GLuint m_clusters_ssbo;
	GLuint m_cull_lights_dispatch_args_ssbo;
	GLuint m_nonempty_clusters_ssbo;
	GLuint m_point_light_index_list_ssbo;
	GLuint m_point_light_grid_ssbo;
	GLuint m_spot_light_index_list_ssbo;
	GLuint m_spot_light_grid_ssbo;
	GLuint m_area_light_index_list_ssbo;
	GLuint m_area_light_grid_ssbo;
	GLuint m_active_clusters_ssbo;

	struct
	{
		struct
		{
			GLuint aabb;
			GLuint cull_lights_dispatch_args;
			GLuint flags;
			GLuint unique_active_clusters;

			GLuint point_light_index_list;
			GLuint point_light_grid;

			GLuint spot_light_index_list;
			GLuint spot_light_grid;

			GLuint area_light_index_list;
			GLuint area_light_grid;

		} ssbo;
	} m_clusterShading;

    // Average number of overlapping lights per cluster AABB.
    // This variable matters when the lights are big and cover more than one cluster.
	static constexpr uint32_t AVERAGE_OVERLAPPING_LIGHTS_PER_CLUSTER      = 50;
	static constexpr uint32_t AVERAGE_OVERLAPPING_AREA_LIGHTS_PER_CLUSTER = 100;

	uint32_t   m_cluster_grid_block_size = 64; // The size of a cluster in the screen space.(unit?)
    glm::uvec3 m_cluster_grid_dim;             // 3D dimensions of the cluster grid.
	float      m_near_k;                       // ( 1 + ( 2 * tan( fov * 0.5 ) / ClusterGridDim.y ) ) // Used to compute the near plane for clusters at depth k.
    float      m_log_grid_dim_y;               // 1.0f / log( NearK )  // Used to compute the k index of the cluster from the view depth of a pixel sample.
	uint32_t   m_clusters_count;


    bool  m_debug_slices                          = false;
    bool  m_debug_clusters_occupancy              = false;
    float m_debug_clusters_occupancy_blend_factor = 0.9f;

    /// Lights
	uint32_t  m_point_lights_count       = 0;
	uint32_t  m_spot_lights_count        = 0;
    uint32_t  m_directional_lights_count = 0;
	uint32_t  m_area_lights_count        = 0;

	glm::vec2 min_max_point_light_radius = { 10, 20 };
	glm::vec2 min_max_spot_light_radius  = { 1, 4 };
	glm::vec2 min_max_spot_angles        = { 10, 15 };
	glm::vec3 min_lights_bounds          = { -11,  0.2f, -6 };
	glm::vec3 max_lights_bounds          = { 11, 12,  6 };

	float     m_point_lights_intensity   = 100;
	float     m_spot_lights_intensity    = 100;
	float     m_area_lights_intensity    = 30;
	glm::vec2 m_area_lights_size         = glm::vec2(0.5f);
	float     m_animation_speed          = 0.1f;
	bool      m_animate_lights           = false;
    bool      m_area_lights_two_sided    = true;
	bool      m_area_lights_geometry     = true;
	bool      m_draw_aabb                = false;
	// GLuint    m_debug_draw_vao           = 0;
	GLuint    m_debug_draw_vbo           = 0;
	GLuint    _gl_time_query             = 0;


	std::vector<DirectionalLight> m_directional_lights;
    std::vector<PointLight>       m_point_lights;
    std::vector<SpotLight>        m_spot_lights;
    std::vector<AreaLight>        m_area_lights;
	// animating lights orbit parameters
	std::vector<glm::vec4>        m_point_lights_orbit; // [x, y, z] => [ellipse a radius, ellipse b radius, light move speed]
	std::vector<glm::vec4>        m_spot_lights_orbit;  // [x, y, z] => [ellipse a radius, ellipse b radius, light move speed]

	std::vector<StaticObject> _scene;  // TODO: Scene _scene;
	std::vector<StaticObject> _scenePvs;  // potentially visible set

	// StaticObject m_sponza_static_object;

	ShaderStorageBuffer<DirectionalLight> m_directional_lights_ssbo;
	ShaderStorageBuffer<PointLight> m_point_lights_ssbo;
	ShaderStorageBuffer<SpotLight> m_spot_lights_ssbo;
	ShaderStorageBuffer<AreaLight> m_area_lights_ssbo;
	// animating lights orbit parameters
	ShaderStorageBuffer<glm::vec4> m_point_lights_orbit_ssbo;
	ShaderStorageBuffer<glm::vec4> m_spot_lights_orbit_ssbo;

    /// Area lights variables
    std::shared_ptr<RGL::Texture2D> m_ltc_amp_lut;
    std::shared_ptr<RGL::Texture2D> m_ltc_mat_lut;

    /* Tonemapping variables */
	RGL::RenderTarget::Texture2d _rt;
	RGL::RenderTarget::Texture2d _pp_half_rt;
	RGL::RenderTarget::Texture2d _pp_full_rt;
	RGL::PP::Tonemapping m_tmo_pp;
	float m_exposure;
	float m_gamma;
	RGL::PP::LightScattering m_scattering_pp;
	RGL::RenderTarget::Texture2d _final_rt;

    float m_background_lod_level;
	// TODO: need to test with actual HDR jpeg xl image (converting from hdr wasn't successful)
	std::string_view m_hdr_maps_names[5] = {
		"../black.hdr",
		"colorful_studio_4k.hdr",
		"phalzer_forest_01_4k.hdr",
		"sunset_fairway_4k.hdr",
		"rogland_clear_night_2k.hdr",
	};
	uint8_t m_current_hdr_map_idx   = 4;

    GLuint m_skybox_vao, m_skybox_vbo;

	RGL::PP::Bloom m_bloom_pp;
	RGL::PP::Blur m_blur_pp;

	seconds_f _running_time { 0 };

	float m_bloom_threshold;
	float m_bloom_knee;
    float m_bloom_intensity;
    float m_bloom_dirt_intensity;
    bool  m_bloom_enabled;

	float m_fog_density;
	float m_fog_falloff_blend;
	float m_ray_march_stride;
	int _ray_march_noise { 0 };  // 0 - 2

	std::chrono::microseconds m_cull_time;
	std::chrono::microseconds m_depth_time;
	std::chrono::microseconds m_cluster_time3;
	std::chrono::microseconds m_cluster_time2;
	std::chrono::microseconds m_cluster_time1;
	std::chrono::microseconds m_lighting_time;
	std::chrono::microseconds m_skybox_time;
	std::chrono::microseconds m_scatter_time;
	// std::chrono::microseconds m_pp_time;

	RGL::GLTimer _gl_timer;
};
