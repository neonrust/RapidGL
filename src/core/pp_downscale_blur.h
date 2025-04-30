#pragma once

#include "postprocess.h"

#include "rendertarget_2d.h"
// #include "rendertarget_3d.h"
#include "shader.h"

#include "container_types.h"


namespace RGL::PP
{

class DownscaleBlur : public PostProcess
{
public:
	bool create();
	operator bool () const override;

	void setLevelLimit(size_t limit);

	void render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out) override;

private:
	void computeWeights();

private:
	Shader _downscale_blur;

	static constexpr auto MAX_WEIGHTS = 16u;  // MAX_SIZE + 1 in shader code

	dense_map<size_t, small_vec<float, MAX_WEIGHTS>> _weights;

	size_t _num_levels;
};

/*
class DownscaleBlur3d
{
public:
	bool create();
	operator bool () const override;

	void setLevelLimit(size_t limit);

	void render(const RenderTarget::Texture3d &in, RenderTarget::Texture3d &out);

private:
	void computeWeights(float sigma, size_t kernelSize);

private:
	Shader _downscale_blur;

	static constexpr auto MAX_WEIGHTS = 16u;

	dense_map<size_t, small_vec<float, MAX_WEIGHTS>> _weight_map;

	size_t _limit;
};
*/

} // RGL::PP
