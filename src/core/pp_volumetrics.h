#pragma once

#include "camera.h"
#include "postprocess.h"
#include "pp_gaussian_blur_fixed.h"
#include "shader.h"
#include "texture.h"

#include "generated/shared-structs.h"

namespace RGL
{
class Camera;
} // RGL
class ShadowAtlas;

namespace RGL::PP
{

class Volumetrics : public PostProcess
{
public:
	Volumetrics();

	bool create();
	operator bool() const override;

	inline Shader &shader() { return _inject_shader; }

	// multiplier for the volumetrics effect
	inline void setStrength(float strength) { _strength = strength; }
	inline float falloffPower() const { return _falloff_power; }
	inline void setFalloffPower(float power) { _falloff_power = power; }
	inline void setFroxelNoiseEnabled(bool enabled) { _z_noise_enabled = enabled; }
	inline void setFroxelBlurEnabled(bool enabled) { _3dblur_enabled = enabled; }
	inline void setPostBlurEnabled(bool enabled) { _2dblur_enabled = enabled; }

	// = 0: isotropic scattering; light is scattered equally in all directions (like fog or smoke).
	// > 0: forward scattering; light tends to keep going the same way it was headed (like mist, clouds, water droplets).
	// < 0: backward scattering; light tends to scatter back toward the source (rare in nature, but can approximate retroreflective effects).
	inline void setAnisotropy(float anisotropy) { _anisotropy = anisotropy; }

	inline void setNoiseEnabled(bool enabled) { _noise_enabled = enabled; }
	inline void setNoiseOffset(glm::vec3 offset) { _noise_offset = offset; }
	inline void setNoiseFrequency(glm::vec3 freq) { _noise_freq = freq; }
	inline void setTemporalBlending(bool enable=true) { _blend_previous = enable; }
	inline void setTemporalBlendWeight(float weight) { _blend_weight = weight; }

	inline void setDensity(float density) { _density = density; }

	void setViewParams(const Camera &camera, float farPlane=0.f);
	void cull_lights();
	void inject();
	void accumulate();
	void render(const RenderTarget::Texture2d &, RenderTarget::Texture2d &out) override;

	// 0 = current injection, 1 = previous injection, 2 = depth accumulation
	const Texture3D &froxel_texture(uint32_t index=0) const;

private:
	Texture3D &blur_froxels(Texture3D &input);

private:
	Shader _select_shader;
	Shader _cull_shader;
	Shader _inject_shader;
	Shader _3dblur_shader;
	Shader _accumulate_shader;
	Shader _bake_shader;
	Texture2DArray _blue_noise;
	Texture3D _transmittance[2];  // read -> write, or write <- read
	Texture3D _accumulation;
	Texture3D _3dblur[2];
	uint32_t _read_index { 0 };  // ping-pong index into '_accumulation'
	uint32_t _frame { 0 };
	Camera _camera;

	buffer::Storage<uint> _all_volumetric_lights;
	buffer::Storage<uint> _all_tile_lights;
	buffer::Storage<IndexRange> _tile_lights_ranges;
	RGL::PP::BlurFixed<3.f> _blur3x3;

	float _strength { 0.15f };
	float _anisotropy { 0.2f };  // ~0.7 Thin haze / atmospheric fog
	float _density { 0.1f };    // small values, less than ~0.2
	bool _blend_previous { true };
	float _blend_weight { 0.95f };
	float _falloff_power { 6.f };
	bool _z_noise_enabled { true };
	bool _3dblur_enabled { true };
	bool _2dblur_enabled { false };

	GLuint _dummy_vao_id;
	bool _noise_enabled { false };
	glm::vec3 _noise_freq { 0.1f, 1.5f, 0.1f };
	glm::vec3 _noise_offset { 0.f };
};

} // RGL::PP
