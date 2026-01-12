#pragma once

#include "core_app.h"

#include "camera.h"
#include "sample_window.h"
#include "ssbo.h"
#include "static_object.h"
#include "shader.h"
#include "lights.h"
#include "buffer_binds.h"
#include "rendertarget_2d.h"
#include "rendertarget_cube.h"
#include "gl_timer.h"
#include "pp_bloom.h"
#include "pp_gaussian_blur_fixed.h"
#include "pp_volumetrics.h"
#include "pp_tonemapping.h"
#include "shadow_atlas.h"
#include "light_manager.h"

#include <memory>
#include <vector>

using seconds_f = std::chrono::duration<float, std::ratio<1>>;

namespace
{
    // Convert HSV to RGB:
    // Source: https://en.wikipedia.org/wiki/HSL_and_HSV#From_HSV
    // Retrieved: 28/04/2016
	// @param H Hue         [0, 360)
	// @param S Saturation  [0, 1]
	// @param V Value       [0, 1]
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

enum struct BlendMode
{
	Replace,
	Add,
	Subtract,
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

	void debug_message(GLenum type, std::string_view severity, std::string_view message) const;

private:
	void calculateShadingClusterGrid();
	void prepareClusterBuffers();
	void createLights();
	void updateLightsSSBOs();

	void bindScreenRenderTarget();
	void HdrEquirectangularToCubemap(const std::shared_ptr<RGL::RenderTarget::Cube> & cubemap_rt, const std::shared_ptr<RGL::Texture2D> & m_equirectangular_map);
	void IrradianceConvolution      (const std::shared_ptr<RGL::RenderTarget::Cube> & cubemap_rt);
	void PrefilterEnvCubemap        (const std::shared_ptr<RGL::RenderTarget::Cube>& cubemap_rt);
    void PrecomputeIndirectLight    (const std::filesystem::path & hdri_map_filepath);
	void PrecomputeBRDF             (const std::shared_ptr<RGL::RenderTarget::Texture2d>& rt);
    void GenSkyboxGeometry();

	const std::vector<StaticObject> &cullScene(const RGL::Camera &camera);
	void renderScene(const glm::mat4 &view_projection, RGL::Shader &shader, MaterialCtrl matCtrl=UseMaterials);
	void renderDepth(const glm::mat4 &view_projection, RGL::RenderTarget::Texture2d &target, const glm::ivec4 &rect={0,0,0,0});
	void renderShadowMaps();
	void renderSceneShadow(uint_fast16_t shadow_index, uint32_t shadow_map_inde);
	void renderSceneShading(const RGL::Camera &camera);
	void renderSkybox();
	void renderSurfaceLightGeometry();
	void downloadAffectingLightSet();
	void draw2d(const RGL::Texture &texture, BlendMode mode=BlendMode::Replace); // TODO: move to CoreApp
	void draw2d(const RGL::Texture &source, RGL::RenderTarget::Texture2d &target, BlendMode blend=BlendMode::Replace); // TODO: move to CoreApp
	void draw2d(const RGL::Texture &texture, const glm::uvec2 &top_left, const glm::uvec2 &bottom_right); // TODO: move to CoreApp

	void debugDrawLine(const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec4 &color={1,1,1,1});
	void debugDrawLine(const glm::uvec2 &p1, const glm::uvec2 &p2, const glm::vec4 &color={1,1,1,1}, float thickness=1.f);
	void debugDrawRect(const glm::uvec2 &top_left, const glm::uvec2 &size, const glm::vec4 &color, float thickness);
	void debugDrawNumber(uint32_t number, const glm::uvec2 &bottom_left, float height=20.f, const glm::vec4 &color=glm::vec4(1), float thickness=1.f);
	void debugDrawSphere(const glm::vec3 &center, float radius, const glm::vec4 &color={1,1,1,1});
	void debugDrawSphere(const glm::vec3 &center, float radius, size_t stacks, size_t slices, const glm::vec4 &color={1,1,1,1});
	void debugDrawIcon(const glm::vec3 &position, const RGL::Texture2D &icon, float size=20.f, const glm::vec4 &color={1,1,1,1});
	void debugDrawSpotLight(const GPULight &light, const glm::vec4 &color={1,1,1,1});
	void debugDrawSceneBounds();
	void debugDrawLightMarkers();
	void debugDrawClusterGrid();

	RGL::Camera m_camera;
	float m_camera_fov { 80.f };

	std::shared_ptr<RGL::RenderTarget::Cube>   m_env_cubemap_rt;
	std::shared_ptr<RGL::RenderTarget::Cube>   m_irradiance_cubemap_rt;
	std::shared_ptr<RGL::RenderTarget::Cube>   m_prefiltered_env_map_rt;
	std::shared_ptr<RGL::RenderTarget::Texture2d> m_brdf_lut_rt;
	// RGL::RenderTarget::Texture2d _shadow_atlas;
	RGL::ShadowAtlas _shadow_atlas;

    std::shared_ptr<RGL::Shader> m_equirectangular_to_cubemap_shader;
    std::shared_ptr<RGL::Shader> m_irradiance_convolution_shader;
    std::shared_ptr<RGL::Shader> m_prefilter_env_map_shader;
    std::shared_ptr<RGL::Shader> m_precompute_brdf;
    std::shared_ptr<RGL::Shader> m_background_shader;

    /// Clustered shading variables.
    std::shared_ptr<RGL::Shader> m_depth_prepass_shader;
    std::shared_ptr<RGL::Shader> m_generate_clusters_shader;
	std::shared_ptr<RGL::Shader> m_find_nonempty_clusters_shader;
	std::shared_ptr<RGL::Shader> m_collect_nonempty_clusters_shader;
    std::shared_ptr<RGL::Shader> m_cull_lights_shader;
    std::shared_ptr<RGL::Shader> m_clustered_pbr_shader;
	std::shared_ptr<RGL::Shader> m_shadow_depth_shader;

	std::shared_ptr<RGL::Shader> m_surface_lights_shader;
	std::shared_ptr<RGL::Shader> m_line_draw_shader;
	std::shared_ptr<RGL::Shader> m_2d_line_shader;
	std::shared_ptr<RGL::Shader> m_2d_rect_shader;
	std::shared_ptr<RGL::Shader> m_2d_7segment_shader;
	std::shared_ptr<RGL::Shader> m_icon_shader;
	std::shared_ptr<RGL::Shader> m_imgui_depth_texture_shader;
	std::shared_ptr<RGL::Shader> m_imgui_3d_texture_shader;
	std::shared_ptr<RGL::Shader> m_fsq_shader;

	// GLuint m_depth_tex2D_id;
	// GLuint m_depth_pass_fbo_id;
	RGL::RenderTarget::Texture2d m_depth_pass_rt;

	GLuint _empty_vao;

	uint32_t   m_cluster_block_size;      // The size of a cluster in screen space.(pixels, x-axis)
	glm::uvec3 m_cluster_resolution;             // 3D dimensions of the cluster grid.
	float      m_near_k;                       // ( 1 + ( 2 * tan( fov * 0.5 ) / ClusterGridDim.y ) ) // Used to compute the near plane for clusters at depth k.
	float      m_log_cluster_res_y;               // 1.0f / log( NearK )  // Used to compute the k index of the cluster from the view depth of a pixel sample.
	uint32_t   m_cluster_count { 0 };


	bool  m_debug_cluster_geom           = false;
	bool  m_debug_clusters_occupancy     = false;
	bool  m_debug_tile_occupancy         = false;
	float m_debug_coverlay_blend         = 0.7f;
	bool _debug_csm_colorize_cascades    = false;

	float m_shadow_bias_constant         = 0.0001f; // 0.0006
	float m_shadow_bias_slope_scale      = 0.01f;
	float m_shadow_bias_slope_power      = 0.02f;
	float m_shadow_bias_distance_scale   = 0.0021f;
	float m_shadow_bias_scale            = -0.3f;
	float m_shadow_bias_texel_size_mix   = 0.f;

	bool      m_animate_lights             = false;
	float     m_animation_speed            = 0.4f;
	bool      m_rect_lights_two_sided      = true;
	bool      m_draw_surface_lights_geometry  = true;

	bool      m_debug_draw_aabb            = false;
	bool      m_debug_draw_light_markers   = false;
	bool      m_debug_draw_cluster_grid    = false;
	GLuint    m_debug_draw_vbo             = 0;


	std::vector<StaticObject> _scene;  // TODO: Scene _scene;
	std::vector<StaticObject> _scenePvs;  // potentially visible set
	std::vector<LightIndex>   _lightsPvs;  // basically all lights within theoretical range
	std::vector<StaticObject> _surfaceLightModels;

	RGL::buffer::Storage<AABB>       m_cluster_aabb_ssbo;
	RGL::buffer::Storage<uint>       m_cluster_discovery_ssbo;
	RGL::buffer::Storage<glm::uvec3> m_cull_lights_args_ssbo;
	RGL::buffer::Storage<IndexRange> m_cluster_light_ranges_ssbo;
	RGL::buffer::Storage<uint>       m_cluster_all_lights_index_ssbo;
	RGL::buffer::ReadBack<uint, 32>  m_affecting_lights_bitfield_ssbo;
	dense_set<uint>                  _affecting_lights;
	RGL::buffer::Storage<uint>       _relevant_lights_index_ssbo;
	RGL::buffer::Mapped<ShadowSlotInfo, MAX_POINT_LIGHTS + MAX_SPOT_LIGHTS + MAX_RECT_LIGHTS> m_shadow_map_slots_ssbo;
	RGL::LightManager _light_mgr;

    /// Rect lights variables
    std::shared_ptr<RGL::Texture2D> m_ltc_amp_lut;
    std::shared_ptr<RGL::Texture2D> m_ltc_mat_lut;

	// Tonemapping variables
	RGL::RenderTarget::Texture2d _rt;
	RGL::RenderTarget::Texture2d _pp_low_rt;
	RGL::RenderTarget::Texture2d _pp_full_rt;
	RGL::PP::Tonemapping m_tmo_pp;
	float m_exposure;
	float m_gamma;
	RGL::PP::Volumetrics m_volumetrics_pp;
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
	uint8_t m_current_hdr_map_idx = 0;

    GLuint m_skybox_vao, m_skybox_vbo;

	RGL::PP::Bloom m_bloom_pp;
	RGL::PP::BlurFixed<3.f> m_blur3_pp;

	seconds_f _running_time { 0 };

	float m_bloom_threshold;
	float m_bloom_knee;
    float m_bloom_intensity;
    float m_bloom_dirt_intensity;
    bool  m_bloom_enabled;

	bool  _fog_enabled;
	float _fog_strength;
	float _fog_density;
	float _fog_blend_weight;

	SampleWindow<std::chrono::microseconds, 30> m_cull_scene_time;
	SampleWindow<std::chrono::microseconds, 30> m_depth_time;
	SampleWindow<std::chrono::microseconds, 30> m_cluster_find_time;
	SampleWindow<std::chrono::microseconds, 30> m_cluster_index_time;
	SampleWindow<std::chrono::microseconds, 30> m_light_cull_time;
	SampleWindow<std::chrono::microseconds, 30> m_shadow_alloc_time;
	SampleWindow<std::chrono::microseconds, 30> m_shadow_time;
	SampleWindow<std::chrono::microseconds, 30> m_shading_time;
	SampleWindow<std::chrono::microseconds, 30> m_skybox_time;
	SampleWindow<std::chrono::microseconds, 30> m_volumetrics_cull_time;
	SampleWindow<std::chrono::microseconds, 30> m_volumetrics_inject_time;
	SampleWindow<std::chrono::microseconds, 30> m_volumetrics_accum_time;
	SampleWindow<std::chrono::microseconds, 30> m_volumetrics_render_time;
	SampleWindow<std::chrono::microseconds, 30> m_tonemap_time;
	SampleWindow<std::chrono::microseconds, 30> m_debug_draw_time;

	// SampleWindow<std::chrono::microseconds, 30> m_pp_blur_time;
	size_t _shadow_atlas_slots_rendered;
	size_t _light_shadow_maps_rendered;

	RGL::GLTimer _gl_timer;

	RGL::Texture2DArray _light_icons;
};

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
