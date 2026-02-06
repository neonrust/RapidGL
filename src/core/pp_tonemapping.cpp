#include "pp_tonemapping.h"

#include "filesystem.h"
#include "rendertarget_2d.h"

using namespace std::literals;

namespace RGL::PP
{

Tonemapping::Tonemapping()//uint32_t width, uint32_t height)
{
}

void Tonemapping::create()
{
	static const std::filesystem::path dir = FileSystem::getResourcesPath() / "shaders";

	_shader = std::make_shared<RGL::Shader>(dir / "FSQ.vert", dir / "tmo.frag");
	_shader->link();
	assert(*_shader.get());

	setSaturation(1.f);

	glCreateVertexArrays(1, &_dummy_vao_id);
}

Tonemapping::~Tonemapping()
{
	if (_dummy_vao_id)
		glDeleteVertexArrays(1, &_dummy_vao_id);
}

Tonemapping::operator bool() const
{
	return bool(_shader);
}

void Tonemapping::render(const RGL::RenderTarget::Texture2d &in, RGL::RenderTarget::Texture2d &out)
{
	out.bindRenderTarget();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	_shader->bind();
	in.bindTextureSampler();

	glBindVertexArray(_dummy_vao_id);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Tonemapping::setExposure(float exposure)
{
	_shader->setUniform("u_exposure"sv, exposure);
}

void Tonemapping::setGamma(float gamma)
{
	_shader->setUniform("u_gamma"sv, gamma);
}

void Tonemapping::setSaturation(float saturation)
{
	_shader->setUniform("u_saturation"sv, saturation);
}

} // RGL:PP
