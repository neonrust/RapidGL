
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
static constexpr glm::uvec3 s_local_size { FROXEL_THREADS_X, FROXEL_THREADS_Y, FROXEL_THREADS_Z };

Volumetrics::Volumetrics() :
	_all_volumetric_lights("volumetric-lights"sv),
	_all_tile_lights("volumetric-all-tile-lights"sv),
	_tile_lights_ranges("volumetric-tile-light-ranges"sv)
{
	_all_volumetric_lights.bindAt(SSBO_BIND_ALL_VOLUMETRIC_LIGHTS_INDEX);
	_all_tile_lights.bindAt(SSBO_BIND_VOLUMETRIC_ALL_TILE_LIGHTS_INDEX);
	_tile_lights_ranges.bindAt(SSBO_BIND_VOLUMETRIC_TILE_LIGHTS_INDEX);
}

bool Volumetrics::create()
{
	const auto shader_dir = FileSystem::getResourcesPath() / "shaders";

	new (&_select_shader) Shader(shader_dir / "volumetrics_select_lights.comp");
	_select_shader.link();
	assert(_select_shader);
	_select_shader.setPreBarrier(Shader::Barrier::SSBO);

	new (&_cull_shader) Shader(shader_dir / "volumetrics_cull.comp");
	_cull_shader.link();
	assert(_cull_shader);
	_cull_shader.setPreBarrier(Shader::Barrier::SSBO);

	new (&_inject_shader) Shader(shader_dir / "volumetrics_inject.comp");
	_inject_shader.link();
	assert(_inject_shader);
	_inject_shader.setPreBarrier(Shader::Barrier::SSBO);

	new (&_3dblur_shader) Shader(shader_dir / "blur_3d.comp");
	_3dblur_shader.link();
	assert(_3dblur_shader);
	_3dblur_shader.setPreBarrier(Shader::Barrier::Image);
	_3dblur_shader.setUniform("u_grid_size"sv, glm::ivec3(s_froxels));

	new (&_accumulate_shader) Shader(shader_dir / "volumetrics_accumulate.comp");
	_accumulate_shader.link();
	assert(_accumulate_shader);
	_accumulate_shader.setPreBarrier(Shader::Barrier::Image);

	new (&_bake_shader) Shader(shader_dir / "volumetrics_bake.comp");
	_bake_shader.link();
	assert(_bake_shader);
	_bake_shader.setPreBarrier(Shader::Barrier::Image);


	_blue_noise.Load("resources/textures/blue-noise.array");

	for(auto idx = 0u; idx < 2; ++idx)
	{
		_transmittance[idx].Create(s_froxels.x, s_froxels.y, s_froxels.z, GL_RGBA16F);
		_transmittance[idx].SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
		_transmittance[idx].SetFiltering(TextureFiltering::Minify, TextureFilteringParam::Linear);
		_transmittance[idx].SetWrapping(TextureWrappingAxis::U, TextureWrappingParam::ClampToEdge);
		_transmittance[idx].SetWrapping(TextureWrappingAxis::V, TextureWrappingParam::ClampToEdge);
		_transmittance[idx].SetWrapping(TextureWrappingAxis::W, TextureWrappingParam::ClampToEdge);

		_3dblur[idx].Create(s_froxels.x, s_froxels.y, s_froxels.z, GL_RGBA16F);
		_3dblur[idx].SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
		_3dblur[idx].SetFiltering(TextureFiltering::Minify, TextureFilteringParam::Linear);
		_3dblur[idx].SetWrapping(TextureWrappingAxis::U, TextureWrappingParam::ClampToEdge);
		_3dblur[idx].SetWrapping(TextureWrappingAxis::V, TextureWrappingParam::ClampToEdge);
		_3dblur[idx].SetWrapping(TextureWrappingAxis::W, TextureWrappingParam::ClampToEdge);
	}

	_accumulation.Create(s_froxels.x, s_froxels.y, s_froxels.z, GL_RGBA16F);
	_accumulation.SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
	_accumulation.SetFiltering(TextureFiltering::Minify, TextureFilteringParam::Linear);
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

void Volumetrics::setViewParams(const Camera &camera, float farPlane)   // TODO: View
{
	_camera = camera;
	if(farPlane > 0)
		_camera.setFarPlane(farPlane);
}

void Volumetrics::cull_lights()
{
	// first pick the volumetric lights
	// SSBO_BIND_RELEVANT_LIGHTS_INDEX -> SSBO_BIND_ALL_VOLUMETRIC_LIGHTS_INDEX
	_all_volumetric_lights.clear();

	// TODO: only needed when camera / lights have moved (however, this is a very fast operation)

	_camera.setUniforms(_select_shader);
	_select_shader.setUniform("u_volumetric_max_distance"sv, _camera.farPlane());
	_select_shader.invoke();

	// then assign lights to overlapping 2d tiles (groups of froxel columns)
	// SSBO_BIND_ALL_VOLUMETRIC_LIGHTS_INDEX -> SSBO_BIND_VOLUMETRIC_TILE_LIGHTS_INDEX
	_camera.setUniforms(_cull_shader);

	_tile_lights_ranges.clear();
	_all_tile_lights.clear();
	_cull_shader.setUniform("u_frustum_corners"sv, _camera.frustum().corners());
	_cull_shader.invoke(s_froxels.x / FROXELS_PER_TILE, s_froxels.y / FROXELS_PER_TILE);
}

void Volumetrics::inject()
{
	++_frame;

	const auto active_idx = _frame & 1;

	// TODO: better API?
	//   _inject_zshader.bindImage("u_output_transmittance"sv, _transmittance[_frame & 1], ImageAccess::Write);
	_transmittance[active_idx].BindImage(5, ImageAccess::Write);
	_transmittance[1 - active_idx].Bind(6);    // previous transmittance as input

	_camera.setUniforms(_inject_shader);
	_inject_shader.setUniform("u_frame_index"sv, _frame);
	_inject_shader.setUniform("u_falloff_power"sv, _falloff_power);
	_inject_shader.setUniform("u_fog_anisotropy"sv, _anisotropy);
	_inject_shader.setUniform("u_froxel_zexp"sv,    1.f);
	_inject_shader.setUniform("u_fog_density"sv, _density);
	// _inject_shader.setUniform("u_falloff_mix"sv, _falloff_mix);
	_inject_shader.setUniform("u_froxel_z_noise"sv, _z_noise_enabled);
	_inject_shader.setUniform("u_fog_noise"sv, true);
	_inject_shader.setUniform("u_fog_noise_offset"sv, 0.f);
	_inject_shader.setUniform("u_fog_noise_frequency"sv, 1.f);
	_inject_shader.setUniform("u_froxel_blend_previous"sv, _blend_previous);
	_inject_shader.setUniform("u_froxel_blend_weight"sv, _blend_weight);

	_inject_shader.setUniform("u_volumetric_max_distance"sv, _camera.farPlane());

	const auto view_projection = _camera.projectionTransform() * _camera.viewTransform();
	const auto inv_view_projection = glm::inverse(view_projection);
	_inject_shader.setUniform("u_inv_view_projection"sv, inv_view_projection);

	static glm::mat4 prev_view = _camera.viewTransform();  // use current view the first frame
	// use current for the first frame (there's no "previous" yet)
	_inject_shader.setUniform("u_prev_view"sv, prev_view);
	prev_view = _camera.viewTransform();

	_blue_noise.BindLayer(_frame % _blue_noise.num_layers(), 3);

	const auto num_groups = glm::uvec3(
		size_t(std::ceil(float(s_froxels.x) / float(s_local_size.x))),
		size_t(std::ceil(float(s_froxels.y) / float(s_local_size.y))),
		size_t(std::ceil(float(s_froxels.z) / float(s_local_size.z)))
	);

	_inject_shader.invoke(num_groups);
}

void Volumetrics::accumulate()
{
	const auto active_idx = _frame & 1;

	if(_3dblur_enabled)
	{
		auto &blur_output = blur_froxels(_transmittance[active_idx]);
		blur_output.BindImage(6, ImageAccess::Read);
	}
	else
		_transmittance[active_idx].BindImage(6, ImageAccess::Read);

	_accumulation.BindImage(5, ImageAccess::Write);

	_accumulate_shader.setUniform("u_near_z"sv, _camera.nearPlane());
	_accumulate_shader.setUniform("u_far_z"sv, _camera.farPlane());
	// for later :)
	_bake_shader.setUniform("u_near_z"sv, _camera.nearPlane());
	_bake_shader.setUniform("u_far_z"sv, _camera.farPlane());

	const auto num_groups = glm::uvec3(
		size_t(std::ceil(float(s_froxels.x) / float(s_local_size.x))),
		size_t(std::ceil(float(s_froxels.y) / float(s_local_size.y))),
		1
	);
	_accumulate_shader.invoke(num_groups);
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

	if(_2dblur_enabled)
	{
		if(not _blur3x3)
		{
			_blur3x3.create(out.width(), out.height());
			assert(_blur3x3);
		}
		_blur3x3.render(out, out);
		// _blur3x3.render(out, out);
		// _blur3x3.render(out, out);
	}
}

const Texture3D &RGL::PP::Volumetrics::froxel_texture(uint32_t index) const
{
	if(index == 0)
		return _transmittance[_frame & 1];
	if(index == 1)
		return _transmittance[1 - (_frame & 1)];
	return _accumulation;
}

Texture3D &Volumetrics::blur_froxels(Texture3D &input)
{
	const auto num_groups = glm::uvec3(
		size_t(std::ceil(float(s_froxels.x) / float(s_local_size.x))),
		size_t(std::ceil(float(s_froxels.y) / float(s_local_size.y))),
		size_t(std::ceil(float(s_froxels.z) / float(s_local_size.z)))
	);

	// 1st pass
	// input-> blur[0]
	input.BindImage(     0, ImageAccess::Read);
	_3dblur[0].BindImage(1, ImageAccess::Write);
	_3dblur_shader.setUniform("u_axis"sv, 0u);  // X axis
	_3dblur_shader.invoke(num_groups);

	// blur[0] -> blur[1]
	_3dblur[0].BindImage(0, ImageAccess::Read);
	_3dblur[1].BindImage(1, ImageAccess::Write);
	_3dblur_shader.setUniform("u_axis"sv, 1u);  // Y axis
	_3dblur_shader.invoke(num_groups);

	// blur[1] -> blur[0]
	_3dblur[1].BindImage(0, ImageAccess::Read);
	_3dblur[0].BindImage(1, ImageAccess::Write);
	_3dblur_shader.setUniform("u_axis"sv, 2u);  // Z axis
	_3dblur_shader.invoke(num_groups);

	return _3dblur[0];

	// // 2nd pass
	// // blur[0] -> blur[1]
	// _3dblur[0].BindImage(0, ImageAccess::Write);
	// _3dblur[1].BindImage(1, ImageAccess::Write);
	// _3dblur_shader.setUniform("u_axis"sv, 0u);  // X axis
	// _3dblur_shader.invoke(num_groups);

	// // blur[1] -> blur[0]
	// _3dblur[1].BindImage(0, ImageAccess::Read);
	// _3dblur[0].BindImage(1, ImageAccess::Write);
	// _3dblur_shader.setUniform("u_axis"sv, 1u);  // Z axis
	// _3dblur_shader.invoke(num_groups);

	// // blur[0] -> blur[1]
	// _3dblur[0].BindImage(0, ImageAccess::Read);
	// _3dblur[1].BindImage(1, ImageAccess::Write);
	// _3dblur_shader.setUniform("u_axis"sv, 2u);  // Y axis
	// _3dblur_shader.invoke(num_groups);

	// return _3dblur[1];
}

} // RGL
