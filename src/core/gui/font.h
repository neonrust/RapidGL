#pragma once
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include <imgui.h>
#pragma GCC diagnostic pop
#include <filesystem>
#include <string>

namespace RGL
{
    class Font
    {
    public:
        Font();
        Font(const std::filesystem::path & filepath, unsigned size_pixels);
        ~Font() = default;

    private:
        friend class GUI;

        ImFont * m_font;
    };
}
