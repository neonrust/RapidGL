
#include "pp_volumetrics.h"

#include "rendertarget_2d.h"
#include "camera.h"
#include "texture.h"
#include "filesystem.h"

#include "../demos/27_clustered_shading/light_constants.h"
#include "../demos/27_clustered_shading/buffer_binds.h"
#include <glm/vec3.hpp>

using namespace std::literals;

namespace RGL::PP
{

static constexpr glm::uvec3 s_froxels { FROXEL_GRID_W, FROXEL_GRID_H, FROXEL_GRID_D };
static constexpr glm::uvec3 s_local_size { 8, 8, 1 };

Volumetrics::Volumetrics() :
	_all_volumetric_lights("volumetric-lights"sv),
	_all_tile_lights("volumetric-all-tile-lights"sv),
	_tile_lights_ranges("volumetric-tile-light-ranges"sv)
	// _debug_stuff("debug-stuff"sv)
{
	_all_volumetric_lights.bindAt(SSBO_BIND_ALL_VOLUMETRIC_LIGHTS_INDEX);
	_all_tile_lights.bindAt(SSBO_BIND_VOLUMETRIC_ALL_TILE_LIGHTS_INDEX);
	_tile_lights_ranges.bindAt(SSBO_BIND_VOLUMETRIC_TILE_LIGHTS_INDEX);
	// _debug_stuff.bindAt(30);
}

bool Volumetrics::create()
{
	const auto shaderPath = FileSystem::getResourcesPath() / "shaders";

	new (&_select_shader) Shader(shaderPath / "volumetrics_select_lights.comp");
	_select_shader.link();
	_select_shader.setPreBarrier(Shader::Barrier::SSBO);
	assert(_select_shader);

	new (&_cull_shader) Shader(shaderPath / "volumetrics_cull.comp");
	_cull_shader.link();
	_cull_shader.setPreBarrier(Shader::Barrier::SSBO);
	assert(_cull_shader);

	new (&_inject_shader) Shader(shaderPath / "volumetrics_inject.comp");
	_inject_shader.link();
	_inject_shader.setPreBarrier(Shader::Barrier::SSBO);
	assert(_inject_shader);

	new (&_accumulate_shader) Shader(shaderPath / "volumetrics_accumulate.comp");
	_accumulate_shader.link();
	assert(_accumulate_shader);
	_accumulate_shader.setPreBarrier(Shader::Barrier::Image);

	new (&_bake_shader) Shader(shaderPath / "volumetrics_bake.comp");
	_bake_shader.link();
	assert(_bake_shader);
	_bake_shader.setPreBarrier(Shader::Barrier::Image);


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

	_accumulation.Create(s_froxels.x, s_froxels.y, s_froxels.z, GL_RGBA32F);
	_accumulation.SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::LinearMipLinear);
	_accumulation.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::LinearMipLinear);
	_accumulation.SetWrapping(TextureWrappingAxis::U, TextureWrappingParam::ClampToEdge);
	_accumulation.SetWrapping(TextureWrappingAxis::V, TextureWrappingParam::ClampToEdge);
	_accumulation.SetWrapping(TextureWrappingAxis::W, TextureWrappingParam::ClampToEdge);

	_all_volumetric_lights.resize(256); // that's a lot :)

	const auto num_tiles = s_froxels.x / FROXELS_PER_TILE * s_froxels.y / FROXELS_PER_TILE;
	_tile_lights_ranges.resize(num_tiles);
	_all_tile_lights.resize(num_tiles * FROXEL_TILE_AVG_LIGHTS);

	return *this;
}

Volumetrics::operator bool() const
{
	return _select_shader \
		and _cull_shader \
		and _inject_shader \
		and _accumulate_shader \
		and _bake_shader \
		and _blue_noise;
}

void Volumetrics::cull_lights(const Camera &camera)
{
	// first pick the volumetric lights
	// SSBO_BIND_RELEVANT_LIGHTS_INDEX -> SSBO_BIND_ALL_VOLUMETRIC_LIGHTS_INDEX
	_all_volumetric_lights.clear();

	_select_shader.setUniform("u_frustum_planes"sv, camera.frustum().planes());  // L, R, T, B, N, F
	_select_shader.invoke();

	// then assign lights to overlapping 2d tiles (groups of froxel columns)
	// SSBO_BIND_ALL_VOLUMETRIC_LIGHTS_INDEX -> SSBO_BIND_VOLUMETRIC_TILE_LIGHTS_INDEX
	camera.setUniforms(_cull_shader);

	_tile_lights_ranges.clear();
	_all_tile_lights.clear();
	_cull_shader.invoke(s_froxels.x / FROXELS_PER_TILE, s_froxels.y / FROXELS_PER_TILE);
}

void Volumetrics::inject(const Camera &camera) // TODO: View
{
	++_frame;

	camera.setUniforms(_inject_shader);

	if(_blend)
		_blue_noise.BindLayer(_frame % _blue_noise.num_layers(), 3);
	else
		_blue_noise.BindLayer(0, 3);

	// TODO: better API?
	//   _inject_zshader.bindImage("u_output_transmittance"sv, _transmittance[_frame & 1], ImageAccess::Write);
	const auto active_idx = _frame & 1;
	_transmittance[active_idx].BindImage(5, ImageAccess::Write);
	_transmittance[1 - active_idx].Bind(6);    // previous transmittance as input

	_inject_shader.setUniform("u_fog_anisotropy"sv, _anisotropy);
	_inject_shader.setUniform("u_froxel_zexp"sv,    1.f);
	_inject_shader.setUniform("u_fog_density"sv, _density);
	// _inject_shader.setUniform("u_falloff_mix"sv, _falloff_mix);
	_inject_shader.setUniform("u_froxel_blend_previous"sv, _blend);
	_inject_shader.setUniform("u_froxel_blend_weight"sv, _blend_weight);

	const auto view_projection = camera.projectionTransform() * camera.viewTransform();
	const auto inv_view_projection = glm::inverse(view_projection);
	_inject_shader.setUniform("u_inv_view_projection"sv, inv_view_projection);
	_bake_shader.setUniform("u_inv_view_projection"sv, inv_view_projection);

	static glm::mat4 prev_view = camera.viewTransform();  // use current view the first frame
	// use current for the first frame (there's no "previous" yet)
	_inject_shader.setUniform("u_prev_view"sv, prev_view);
	// now there is :)
	prev_view = camera.viewTransform();

	const auto num_groups = glm::uvec3(
		size_t(std::ceil(float(s_froxels.x) / float(s_local_size.x))),
		size_t(std::ceil(float(s_froxels.y) / float(s_local_size.y))),
		size_t(std::ceil(float(s_froxels.z) / float(s_local_size.z)))
	);

	_inject_shader.invoke(num_groups);
}

void Volumetrics::accumulate(const Camera &camera)
{
	_accumulation.BindImage(5, ImageAccess::Write);
	_transmittance[_frame & 1].Bind(6);

	_accumulate_shader.setUniform("u_near_z"sv, camera.nearPlane());
	_accumulate_shader.setUniform("u_far_z"sv, camera.farPlane());
	// for later :)
	_bake_shader.setUniform("u_near_z"sv, camera.nearPlane());
	_bake_shader.setUniform("u_far_z"sv, camera.farPlane());


	const auto num_groups = glm::uvec3(
		size_t(std::ceil(float(s_froxels.x))),
		size_t(std::ceil(float(s_froxels.y))),
		1
	);

	_accumulate_shader.invoke(num_groups);

	// TODO: blur '_accumulation'

}

void Volumetrics::render(const RenderTarget::Texture2d &, RenderTarget::Texture2d &out)
{
	_accumulation.Bind(5);  // read the accumulated transmittance
	//_transmittance[1 - (_frame & 1)].Bind(5);  // read the froxels previously written in inject()

	out.bindImage(1, ImageAccess::Write);

	_bake_shader.setUniform("u_effect_scale"sv, _strength);
	_bake_shader.setUniform("u_froxel_zexp"sv, 1.f);

	_bake_shader.invoke(size_t(std::ceil(float(out.width())  / float(s_local_size.x))),
						size_t(std::ceil(float(out.height()) / float(s_local_size.y))),
						1);

	// TODO: blur 'out'
}

const Texture3D &RGL::PP::Volumetrics::froxel_texture(uint32_t index) const
{
	if(index == 0)
		return _transmittance[_frame & 1];
	if(index == 1)
		return _transmittance[1 - (_frame & 1)];
	return _accumulation;
}

} // RGL
