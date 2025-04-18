#pragma once
#include "core_app.h"

#include "camera.h"
#include "static_model.h"
#include "shader.h"

#include <memory>
#include <vector>

class ToonOutline : public RGL::CoreApp
{
public:
    ToonOutline();
    ~ToonOutline();

    void init_app()                override;
    void input()                   override;
    void update(double delta_time) override;
    void render()                  override;
    void render_gui()              override;

private:
    void render_toon_shaded_objects();
    void stencil_outline();
    void ps_outline();

    glm::vec3 calcDirection(const glm::vec2 & azimuth_elevation_angles)
    {
        float az = glm::radians(azimuth_elevation_angles.x);
        float el = glm::radians(azimuth_elevation_angles.y);

        glm::vec3 direction(0.0);

        direction.x = glm::sin(el) * glm::cos(az);
        direction.y = glm::cos(el);
        direction.z = glm::sin(el) * glm::sin(az);

        direction = glm::normalize(-direction);

        return direction;
    }

    std::shared_ptr<RGL::Camera> m_camera;
    std::shared_ptr<RGL::Shader> m_simple_toon_shader;
    std::shared_ptr<RGL::Shader> m_advanced_toon_shader;
    std::shared_ptr<RGL::Shader> m_simple_rim_toon_shader;
    std::shared_ptr<RGL::Shader> m_toon_twin_shade_shader;

    std::vector<RGL::StaticModel> m_objects;
    std::vector<glm::mat4> m_objects_model_matrices;
    std::vector<glm::vec3> m_objects_colors;

    glm::vec3 m_light_color;
    glm::vec3 m_light_direction;
    glm::vec2 m_dir_light_azimuth_elevation_angles;
    float m_light_intensity;
    float m_ambient_factor;

    float m_specular_power;
    float m_specular_intensity;
    float m_gamma;

    enum class ToonShadingMethod { SIMPLE, ADVANCED, SIMPLE_RIM, TWIN_SHADE} m_toon_shading_method;
    std::vector<std::string> m_toon_shading_methods_names;
    std::vector<std::shared_ptr<RGL::Shader>> m_toon_shaders;

    /* Advanced toon properties */
    float m_advanced_toon_A;
    float m_advanced_toon_B;
    float m_advanced_toon_C;
    float m_advanced_toon_D;

    /* Simple toon properties */
    float m_simple_toon_diffuse_levels;
    float m_simple_toon_specular_levels;

    /* Simple toon rim properties */
    glm::vec3 m_rim_color;
    float m_rim_threshold;
    float m_rim_amount;

    /* Twin shade toon properties */
    float m_twin_shade_toon_diffuse_levels;
    float m_twin_shade_toon_specular_levels;
    float m_twin_shade_light_shade_cutoff;
    float m_twin_shade_dark_shade_cutoff;

    /* Stencil outline properties */
    enum class OutlineMethod { STENCIL, POSTPROCESS} m_outline_method;
    std::vector<std::string> m_outline_methods_names;

    glm::vec3 m_outline_color;
    float m_stencil_outline_width;

    std::shared_ptr<RGL::Shader> m_stencil_outline_shader;

    /* GL objects for outlines as a postprocess effect */
    GLuint m_fbo_normal_depth;
    GLuint m_fbo_shading;
    GLuint m_rbo;
    GLuint m_normals_depth_tex_buffer;
    GLuint m_shading_tex_buffer;
    GLuint m_ps_vao_id;

    std::shared_ptr<RGL::Shader> m_generate_data_outline_shader;
    std::shared_ptr<RGL::Shader> m_outline_ps_shader;

    float m_depth_threshold;
    float m_depth_normal_threshold;
    float m_depth_normal_threshold_scale;
    float m_normal_threshold;
    float m_ps_outline_width;
};