#include "rendertarget_2d.h"

#include <cstdio>

namespace RGL::RenderTarget
{

// relevant for bloom downscale/upscale (blur)
static constexpr uint8_t s_downscale_limit { 8 }; // essentially max blur radius TODO: should be screen size dependent
static constexpr uint8_t s_max_iterations { 18 }; // max mipmap levels


void Texture2d::create(size_t width, size_t height, Format format)
{
	if(texture_id())
		release();

	m_metadata.width = GLuint(width);
	m_metadata.height = GLuint(height);

	bool depthOnly = false;
	if(format & ColorFloat)
	{
		_internal_format = GL_RGBA32F;
		_has_color = true;
	}
	else if(format & Color)
	{
		_internal_format = GL_RGBA;
		_has_color = true;
	}
	else if(format & Depth)
	{
		_internal_format = GL_DEPTH_COMPONENT32F;
		depthOnly = true;
		_mip_levels = 1;
		_has_color = false;
		_has_depth = true;
	}
	else
		assert(false);

	if(not depthOnly)
		_mip_levels = Texture::calculateMipMapLevels(m_metadata.width, m_metadata.height, 0, s_downscale_limit, s_max_iterations);

	Create(GLuint(width), GLuint(height), 0, _internal_format, _mip_levels);

	SetFiltering(TextureFiltering::Minify, depthOnly? TextureFilteringParam::NEAREST: TextureFilteringParam::LINEAR);
	SetFiltering(TextureFiltering::Magnify, depthOnly? TextureFilteringParam::NEAREST: TextureFilteringParam::LINEAR);
	SetWrapping(TextureWrappingAxis::S, TextureWrappingParam::CLAMP_TO_EDGE);
	SetWrapping(TextureWrappingAxis::T, TextureWrappingParam::CLAMP_TO_EDGE);

	glCreateFramebuffers(1, &_fbo_id);

	glNamedFramebufferTexture(_fbo_id, depthOnly? GL_DEPTH_ATTACHMENT: GL_COLOR_ATTACHMENT0, texture_id(), 0);

	if(depthOnly)
	{
		// disable writing color
		GLenum draw_buffers = GL_NONE;
		glNamedFramebufferDrawBuffers(_fbo_id, 1, &draw_buffers);
	}
	else if(format & Depth)
	{
		glCreateRenderbuffers(1, &_rbo_id);
		glNamedRenderbufferStorage(_rbo_id, GL_DEPTH_COMPONENT32F, GLsizei(width), GLsizei(height));
		glNamedFramebufferRenderbuffer(_fbo_id, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _rbo_id);
		_has_depth = true;
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

void Texture2d::bindRenderTarget(BufferMask clear_buffers)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo_id);
	glViewport(0, 0, GLsizei(m_metadata.width), GLsizei(m_metadata.height));

	// NOTE: makes no sense to use clear_mask = GL_DEPTH_BUFFER_BIT if the framebuffer has no depth component
	glClear(clear_buffers);
}

void Texture2d::bindImage(GLuint image_unit, RenderTarget::Access access, GLint mip_level)
{
	glBindImageTexture(image_unit, texture_id(), mip_level, GL_FALSE, 0, GLenum(access), _internal_format);
}

void Texture2d::bindImageRead(GLuint image_unit, GLint mip_level) const
{
	glBindImageTexture(image_unit, texture_id(), mip_level, GL_FALSE, 0, GL_READ_ONLY, _internal_format);
}

void Texture2d::copyTo(Texture2d &dest, GLbitfield mask, GLenum filter) const
{
	if(not _has_depth)
		mask &= ~GLbitfield(GL_DEPTH_BUFFER_BIT);
	if(not _has_color)
		mask &= ~GLbitfield(GL_COLOR_BUFFER_BIT);

	glBlitNamedFramebuffer(_fbo_id,
						   dest._fbo_id,
						   0, 0, GLint(width()), GLint(height()),
						   0, 0, GLint(dest.width()), GLint(dest.height()),
						   mask,
						   filter);
}

void Texture2d::copyFrom(const Texture2d &source, GLbitfield mask, GLenum filter)
{
	source.copyTo(*this, mask, filter);
}

void Texture2d::clear()
{
	// TODO
	if(_has_color)
	{
		const GLfloat clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // RGBA
		glClearNamedFramebufferfv(_fbo_id, GL_COLOR, 0, clearColor);
	}
	if(_has_depth)
	{
		const GLfloat clearDepth = 1.0f;
		glClearNamedFramebufferfv(_fbo_id, GL_DEPTH, 0, &clearDepth);
	}
}

} // RGL::RenderTarget
