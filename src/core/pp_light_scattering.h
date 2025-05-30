#pragma once

#include "postprocess.h"
#include "shader.h"
#include "texture.h"

namespace RGL
{
class Camera;
} // RGL

namespace RGL::PP
{

class LightScattering : public PostProcess
{
public:
	bool create();
	operator bool() const override;

	inline Shader &shader() { return _shader; }
	void setCameraUniforms(const Camera &camera);
	
	void render(const RenderTarget::Texture2d &, RenderTarget::Texture2d &out) override;

private:
	Shader _shader;
	Texture2D _blue_noise;

	GLuint _dummy_vao_id;
};

} // RGL::PP
