#include "core_app.h"

#include <vector>
#include <array>
#include <algorithm>
#include <chrono>
using namespace std::chrono;

#include "stb_image_write.h"
#include "stb_image_resize2.h"

#include "filesystem.h"
#include "input.h"
#include "timer.h"
#include "window.h"
#include "log.h"

#include "gui/gui.h"  // IWYU pragma: keep
#include <implot.h>

namespace RGL
{
CoreApp::CoreApp():
	m_frame_time(0.0),
	_fps       (0),
	_is_running(false)
{
#if defined(_DEBUG)
	Log::set_level(Log::DEBUG);
	Log::set_file(fs::current_path() / "app.log", true);
#endif
}

void CoreApp::init(unsigned int width, unsigned int height, const std::string & title, double framerate)
{
	m_frame_time = 1.0 / framerate;

	Window::createWindow(width, height, title);

	init_app();
}

void CoreApp::render_gui()
{
	const float DISTANCE = 10.0f;
	static int corner = 0;
	ImVec2 window_pos = ImVec2((corner & 1) ? ImGui::GetIO().DisplaySize.x - DISTANCE : DISTANCE, (corner & 2) ? ImGui::GetIO().DisplaySize.y - DISTANCE : DISTANCE);
	ImVec2 window_pos_pivot = ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f);
	if (corner != -1)
	{
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
		ImGui::SetNextWindowSize({ 250, 0 });
	}

	ImGui::SetNextWindowBgAlpha(0.3f);

	if (ImGui::Begin("Perf info", 0, (corner != -1 ? ImGuiWindowFlags_NoMove : 0) | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		ImGui::Text("%.1f FPS (%.3f ms/frame)",	ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
		ImGui::Text("r-time: %ld µs (~%ld FPS)", _render_time.average().count(), 1'000'000 / (_render_time.average().count() + 1)); // +1 to just avoid DBZ

		// recent render times, oldest to the left
		if(ImPlot::BeginPlot("##render_time", ImVec2(-1, 60), ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs | ImPlotFlags_NoFrame))
		{
			// y-axis: the smallest of these ranges that fits the highest sample (higher samples are clipped)
			static constexpr std::array y_ranges { 5.0, 10.0, 20.0, 50.0 };
			const auto max_ms = double(_render_time.max().count()) / 1000.0;
			const auto fits = std::ranges::find_if(y_ranges, [max_ms](double range) { return max_ms <= range; });
			const auto y_max = fits != y_ranges.end() ? *fits : y_ranges.back();

			ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_AutoFit);
			ImPlot::SetupAxis(ImAxis_Y1, nullptr);
			ImPlot::SetupAxisLimits(ImAxis_Y1, 0, y_max, ImPlotCond_Always);
			ImPlot::SetupAxisTicks(ImAxis_Y1, 0, y_max, 3);  // bottom, middle & top
			ImPlot::SetupAxisFormat(ImAxis_Y1, "%g ms");

			const auto sample = [](int index, void *data) -> ImPlotPoint {
				const auto &render_time = *static_cast<const decltype(_render_time) *>(data);
				return { double(index), double(render_time[size_t(index)].count()) / 1000.0 };
			};
			ImPlot::PlotLineG("r-time", sample, &_render_time, int(_render_time.num_samples()));

			ImPlot::EndPlot();
		}
	}
	ImGui::End();
}

unsigned int CoreApp::get_fps() const
{
	return _fps;
}

int CoreApp::run()
{
	if (_is_running)
		return 0;

	return run_app();
}

void CoreApp::stop()
{
	if (!_is_running)
	{
		return;
	}

	_is_running = false;
}

bool CoreApp::take_screenshot(const std::string & filename)
{
	const auto width  = Window::width();
	const auto height = Window::height();

	std::vector<uint8_t> image;
	image.resize(width * height * 3);

	glReadPixels(0, 0, GLsizei(width), GLsizei(height), GL_RGB, GL_UNSIGNED_BYTE, image.data());

	auto screenshots_dir = FileSystem::rootPath() / "screenshots";
	if (!FileSystem::directoryExists(screenshots_dir))
	{
		FileSystem::createDirectory(screenshots_dir);
	}

	auto filepath = screenshots_dir / filename;
	filepath += ".png";

	stbi_flip_vertically_on_write(true);
	auto ret = stbi_write_png(filepath.string().c_str(), GLsizei(width), GLsizei(height), 3, image.data(), 0);

	return ret;
}

int CoreApp::run_app()
{
	_is_running = true;

	int frames = 0;
	double frame_counter = 0.0;

	double last_time = Timer::getTime();
	double unprocessed_time = 0.0;

	double start_time = 0.0;
	double passed_time = 0.0;

	bool should_render = false;

	steady_clock::time_point last_frame_time = steady_clock::now();

	while (_is_running)
	{
		should_render = false;

		start_time = Timer::getTime();
		passed_time = start_time - last_time;

		last_time = start_time;

		unprocessed_time += passed_time;
		frame_counter += passed_time;

		// don't render until we've accumulated enough "frame time debt"  (as requested to init())
		while(unprocessed_time > m_frame_time)
		{
			should_render = true;

			unprocessed_time -= m_frame_time;

			if(Window::isCloseRequested())
				_is_running = false;

			// update input, game entities, etc.
			input();

			const auto now = steady_clock::now();
			const auto frame_duration = now - last_frame_time;
			last_frame_time = now;
			update(duration_cast<nanoseconds>(frame_duration));

			// isn't multiple calls to this pointless, until next Windo::endFrame() ?
			Input::update();

			if (frame_counter >= 1.0)
			{
				_fps = uint32_t(1000.0 / double(frames));

				frames = 0;
				frame_counter = 0;
			}
		}

		if(should_render and _is_running)
		{
			const auto T0 = steady_clock::now();
			render();
			_render_time.add(duration_cast<microseconds>(steady_clock::now() - T0));

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			GUI::prepare();
			{
				render_gui();
			}
			GUI::render();

			Window::endFrame();
			frames++;
		}
	}
	return 0;
}

} // RGL
