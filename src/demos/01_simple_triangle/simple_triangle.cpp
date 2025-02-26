#include "simple_triangle.h"
#include "filesystem.h"
#include "input.h"
#include "gui/gui.h"
#include "glm/gtc/constants.hpp"

SimpleTriangle::SimpleTriangle()
    : m_vao_id(0),
      m_vbo_id(0),
      m_triangle_color(1.0, 0.5, 0.2),
      m_triangle_translation(0.0, 0.0)
{
}

SimpleTriangle::~SimpleTriangle()
{
}

void SimpleTriangle::init_app()
{
    glClearColor(0.5, 0.5, 0.5, 1.0);
    glEnable(GL_MULTISAMPLE);

    float vertices[] = { 
                         -0.5f, -0.5f, 0.0f, 
                          0.5f, -0.5f, 0.0f,
                          0.0f,  0.5f, 0.0f
                       };

    glGenVertexArrays(1, &m_vao_id);
    glBindVertexArray(m_vao_id);

    glGenBuffers(1, &m_vbo_id);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo_id);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(vertices[0]), nullptr);
    glEnableVertexAttribArray(0);

    std::string dir = "src/demos/01_simple_triangle/";
    m_shader = std::make_shared<RGL::Shader>(dir + "simple_triangle.vert", dir + "simple_triangle.frag");
    m_shader->link();
}

void SimpleTriangle::input()
{
    if (RGL::Input::wasKeyReleased(RGL::KeyCode::Escape))
    {
        stop();
    }

    if (RGL::Input::wasKeyReleased(RGL::KeyCode::F1))
    {
        std::string filename = "01_simple_triangle";
        if (take_screenshot_png(filename, RGL::Window::width() / 2.0, RGL::Window::height() / 2.0))
        {
            std::cout << "Saved " << filename << ".png to " << RGL::FileSystem::rootPath() / "screenshots/" << std::endl;
        }
        else
        {
            std::cerr << "Could not save " << filename << ".png to " << RGL::FileSystem::rootPath() / "screenshots/" << std::endl;
        }
    }
}

void SimpleTriangle::update(double delta_time)
{
}

void SimpleTriangle::render()
{
    glClear(GL_COLOR_BUFFER_BIT);

    m_shader->bind();
    m_shader->setUniform("triangle_color",       m_triangle_color);
    m_shader->setUniform("triangle_translation", m_triangle_translation);

    glBindVertexArray(m_vao_id);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void SimpleTriangle::render_gui()
{
    CoreApp::render_gui();

    ImVec2 window_pos       = ImVec2(10.0, 150.0);
    ImVec2 window_pos_pivot = ImVec2(0.0f, 1.0f);

    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    ImGui::SetNextWindowSize({ 250, 0 });

    ImGui::Begin("Triangle Position/Color");
    {
        static float translation[] = { 0.0, 0.0 };
        ImGui::SliderFloat2("position", translation, -1.0, 1.0);
        m_triangle_translation = glm::vec2(translation[0], translation[1]);

        static float color[3] = { m_triangle_color.r, m_triangle_color.g, m_triangle_color.b };
        ImGui::ColorEdit3("color", color);
        m_triangle_color = glm::vec3(color[0], color[1], color[2]);
    }
    ImGui::End();
}
