#include "rendertarget_2d.h"

#include <cstdio>

void Texture2DRenderTarget::create(uint32_t width, uint32_t height, GLenum internalformat)
{
	m_width          = GLsizei(width);
	m_height         = GLsizei(height);
	m_internalformat = internalformat;

	m_mip_levels = calculateMipmapLevels();

	glCreateFramebuffers(1, &m_fbo_id);

	glCreateTextures(GL_TEXTURE_2D, 1, &m_texture_id);
	glTextureStorage2D(m_texture_id, m_mip_levels, internalformat, m_width, m_height); // internalformat = GL_RGB32F

	glTextureParameteri(m_texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(m_texture_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(m_texture_id, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
	glTextureParameteri(m_texture_id, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

	glCreateRenderbuffers(1, &m_rbo_id);
	glNamedRenderbufferStorage(m_rbo_id, GL_DEPTH_COMPONENT32F, m_width, m_height);

	glNamedFramebufferTexture(m_fbo_id, GL_COLOR_ATTACHMENT0, m_texture_id, 0);
	glNamedFramebufferRenderbuffer(m_fbo_id, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_rbo_id);
}

void Texture2DRenderTarget::cleanup()
{
	if (m_texture_id != 0)
	{
		glDeleteTextures(1, &m_texture_id);
	}

	if (m_fbo_id != 0)
	{
		glDeleteFramebuffers(1, &m_fbo_id);
	}

	if (m_rbo_id != 0)
	{
		glDeleteRenderbuffers(1, &m_rbo_id);
	}
}

void Texture2DRenderTarget::bindTexture(GLuint unit)
{
	glBindTextureUnit(unit, m_texture_id);
}

void Texture2DRenderTarget::bindRenderTarget(GLbitfield clear_mask)
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_id);
	glViewport(0, 0, m_width, m_height);
	glClear(clear_mask);
}

void Texture2DRenderTarget::bindImageForRead(GLuint image_unit, GLint mip_level)
{
	glBindImageTexture(image_unit, m_texture_id, mip_level, GL_FALSE, 0, GL_READ_ONLY, m_internalformat);
}

void Texture2DRenderTarget::bindImageForWrite(GLuint image_unit, GLint mip_level)
{
	glBindImageTexture(image_unit, m_texture_id, mip_level, GL_FALSE, 0, GL_WRITE_ONLY, m_internalformat);
}

void Texture2DRenderTarget::bindImageForReadWrite(GLuint image_unit, GLint mip_level)
{
	glBindImageTexture(image_unit, m_texture_id, mip_level, GL_FALSE, 0, GL_READ_WRITE, m_internalformat);
}

uint8_t Texture2DRenderTarget::calculateMipmapLevels()
{
	uint32_t width      = uint32_t(m_width  / 2);
	uint32_t height     = uint32_t(m_height / 2);
	uint8_t  mip_levels = 1;

	std::printf("Mip level %d: %d x %d\n", 0, m_width, m_height);
	std::printf("Mip level %d: %d x %d\n", mip_levels, width, height);

	for (uint8_t i = 0; i < m_max_iterations; ++i)
	{
		width  = width  / 2;
		height = height / 2;

		if (width < m_downscale_limit || height < m_downscale_limit) break;

		++mip_levels;

		std::printf("Mip level %d: %d x %d\n", mip_levels, width, height);
	}

	return mip_levels + 1;
}
