#pragma once
#include "core_app.h"

#include "camera.h"
#include "model.h"
#include "shader.h"

#include <memory>
#include <vector>

class ProceduralNoise : public RGL::CoreApp
{
public:
    ProceduralNoise();
    ~ProceduralNoise();

    void init_app()                override;
    void input()                   override;
    void update(double delta_time) override;
    void render()                  override;
    void render_gui()               override;

private:
    RGL::Texture gen_perlin_data(uint32_t width, uint32_t height, float base_frequency = 4.0f, float persistance = 0.5f, bool periodic = true);

    std::shared_ptr<RGL::Camera> m_camera;
    std::shared_ptr<RGL::Shader> m_noise_texturing_shader;

    std::vector<std::shared_ptr<RGL::Model>> m_objects;
    std::vector<glm::mat4> m_objects_model_matrices;

    // Cloud
    glm::vec3 m_sky_color;
    glm::vec3 m_cloud_color;

    // Wood grain
    glm::vec3 m_dark_wood_color;
    glm::vec3 m_light_wood_color;
    glm::mat4 m_slice_matrix;

    // Disintegration
    float m_low_threshold;
    float m_high_threshold;
};