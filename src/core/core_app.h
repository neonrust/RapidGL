#pragma once

#include "asset_manager.h"
#include "sample_window.h"
#include <string>
#include <cstdint>
#include <chrono>

namespace RGL
{
	class CoreApp
    {
    public:
        CoreApp();
        virtual ~CoreApp();

		CoreApp(const CoreApp&)              = delete;
		CoreApp& operator = (const CoreApp&) = delete;

        virtual void init(unsigned int width, unsigned int height, const std::string & title, double framerate = 60.0) final;

        virtual void init_app()                = 0;
        virtual void input()                   = 0;
		virtual void update(std::chrono::nanoseconds delta_time) = 0;
        virtual void render()                  = 0;
        virtual void render_gui();

        uint32_t get_fps() const;
		
		virtual int run();
		virtual void stop();

		inline AssetManager &assets() { return AssetManager::the(); }

		bool take_screenshot(const std::string & filename);

	protected:
		double   m_frame_time;

    private:
		int run_app();

        uint32_t m_fps;
        bool     m_is_running;
		SampleWindow<std::chrono::microseconds, 30> _render_time;
	};
}
