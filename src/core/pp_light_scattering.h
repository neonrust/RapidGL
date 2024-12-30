#pragma once

#include "postprocess.h"
#include "shader.h"

namespace RGL::PP
{

class LightScattering : public PostProcess
{
public:
	bool create();
	operator bool() const override;

	inline Shader &shader() { return _shader; }

	void render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out) override;

private:
	Shader _shader;

	GLuint _dummy_vao_id;
};

} // RGL::PostProcess
