#include "rendertarget_2d.h"
#include "container_types.h"

#include <cstdio>
#include <glm/gtc/type_ptr.hpp>


#include <iostream>
#include <string>
#include <fstream>
#include <regex>

using namespace std::literals;

std::string valueEnum(uint32_t value)
{
	static dense_map<uint32_t, std::string> enumMap;
	static bool tried_read = false;

	// Parse once
	if (not tried_read)
	{
		tried_read = true;

		static const auto gl_h = "thirdparty/glad/include/glad/glad.h"s;

		std::ifstream file(gl_h);
		if (!file.is_open())
		{
			std::cerr << "Failed to open: " << gl_h << "\n";
			return {};
		}

		std::string line;
		std::regex defineRegex(R"-(#define\s+(GL_[A-Za-z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+))-");

		while (std::getline(file, line))
		{
			std::smatch match;
			if (std::regex_search(line, match, defineRegex))
			{
				std::string name = match[1];
				std::string valStr = match[2];

				uint32_t val = 0;
				if (valStr.find("0x") == 0 || valStr.find("0X") == 0)
					val = uint32_t(std::stoul(valStr, nullptr, 16));
				else
					val = uint32_t(std::stoul(valStr));

				// Only keep first match (many names may share the same value)
				enumMap.emplace(val, name);
			}
		}
	}

	auto found = enumMap.find(value);
	if (found != enumMap.end())
		return found->second;
	else
		return "(unknown: " + std::to_string(value) + ")";
}

void dump_config(const char *fbo_name, GLuint fbo)
{
	auto printAttachment = [&](GLenum attachment) {
		GLenum type;
		GLuint obj;
		glGetNamedFramebufferAttachmentParameteriv(fbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, reinterpret_cast<GLint*>(&type));
		glGetNamedFramebufferAttachmentParameteriv(fbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, reinterpret_cast<GLint*>(&obj));
		// GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL

		if (type == GL_NONE)
			return;

		std::string label;
		switch (attachment)
		{
		case GL_DEPTH_ATTACHMENT:         label = "   Depth"; break;
		case GL_STENCIL_ATTACHMENT:       label = " Stencil"; break;
		// case GL_DEPTH_STENCIL_ATTACHMENT: label = "Depth+Stencil"; break;
		default:
			if (attachment >= GL_COLOR_ATTACHMENT0 and attachment <= GL_COLOR_ATTACHMENT31)
				label = " Color." + std::to_string(attachment - GL_COLOR_ATTACHMENT0);
			else
				label = valueEnum(attachment);
		}

		std::cout << "  " << label << ": ";

		GLint fmt = 0;
		GLint w = 0;
		GLint h = 0;

		if (type == GL_RENDERBUFFER)
		{
			std::cout << "Renderb.(ID: " << std::setw(2) << obj << ")";

			glGetNamedRenderbufferParameteriv(obj, GL_RENDERBUFFER_INTERNAL_FORMAT, &fmt);
			glGetNamedRenderbufferParameteriv(obj, GL_RENDERBUFFER_WIDTH, &w);
			glGetNamedRenderbufferParameteriv(obj, GL_RENDERBUFFER_HEIGHT, &h);
		}
		else if (type == GL_TEXTURE)
		{
			std::cout << "Texture (ID: " << std::setw(2) << obj << ")";

			glGetTextureLevelParameteriv(obj, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
			glGetTextureLevelParameteriv(obj, 0, GL_TEXTURE_WIDTH, &w);
			glGetTextureLevelParameteriv(obj, 0, GL_TEXTURE_HEIGHT, &h);
		}
		std::cout << "  " << w << " x " << h << "  " << valueEnum(uint32_t(fmt)) << " (0x" << std::hex << fmt << std::dec << ")\n";
	};

	std::cout << "FBO \"" << fbo_name << "\" (" << fbo << ")\n";

	// Check for standard attachments
	for (int i = 0; i < 8; ++i)
		printAttachment(GLenum(GL_COLOR_ATTACHMENT0 + i));

	printAttachment(GL_DEPTH_ATTACHMENT);
	printAttachment(GL_STENCIL_ATTACHMENT);
//	printAttachment(GL_DEPTH_STENCIL_ATTACHMENT);
}


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
	dump_config(_name, _fbo_id);
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
	glBindTextureUnit(unit, _color_texture.texture_id());
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
	glBindTextureUnit(unit, _depth_texture.texture_id());
}

void Texture2d::bindRenderTarget(BufferMask clear_mask)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo_id);

	glViewport(0, 0, GLsizei(m_metadata.width), GLsizei(m_metadata.height));

	if(clear_mask != 0)
	{
		if(not _has_color)
			clear_mask &= ~ColorBuffer;
		if(not _has_depth)
			clear_mask &= ~DepthBuffer;
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

void Texture2d::fillColor(const glm::vec4 &color)
{
	assert(_has_color);
	if(_has_color)
		glClearNamedFramebufferfv(_fbo_id, GL_COLOR, 0, glm::value_ptr(color));
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

} // RGL::RenderTarget
