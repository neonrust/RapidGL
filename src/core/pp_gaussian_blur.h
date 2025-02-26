#pragma once

#include "postprocess.h"

#include "rendertarget_2d.h"
#include "shader.h"


namespace RGL::PP
{

class Blur : public PostProcess
{
public:
	bool create(size_t width, size_t height);
	operator bool () const override;

	void setSigma(float radius);

	void render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out) override;

private:
	void computeWeights(float sigma, size_t kernelSize);

private:
	Shader _blur_horizontal;
	Shader _blur_vertical;
	RenderTarget::Texture2d _temp;

	static constexpr auto MAX_WEIGHTS = 33;  // MAX_SIZE + 1 in shader code

	small_vec<float, MAX_WEIGHTS> _weights;

	float _radius;
};

} // RGL::PP
