#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include <imgui.h>
#pragma GCC diagnostic pop
#include "imgui_impl_glfw.h"  // IWYU pragma: keep

#include "window.h"  // IWYU pragma: keep
#include "font.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <memory>

namespace RGL
{
    class GUI
    {
    public:
        typedef void(*Style)(ImGuiStyle &);

        ~GUI();
        static void init(GLFWwindow * window);
        static void prepare();
        static void render();
        static void updateWindowSize(float width, float height);

		// HUD rendering
        static void beginHUD();
        static void endHUD();

		static float text(const std::shared_ptr<Font> & font, const std::string& text, const glm::vec2 & position, float size, const glm::vec4 & color=glm::vec4(1), bool center=false, bool text_shadow=false);
		static void line(const glm::vec2 & from, const glm::vec2 & to, const glm::vec4 & color=glm::vec4(1), float thickness=1);
		static void circle(const glm::vec2 & position, float radius, const glm::vec4 & color=glm::vec4(1), float thickness=1, uint32_t segments=16);
		static void circleFilled(const glm::vec2 & position, float radius, const glm::vec4 & color=glm::vec4(1), uint32_t segments=16);
		static void rect(const glm::vec2 & from, const glm::vec2 & to, const glm::vec4 & color=glm::vec4(1), float rounding=0, uint32_t roundingCornersFlags = ImDrawFlags_RoundCornersAll, float thickness=1);
		static void rectFilled(const glm::vec2 & from, const glm::vec2 & to, const glm::vec4 & color=glm::vec4(1), float rounding=0, uint32_t roundingCornersFlags = ImDrawFlags_RoundCornersAll);

    private:
        static glm::vec2 m_window_size;
    };
}
