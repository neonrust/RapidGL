#include "pp_tonemapping.h"

#include "filesystem.h"
#include "rendertarget_2d.h"
#include "buffer_binds.h"

#include "log.h"

using namespace std::literals;

namespace RGL::PP
{

Tonemapping::Tonemapping() : //uint32_t width, uint32_t height)
	_exposure_regulation_ssbo("avg-luminance"sv)
{
}

void Tonemapping::create(uint32_t width, uint32_t height)
{
	static const std::filesystem::path dir = FileSystem::getResourcesPath() / "shaders";
	static constexpr glm::uvec2 eval_grid_size { 8, 8 };

	_filter_rt.create("luma_filter"sv, std::bit_ceil(width / 2), std::bit_ceil(height / 2), RenderTarget::Color::Texture | RenderTarget::Color::HalfFloat, RenderTarget::Depth::None);
	auto filter_width = _filter_rt.width();
	_filter_lod = 0;
	while(filter_width > eval_grid_size.x)
	{
		filter_width >>= 1;
		++_filter_lod;
	}
	Log::debug("luma filter kernel {}x{} @ mip {}", filter_width, _filter_rt.height() >> _filter_lod, _filter_lod);

	const string_set conditionals {
		{ std::format("KERNEL_W {}", eval_grid_size.x) },
		{ std::format("KERNEL_H {}", eval_grid_size.y) },
	};
	new (&_calc_exposure) RGL::Shader(dir / "calc_exposure.comp", conditionals);
	_calc_exposure.link();
	assert(_calc_exposure);
	_calc_exposure.setPreBarrier(Shader::Barrier::SSBO | Shader::Barrier::Texture);
	_calc_exposure.setPostBarrier(Shader::Barrier::SSBO);
	_calc_exposure.setUniform("u_weights"sv, generateWeights(eval_grid_size));
	_calc_exposure.setUniform("u_luma_range"sv, glm::vec2(-5.f, 5.f));
	_calc_exposure.setUniform("u_adaptation_rate"sv, glm::vec2(5.f, 2.f)); // { up, down }


	_exposure_regulation_ssbo.bindAt(SSBO_EXPOSURE_REGL);
	_exposure_regulation_ssbo.resize(4); // actually not all floats, but the size is correct
	_exposure_regulation_ssbo.clear();

	new (&_tmo_shader) RGL::Shader(dir / "FSQ.vert", dir / "tmo.frag");
	_tmo_shader.link();
	assert(_tmo_shader);

	setSaturation(1.f);

	glCreateVertexArrays(1, &_dummy_vao_id);
}

Tonemapping::~Tonemapping()
{
	if (_dummy_vao_id)
		glDeleteVertexArrays(1, &_dummy_vao_id);
}

Tonemapping::operator bool() const
{
	return bool(_calc_exposure) and bool(_tmo_shader);
}

void Tonemapping::render(const RGL::RenderTarget::Texture2d &in, RGL::RenderTarget::Texture2d &out)
{
	in.copyTo(_filter_rt);
	_filter_rt.color_texture().GenerateMipMaps();

	_calc_exposure.bind();
	_filter_rt.color_texture().BindImage(1, ImageAccess::Read, _filter_lod);
	_calc_exposure.invoke();
	// {
	// 	// auto er = _exposure_regulation_ssbo.download();
	// 	auto &er = _exposure_regulation_ssbo;
	// 	Log::debug("  {:.1f} -> {:.1f} exposure: {:.2f}", er[1], er[3], er[0]);
	// }

	out.bindRenderTarget();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	_tmo_shader.bind();
	in.bindTextureSampler();

	glBindVertexArray(_dummy_vao_id);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Tonemapping::setBrightness(float brighrness)
{
	_calc_exposure.setUniform("u_brightness"sv, brighrness);
}

void Tonemapping::setFrameTime(float frame_time)
{
	_calc_exposure.setUniform("u_frame_time"sv, frame_time);
}

void Tonemapping::setGamma(float gamma)
{
	_tmo_shader.setUniform("u_gamma"sv, gamma);
}

void Tonemapping::setSaturation(float saturation)
{
	_tmo_shader.setUniform("u_saturation"sv, saturation);
}

void Tonemapping::setLumaRange(float darkest, float brightest)
{
	_calc_exposure.setUniform("u_luma_range"sv, glm::vec2(darkest, brightest));
}

void Tonemapping::setAdaptationRate(float units_per_second_to_brighter, float units_per_second_to_darker)
{
	_calc_exposure.setUniform("u_adaptation_rate"sv, glm::vec2(units_per_second_to_brighter, units_per_second_to_darker));
}

std::vector<float> Tonemapping::generateWeights(glm::uvec2 grid_size)
{
	static constexpr float sigma_sq = 2.f * 2.f; // the outer weights will not be close to zero
	static constexpr float steepocity = 0.5f;

	auto gauss_weight = [](glm::ivec2 pos, glm::ivec2 size) {
		const auto offset = glm::abs(glm::vec2(pos) - glm::vec2(size - 1)/2.f) - 0.5f;
		assert(offset.x >= 0 and offset.y >= 0);
		const auto x2 = glm::dot(offset, offset);
		return std::exp(-steepocity * x2 / sigma_sq); // e^(-0.5x^2/σ^2)
	};
	float sum = 0.f;
	std::vector<float> weights;
	weights.resize(grid_size.x * grid_size.y);
	for(auto y = 0u; y < grid_size.y; ++y)
	{
		for(auto x = 0u; x < grid_size.x; ++x)
		{
			const auto w = gauss_weight({x, y}, grid_size);
			weights[y * grid_size.x + x] = w;
			sum += w;
		}
	}
	for(auto &w: weights)
		w /= sum;

	return weights;
}

} // RGL:PP
