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

		static size_t     width();
		static size_t     height();
		static glm::uvec2 center();
		static glm::uvec2 size();
		static float      aspectRatio();
		static glm::mat4  viewportMatrix();

        static const std::string & getTitle();

        static void setVSync(bool enabled);
        static void bindDefaultFramebuffer();

    private:
        static GLFWwindow * m_window;
        static std::string  m_title;
        static glm::mat4    m_viewport_matrix;
        static glm::ivec2   m_window_pos;
		static glm::uvec2   m_window_size;
		static glm::uvec2   m_viewport_size;

		static void setViewportMatrix(size_t width, size_t height);

		static void error_callback([[maybe_unused]] int error, const char* description)
        {
            std::cerr << description << std::endl;
        }

        static void framebuffer_size_callback(GLFWwindow * window, int width, int height);
    };
}
