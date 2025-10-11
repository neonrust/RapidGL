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

	// TODO: add attachments more generally?
	//    attach(<channels>, <type>)
	//    e.g. attach(3, Float | Texture)
	//         attach(1, Depth)
	//  could return *this to enable chaining

	~Texture2d() { release(); }

	void release();

	// inline GLuint framebuffer_id() const { return _fbo_id; }
	inline GLuint width() const { return m_metadata.width; }
	inline GLuint height() const { return m_metadata.height; }  // for 2d & 3d
	inline GLuint depth() const { return m_metadata.depth; }    // for 3d
	inline glm::uvec2 size() const { return { m_metadata.width, m_metadata.height }; }
	void resize(size_t width, size_t height);

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
	void enableHardwarePCF();

	//! bind color (if any) for use in shader as a texture
	void bindTextureSampler(GLuint unit=0) const;
	//! bind depth (if any) for use in shader as a texture
	void bindDepthTextureSampler(GLuint unit=0) const;

	//! bind for rendering into using regular draw calls (and clear specified aspects of the RT)
	inline void bindRenderTarget() { bindRenderTarget(FullScreen, ColorBuffer | DepthBuffer); }
	void bindRenderTarget(BufferMask clear_mask);
	void bindRenderTarget(glm::ivec4 rect, BufferMask clear_mask=ColorBuffer | DepthBuffer);

	//! bind color for read/write from compute shaders
	void bindImage(GLuint image_unit=0, ImageAccess access=ImageAccess::Read, uint32_t mip_level=0) const;
	void bindImageRead(GLuint image_unit=0, uint32_t mip_level=0) const;

	//! bind depth for read/write from compute shaders (only if depth is a texture)
	void bindDepthImage(GLuint image_unit, ImageAccess access=ImageAccess::Read, uint32_t mip_level=0) const;
	void bindDepthImageRead(GLuint image_unit, uint32_t mip_level) const;

	// copy this texture to another texture
	void copyTo(Texture2d &dest, BufferMask mask=ColorBuffer | DepthBuffer, TextureFilteringParam filter=TextureFilteringParam::Linear) const;
	void copyFrom(const Texture2d &source, BufferMask mask=ColorBuffer | DepthBuffer, TextureFilteringParam filter=TextureFilteringParam::Linear);

	void clear();
	void clear(const glm::uvec4 &rect);
	void fillColor(const glm::vec4 &color);
	void fillDepth(float value=1.f);

	operator bool () const;

	// color texture parameters
	void SetFiltering(TextureFiltering type, TextureFilteringParam filtering);     // color
	void SetWrapping(TextureWrappingAxis axis, TextureWrappingParam wrapping);     // color

	// depth texture parameters
	void SetDepthFiltering(TextureFiltering type, TextureFilteringParam filtering);
	void SetDepthWrapping(TextureWrappingAxis axis, TextureWrappingParam wrapping);

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
	ImageMeta m_metadata;

	const char *_name { nullptr };
};

} // RGL::RenderTarget

