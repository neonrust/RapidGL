#include "rendertarget_cube.h"

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

#include "constants.h"


namespace RGL::RenderTarget
{

Cube::Cube()
{
	set_position({ 0, 0, 0 });
}

void Cube::create(const char *name, uint32_t width, uint32_t height, Color::Config color_cfg, Depth::Config depth_cfg)
{
	_name = name;
	if(_fbo_id)
		release();

	// TODO: merge with RenderTarget::Texture2d (it's almost identical)

	_has_color = color_cfg != Color::None;
	_has_depth = depth_cfg != Depth::None;

	assert(_has_color or _has_depth);

	_width  = GLsizei(width);
	_height = GLsizei(height);

	if(color_cfg & Color::Texture or depth_cfg & Depth::Texture)
		_mip_levels = Texture::calculateMipMapLevels(width, height);
	else
		_mip_levels = 1;

	if(_has_color)
	{
		if(Color::is_custom(color_cfg))
			_color_format = Color::custom_mask & color_cfg;
		else
			_color_format = (color_cfg & Color::Float) ? GL_RGBA32F : GL_RGBA;
	}
	if(_has_depth)
		_depth_format = GL_DEPTH_COMPONENT32F; // only float supported

	if(_has_color and (color_cfg & Color::Texture))
	{
		// TODO: bake this into add_component() ?
		bool ok = _color_texture.Create(width, height, _color_format, _mip_levels);
		assert(ok);

		_color_texture.SetFiltering(RGL::TextureFiltering::Minify,  RGL::TextureFilteringParam::Linear);
		_color_texture.SetFiltering(RGL::TextureFiltering::Magnify, RGL::TextureFilteringParam::Linear);
		_color_texture.SetWrapping (RGL::TextureWrappingAxis::U,    RGL::TextureWrappingParam::ClampToEdge);
		_color_texture.SetWrapping (RGL::TextureWrappingAxis::V,    RGL::TextureWrappingParam::ClampToEdge);
	}

	if(_has_depth and (depth_cfg & Depth::Texture))
	{
		// TODO: bake this into add_component() ?
		bool ok = _depth_texture.Create(width, height, _depth_format, _mip_levels);
		assert(ok);

		_depth_texture.SetFiltering(RGL::TextureFiltering::Minify,  RGL::TextureFilteringParam::Nearest);
		_depth_texture.SetFiltering(RGL::TextureFiltering::Magnify, RGL::TextureFilteringParam::Nearest);
		_depth_texture.SetWrapping (RGL::TextureWrappingAxis::U,    RGL::TextureWrappingParam::ClampToEdge);
		_depth_texture.SetWrapping (RGL::TextureWrappingAxis::V,    RGL::TextureWrappingParam::ClampToEdge);
		_depth_texture.SetBorderColor(1, 1, 1, 1);
	}

	glCreateFramebuffers(1, &_fbo_id);

	if(_has_color)
		attach(GL_COLOR_ATTACHMENT0, _color_format, _color_texture.texture_id(), _color_rbo_id);
	if(_has_depth)
		attach(GL_DEPTH_ATTACHMENT, _depth_format, _depth_texture.texture_id(), _depth_rbo_id);

	if(_has_color)
		glNamedFramebufferDrawBuffer(_fbo_id, GL_COLOR_ATTACHMENT0);
	else
		glNamedFramebufferDrawBuffer(_fbo_id, GL_NONE);  // disable writing color
	glNamedFramebufferReadBuffer(_fbo_id, GL_NONE);

	check_fbo(_fbo_id);
#if !defined(NDEBUG)
	dump_config(_name, _fbo_id);
#endif
}

// void Cube::attach_color(uint32_t index, GLenum internal_format, bool as_texture)
// {
// 	_color_format = internal_format;

// 	if(as_texture)
// 	{
// 		bool ok = _color_texture.Create(_width, _height, _color_format, _mip_levels);
// 		assert(ok);

// 		_color_texture.SetFiltering(RGL::TextureFiltering::Minify,  RGL::TextureFilteringParam::Linear);
// 		_color_texture.SetFiltering(RGL::TextureFiltering::Magnify, RGL::TextureFilteringParam::Linear);
// 		_color_texture.SetWrapping (RGL::TextureWrappingAxis::U,    RGL::TextureWrappingParam::ClampToEdge);
// 		_color_texture.SetWrapping (RGL::TextureWrappingAxis::V,    RGL::TextureWrappingParam::ClampToEdge);

// 		glNamedFramebufferTextureLayer(_fbo_id, GL_COLOR_ATTACHMENT0 + index, _color_texture.texture_id(), 0, 0);
// 	}
// 	else
// 	{
// 		glCreateRenderbuffers(1, &_color_rbo_id);
// 		assert(_color_rbo_id);
// 		glNamedRenderbufferStorage(_color_rbo_id, _color_format, _width, _height);

// 		glNamedFramebufferRenderbuffer(_fbo_id, GL_COLOR_ATTACHMENT0 + index, GL_RENDERBUFFER, _color_rbo_id);
// 	}
// }

void Cube::attach(GLenum attachment, GLenum internal_format, GLuint texture_id, GLuint &rbo_id)
{
	if(texture_id)
		glNamedFramebufferTextureLayer(_fbo_id, attachment, texture_id, 0, 0);
	else
	{
		glCreateRenderbuffers(1, &rbo_id);
		assert(rbo_id);
		glNamedRenderbufferStorage(rbo_id, internal_format, _width, _height);
		glNamedFramebufferRenderbuffer(_fbo_id, attachment, GL_RENDERBUFFER, rbo_id);
	}
}

void Cube::set_position(const glm::vec3 pos)
{
	_position = pos;
	_view_transforms[0] = glm::lookAt(pos, pos + AXIS_X, -AXIS_Y);
	_view_transforms[1] = glm::lookAt(pos, pos - AXIS_X, -AXIS_Y);
	_view_transforms[2] = glm::lookAt(pos, pos + AXIS_Y,  AXIS_Z);
	_view_transforms[3] = glm::lookAt(pos, pos - AXIS_Y, -AXIS_Z);
	_view_transforms[4] = glm::lookAt(pos, pos + AXIS_Z, -AXIS_Y);
	_view_transforms[5] = glm::lookAt(pos, pos - AXIS_Z, -AXIS_Y);

	_projection = glm::perspective(glm::radians(90.f), 1.f, 0.1f, 10.f);
}

void Cube::resizeDepth(size_t width, size_t height)
{
	assert(_depth_rbo_id);
	glNamedRenderbufferStorage(_depth_rbo_id, GL_DEPTH_COMPONENT24, GLsizei(width), GLsizei(height));
}

void Cube::bindTexture(uint32_t unit)
{
	assert(_color_texture);
	_color_texture.Bind(unit);
}

void Cube::bindTextureFace(CubeFace face, uint32_t unit)
{
	assert(_color_texture);
	_color_texture.BindFace(face, unit);
}



void Cube::bindDepthTexture(GLuint unit)
{
	assert(_depth_texture);
	_depth_texture.Bind(unit);
}

void Cube::bindRenderTargetMip(uint32_t face, uint32_t mip_level, BufferMask clear_buffers)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo_id);

	if(_has_color and _color_texture)
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
							   GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
							   _color_texture.texture_id(), GLint(mip_level));

	if(_has_depth and _depth_texture)
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
							   GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
							   _depth_texture.texture_id(), GLint(mip_level));

	glViewport(0, 0, std::max(1, _width >> mip_level), std::max(1, _height >> mip_level));

	if(not _has_color)
		clear_buffers &= ~ColorBuffer;
	if(not _has_depth)
		clear_buffers &= ~DepthBuffer;
	assert(clear_buffers != 0);
	glClear(clear_buffers);
}

void Cube::release()
{
	if(_fbo_id)
		glDeleteFramebuffers(1, &_fbo_id);
	_fbo_id = 0;

	if(_color_rbo_id)
		glDeleteRenderbuffers(1, &_color_rbo_id);
	_color_rbo_id = 0;

	if(_color_texture)
		_color_texture.Release();

	if(_depth_rbo_id)
		glDeleteRenderbuffers(1, &_depth_rbo_id);
	_depth_rbo_id = 0;

	if(_depth_texture)
		_depth_texture.Release();
}

} // RGL::RenderTarget
