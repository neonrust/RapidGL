#include "pp_light_scattering.h"

#include "rendertarget_2d.h"

namespace RGL::PP
{

bool LightScattering::create()
{
	new (&_shader) Shader("src/demos/27_clustered_shading/light_scattering.comp");
	_shader.link();

	return *this;
}

LightScattering::operator bool() const
{
	return bool(_shader);
}

void LightScattering::render(const RenderTarget::Texture2d &in, RenderTarget::Texture2d &out)
{
	in.bindTextureSampler();
	out.bindImage(0, RGL::RenderTarget::Write);

	_shader.bind();

	glDispatchCompute(GLuint(glm::ceil(float(in.width()) / 64.f)),
					  GLuint(glm::ceil(float(in.height()) / 64.f)),
					  1);
}

} // RGL
