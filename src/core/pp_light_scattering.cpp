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
	_shader.setPostBarrier(Shader::Barrier::Image);

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
	out.clear();
	out.bindImage(1, RenderTarget::Access::Write);

	_blue_noise.Bind(3);

	// dispatch only half resolution
	//   shader ray casts only every other pixel (in a chessboard pattern)

	static constexpr auto group_size = 8;

	_shader.invoke(GLuint(glm::ceil(float(out.width() / 2) / float(group_size))),
				   GLuint(glm::ceil(float(out.height()) / float(group_size))));
}

} // RGL
