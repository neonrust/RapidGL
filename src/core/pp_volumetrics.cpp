#include "pp_volumetrics.h"

#include "rendertarget_2d.h"
#include "camera.h"
#include "texture.h"
#include "filesystem.h"

#include "../demos/27_clustered_shading/light_constants.h"

using namespace std::literals;

namespace RGL::PP
{

static constexpr glm::uvec3 s_froxels { FROXEL_GRID_W, FROXEL_GRID_H, FROXEL_GRID_D };
static constexpr glm::uvec3 s_local_size { 8, 8, 1 };

bool Volumetrics::create()
{
	const auto shaderPath = FileSystem::getResourcesPath() / "shaders";

	new (&_inject_shader) Shader(shaderPath / "volumetrics_inject.comp");
	_inject_shader.link();
	_inject_shader.setPostBarrier(Shader::Barrier::Image);
	assert(_inject_shader);
	// set default values
	_inject_shader.setUniform("u_effect_scale"sv, 2.f);

	new (&_march_shader) Shader(shaderPath / "volumetrics_march.comp");
	_march_shader.link();
	assert(_march_shader);
	_march_shader.setPostBarrier(Shader::Barrier::Image);

	_blue_noise.Load("resources/textures/blue-noise.array");

	for(auto idx = 0u; idx < 2; ++idx)
	{
		_transmittance[idx].Create(s_froxels.x, s_froxels.y, s_froxels.z, GL_RGBA32F);
		_transmittance[idx].SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::LinearMipLinear);
		_transmittance[idx].SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipLinear);
		_transmittance[idx].SetWrapping(TextureWrappingAxis::U, TextureWrappingParam::ClampToEdge);
		_transmittance[idx].SetWrapping(TextureWrappingAxis::V, TextureWrappingParam::ClampToEdge);
		_transmittance[idx].SetWrapping(TextureWrappingAxis::W, TextureWrappingParam::ClampToEdge);
	}

	return *this;
}

Volumetrics::operator bool() const
{
	return _inject_shader and _march_shader and _blue_noise;
}

void Volumetrics::setCameraUniforms(const Camera &camera)
{
	camera.setUniforms(_inject_shader);
	camera.setUniforms(_march_shader);
}



void Volumetrics::inject()
{
	++_frame;

	_blue_noise.BindLayer(_frame % _blue_noise.num_layers(), 3);

	// TODO: better API?
	//   _inject_shader.bindImage("u_output_transmittance"sv, _transmittance[_frame & 1], ImageAccess::Write);
	_transmittance[_frame & 1].BindImage(5, ImageAccess::Write);
	_transmittance[1 - (_frame & 1)].Bind(6);

	_inject_shader.setUniform("u_fog_anisotropy"sv, _anisotropy);
	_inject_shader.setUniform("u_froxel_zexp"sv,    1.f);
	_inject_shader.setUniform("u_fog_density"sv, _density);
	_inject_shader.setUniform("u_froxel_blend_previous"sv, _blend);
	_inject_shader.setUniform("u_froxel_blend_weight"sv, _blend_weight);

	// TODO: injection is quite slow, probably b/c it naively loops through all lights for all froxel "columns"
	//   it's probably worth it to do a light culling pre-pass
	//   maybe with a coarser grid, maybe just 2d (screen-space projection of the lights)


	_inject_shader.invoke(s_froxels / s_local_size);
}

void Volumetrics::render(const RenderTarget::Texture2d &, RenderTarget::Texture2d &out)
{
	// TODO: accumulate transmittance, from camera to scene depth
	_transmittance[_frame & 1].Bind(5);  // read the froxels previously written in inject()

	out.bindImage(1, ImageAccess::Write);

	_march_shader.invoke(out.width() / s_local_size.x, out.height() / s_local_size.y);
}

} // RGL
