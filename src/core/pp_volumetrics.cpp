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
	camera.setUniforms(_march_shader);
}

void Volumetrics::inject(const Camera &camera) // TODO: View
{
	++_frame;

	camera.setUniforms(_inject_shader);

	_blue_noise.BindLayer(_frame % _blue_noise.num_layers(), 3);

	// TODO: better API?
	//   _inject_shader.bindImage("u_output_transmittance"sv, _transmittance[_frame & 1], ImageAccess::Write);
	const auto active_idx = _frame & 1;
	_transmittance[active_idx].BindImage(5, ImageAccess::Write);
	_transmittance[1 - active_idx].Bind(6);

	_inject_shader.setUniform("u_fog_anisotropy"sv, _anisotropy);
	_inject_shader.setUniform("u_froxel_zexp"sv,    1.f);
	_inject_shader.setUniform("u_fog_density"sv, _density);
	_inject_shader.setUniform("u_froxel_blend_previous"sv, _blend);
	_inject_shader.setUniform("u_froxel_blend_weight"sv, _blend_weight);

	const auto view_projection = camera.projectionTransform() * camera.viewTransform();
	const auto inv_view_projection = glm::inverse(view_projection);
	_inject_shader.setUniform("u_inv_view_projection"sv, inv_view_projection);

	static glm::mat4 prev_view_projection = glm::mat4(1);
	// use current for the first frame (there's no "previous" yet)
	_inject_shader.setUniform("u_prev_view_projection"sv, _frame == 1? view_projection: prev_view_projection);
	// now there is :)
	prev_view_projection = view_projection;

	// TODO: injection is quite slow, probably b/c it naively loops through all lights for all froxel "columns"
	//   it's probably worth it to do a light culling pre-pass
	//   maybe with a coarser grid, maybe just 2d (screen-space projection of the lights)
	//   or build a BVH for the lights?
	//   see https://worldoffries.wordpress.com/2015/02/19/simple-alternative-to-clustered-shading-for-thousands-of-lights/
	_inject_shader.invoke(size_t(std::ceil(float(s_froxels.x) / float(s_local_size.x))),
						  size_t(std::ceil(float(s_froxels.y) / float(s_local_size.y))),
						  size_t(std::ceil(float(s_froxels.z) / float(s_local_size.z))));
}

void Volumetrics::render(const RenderTarget::Texture2d &, RenderTarget::Texture2d &out)
{
	// TODO: accumulate transmittance, from camera to scene depth
	_transmittance[_frame & 1].Bind(5);  // read the froxels previously written in inject()

	out.bindImage(1, ImageAccess::Write);

	_march_shader.setUniform("u_effect_scale"sv, _strength);

	_march_shader.invoke(size_t(std::ceil(float(out.width())  / float(s_local_size.x))),
						 size_t(std::ceil(float(out.height()) / float(s_local_size.y))),
						 size_t(std::ceil(float(out.depth())  / float(s_local_size.z))));

	// TODO: blur 'out'
}

} // RGL
