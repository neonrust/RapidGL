#pragma once

namespace RGL::RenderTarget
{
class Texture2d;
}

namespace RGL::PP
{

class PostProcess
{
public:
	virtual operator bool () const = 0;

	inline bool enabled() const { return _enabled; }
	inline void setEnabled(bool enabled=true) { _enabled = enabled; }

	virtual void render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out) = 0;

private:
	bool _enabled { true };
};

} // RGL::PP
