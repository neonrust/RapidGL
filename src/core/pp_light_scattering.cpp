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
	in.bindImageRead(0);
	out.bindImage(1, RGL::RenderTarget::Write);

	_shader.bind();

	glDispatchCompute(GLuint(glm::ceil(float(in.width()) / 8.f)),
					  GLuint(glm::ceil(float(in.height()) / 8.f)),
					  1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

} // RGL
