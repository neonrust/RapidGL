#pragma once

#include "glad/glad.h"
#include "glm/ext/vector_uint2.hpp"
#include "texture.h"

#include <cstddef>
#include <cstdint>

namespace RGL::RenderTarget
{

using Format = uint32_t;
static constexpr Format Color      = 0x01;
static constexpr Format ColorFloat = Color | 0x02;
static constexpr Format Depth      = 0x04;

using BufferMask = uint32_t;
static constexpr BufferMask ColorBuffer = GL_COLOR_BUFFER_BIT;
static constexpr BufferMask DepthBuffer = GL_DEPTH_BUFFER_BIT;


enum Access : GLenum
{
	Read      = GL_READ_ONLY,
	Write     = GL_WRITE_ONLY,
	ReadWrite = GL_READ_WRITE
};

struct Texture2d : public RGL::Texture2D
{
	void create(size_t width, size_t height, Format format=ColorFloat);

	// TODO: instead of above 'internalformat':
	//    addColor( format )
	//    addDepth( format )
	//  could return *this to enable chaining, than calling a:
	//    build()

	~Texture2d() { release(); }

	void release();

	// inline GLuint framebuffer_id() const { return _fbo_id; }
	inline GLuint width() const { return m_metadata.width; }
	inline GLuint height() const { return m_metadata.height; }
	inline glm::uvec2 size() const { return { m_metadata.width, m_metadata.height }; }
	inline uint_fast8_t mip_levels() const { return _mip_levels; }

	//! bind for use in shader as a texture
	void bindTextureSampler(GLuint unit=0) const;
	//! bind for drawing into using regular draw calls (and clear specified aspects of the RT)
	void bindRenderTarget(BufferMask clear_buffers=ColorBuffer | DepthBuffer);
	//! bind for read/write from compute shaders
	void bindImage(GLuint image_unit=0, RenderTarget::Access access=RenderTarget::Read, GLint mip_level=0);
	void bindImageRead(GLuint image_unit=0, GLint mip_level=0) const;

	// copy this texture to another texture
	void copyTo(Texture2d &dest, GLbitfield mask=GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GLenum filter=GL_LINEAR) const;
	void copyFrom(const Texture2d &source, GLbitfield mask=GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GLenum filter=GL_LINEAR);

	void clear();

private:
	uint8_t calculateMipmapLevels();

private:
	GLuint _fbo_id { 0 };
	GLuint _rbo_id { 0 };
	GLenum _internal_format { 0 };

	uint_fast8_t _mip_levels { 1 };
	bool _has_color { false };
	bool _has_depth { false };
};

} // RGL::RenderTarget

