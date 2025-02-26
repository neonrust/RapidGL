#include "window.h"
#include "common.h"
#include "input.h"
#include "gui/gui.h"

namespace RGL
{
    GLFWwindow * Window::m_window          = nullptr;
    std::string  Window::m_title           = "";
    glm::mat4    Window::m_viewport_matrix = glm::mat4(1.0);
    glm::ivec2   Window::m_window_pos      = glm::ivec2(0);
	glm::uvec2   Window::m_window_size     = glm::uvec2(0);
	glm::uvec2   Window::m_viewport_size   = glm::uvec2(0);

    Window::Window()
    {
    }

    Window::~Window()
    {
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }

    void Window::createWindow(unsigned int width, unsigned int height, const std::string & title)
    {
        m_title       = title;
        m_window_size = glm::ivec2(width, height);

        if (!glfwInit())
        {
			std::fprintf(stderr, "ERROR: Could not initialize GLFW.\n");
            exit(EXIT_FAILURE);
        }

        glfwSetErrorCallback(error_callback);

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, MIN_GL_VERSION_MAJOR);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, MIN_GL_VERSION_MINOR);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_SAMPLES, 4);

        #ifdef _DEBUG
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
        #endif

		m_window = glfwCreateWindow(int(m_window_size.x), int(m_window_size.y), title.c_str(), nullptr, nullptr);

        if (!m_window)
        {
			std::fprintf(stderr,"ERROR: Could not create window and OpenGL context.\n");

            glfwTerminate();
            exit(EXIT_FAILURE);
        }

        glfwMakeContextCurrent(m_window);

        /* Initialize GLAD */
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
			std::fprintf(stderr,"ERROR: Could not initialize GLAD.\n");
            exit(EXIT_FAILURE);
        }

        #ifdef _DEBUG
        GLint flags;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);

        if (flags & GL_CONTEXT_FLAG_DEBUG_BIT)
        {
            /* Create OpenGL debug context */
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

            /* Register callback */
            glDebugMessageCallback(DebugOutputGL::GLerrorCallback, nullptr /*userParam*/);
            glDebugMessageControl(GL_DONT_CARE /*source*/, GL_DONT_CARE /*type*/, GL_DEBUG_SEVERITY_MEDIUM /*severity*/, 0 /*count*/, nullptr /*ids*/, GL_TRUE /*enabled*/);
        }
        #endif

        const GLubyte* vendor_name    = glGetString(GL_VENDOR);
        const GLubyte* renderer_name  = glGetString(GL_RENDERER);
        const GLubyte* driver_version = glGetString(GL_VERSION);
        const GLubyte* glsl_version   = glGetString(GL_SHADING_LANGUAGE_VERSION);

		std::printf("%s %s\nDriver: %s\nGLSL Version: %s\n", vendor_name, renderer_name, driver_version, glsl_version);

		{
			int groupCounts[3];
			for(auto idx = 0u; idx < 3; ++idx)
				glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, idx, &groupCounts[idx]);
			int groupSizes[3];
			for(auto idx = 0u; idx < 3; ++idx)
				glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, idx, &groupSizes[idx]);
			int invocations;
			glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &invocations);

			std::printf("Compute shader work group limits:\n");
			std::printf("   Counts:      %d x %d x %d\n", groupCounts[0], groupCounts[1], groupCounts[2]);
			std::printf("   Sizes:       %d x %d x %d\n", groupSizes[0], groupSizes[1], groupSizes[2]);
			std::printf("   Invocations: %d\n", invocations);
		}


        /* Set the viewport */
        glfwGetWindowPos(m_window, &m_window_pos.x, &m_window_pos.y);
		glfwGetFramebufferSize(m_window, (int *)&m_viewport_size.x, (int *)&m_viewport_size.y);

		glViewport(0, 0, GLsizei(m_viewport_size.x), GLsizei(m_viewport_size.y));
        setViewportMatrix(m_viewport_size.x, m_viewport_size.y);

        setVSync(false);
        glfwSetFramebufferSizeCallback(m_window, framebuffer_size_callback);

        /* Init Input & GUI */
        Input::init(m_window);
        GUI::init(m_window);

		std::printf("--------------------------------------------------\n\n");
    }

    void Window::endFrame()
    {
        glfwPollEvents();
        glfwSwapBuffers(m_window);
    }

    int Window::isCloseRequested()
    {
        return glfwWindowShouldClose(m_window);
    }

	size_t Window::width()
    {
        return m_viewport_size.x;
    }

	size_t Window::height()
    {
        return m_viewport_size.y;
    }
	
	glm::uvec2 Window::center()
    {
        return glm::vec2(m_viewport_size) / 2.0f;
    }
	
	glm::uvec2 Window::size()
    {
        return m_viewport_size;
    }

    float Window::aspectRatio()
    {
        return float(m_viewport_size.x) / float(m_viewport_size.y);
    }

    glm::mat4 Window::viewportMatrix()
    {
        return m_viewport_matrix;
    }

    const std::string& Window::getTitle()
    {
        return m_title;
    }

    void Window::setVSync(bool enabled)
    {
        auto value = enabled ? true : false;

        glfwSwapInterval(value);
    }

    void Window::bindDefaultFramebuffer()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, GLsizei(m_viewport_size.x), GLsizei(m_viewport_size.y));
	}

	void Window::setViewportMatrix(size_t width, size_t height)
    {
		float w2 = float(width) / 2.f;
		float h2 = float(height) / 2.f;

        m_viewport_matrix = glm::mat4(glm::vec4(w2,  0.0, 0.0, 0.0), 
                                      glm::vec4(0.0, h2,  0.0, 0.0), 
                                      glm::vec4(0.0, 0.0, 1.0, 0.0), 
                                      glm::vec4(w2,  h2,  0.0, 1.0));
    }

	void Window::framebuffer_size_callback(GLFWwindow *, int width, int height)
    {
        m_viewport_size = { width, height };

		glViewport(0, 0, GLsizei(m_viewport_size.x), GLsizei(m_viewport_size.y));
        setViewportMatrix(m_viewport_size.x, m_viewport_size.y);

		m_window_size = { size_t(width), size_t(height) };

        GUI::updateWindowSize(float(m_viewport_size.x), float(m_viewport_size.y));
    }
}
