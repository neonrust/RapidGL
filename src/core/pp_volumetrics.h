#pragma once

#include "postprocess.h"
#include "shader.h"
#include "texture.h"

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
	bool create();
	operator bool() const override;

	inline Shader &shader() { return _inject_shader; }
	void setCameraUniforms(const Camera &camera);

	// multiplier for the volumetrics effect
	inline void setStrength(float strength) { _strength = strength; }

	// = 0: isotropic scattering; light is scattered equally in all directions (like fog or smoke).
	// > 0: forward scattering; light tends to keep going the same way it was headed (like mist, clouds, water droplets).
	// < 0: backward scattering; light tends to scatter back toward the source (rare in nature, but can approximate retroreflective effects).
	inline void setAnisotropy(float anisotropy) { _anisotropy = anisotropy; }

	inline void setTemporalBlending(bool enable) { _blend = enable; }
	inline void setTemporalBlendWeight(float weight) { _blend_weight = weight; }

	inline void setDensity(float density) { _density = density; }

	void inject(const Camera &camera);
	void render(const RenderTarget::Texture2d &, RenderTarget::Texture2d &out) override;

	inline const Texture3D &froxel_texture(uint32_t index=0) const { return _transmittance[index? 1 - (_frame & 1): _frame & 1]; }

private:
	Shader _inject_shader;
	Shader _march_shader;
	Texture2DArray _blue_noise;
	Texture3D _transmittance[2];  // read -> write, or write <- read
	uint32_t _read_index { 0 };  // ping-pong index into '_accumulation'
	uint32_t _frame { 0 };

	float _strength { 1.f };
	float _anisotropy { 0.7f };  // ~0.7 Thin haze / atmospheric fog
	float _density { 0.1f };    // small values, less than ~0.2
	bool _blend { true };
	float _blend_weight { 0.5f };

	GLuint _dummy_vao_id;
};

} // RGL::PP
