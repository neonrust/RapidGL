#include "window.h"
#include "common.h"
#include "input.h"
#include "gui/gui.h"
#include "log.h"

namespace RGL
{

static void error_callback([[maybe_unused]] int error, const char *message)
{
	Log::error("GLFW Error {}: {}", error, message);
}


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

	if(not glfwInit())
	{
		Log::error("Could not initialize GLFW");
		std::exit(EXIT_FAILURE);
	}

	glfwSetErrorCallback(error_callback);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, MIN_GL_VERSION_MAJOR);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, MIN_GL_VERSION_MINOR);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	glfwWindowHint(GLFW_SAMPLES, 4);

#ifdef _DEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif

	auto screen_w = 0;
	auto screen_h = 0;

	{
		int num_monitors { 0 };
		auto *monitors = glfwGetMonitors(&num_monitors);
		for(auto idx = 0; idx < num_monitors; ++idx)
		{
			auto *monitor = monitors[idx];
			const auto is_primary = monitor == glfwGetPrimaryMonitor();

			int mm_width { 0 }, mm_height { 0 };
			glfwGetMonitorPhysicalSize(monitor, &mm_width, &mm_height);

			const auto *mode = glfwGetVideoMode(monitor);
			const auto bits = mode->redBits + mode->greenBits + mode->blueBits;

			Log::debug("Monitor {}: '{}' {}x{} @ {} Hz  ({}x{} mm){}", idx, glfwGetMonitorName(monitor),
					   mode->width, mode->height, bits, mode->refreshRate,
					   mm_width, mm_height,
					   is_primary?"  [primary]": "");

			if(is_primary)
			{
				screen_w = mode->width;
				screen_h = mode->height;
			}
		}
		// glfwSetMonitorCallback()
		// glfwSetGamma()
		// glfwSetGammaRamp()
	}

	GLFWmonitor *fullscreenMonitor = nullptr;// glfwGetPrimaryMonitor();

	m_window = glfwCreateWindow(int(m_window_size.x), int(m_window_size.y), title.c_str(), fullscreenMonitor, nullptr);

	if(not m_window)
	{
		Log::error("Could not create window with OpenGL context");

		glfwTerminate();
		exit(EXIT_FAILURE);
	}

	// always open the window in a predictable position (if not full screen)
	glfwSetWindowPos(m_window, screen_w - int(m_window_size.x) - 4, 40);

	glfwMakeContextCurrent(m_window);

	/* Initialize GLAD */
	if(not gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		Log::error("Could not initialize GLAD");
		exit(EXIT_FAILURE);
	}

	const GLubyte* vendor_name    = glGetString(GL_VENDOR);
	const GLubyte* renderer_name  = glGetString(GL_RENDERER);
	const GLubyte* driver_version = glGetString(GL_VERSION);
	const GLubyte* glsl_version   = glGetString(GL_SHADING_LANGUAGE_VERSION);

	Log::info("{} {}", (const char *)vendor_name, (const char *)renderer_name);
	Log::info("Driver: {}", (const char *)driver_version);
	Log::info("GLSL Version: {}", (const char *)glsl_version);

	{
		int groupCounts[3];
		for(auto idx = 0u; idx < 3; ++idx)
			glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, idx, &groupCounts[idx]);
		int groupSizes[3];
		for(auto idx = 0u; idx < 3; ++idx)
			glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, idx, &groupSizes[idx]);
		int invocations;
		glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &invocations);

		Log::info("Compute shader work group limits:");
		Log::info("   Counts:      {} x {} x {}", groupCounts[0], groupCounts[1], groupCounts[2]);
		Log::info("   Sizes:       {} x {} x {}", groupSizes[0], groupSizes[1], groupSizes[2]);
		Log::info("   Invocations: {}", invocations);

	}
	{
		GLint max_size = 0;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
		GLint max_size3d = 0;
		glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max_size3d);
		Log::info("Max texture size: {}  3D: {}", max_size, max_size3d);
	}
	{
		GLint max_attrs;
		glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_attrs);
		Log::info("Max vertex shader attributes: {}", max_attrs);
	}


	/* Set the viewport */
	glfwGetWindowPos(m_window, &m_window_pos.x, &m_window_pos.y);
	glfwGetFramebufferSize(m_window, (int *)&m_viewport_size.x, (int *)&m_viewport_size.y);

	glViewport(0, 0, GLsizei(m_viewport_size.x), GLsizei(m_viewport_size.y));
	setViewportMatrix(m_viewport_size.x, m_viewport_size.y);

	setVSync(false);
	glfwSetFramebufferSizeCallback(m_window, framebuffer_size_callback);

	// Init Input & GUI
	Input::init(m_window);
	GUI::init(m_window);

	Log::info("--------------------------------------------------");
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

} // RGL
