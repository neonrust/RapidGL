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
		else if(color_cfg & Color::HalfFloat)
			_color_format = GL_RGBA16F;
		else
			_color_format = GL_RGBA8;
	}
	if(_has_depth)
		_depth_format = GL_DEPTH_COMPONENT32F; // only float supported

	if(_has_color and (color_cfg & Color::Texture))
	{
		// TODO: bake this into attach() ?
		bool ok = _color_texture.Create(width, height, _color_format, _mip_levels);
		assert(ok);

		_color_texture.SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::Linear);
		_color_texture.SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
		_color_texture.SetWrapping (TextureWrappingAxis::U,    TextureWrappingParam::ClampToEdge);
		_color_texture.SetWrapping (TextureWrappingAxis::V,    TextureWrappingParam::ClampToEdge);
	}

	if(_has_depth and (depth_cfg & Depth::Texture))
	{
		// TODO: bake this into attach() ?
		bool ok = _depth_texture.Create(width, height, _depth_format, _mip_levels);
		assert(ok);

		_depth_texture.SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::Nearest);
		_depth_texture.SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Nearest);
		_depth_texture.SetWrapping (TextureWrappingAxis::U,    TextureWrappingParam::ClampToEdge);
		_depth_texture.SetWrapping (TextureWrappingAxis::V,    TextureWrappingParam::ClampToEdge);
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
#if defined(DEBUG)
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

void Texture2d::resize(size_t width, size_t height)
{
	if(_has_color)
	{
		if(color_texture())
		{
			GLuint prev_tx { 0 };
			glGetIntegerv(GL_TEXTURE_BINDING_2D, reinterpret_cast<GLint *>(&prev_tx));
			GLint minFilter;
			GLint magFilter;
			glGetTextureParameteriv(color_texture().texture_id(), GL_TEXTURE_MIN_FILTER, &minFilter);
			glGetTextureParameteriv(color_texture().texture_id(), GL_TEXTURE_MAG_FILTER, &magFilter);

			glBindTexture(GL_TEXTURE_2D, color_texture().texture_id());
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, GLsizei(width), GLsizei(height), 0, _color_format, GL_FLOAT, nullptr);

			color_texture().SetFiltering(TextureFiltering::Minify, TextureFilteringParam(minFilter));
			color_texture().SetFiltering(TextureFiltering::Magnify, TextureFilteringParam(magFilter));

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture().texture_id(), 0);

			glBindTexture(GL_TEXTURE_2D, prev_tx);
		}
		else
		{
			// TODO: resize color render buffer
			GLuint prev_rbo { 0 };
			glGetIntegerv(GL_RENDERBUFFER_BINDING, reinterpret_cast<GLint *>(&prev_rbo));

			glBindRenderbuffer(GL_RENDERBUFFER, _color_rbo_id);
			glRenderbufferStorage(GL_RENDERBUFFER, _color_format, GLsizei(width), GLsizei(height));

			glBindRenderbuffer(GL_RENDERBUFFER, prev_rbo);
		}
	}

	if(_has_depth)
	{
		if(depth_texture())
		{
			GLuint prev_tx { 0 };
			glGetIntegerv(GL_TEXTURE_BINDING_2D, reinterpret_cast<GLint *>(&prev_tx));
			GLint minFilter;
			GLint magFilter;
			glGetTextureParameteriv(depth_texture().texture_id(), GL_TEXTURE_MIN_FILTER, &minFilter);
			glGetTextureParameteriv(depth_texture().texture_id(), GL_TEXTURE_MAG_FILTER, &magFilter);

			glBindTexture(GL_TEXTURE_2D, depth_texture().texture_id());
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, GLsizei(width), GLsizei(height), 0, _depth_format, GL_FLOAT, nullptr);

			depth_texture().SetFiltering(TextureFiltering::Minify, TextureFilteringParam(minFilter));
			depth_texture().SetFiltering(TextureFiltering::Magnify, TextureFilteringParam(magFilter));

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_texture().texture_id(), 0);

			glBindTexture(GL_TEXTURE_2D, prev_tx);
		}
		else
		{
			GLuint prev_rbo { 0 };
			glGetIntegerv(GL_RENDERBUFFER_BINDING, reinterpret_cast<GLint *>(&prev_rbo));

			glBindRenderbuffer(GL_RENDERBUFFER, _depth_rbo_id);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, GLsizei(width), GLsizei(height));

			glBindRenderbuffer(GL_RENDERBUFFER, prev_rbo);
		}
	}
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

void Texture2d::bindShadowSampler(GLuint unit) const
{
	assert(_depth_texture);
	if(not _shadow_view)
		const_cast<Texture2d *>(this)->create_shadow_view();
	_shadow_view.Bind(unit);
}

bool Texture2d::create_shadow_view()
{
	auto descr = _depth_texture.CreateView();
	if(not descr)
		return false;

	_shadow_view.set(descr);
	_shadow_view.SetCompareMode(TextureCompareMode::Ref);
	_shadow_view.SetCompareFunc(TextureCompareFunc::Less);
	_shadow_view.SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::Linear);
	_shadow_view.SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);

	return true;
}

void Texture2d::bindRenderTarget(BufferMask clear_mask)
{
	bindRenderTarget({ 0, 0, m_metadata.width, m_metadata.height }, clear_mask);
}

void Texture2d::bindRenderTarget(glm::ivec4 rect, BufferMask clear_mask)
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

void Texture2d::bindImage(GLuint image_unit, ImageAccess access, uint32_t mip_level) const
{
	assert(_color_texture);
	// TODO _color_texture.BindImage(image_unit, access, mip_level);
	glBindImageTexture(image_unit, _color_texture.texture_id(), GLint(mip_level), GL_FALSE, 0, GLenum(access), _color_format);
}

void Texture2d::bindImageRead(GLuint image_unit, uint32_t mip_level) const
{
	bindImage(image_unit, ImageAccess::Read, mip_level);
}

void Texture2d::bindDepthImage(GLuint image_unit, ImageAccess access, uint32_t mip_level) const
{
	assert(_depth_texture);
	// TODO _depth_texture.BindImage(image_unit, access, mip_level);
	glBindImageTexture(image_unit, _depth_texture.texture_id(), GLint(mip_level), GL_FALSE, 0, GLenum(access), _depth_format);
}

void Texture2d::bindDepthImageRead(GLuint image_unit, uint32_t mip_level) const
{
	bindDepthImage(image_unit, ImageAccess::Read, mip_level);
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

std::pair<GLenum, GLenum> _clear_format(GLenum internalFormat);

void Texture2d::clear(const glm::uvec4 &rect)
{
	if(_has_color)
	{
		if(color_texture())
		{
			const auto &[format, type] = _clear_format(_color_format);

			// hm, this doesn't seem to work?
			static constexpr float clear_color[] = { 0, 0 };
			glClearTexSubImage(color_texture().texture_id(),
							   0,                   // mip level
							   GLint(rect.x), GLint(rect.y), 0, // offset (xyz)
							   GLsizei(rect.z), GLsizei(rect.w), 1, // size (xyz)
							   format,            // format
							   type,              // type
							   &clear_color);
		}
		else
			; // TODO: clear framebuffer?
	}

	if(_has_depth)
	{
		if(depth_texture())
		{
			static constexpr auto clear_depth = 1.f; // far plane
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


std::pair<GLenum, GLenum> _clear_format(GLenum internalFormat)
{
	switch (internalFormat)
	{
	// ---- Red channel ----
	case GL_R8:          return { GL_RED, GL_UNSIGNED_BYTE };
	case GL_R8_SNORM:    return { GL_RED, GL_BYTE };
	case GL_R16:         return { GL_RED, GL_UNSIGNED_SHORT };
	case GL_R16_SNORM:   return { GL_RED, GL_SHORT };
	case GL_R16F:        return { GL_RED, GL_HALF_FLOAT };
	case GL_R32F:        return { GL_RED, GL_FLOAT };

	case GL_R8UI:        return { GL_RED_INTEGER, GL_UNSIGNED_BYTE };
	case GL_R8I:         return { GL_RED_INTEGER, GL_BYTE };
	case GL_R16UI:       return { GL_RED_INTEGER, GL_UNSIGNED_SHORT };
	case GL_R16I:        return { GL_RED_INTEGER, GL_SHORT };
	case GL_R32UI:       return { GL_RED_INTEGER, GL_UNSIGNED_INT };
	case GL_R32I:        return { GL_RED_INTEGER, GL_INT };

			   // ---- RG ----
	case GL_RG8:         return { GL_RG, GL_UNSIGNED_BYTE };
	case GL_RG8_SNORM:   return { GL_RG, GL_BYTE };
	case GL_RG16:        return { GL_RG, GL_UNSIGNED_SHORT };
	case GL_RG16_SNORM:  return { GL_RG, GL_SHORT };
	case GL_RG16F:       return { GL_RG, GL_HALF_FLOAT };
	case GL_RG32F:       return { GL_RG, GL_FLOAT };

	case GL_RG8UI:       return { GL_RG_INTEGER, GL_UNSIGNED_BYTE };
	case GL_RG8I:        return { GL_RG_INTEGER, GL_BYTE };
	case GL_RG16UI:      return { GL_RG_INTEGER, GL_UNSIGNED_SHORT };
	case GL_RG16I:       return { GL_RG_INTEGER, GL_SHORT };
	case GL_RG32UI:      return { GL_RG_INTEGER, GL_UNSIGNED_INT };
	case GL_RG32I:       return { GL_RG_INTEGER, GL_INT };

			   // ---- RGB ----
	case GL_RGB8:        return { GL_RGB, GL_UNSIGNED_BYTE };
	case GL_RGB8_SNORM:  return { GL_RGB, GL_BYTE };
	case GL_RGB16:       return { GL_RGB, GL_UNSIGNED_SHORT };
	case GL_RGB16_SNORM: return { GL_RGB, GL_SHORT };
	case GL_RGB16F:      return { GL_RGB, GL_HALF_FLOAT };
	case GL_RGB32F:      return { GL_RGB, GL_FLOAT };

	case GL_RGB8UI:      return { GL_RGB_INTEGER, GL_UNSIGNED_BYTE };
	case GL_RGB8I:       return { GL_RGB_INTEGER, GL_BYTE };
	case GL_RGB16UI:     return { GL_RGB_INTEGER, GL_UNSIGNED_SHORT };
	case GL_RGB16I:      return { GL_RGB_INTEGER, GL_SHORT };
	case GL_RGB32UI:     return { GL_RGB_INTEGER, GL_UNSIGNED_INT };
	case GL_RGB32I:      return { GL_RGB_INTEGER, GL_INT };

			   // ---- RGBA ----
	case GL_RGBA8:       return { GL_RGBA, GL_UNSIGNED_BYTE };
	case GL_RGBA8_SNORM: return { GL_RGBA, GL_BYTE };
	case GL_RGBA16:      return { GL_RGBA, GL_UNSIGNED_SHORT };
	case GL_RGBA16_SNORM:return { GL_RGBA, GL_SHORT };
	case GL_RGBA16F:     return { GL_RGBA, GL_HALF_FLOAT };
	case GL_RGBA32F:     return { GL_RGBA, GL_FLOAT };

	case GL_RGBA8UI:     return { GL_RGBA_INTEGER, GL_UNSIGNED_BYTE };
	case GL_RGBA8I:      return { GL_RGBA_INTEGER, GL_BYTE };
	case GL_RGBA16UI:    return { GL_RGBA_INTEGER, GL_UNSIGNED_SHORT };
	case GL_RGBA16I:     return { GL_RGBA_INTEGER, GL_SHORT };
	case GL_RGBA32UI:    return { GL_RGBA_INTEGER, GL_UNSIGNED_INT };
	case GL_RGBA32I:     return { GL_RGBA_INTEGER, GL_INT };

	default:
		assert(false);
		return { 0, 0 };
	}
}

} // RGL::RenderTarget
