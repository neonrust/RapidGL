#include "tonemapping_filter.h"

#include "rendertarget_2d.h"

TonemappingFilter::TonemappingFilter(uint32_t width, uint32_t height)
{
	m_shader = std::make_shared<RGL::Shader>("src/demos/10_postprocessing_filters/FSQ.vert", "src/demos/27_clustered_shading/tmo.frag");
	m_shader->link();

	rt = std::make_shared<Texture2DRenderTarget>();
	rt->create(width, height, GL_RGBA32F);
	glTextureParameteri(rt->m_texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
	glTextureParameteri(rt->m_texture_id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(rt->m_texture_id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glCreateVertexArrays(1, &m_dummy_vao_id);
}

TonemappingFilter::~TonemappingFilter()
{
	if (m_dummy_vao_id)
		glDeleteVertexArrays(1, &m_dummy_vao_id);
}

void TonemappingFilter::bindTexture(GLuint unit)
{
	rt->bindTexture(unit);
}

void TonemappingFilter::bindFilterFBO(GLbitfield clear_mask)
{
	rt->bindRenderTarget(clear_mask);
}

void TonemappingFilter::render(float exposure, float gamma)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	m_shader->bind();
	m_shader->setUniform("u_exposure", exposure);
	m_shader->setUniform("u_gamma", gamma);
	bindTexture();

	glBindVertexArray(m_dummy_vao_id);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}
