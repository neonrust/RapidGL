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

	virtual void render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out) = 0;
};

} // RGL::PP
