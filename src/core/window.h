#pragma once

#include <iostream>
#include <string>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>
#include <glm/mat4x4.hpp>

namespace RGL
{
    class Window final
    {
    public:
        Window();
        ~Window();

        static void createWindow(unsigned int width, unsigned int height, const std::string & title);
        static void endFrame();

        static int isCloseRequested();

        static int       getWidth();
        static int       getHeight();
        static glm::vec2 getCenter();
        static glm::vec2 getSize();
        static float     aspectRatio();
        static glm::mat4 getViewportMatrix();

        static const std::string & getTitle();

        static void setVSync(bool enabled);
        static void bindDefaultFramebuffer();

    private:
        static GLFWwindow * m_window;
        static std::string  m_title;
        static glm::mat4    m_viewport_matrix;
        static glm::ivec2   m_window_pos;
        static glm::ivec2   m_window_size;
        static glm::ivec2   m_viewport_size;

        static void setViewportMatrix(int width, int height);

		static void error_callback([[maybe_unused]] int error, const char* description)
        {
            std::cerr << description << std::endl;
        }

        static void framebuffer_size_callback(GLFWwindow * window, int width, int height);
    };
}
