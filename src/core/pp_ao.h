#pragma once

#include "postprocess.h"
#include "shader.h"
#include "texture.h"

#include <glm/mat4x4.hpp>

namespace RGL
{
class Texture;
} // RGL

namespace RGL::PP
{

// Quality:
//   -> Medium  = 2 slices and 2 steps
//   High       = 3 slices and 3 steps
//   Super ->   = 4 slices and 3 steps

// 3 to 4 is the sweet spot
static constexpr uint32_t s_max_denoise_passes { 5 };

class GroundTruthAmbientOcclusion : public PostProcess
{
public:
	inline GroundTruthAmbientOcclusion() {};

	void resize(uint32_t width, uint32_t height) override;

	inline float radius() const { return _radius; }
	inline void setRadius(float radius) { _radius = radius; }

	inline float falloff() const { return _falloff; }
	inline void setFalloff(float falloff) { _falloff = falloff; }

	inline float radiusMul() const { return _radius_mul; }
	inline void setRadiusMul(float mul) { _radius_mul = mul; }

	inline float finalValuePower() const { return _final_value_power; }
	inline void setFinalValuePower(float power) { _final_value_power = power; }

	inline uint32_t denoisePassCount() const { return _denoise_pass_count; }
	void setDenoisePassCount(uint32_t count);

	inline float denoiseBlurBeta() const { return _denoise_blur_beta; }
	inline void setDenoiseBlurBeta(float beta) { _denoise_blur_beta = beta; }

	inline float sampleDistPower() const { return _sample_dist_power; }
	inline void setSampleDistPower(float power) { _sample_dist_power = power; }

	inline float thinOccluderComp() const { return _thin_occluder_comp; }
	inline void setThinOccluderComp(float comp) { _thin_occluder_comp = comp; }

	inline float depthMipSampleOffset() const { return _depth_nip_sample_offset; }
	inline void setDepthMipSampleOffset(float offset) { _depth_nip_sample_offset = offset; }

	inline uint32_t sliceCount() const { return _slice_count; }
	inline void setSliceCount(uint32_t count) { _slice_count = count; }

	inline uint32_t stepsPerSlice() const { return _steps_per_slice; }
	inline void setStepsPerSlice(uint32_t steps) { _steps_per_slice = steps; }

	void setProjection(const glm::mat4 &projection);

	void setQuality(Quality quality) override;

	void renderTexture(const Texture2D &in, Texture2D &out);

	operator bool() const override;

	const Texture2D &linearDepthTexture() { return _linearDepth; }
	const Texture2D &filteredDepthTexture() { return _filteredDepth; }
	const Texture2D &noisyTexture(uint_fast8_t idx) { assert(idx <= 1); return _noisy[idx]; }

private:
	void render(const RenderTarget::Texture2d &, RenderTarget::Texture2d &) override {}

	void linearizeDepth(const Texture2D &sceneDepth);
	void prefilterDepth();
	void renderNoisy();
	void denoise(Texture2D &out);

	bool loadShaders() override;

private:
	float    _radius { 0.5f };                  // EffectRadius, world-space sample radius
	float    _falloff { 0.615f };               // EffectFalloffRange, fraction of radius over which the weight tapers
	float    _radius_mul { 1.457f };            // RadiusMultiplier, per-MIP scale
	float    _final_value_power { 2.2f };       // FinalValuePower, non-linear contrast on the final term
	uint32_t _denoise_pass_count { 3 };         // Total denoise dispatches, >=1 (final apply), clamped to GTAO_MAX_DENOISE_PASSES
	float    _denoise_blur_beta { 1.2f };       // DenoiseBlurBeta, bilateral blur strength
	float    _sample_dist_power { 2.0f };       // SampleDistributionPower, exponent on the slice-sampling distribution
	float    _thin_occluder_comp { 0 };         // ThinOccluderCompensation, extra weight for thin features
	float    _depth_nip_sample_offset { 3.3f }; // DepthMIPSamplingOffset, bias on the prefilter MIP selection
	uint32_t _slice_count { 3 };                // SliceCount, trace slice count (Medium=2, High=3, Ultra=9)
	uint32_t _steps_per_slice { 3 };             // StepsPerSlice, trace steps per slice (Medium=2, High=3, Ultra=3)

	glm::mat4 _projection { 1.f };

	Texture2D _linearDepth;
	Texture2D _filteredDepth;
	Texture2D _filteredMips[5];  // views into mips of '_filteredDepth'
	Texture2D _edges;
	Texture2D _noisy[2];

	Shader _linearize_shader;
	Shader _prefilter_shader;
	Shader _main_shader;
	Shader _denoise_shader;
};





} // RGL::PP
