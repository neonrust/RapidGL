#pragma once

#include "postprocess.h"

#include "shader.h"
#include "texture.h"


namespace RGL::PP
{

class Bloom : public PostProcess
{
public:
	bool create();
	operator bool () const override;

	inline void setThreshold(float threshold) { _threshold = threshold; }
	inline void setIntensity(float intensity) { _intensity = intensity;}
	inline void setKnee(float knee) { _knee = knee; }
	inline void setDirtIntensity(float intensity) { _dirt_intensity = intensity; }

	void render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out) override;

private:
	Shader _downscale_shader;
	Shader _upscale_shader;
	Texture2D _dirt_texture;

	float _threshold      { 0.8f };
	float _intensity      { 1.5f };
	float _knee           { 0.1f };
	float _dirt_intensity { 0.1f };
};

} // RGL::PP
