#include "rendertarget_2d.h"

#include <glm/gtc/type_ptr.hpp>


using namespace std::literals;

namespace RGL::RenderTarget
{

// relevant for bloom downscale/upscale (blur)
static constexpr uint8_t s_downscale_limit { 8 }; // essentially max blur radius   TODO: should be screen size dependent
static constexpr uint8_t s_max_iterations { 18 }; // max number of mipmap levels


// TODO: this is more of a "general" render target
//   make a more texture-like specialization

void Texture2d::create(const char *name, size_t width, size_t height, Color::Config color_cfg, Depth::Config depth_cfg)
{
	_name = name;
	if(_fbo_id)
		release();

	_has_color = color_cfg != Color::None;
	_has_depth = depth_cfg != Depth::None;

	assert(_has_color or _has_depth);

	m_metadata.width = GLuint(width);
	m_metadata.height = GLuint(height);

	if(color_cfg & Color::Texture or depth_cfg & Depth::Texture)
		_mip_levels = Texture::calculateMipMapLevels(width, height, 0, s_downscale_limit, s_max_iterations);
	else
		_mip_levels = 1;

	if(_has_color)
	{
		if(Color::is_custom(color_cfg))
			_color_format = Color::custom_mask & color_cfg;
		else if((color_cfg & Color::Float2) == Color::Float2)
			_color_format = GL_RG16F;
		else if(color_cfg & Color::Float)
			_color_format = GL_RGBA32F;
		else
			_color_format = GL_RGBA;
	}
	if(_has_depth)
		_depth_format = GL_DEPTH_COMPONENT32F; // only float supported

	if(_has_color and (color_cfg & Color::Texture))
	{
		// TODO: bake this into add_component() ?
		bool ok = _color_texture.Create(width, height, _color_format, _mip_levels);
		assert(ok);

		_color_texture.SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::Linear);
		_color_texture.SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
		_color_texture.SetWrapping (TextureWrappingAxis::U,    TextureWrappingParam::ClampToEdge);
		_color_texture.SetWrapping (TextureWrappingAxis::V,    TextureWrappingParam::ClampToEdge);
	}

	if(_has_depth and (depth_cfg & Depth::Texture))
	{
		// TODO: bake this into add_component() ?
		bool ok = _depth_texture.Create(width, height, _depth_format, _mip_levels);
		assert(ok);

		_depth_texture.SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::Nearest);
		_depth_texture.SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Nearest);
		_depth_texture.SetWrapping (TextureWrappingAxis::U,    TextureWrappingParam::ClampToEdge);
		_depth_texture.SetWrapping (TextureWrappingAxis::V,    TextureWrappingParam::ClampToEdge);
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

void Texture2d::attach(GLenum attachment, GLenum internal_format, GLuint texture_id, GLuint &rbo_id)
{
	if(texture_id)
		glNamedFramebufferTexture(_fbo_id, attachment, texture_id, 0);
	else
	{
		glCreateRenderbuffers(1, &rbo_id);
		assert(rbo_id);
		glNamedRenderbufferStorage(rbo_id, internal_format, GLsizei(m_metadata.width), GLsizei(m_metadata.height));
		glNamedFramebufferRenderbuffer(_fbo_id, attachment, GL_RENDERBUFFER, rbo_id);
	}
}

void Texture2d::release()
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

Texture &Texture2d::color_texture()
{
	assert(_color_texture);
	return _color_texture;
}

const Texture &Texture2d::color_texture() const
{
	assert(_color_texture);
	return _color_texture;
}

void Texture2d::bindTextureSampler(GLuint unit) const
{
	assert(_color_texture);
	_color_texture.Bind(unit);
}

Texture &Texture2d::depth_texture()
{
	return _depth_texture;
}

const Texture &Texture2d::depth_texture() const
{
	return _depth_texture;
}

void Texture2d::bindDepthTextureSampler(GLuint unit) const
{
	assert(_depth_texture);
	_depth_texture.Bind(unit);
}

void Texture2d::bindRenderTarget(BufferMask clear_mask, glm::ivec4 rect)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo_id);

	if(rect == FullScreen)
	{
		rect.x = 0;
		rect.y = 0;
		rect.z = int(m_metadata.width);
		rect.w = int(m_metadata.height);
	}
	glViewport(rect.x, rect.y, rect.z, rect.w);

	// TODO: do this in a separate call?
	if(clear_mask != 0)
	{
		if(not _has_color)
			clear_mask &= ~ColorBuffer;
		if(not _has_depth)
			clear_mask &= ~DepthBuffer;
		glScissor(rect.x, rect.y, rect.z, rect.w);
		glClear(clear_mask);
	}
}

void Texture2d::bindImage(GLuint image_unit, RenderTarget::Access access, GLint mip_level)
{
	assert(_color_texture);
	glBindImageTexture(image_unit, _color_texture.texture_id(), mip_level, GL_FALSE, 0, GLenum(access), _color_format);
}

void Texture2d::bindImageRead(GLuint image_unit, GLint mip_level) const
{
	assert(_color_texture);
	glBindImageTexture(image_unit, _color_texture.texture_id(), mip_level, GL_FALSE, 0, GL_READ_ONLY, _color_format);
}

void Texture2d::bindDepthImage(GLuint image_unit, RenderTarget::Access access, GLint mip_level)
{
	assert(_depth_texture);
	glBindImageTexture(image_unit, _depth_texture.texture_id(), mip_level, GL_FALSE, 0, GLenum(access), _color_format);
}

void Texture2d::bindDepthImageRead(GLuint image_unit, GLint mip_level) const
{
	assert(_depth_texture);
	glBindImageTexture(image_unit, _depth_texture.texture_id(), mip_level, GL_FALSE, 0, GL_READ_ONLY, _color_format);
}

void Texture2d::copyTo(Texture2d &dest, BufferMask mask, TextureFilteringParam filter) const
{
	if(not _has_color or not dest._has_color)
		mask &= ~ColorBuffer;
	if(not _has_depth or not dest._has_depth)
		mask &= ~DepthBuffer;
	assert(mask != 0);

	glBlitNamedFramebuffer(_fbo_id,
						   dest._fbo_id,
						   0, 0, GLint(width()), GLint(height()),            // source rect
						   0, 0, GLint(dest.width()), GLint(dest.height()),  // dest rect
						   GLbitfield(mask),
						   GLenum(filter));
}

void Texture2d::copyFrom(const Texture2d &source, BufferMask mask, TextureFilteringParam filter)
{
	source.copyTo(*this, mask, filter);
}

void Texture2d::clear()
{
	if(_has_color)
		fillColor({0, 0, 0, 0});

	if(_has_depth)
	{
		static const GLfloat clearDepth = 1;
		glClearNamedFramebufferfv(_fbo_id, GL_DEPTH, 0, &clearDepth);
	}
}

void Texture2d::clear(const glm::uvec4 &rect)
{
	if(_has_color)
	{
		if(color_texture())
		{
			// hm, this doesn't seem to work?
			static constexpr float clear_color[] = { 0, 0 };
			glClearTexSubImage(color_texture().texture_id(),
							   0,                   // mip level
							   GLint(rect.x), GLint(rect.y), 0, // offset (xyz)
							   GLsizei(rect.z), GLsizei(rect.w), 1, // size (xyz)
							   _color_format,  // format
							   _color_format == GL_RG16F? GL_HALF_FLOAT: GL_FLOAT,  // type  TODO: better logic
							   &clear_color);
		}
		else
			; // TODO: clear framebuffer?
	}

	if(_has_depth)
	{
		if(depth_texture())
		{
			static constexpr auto clear_depth = 0.f;
			glClearTexSubImage(depth_texture().texture_id(),
							   0,                   // mip level
							   GLint(rect.x), GLint(rect.y), 0, // offset (xyz)
							   GLsizei(rect.z), GLsizei(rect.w), 1, // size (xyz)
							   GL_DEPTH_COMPONENT,  // format
							   GL_FLOAT,            // type
							   &clear_depth);
		}
		else
			; // TODO: clear framebuffer?
	}
}

void Texture2d::fillColor(const glm::vec4 &color)
{
	assert(_has_color);
	if(_has_color)
		glClearNamedFramebufferfv(_fbo_id, GL_COLOR, 0, glm::value_ptr(color));
}

void Texture2d::fillDepth(float value)
{
	assert(_has_depth);
	if(_has_depth)
		glClearNamedFramebufferfv(_fbo_id, GL_DEPTH, 0, &value);
}

Texture2d::operator bool() const
{
	return _fbo_id
		and ((_has_color and (_color_texture or _color_rbo_id))
			 or (_has_depth and (_depth_texture or _depth_rbo_id)));
}

void Texture2d::SetFiltering(TextureFiltering type, TextureFilteringParam filtering)
{
	assert(_color_texture);
	_color_texture.SetFiltering(type, filtering);
}

void Texture2d::SetWrapping(TextureWrappingAxis axis, TextureWrappingParam wrapping)
{
	assert(_color_texture);
	_color_texture.SetWrapping(axis, wrapping);
}

void Texture2d::SetDepthFiltering(TextureFiltering type, TextureFilteringParam filtering)
{
	assert(_depth_texture);
	_depth_texture.SetFiltering(type, filtering);
}

void Texture2d::SetDepthWrapping(TextureWrappingAxis axis, TextureWrappingParam wrapping)
{
	assert(_depth_texture);
	_depth_texture.SetWrapping(axis, wrapping);
}

} // RGL::RenderTarget
