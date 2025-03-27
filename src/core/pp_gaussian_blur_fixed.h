#pragma once

#include "postprocess.h"

#include "rendertarget_2d.h"
#include "shader.h"


namespace RGL::PP
{

bool _blur_fixed_init(size_t width, size_t height, float sigma, Shader &horizontal, Shader &vertical, RenderTarget::Texture2d &temp);
void _blur_fixed_render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out, Shader &horizontal, Shader &vertical, RenderTarget::Texture2d &temp);

template<float Sigma>
class BlurFixed : public PostProcess
{
public:
	static_assert(Sigma == 1.0f or Sigma == 1.5f or Sigma == 2.0f or Sigma == 3.0f);

public:
	bool create(size_t width, size_t height);
	operator bool () const override;

	void render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out) override;

private:
	Shader _blur_horizontal;
	Shader _blur_vertical;
	RenderTarget::Texture2d _temp;
};

template<float Sigma>
inline BlurFixed<Sigma>::operator bool() const
{
	return _blur_horizontal and _blur_vertical and _temp;
}

template<float Sigma>
inline bool BlurFixed<Sigma>::create(size_t width, size_t height)
{
	return _blur_fixed_init(width, height, Sigma, _blur_horizontal, _blur_vertical, _temp);
}

template<float Sigma>
inline void BlurFixed<Sigma>::render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out)
{
	_blur_fixed_render(in, out, _blur_horizontal, _blur_vertical, _temp);
}

} // RGL::PP
