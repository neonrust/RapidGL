#pragma once

#include "postprocess.h"
#include "rendertarget_2d.h"
#include "shader.h"
#include "ssbo.h"
#include "texture.h"

namespace RGL::PP
{

struct Tonemapping : public PostProcess
{
	Tonemapping();
	~Tonemapping();

	void create(uint32_t width, uint32_t height);

	operator bool() const override;

	void setBrightness(float brighrness);
	void setFrameTime(float frame_time);
	void setGamma(float gamma);
	void setSaturation(float saturation);
	void setLumaRange(float darkest, float brightest);
	void setAdaptationRate(float units_per_second_to_brighter,float units_per_second_to_darker);

	void render(const RGL::RenderTarget::Texture2d &in, RGL::RenderTarget::Texture2d &out) override;


private:
	std::vector<float> generateWeights(glm::uvec2 grid_size);

private:
	float _frame_time;
	RGL::Shader _calc_exposure;
	RGL::RenderTarget::Texture2d _filter_rt;
	uint32_t _filter_lod;
	RGL::buffer::Storage<float> _exposure_regulation_ssbo;
	RGL::Shader _tmo_shader;

	GLuint _dummy_vao_id;

};

} // RGL::PP
