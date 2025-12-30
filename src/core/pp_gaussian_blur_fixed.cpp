#include "pp_gaussian_blur_fixed.h"

#include "filesystem.h"
#include "log.h"
#include "zstr.h"

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
		Log::debug("[PP Gaussian blur] Conditionals:\n  {}", zstr::join(conditionals.begin(), conditionals.end(), "\n  "sv));

	const auto shader_dir = FileSystem::getResourcesPath() / "shaders";

	new (&vertical) Shader(shader_dir / "gaussian_blur.comp", conditionals);
	vertical.link();
	assert(bool(vertical));
	vertical.setPostBarrier(Shader::Barrier::Image);

	conditionals.insert("HORIZONTAL");

	new (&horizontal) Shader(shader_dir / "gaussian_blur.comp", conditionals);
	horizontal.link();
	assert(bool(horizontal));
	horizontal.setPostBarrier(Shader::Barrier::Image);

	temp.create("blur temp", width, height, RenderTarget::Color::HalfFloat | RenderTarget::Color::Texture, RenderTarget::Depth::None);
	temp.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipNearest);

	return false;
}

void _blur_fixed_render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out, Shader &horizontal, Shader &vertical, RenderTarget::Texture2d &temp)
{
	static constexpr size_t group_size = 64; // MAX_SIZE + 1 in shader code

	// horizontal
	in.bindImageRead(0);
	temp.bindImage(1, ImageAccess::Write);

	horizontal.invoke((in.width() + group_size - 1) / group_size, in.height());

	// vertical
	temp.bindImageRead(0);
	out.bindImage(1, ImageAccess::Write);

	vertical.invoke(in.width(), (in.height() + group_size - 1) / group_size);
}

} // RGL::PP
