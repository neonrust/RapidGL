#include "pp_downscale_blur.h"

using namespace std::literals;

namespace RGL::PP
{

bool DownscaleBlur::create()
{
	new (&_downscale_blur) Shader("src/demos/27_clustered_shading/downscale_blur.comp");
	_downscale_blur.link();
	assert(_downscale_blur);
	_downscale_blur.setPostBarrier(Shader::Barrier::Image);

	return bool(_downscale_blur);
}

DownscaleBlur::operator bool() const
{
	return _downscale_blur;
}

void DownscaleBlur::setLevelLimit(size_t limit)
{
	// build weights for a blur over 'limit' mip-map levels (capped to MAX_WEIGHTS)

	if(limit > MAX_WEIGHTS)
	{
		std::fprintf(stderr, "PP::DownscaleBlur: capped mip levels; %lu -> %u\n", limit, MAX_WEIGHTS);
		limit = MAX_WEIGHTS;
	}

	if(limit == _num_levels)
		return;

	_num_levels = limit;
	if(not _weights.contains(_num_levels))
		computeWeights();

	_downscale_blur.setUniform("u_weights"sv, _weights[_num_levels].size(), _weights[_num_levels].data());
	_downscale_blur.setUniform("u_num_levsl"sv, uint32_t(_num_levels));
}

void DownscaleBlur::computeWeights()
{
	auto &weights = _weights[_num_levels];

	weights.clear();
	weights.resize(_num_levels);

	static constexpr float sigmaBase = 1.f;
	static constexpr float sigmaBase_sq = sigmaBase*sigmaBase;

	float sum = 0;
	for(auto idx = 0u; idx < _num_levels; ++idx)
	{
		float sigma = sigmaBase * float(1 << idx);
		weights[idx] = std::exp(-0.5f * (sigma*sigma)/(sigmaBase_sq));
		sum += weights[idx];
	}
	// normalize weights (the sum of all weights = 1.0)
	for(auto &w : weights)
		w /= sum;
}

void DownscaleBlur::render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out)
{
	static constexpr size_t group_size = 16;

	// TODO: 'in' must have a complete mip-map pyramid!
	const_cast<RenderTarget::Texture2d *>(&in)->color_texture().GenerateMipMaps();

	in.bindTextureSampler();
	out.bindImage(1, RenderTarget::Access::Write);

	_downscale_blur.invoke(size_t(std::ceil(float(in.width())/float(group_size))),
						   size_t(std::ceil(float(in.height())/float(group_size))));
}

} // RGL::PP
