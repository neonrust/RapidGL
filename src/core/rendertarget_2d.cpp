#include "rendertarget_2d.h"

#include <cstdio>

namespace RGL::RenderTarget
{

void Texture2d::create(size_t width, size_t height, GLenum internalformat)
{
	if(texture_id())
		release();

	m_metadata.width = GLuint(width);
	m_metadata.height = GLuint(height);

	_internal_format = internalformat;

	const auto isDepth = internalformat == GL_DEPTH_COMPONENT32F
		or internalformat == GL_DEPTH_COMPONENT24
		or internalformat == GL_DEPTH_COMPONENT16;

	_mip_levels = isDepth? 1: Texture::calculateMipMapLevels(m_metadata.width, m_metadata.height, 0, _downscale_limit, _max_iterations);

	Create(GLuint(width), GLuint(height), 0, internalformat, _mip_levels);

	SetFiltering(TextureFiltering::Minify, isDepth? TextureFilteringParam::NEAREST: TextureFilteringParam::LINEAR);
	SetFiltering(TextureFiltering::Magnify, isDepth? TextureFilteringParam::NEAREST: TextureFilteringParam::LINEAR);
	SetWrapping(TextureWrappingAxis::S, TextureWrappingParam::CLAMP_TO_EDGE);
	SetWrapping(TextureWrappingAxis::T, TextureWrappingParam::CLAMP_TO_EDGE);

	glCreateFramebuffers(1, &_fbo_id);

	glNamedFramebufferTexture(_fbo_id, isDepth? GL_DEPTH_ATTACHMENT: GL_COLOR_ATTACHMENT0, texture_id(), 0);

	if(isDepth)
	{
		// disable writing color
		GLenum draw_buffers = GL_NONE;
		glNamedFramebufferDrawBuffers(_fbo_id, 1, &draw_buffers);
	}
	else
	{
		// TODO: depth buffer optional?
		glCreateRenderbuffers(1, &_rbo_id);
		glNamedRenderbufferStorage(_rbo_id, GL_DEPTH_COMPONENT32F, GLsizei(width), GLsizei(height));
		glNamedFramebufferRenderbuffer(_fbo_id, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _rbo_id);
	}
}

void Texture2d::release()
{
	Release();

	if(_fbo_id)
		glDeleteFramebuffers(1, &_fbo_id);
	_fbo_id = 0;

	if(_rbo_id)
		glDeleteRenderbuffers(1, &_rbo_id);
	_rbo_id = 0;
}

void Texture2d::bindTextureSampler(GLuint unit) const
{
	glBindTextureUnit(unit, texture_id());
}

void Texture2d::bindRenderTarget(GLbitfield clear_mask)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo_id);
	glViewport(0, 0, GLsizei(m_metadata.width), GLsizei(m_metadata.height));

	// NOTE: makes no sense to use clear_mask = GL_DEPTH_BUFFER_BIT if the framebuffer has no depth component
	glClear(clear_mask);
}

void Texture2d::bindImage(GLuint image_unit, RenderTarget::Access access, GLint mip_level)
{
	glBindImageTexture(image_unit, texture_id(), mip_level, GL_FALSE, 0, GLenum(access), _internal_format);
}

void Texture2d::copyTo(Texture2d &dest, GLbitfield mask, GLenum filter) const
{
	glBlitNamedFramebuffer(_fbo_id,
						   dest._fbo_id,
						   0, 0, GLint(width()), GLint(height()),
						   0, 0, GLint(dest.width()), GLint(dest.height()),
						   mask,
						   filter);
}

void Texture2d::copyFrom(const Texture2d &source, GLbitfield mask, GLenum filter)
{
	glBlitNamedFramebuffer(source._fbo_id,
						   _fbo_id,
						   0, 0, GLint(source.width()), GLint(source.height()),
						   0, 0, GLint(width()), GLint(height()),
						   mask,
						   filter);
}

} // RGL::RenderTarget
