#include "tonemapping_filter.h"

#include "rendertarget_2d.h"

TonemappingFilter::TonemappingFilter(uint32_t width, uint32_t height)
{
	_shader = std::make_shared<RGL::Shader>("src/demos/27_clustered_shading/FSQ.vert", "src/demos/27_clustered_shading/tmo.frag");
	_shader->link();

	_rt = std::make_shared<RGL::RenderTarget::Texture2d>();
	_rt->create(width, height, GL_RGBA32F);
	_rt->SetFiltering(RGL::TextureFiltering::Minify, RGL::TextureFilteringParam::LINEAR_MIP_NEAREST);
	_rt->SetWrapping (RGL::TextureWrappingAxis::S, RGL::TextureWrappingParam::CLAMP_TO_EDGE);
	_rt->SetWrapping (RGL::TextureWrappingAxis::T, RGL::TextureWrappingParam::CLAMP_TO_EDGE);

	glCreateVertexArrays(1, &_dummy_vao_id);
}

TonemappingFilter::~TonemappingFilter()
{
	if (_dummy_vao_id)
		glDeleteVertexArrays(1, &_dummy_vao_id);
}

void TonemappingFilter::bindTexture(GLuint unit)
{
	_rt->bindTextureSampler(unit);
}

void TonemappingFilter::bindRenderTarget(GLbitfield clear_mask)
{
	_rt->bindRenderTarget(clear_mask);
}

void TonemappingFilter::render(float exposure, float gamma)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	_shader->bind();
	_shader->setUniform("u_exposure", exposure);
	_shader->setUniform("u_gamma", gamma);
	bindTexture();

	glBindVertexArray(_dummy_vao_id);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}
