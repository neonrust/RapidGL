#include "pp_light_scattering.h"

#include "rendertarget_2d.h"
#include "camera.h"
#include "texture.h"

namespace RGL::PP
{

bool LightScattering::create()
{
	new (&_shader) Shader("src/demos/27_clustered_shading/light_scattering.comp");
	_shader.link();

	_blue_noise.Load("resources/textures/noise.png");

	return *this;
}

LightScattering::operator bool() const
{
	return _shader and _blue_noise;
}

void LightScattering::setCameraUniforms(const Camera &camera)
{
	camera.setUniforms(shader());
}

void LightScattering::render(const RenderTarget::Texture2d &, RenderTarget::Texture2d &out)
{
	out.bindImage(1, RGL::RenderTarget::Write);

	_shader.bind();

	_blue_noise.Bind(3);

	glDispatchCompute(GLuint(glm::ceil(float(out.width()) / 8.f)),
					  GLuint(glm::ceil(float(out.height()) / 8.f)),
					  1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

} // RGL
