#pragma once

#include <cstdint>
#include <utility>
#include <string_view>

namespace RGL::RenderTarget
{
class Texture2d;
}

namespace RGL::PP
{

enum class Quality : int_fast8_t
{
	Terrible   = -2,
	Low        = -1,
	Medium     =  0,
	High       =  1,
	Super      =  2,
	Insane     =  3,
};
std::string_view qualityName(Quality q);

class PostProcess
{
public:
	virtual operator bool () const = 0;

	virtual void resize(uint32_t width, uint32_t height);
	inline std::pair<uint32_t, uint32_t> size() const { return { _width, _height }; }

	inline bool enabled() const { return _enabled; }
	inline void setEnabled(bool enabled=true) { _enabled = enabled; }

	inline Quality quality() const { return _quality; }
	virtual inline void setQuality(Quality quality) { _quality = quality; }

	virtual void render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out) = 0;

protected:
	virtual inline bool loadShaders() { return true; };

protected:
	uint32_t _width;
	uint32_t _height;
	Quality _quality { Quality::Medium };

private:
	bool _enabled { true };
};

} // RGL::PP
