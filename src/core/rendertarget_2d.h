#pragma once

#include "glad/glad.h"
#include "glm/ext/vector_uint2.hpp"
#include "glm/vec4.hpp"   // IWYU pragma: keep
#include "texture.h"
#include "rendertarget_common.h"

#include <cstddef>
#include <cstdint>

namespace RGL::RenderTarget
{

static constexpr glm::ivec4 FullScreen { 0, 0, 0, 0 };

// TODO: rename to something not containing the word "texture"... e.g. "flat" ?
struct Texture2d
{
	void create(const char *name, size_t width, size_t height, Color::Config color_cfg=Color::Default, Depth::Config depth_cfg=Depth::Default);

	// TODO: explicitly add the components?
	//    addColor( format )
	//    addDepth( format )
	//  could return *this to enable chaining

	~Texture2d() { release(); }

	void release();

	// inline GLuint framebuffer_id() const { return _fbo_id; }
	inline GLuint width() const { return m_metadata.width; }
	inline GLuint height() const { return m_metadata.height; }
	inline glm::uvec2 size() const { return { m_metadata.width, m_metadata.height }; }
	inline uint_fast8_t mip_levels() const { return _mip_levels; }

	inline bool has_color() const { return _has_color; }
	// might be invalid (if not a texture)
	Texture &color_texture();
	const Texture &color_texture() const;
	inline GLenum color_format() const { return _color_format; }

	inline bool has_depth() const { return _has_depth; }
	// might be invalid (if not a texture)
	Texture &depth_texture();
	const Texture &depth_texture() const;
	inline GLenum depth_format() const { return _depth_format; }

	//! bind color (if any) for use in shader as a texture
	void bindTextureSampler(GLuint unit=0) const;
	//! bind depth (if any) for use in shader as a texture
	void bindDepthTextureSampler(GLuint unit=0) const;

	//! bind for rendering into using regular draw calls (and clear specified aspects of the RT)
	void bindRenderTarget(BufferMask clear_mask=ColorBuffer | DepthBuffer, glm::ivec4 rect=FullScreen);

	//! bind color for read/write from compute shaders
	void bindImage(GLuint image_unit=0, RenderTarget::Access access=Access::Read, GLint mip_level=0);
	void bindImageRead(GLuint image_unit=0, GLint mip_level=0) const;

	//! bind depth for read/write from compute shaders (only if depth is a texture)
	void bindDepthImage(GLuint image_unit, RenderTarget::Access access=Access::Read, GLint mip_level=0);
	void bindDepthImageRead(GLuint image_unit, GLint mip_level) const;

	// copy this texture to another texture
	void copyTo(Texture2d &dest, BufferMask mask=ColorBuffer | DepthBuffer, TextureFilteringParam filter=TextureFilteringParam::Linear) const;
	void copyFrom(const Texture2d &source, BufferMask mask=ColorBuffer | DepthBuffer, TextureFilteringParam filter=TextureFilteringParam::Linear);

	void clear();
	void fillColor(const glm::vec4 &color);

	operator bool () const;

	// color texture parameters
	void SetFiltering(TextureFiltering type, TextureFilteringParam filtering);  // color
	void SetWrapping(TextureWrappingAxis axis, TextureWrappingParam wrapping);     // color

	// depth texture parameters
	void SetDepthFiltering(TextureFiltering type, TextureFilteringParam filtering);  // color
	void SetDepthWrapping(TextureWrappingAxis axis, TextureWrappingParam wrapping);     // color

private:
	void attach(GLenum attachment, GLenum internal_format, GLuint texture_id, GLuint &rbo_id);

private:
	GLuint _fbo_id { 0 };

	bool _has_color { false };
	GLenum _color_format { 0 };
	RGL::Texture2D _color_texture;
	GLuint _color_rbo_id { 0 };

	bool _has_depth { false };
	GLenum _depth_format { 0 };
	RGL::Texture2D _depth_texture;
	GLuint _depth_rbo_id { 0 };

	uint_fast8_t _mip_levels { 1 };
	ImageData m_metadata;

	const char *_name { nullptr };
};

} // RGL::RenderTarget

