#include "pp_gaussian_blur_fixed.h"

using namespace std::literals;

namespace RGL::PP
{

bool _blur_fixed_init(size_t width, size_t height, float sigma, Shader &horizontal, Shader &vertical, RenderTarget::Texture2d &temp)
{
	assert(sigma == 1.f or sigma == 1.5f or sigma == 2.f or sigma == 3.f);

	string_set conditionals;
	conditionals.reserve(7);

	if(sigma >= 3.f)
		conditionals.insert("SAMPLES_30");
	if(sigma >= 2.f)
		conditionals.insert("SAMPLES_20");
	if(sigma >= 1.5f)
		conditionals.insert("SAMPLES_15");
	conditionals.insert("SAMPLES_10");

	if(sigma == 3.f)
	{
		conditionals.insert("WEIGHTS_9");
		conditionals.insert("NUM_WEIGHTS 9");
	}
	if(sigma == 2.f)
	{
		conditionals.insert("WEIGHTS_6");
		conditionals.insert("NUM_WEIGHTS 6");
	}
	if(sigma == 1.5f)
	{
		conditionals.insert("WEIGHTS_4");
		conditionals.insert("NUM_WEIGHTS 4");
	}
	if(sigma == 1.f)
	{
		conditionals.insert("WEIGHTS_3");
		conditionals.insert("NUM_WEIGHTS 3");
	}

	if(not conditionals.empty())
	{
		std::printf("Conditionals:\n");
		for(const auto &c: conditionals)
			std::printf("  %s\n", c.c_str());
	}

	new (&vertical) Shader("src/demos/27_clustered_shading/gaussian_blur.comp", conditionals);
	vertical.link();
	assert(bool(vertical));
	vertical.setPostBarrier(Shader::Barrier::Image);

	conditionals.insert("HORIZONTAL");

	new (&horizontal) Shader("src/demos/27_clustered_shading/gaussian_blur.comp", conditionals);
	horizontal.link();
	assert(bool(horizontal));
	horizontal.setPostBarrier(Shader::Barrier::Image);

	temp.create(width, height, RenderTarget::Color::Default, RenderTarget::Depth::None);
	temp.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipNearest);

	return false;
}

void _blur_fixed_render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out, Shader &horizontal, Shader &vertical, RenderTarget::Texture2d &temp)
{
	static constexpr size_t group_size = 64; // MAX_SIZE + 1 in shader code

	// horizontal
	in.bindImageRead(0);
	temp.bindImage(1, RenderTarget::Access::Write);

	horizontal.invoke((in.width() + group_size - 1) / group_size, in.height());

	// vertical
	temp.bindImageRead(0);
	out.bindImage(1, RenderTarget::Access::Write);

	vertical.invoke(in.width(), (in.height() + group_size - 1) / group_size);
}

} // RGL::PP
