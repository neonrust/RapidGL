#pragma once

#include "postprocess.h"
#include "shader.h"

namespace RGL::RenderTarget
{
class Texture2d;
}

namespace RGL::PP
{

struct Tonemapping : public PostProcess
{
	Tonemapping();
	~Tonemapping();

	void create();

	operator bool() const override;

	void setExposure(float exposure);
	void setGamma(float gamma);
	void setSaturation(float saturation);

	void render(const RGL::RenderTarget::Texture2d &in, RGL::RenderTarget::Texture2d &out) override;


private:
	std::shared_ptr<RGL::Shader> _shader;

	GLuint _dummy_vao_id;

};

} // RGL::PP
