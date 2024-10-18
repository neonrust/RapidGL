#include "core_app.h"

#include <vector>
#include <chrono>
using namespace std::chrono;

#include "stb_image_write.h"
#include "stb_image_resize.h"

#include "filesystem.h"
#include "input.h"
#include "timer.h"
#include "window.h"

#include "gui/gui.h"

namespace RGL
{
    CoreApp::CoreApp()
        : m_frame_time(0.0),
          m_fps       (0),
          m_is_running(false)
    {
    }

    CoreApp::~CoreApp()
    {
    }

    void CoreApp::init(unsigned int width, unsigned int height, const std::string & title, double framerate)
    {
        m_frame_time = 1.0 / framerate;

        /* Init window */
        Window::createWindow(width, height, title);

        init_app();
    }

    void CoreApp::render_gui()
    {
        /* Overlay start */
        const float DISTANCE = 10.0f;
        static int corner = 0;
        ImVec2 window_pos = ImVec2((corner & 1) ? ImGui::GetIO().DisplaySize.x - DISTANCE : DISTANCE, (corner & 2) ? ImGui::GetIO().DisplaySize.y - DISTANCE : DISTANCE);
        ImVec2 window_pos_pivot = ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f);
        if (corner != -1)
        {
            ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
            ImGui::SetNextWindowSize({ 250, 0 });
        }

        ImGui::SetNextWindowBgAlpha(0.3f); // Transparent background
        if (ImGui::Begin("Perf info", 0, (corner != -1 ? ImGuiWindowFlags_NoMove : 0) | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
        {
            ImGui::Text("Performance info\n");
            ImGui::Separator();
			ImGui::Text("%.1f FPS (%.3f ms/frame)",	ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
			ImGui::Text("r-time: %ld us (~%ld FPS)", _render_time.count(), 1'000'000 / _render_time.count());
        }
        ImGui::End();
        /* Overlay end */
    }

    unsigned int CoreApp::get_fps() const
    {
        return m_fps;
    }

    void CoreApp::start()
    {
        if (m_is_running)
        {
            return;
        }

        run();
    }

    void CoreApp::stop()
    {
        if (!m_is_running)
        {
            return;
        }

        m_is_running = false;
    }

    bool CoreApp::take_screenshot_png(const std::string & filename, size_t dst_width, size_t dst_height)
    {
		auto width  = Window::getWidth();
		auto height = Window::getHeight();
        bool   resize = true;

        if (dst_width == 0 || dst_height == 0)
        {
            resize = false;
        }

        std::vector<uint8_t> image;
        image.resize(width * height * 3);

        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, image.data());

        if (resize)
        {
            auto resized_image = image;
            stbir_resize_uint8(image.data(), width, height, 0, resized_image.data(), dst_width, dst_height, 0, 3);

            width  = dst_width;
            height = dst_height;
            image  = resized_image;
        }

        auto screenshots_dir = FileSystem::getRootPath() / "screenshots";
        if (!FileSystem::directoryExists(screenshots_dir))
        {
            FileSystem::createDirectory(screenshots_dir);
        }

        auto filepath = screenshots_dir / filename;
        filepath += ".png";

        stbi_flip_vertically_on_write(true);
        auto ret = stbi_write_png(filepath.string().c_str(), width, height, 3, image.data(), 0);

        return ret;
    }

    void CoreApp::run()
    {
        m_is_running = true;

        int frames = 0;
        double frame_counter = 0.0;

        double last_time = Timer::getTime();
        double unprocessed_time = 0.0;

        double start_time = 0.0;
        double passed_time = 0.0;

        bool should_render = false;

        while (m_is_running)
        {
            should_render = false;

            start_time = Timer::getTime();
            passed_time = start_time - last_time;

            last_time = start_time;

            unprocessed_time += passed_time;
            frame_counter += passed_time;

			// don't render until we've accumulated enough "frame time debt"  (as requested to init())
            while (unprocessed_time > m_frame_time)
            {
                should_render = true;

                unprocessed_time -= m_frame_time;

                if (Window::isCloseRequested())
                {
                    m_is_running = false;
                }

                /* Update input, game entities, etc. */
                input();
                update(m_frame_time);
                Input::update();

                if (frame_counter >= 1.0)
                {
					m_fps = uint32_t(1000.0 / double(frames));

                    frames = 0;
                    frame_counter = 0;
                }
            }

            if (should_render)
            {
                /* Render */
				const auto T0 = steady_clock::now();
				render();
				_render_time = duration_cast<microseconds>(steady_clock::now() - T0);

                GUI::prepare();
                {
                    render_gui();
                }
                GUI::render();

                Window::endFrame();
                frames++;
            }
        }
    }
}
