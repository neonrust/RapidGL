#include "rendertarget_2d.h"

#include <cstdio>

namespace RGL
{

namespace RenderTarget
{

void Texture2d::create(size_t width, size_t height, GLenum internalformat)
{
	if(_texture_id)
		cleanup();

	_width           = GLsizei(width);
	_height          = GLsizei(height);
	_internal_format = internalformat;

	const auto isDepth = internalformat == GL_DEPTH_COMPONENT32F
		or internalformat == GL_DEPTH_COMPONENT24
		or internalformat == GL_DEPTH_COMPONENT16;

	_mip_levels = isDepth? 1: calculateMipmapLevels();

	glCreateFramebuffers(1, &_fbo_id);

	glCreateTextures(GL_TEXTURE_2D, 1, &_texture_id);
	glTextureStorage2D(_texture_id, _mip_levels, internalformat, _width, _height); // internalformat = GL_RGB32F

	glTextureParameteri(_texture_id, GL_TEXTURE_MIN_FILTER, isDepth? GL_NEAREST: GL_LINEAR);
	glTextureParameteri(_texture_id, GL_TEXTURE_MAG_FILTER, isDepth? GL_NEAREST: GL_LINEAR);
	glTextureParameteri(_texture_id, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
	glTextureParameteri(_texture_id, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

	glCreateRenderbuffers(1, &_rbo_id);

	glNamedRenderbufferStorage(_rbo_id, GL_DEPTH_COMPONENT32F, _width, _height);

	if(isDepth)
	{
		glNamedFramebufferTexture(_fbo_id, GL_DEPTH_ATTACHMENT, _texture_id, 0);
		// render no colors
		GLenum draw_buffers[] = { GL_NONE };
		glNamedFramebufferDrawBuffers(_fbo_id, 1, draw_buffers);
	}
	else
	{
		glNamedFramebufferTexture(_fbo_id, GL_COLOR_ATTACHMENT0, _texture_id, 0);
		glNamedFramebufferRenderbuffer(_fbo_id, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _rbo_id);
	}
}

void Texture2d::cleanup()
{
	if(_texture_id)
		glDeleteTextures(1, &_texture_id);
	_texture_id = 0;

	if(_fbo_id)
		glDeleteFramebuffers(1, &_fbo_id);
	_fbo_id = 0;

	if(_rbo_id)
		glDeleteRenderbuffers(1, &_rbo_id);
	_rbo_id = 0;
}

void Texture2d::bindTexture(GLuint unit)
{
	glBindTextureUnit(unit, _texture_id);
}

void Texture2d::bindRenderTarget(GLbitfield clear_mask)
{
	glBindFramebuffer(GL_FRAMEBUFFER, _fbo_id);
	glViewport(0, 0, _width, _height);
	glClear(clear_mask);
}

// void RenderTargetTexture2d::bindImageForRead(GLuint image_unit, GLint mip_level)
// {
// 	glBindImageTexture(image_unit, _texture_id, mip_level, GL_FALSE, 0, GL_READ_ONLY, _internal_format);
// }

// void RenderTargetTexture2d::bindImageForWrite(GLuint image_unit, GLint mip_level)
// {
// 	glBindImageTexture(image_unit, _texture_id, mip_level, GL_FALSE, 0, GL_WRITE_ONLY, _internal_format);
// }

// void RenderTargetTexture2d::bindImageForReadWrite(GLuint image_unit, GLint mip_level)
// {
// 	glBindImageTexture(image_unit, _texture_id, mip_level, GL_FALSE, 0, GL_READ_WRITE, _internal_format);
// }

void Texture2d::bindMipImage(GLuint image_unit, GLint mip_level, RenderTarget::Access access)
{
	glBindImageTexture(image_unit, _texture_id, mip_level, GL_FALSE, 0, GLenum(access), _internal_format);
}

uint8_t Texture2d::calculateMipmapLevels()
{
	auto width      = _width  / 2;
	auto height     = _height / 2;
	uint8_t  mip_levels = 1;

	for (uint8_t i = 0; i < _max_iterations; ++i)
	{
		width  = width  / 2;
		height = height / 2;

		if (width < _downscale_limit || height < _downscale_limit) break;

		++mip_levels;
	}

	return mip_levels + 1;
}

void Texture2d::copyTo(Texture2d &dest, GLbitfield mask, GLenum filter) const
{
	glBlitNamedFramebuffer(framebuffer_id(),
						   dest.framebuffer_id(),
						   0, 0, width(), height(),
						   0, 0, dest.width(), dest.height(),
						   mask,
						   filter);
}

} // RenderTarget

} // RGL
