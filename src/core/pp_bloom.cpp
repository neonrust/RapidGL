#include "pp_bloom.h"

#include "filesystem.h"
#include "rendertarget_2d.h"

using namespace std::literals;

namespace RGL::PP
{

bool Bloom::create()
{
	static const auto dir = "src/demos/27_clustered_shading/"s;

	// load shaders and dirt texture

	// TODO: slightly less ugly syntax ;)

	new (&_downscale_shader) Shader(dir + "downscale.comp");
	_downscale_shader.link();
	assert(_downscale_shader);

	new (&_upscale_shader) Shader(dir + "upscale.comp");
	_upscale_shader.link();
	assert(_upscale_shader);

	_dirt_texture.Load(FileSystem::getResourcesPath() / "textures/bloom_dirt_mask.jxl");
	assert(_dirt_texture);

	return *this;
}

static constexpr auto IMAGE_UNIT_WRITE = 0;

Bloom::operator bool() const
{
	return _upscale_shader and _downscale_shader and _dirt_texture;
}

void Bloom::render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out)
{
	// Bloom: downscale
	_downscale_shader.bind();
	_downscale_shader.setUniform("u_threshold"sv, glm::vec4(
														_threshold,
														_threshold - _knee,
														2.0f * _knee,
														0.25f * _knee
														));
	// input pixels (the stuff we rendered above)
	// m_tmo_ps->bindTextureSampler();
	in.bindTextureSampler();

	// render at half resolution
	const auto mip_cap = 1;
	auto mip_size = glm::uvec2(
		in.width() / (1 << mip_cap),
		in.height() / (1 << mip_cap)
		);

	static auto printed = false;

	if(not printed)
		std::printf("PP mip_levels: %d\n", in.mip_levels());

	// TODO: couldn't these loops be made entirely in the compute shader, i.e. binding all mips at once
	for (auto idx = 0; idx < in.mip_levels() - mip_cap; ++idx)
	{
		if(not printed)
			std::fprintf(stderr, "PP dn size[%d]: %d x %d\n", idx, mip_size.x, mip_size.y);

		_downscale_shader.setUniform("u_texel_size"sv,    1.0f / glm::vec2(mip_size));
		_downscale_shader.setUniform("u_mip_level"sv,     idx);
		_downscale_shader.setUniform("u_use_threshold"sv, idx == 0);

		// m_tmo_ps->renderTarget().bindImage(IMAGE_UNIT_WRITE, RenderTarget::Write, idx + mip_cap);
		out.bindImage(IMAGE_UNIT_WRITE, RenderTarget::Write, idx + mip_cap);

		glDispatchCompute(GLuint(glm::ceil(float(mip_size.x) / 8.f)),
						  GLuint(glm::ceil(float(mip_size.y) / 8.f)),
						  1);

		mip_size >>= 1; // halve size each iteration

		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT); // GL_TEXTURE_UPDATE_BARRIER_BIT ?
	}

	// Bloom: upscale
	_upscale_shader.bind();
	_upscale_shader.setUniform("u_bloom_intensity"sv, _intensity);
	_upscale_shader.setUniform("u_dirt_intensity"sv,  _dirt_intensity);
	// m_tmo_ps->bindTextureSampler();
	_dirt_texture.Bind(1);

	for (auto idx = in.mip_levels() - mip_cap; idx >= mip_cap; --idx)
	{
		mip_size.x = glm::max(1u, uint32_t(glm::floor(float(in.width()) / glm::pow(2.f, idx - 1))));
		mip_size.y = glm::max(1u, uint32_t(glm::floor(float(in.height()) / glm::pow(2.f, idx - 1))));

		if(not printed)
			std::fprintf(stderr, "PP up size[%d]: %d x %d\n", idx, mip_size.x, mip_size.y);

		_upscale_shader.setUniform("u_texel_size"sv, 1.0f / glm::vec2(mip_size));
		_upscale_shader.setUniform("u_mip_level"sv,  int(idx));

		out.bindImage(IMAGE_UNIT_WRITE, RenderTarget::ReadWrite, idx - mip_cap);

		glDispatchCompute(GLuint(glm::ceil(float(mip_size.x) / 8.f)),
						  GLuint(glm::ceil(float(mip_size.y) / 8.f)),
						  1);

		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT); // not GL_TEXTURE_UPDATE_BARRIER_BIT ?
	}

	printed = true;
}

} // RGL::PP
