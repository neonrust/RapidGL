#include "pp_ao.h"

#include "filesystem.h"

#include "rendertarget_2d.h"

using namespace std::literals;


namespace RGL::PP
{

void GroundTruthAmbientOcclusion::resize(uint32_t width, uint32_t height)
{
	PostProcess::resize(width, height);

	if(not _prefilter_shader)
		loadShaders();

	const auto sz = size();

	if(_linearDepth)
		_linearDepth.Release();
	_linearDepth.Create(sz.first, sz.second, GL_R32F, 1);

	if(_filteredDepth)
		_filteredDepth.Release();
	_filteredDepth.Create(sz.first, sz.second, GL_R32F, 5);

	// MainPass's trace samples this chain via textureLod at UV offsets that
	// can land outside the visible frame near the screen edges. Clamp-to-edge
	// would replicate whatever surface happens to sit at that literal edge
	// column/row (often unrelated geometry), producing spurious "close
	// occluder" horizon estimates. Clamp-to-border with a huge sentinel
	// depth (matches the "sky sentinel" convention used elsewhere for
	// out-of-scene depth) makes out-of-frame samples read as "nothing there,
	// very far away" instead, so they can't manufacture false occlusion.
	_filteredDepth.SetWrapping(TextureWrappingAxis::U, TextureWrappingParam::ClampToBorder);
	_filteredDepth.SetWrapping(TextureWrappingAxis::V, TextureWrappingParam::ClampToBorder);
	_filteredDepth.SetBorderColor(1e9f, 0.f, 0.f, 0.f);

	for(uint8_t mip = 0; mip < 5; ++mip)
	{
		auto view = _filteredDepth.CreateView(mip);
		_filteredMips[mip].set(view);
	}

	if(_edges)
		_edges.Release();
	_edges.Create(sz.first, sz.second, GL_R32F, 1);

	for(auto idx = 0; idx < 2; ++idx)
	{
		if(_noisy[idx])
			_noisy[idx].Release();
		_noisy[idx].Create(sz.first, sz.second, GL_R32F, 1);
	}
}

void GroundTruthAmbientOcclusion::setDenoisePassCount(uint32_t count)
{
	_denoise_pass_count = std::max(1u, std::min(count, s_max_denoise_passes));
}

void GroundTruthAmbientOcclusion::setProjection(const glm::mat4 &projection)
{
	_projection = projection;
}

void GroundTruthAmbientOcclusion::setQuality(Quality quality)
{
	PostProcess::setQuality(quality);

	switch(quality)
	{
	case Quality::PissPoor: _slice_count = 1; _steps_per_slice = 1; break;
	case Quality::Low:      _slice_count = 2; _steps_per_slice = 1; break;
	case Quality::Medium:   _slice_count = 2; _steps_per_slice = 2; break;
	case Quality::High:     _slice_count = 3; _steps_per_slice = 3; break;
	case Quality::Super:    _slice_count = 4; _steps_per_slice = 3; break;
	case Quality::Insane:   _slice_count = 9; _steps_per_slice = 3; break;
	}
}

GroundTruthAmbientOcclusion::operator bool() const
{
	return _linearize_shader and _prefilter_shader and _main_shader and _denoise_shader and _linearDepth and _edges and _noisy[0] and _noisy[1];
}

bool GroundTruthAmbientOcclusion::loadShaders()
{
	const auto shader_dir = FileSystem::getResourcesPath() / "shaders";

	new (&_linearize_shader) Shader(shader_dir / "gtao_linearize_depth.comp");
	_linearize_shader.link();
	assert(_linearize_shader);
	_linearize_shader.setPreBarrier(Shader::Barrier::TextureFetch);

	new (&_prefilter_shader) Shader(shader_dir / "gtao_prefilter_depth.comp");
	_prefilter_shader.link();
	assert(_prefilter_shader);
	_prefilter_shader.setPreBarrier(Shader::Barrier::TextureFetch);

	new (&_main_shader) Shader(shader_dir / "gtao_main_pass.comp");
	_main_shader.link();
	assert(_main_shader);
	_main_shader.setPreBarrier(Shader::Barrier::TextureFetch);

	new (&_denoise_shader) Shader(shader_dir / "gtao_denoise.comp");
	_denoise_shader.link();
	assert(_denoise_shader);
	_denoise_shader.setPreBarrier(Shader::Barrier::TextureFetch);

	return _linearize_shader and _prefilter_shader and _main_shader and _denoise_shader;
}

void GroundTruthAmbientOcclusion::renderTexture(const Texture2D &in, Texture2D &out)
{
	linearizeDepth(in);

	prefilterDepth();

	renderNoisy();

	denoise(out);
}

void GroundTruthAmbientOcclusion::linearizeDepth(const Texture2D &sceneDepth)
{
	_linearize_shader.setUniform("u_viewport_size"sv, glm::ivec2(_width, _height));
	_linearize_shader.setUniform("u_proj_22"sv, _projection[2][2]);
	_linearize_shader.setUniform("u_proj_32"sv, _projection[3][2]);

	_linearDepth.BindImage(0, ImageAccess::Write);
	sceneDepth.Bind(1);//bindDepthTextureSampler(1);

	const auto num_groups = glm::uvec3(
		( _width + 7 ) / 8,
		( _height + 7 ) / 8,
		1
	);
	_linearize_shader.invoke(num_groups);
}

void GroundTruthAmbientOcclusion::prefilterDepth()
{
	for(auto mip = 0u; mip < 5; ++mip)
		_filteredMips[mip].BindImage(mip, ImageAccess::Write);
	_linearDepth.Bind(5);

	_prefilter_shader.setUniform("u_viewport_size"sv, glm::ivec2(_width, _height));
	_prefilter_shader.setUniform("u_effect_radius"sv, _radius);
	_prefilter_shader.setUniform("u_falloff_range"sv, _falloff);
	_prefilter_shader.setUniform("u_radius_multiplier"sv, _radius_mul);

	const auto num_groups = glm::uvec3(
		( _width + 15 ) / 16,
		( _height + 15 ) / 16,
		1
	);
	_prefilter_shader.invoke(num_groups);
}

void GroundTruthAmbientOcclusion::renderNoisy()
{
	_linearDepth.Bind(2);
	_filteredDepth.Bind(3);
	_noisy[0].BindImage(0, ImageAccess::Write);
	_edges.BindImage(1, ImageAccess::Write);

	_main_shader.setUniform("u_viewport_size"sv, glm::ivec2(_width, _height));
	const auto pixel_size = glm::vec2(1.f / float(_width), 1.f / float(_height));
	_main_shader.setUniform("u_pixel_size"sv, pixel_size);

	const float tanHalfFOVY = 1.0f / _projection[1][1];
	const float tanHalfFOVX = 1.0f / _projection[0][0];
	const auto ndc_to_view_mul = glm::vec2{ tanHalfFOVX * 2.f, tanHalfFOVY * -2.f };
	_main_shader.setUniform("u_ndc_to_view_mul"sv, ndc_to_view_mul);
	const auto ndc_to_view_add = glm::vec2{ tanHalfFOVX * -1.f, tanHalfFOVY * 1.f };
	_main_shader.setUniform("u_ndc_to_view_add"sv, ndc_to_view_add);
	_main_shader.setUniform("u_ndc_to_view_mul_pixelsize"sv, ndc_to_view_mul * pixel_size);

	_main_shader.setUniform("u_effect_radius"sv, _radius);
	_main_shader.setUniform("u_falloff_range"sv, _falloff);
	_main_shader.setUniform("u_radius_multiplier"sv, _radius_mul);
	_main_shader.setUniform("u_final_value_power"sv, _final_value_power);
	_main_shader.setUniform("u_sample_distro_power"sv, _sample_dist_power);
	_main_shader.setUniform("u_thin_occluder_comp"sv, _thin_occluder_comp);
	_main_shader.setUniform("u_depth_mip_sample_offset"sv, _depth_nip_sample_offset);
	_main_shader.setUniform("u_slice_count"sv, float(_slice_count));
	_main_shader.setUniform("u_slice_steps"sv, float(_steps_per_slice));

	const auto num_groups = glm::uvec3(
		( _width + 7 ) / 8,
		( _height + 7 ) / 8,
		1
	);
	_main_shader.invoke(num_groups);
}

void GroundTruthAmbientOcclusion::denoise(Texture2D &out)
{
	const auto num_groups = glm::uvec3(
		( _width + 15 ) / 16,
		( _height + 7 ) / 8,
		1
	);

	_denoise_shader.setUniform("u_viewport_size"sv, glm::ivec2(_width, _height));
	_denoise_shader.setUniform("u_denoise_blur_beta"sv, _denoise_blur_beta);

	_edges.Bind(1);

	auto src = 0;
	auto dst = 1;

	for(auto pass = 0u; pass < _denoise_pass_count; ++pass)
	{
		const bool finalPass = (pass == _denoise_pass_count - 1);

		_denoise_shader.setUniform("u_final_apply"sv, finalPass ? 1 : 0);

		_noisy[src].Bind(2);

		// Intermediate passes ping-pong between the two noisy buffers; the
		// final pass always lands in `out`, not one of the two.
		if(finalPass)
			out.BindImage(0, ImageAccess::Write);
		else
			_noisy[dst].BindImage(0, ImageAccess::Write);

		_denoise_shader.invoke(num_groups);

		std::swap(src, dst);
	}
}

} // RGL::PP