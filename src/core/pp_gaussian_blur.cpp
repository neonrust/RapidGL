#include "pp_gaussian_blur.h"

using namespace std::literals;

namespace RGL::PP
{

bool Blur::create(size_t width, size_t height)
{
	new (&_blur_horizontal) Shader("src/demos/27_clustered_shading/shaders/gaussian_blur_parametric.comp", string_set{ "HORIZONTAL"s });
	_blur_horizontal.link();
	_blur_horizontal.setPostBarrier(Shader::Barrier::Image);

	new (&_blur_vertical) Shader("src/demos/27_clustered_shading/shaders/gaussian_blur_parametric.comp");
	_blur_vertical.link();
	_blur_vertical.setPostBarrier(Shader::Barrier::Image);

	_temp.create("blur-temp", width, height, RenderTarget::Color::Default, RenderTarget::Depth::None);
	_temp.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipNearest);

	return false;
}

Blur::operator bool() const
{
	return _blur_horizontal and _blur_vertical and _temp;
}

void Blur::setSigma(float sigma)
{
	// build weights for a blur kernel with 'sigma'; set 'u_weights' and 'u_num_weights'

	static constexpr float sigma_kernel_factor = 3.f;  // maybe 3.0

	auto kernelSize = size_t(std::floor(sigma_kernel_factor * sigma)); // nice, round approximation

	if(not kernelSize)
		kernelSize = 1;

	if(kernelSize > MAX_WEIGHTS)
	{
		auto new_sigma = float(MAX_WEIGHTS) / sigma_kernel_factor;
		std::fprintf(stderr, "PP::Blur: capped kernel size 17; sigma %.1f -> %.1f\n", sigma, new_sigma);
		assert(kernelSize <= MAX_WEIGHTS); // limit dictated by shader code (MAX_SIZE + 1)
		sigma = new_sigma;
		kernelSize = MAX_WEIGHTS;
	}

	if(kernelSize != _weights.size())
	{
		computeWeights(sigma, kernelSize);

		_blur_horizontal.setUniform("u_weights"sv, _weights.size(), _weights.data());
		_blur_horizontal.setUniform("u_num_weights"sv, uint32_t(_weights.size()));
		_blur_vertical.setUniform("u_weights"sv, _weights.size(), _weights.data());
		_blur_vertical.setUniform("u_num_weights"sv, uint32_t(_weights.size()));
	}
}

void Blur::computeWeights(float sigma, size_t kernelSize)
{
	_weights.clear();
	_weights.resize(kernelSize);

	const float sigma_sq = sigma*sigma;
	static constexpr float steepocity = 0.5f;  // 0.5 = standard gauss

	auto sum = 0.f;

	for (auto idx = 1u; idx <= kernelSize; idx++)
	{
		float x2 = float(idx * idx);
		float w = std::exp(-steepocity * x2 / sigma_sq); // e^(-0.5x^2/σ^2)

		// store in edge-to-center order
		_weights[kernelSize - idx] = w;
		sum += 2.f * w; // on either side of the center sample
	}

	// center pixel last  (last in the array)
	_weights[kernelSize - 1] = 1.f; // center pixel is always 1 (before normalization); e^0 == 1
	sum += 1.f;

	std::printf("PP::Blur: sigma %.1f -> %lu weights\n", sigma, _weights.size());

	// normalize the weights (total should be 1.0)
	for(auto &w: _weights)
	{
		w /= sum;
		// std::printf("  %.3f\n", w);
	}
}

void Blur::render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out)
{
	static constexpr size_t group_size = 64; // MAX_SIZE + 1 in shader code


	// horizontal
	in.bindImageRead(0);
	_temp.bindImage(1, RenderTarget::Access::Write);

	_blur_horizontal.invoke((in.width() + group_size - 1) / group_size, in.height());

	// vertical
	_temp.bindImageRead(0);
	out.bindImage(1, RenderTarget::Access::Write);

	_blur_vertical.invoke(in.width(), (in.height() + group_size - 1) / group_size, 1);
}

} // RGL::PP
