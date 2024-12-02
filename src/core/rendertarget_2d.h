#pragma once

#include "glad/glad.h"

#include <cstddef>
#include <cstdint>


struct Texture2DRenderTarget
{
	void create(size_t width, size_t height, GLenum internalformat);

	~Texture2DRenderTarget() { cleanup(); }

	void cleanup();

	inline GLuint texture_id() const { return _texture_id; }
	inline GLuint framebuffer_id() const { return _fbo_id; }
	inline GLsizei width() const { return _width; }
	inline GLsizei height() const { return _height; }
	inline uint8_t mip_levels() const { return _mip_levels; }

	void bindTexture(GLuint unit = 0);
	void bindRenderTarget(GLbitfield clear_mask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	void bindImageForRead(GLuint image_unit, GLint mip_level);
	void bindImageForWrite(GLuint image_unit, GLint mip_level);
	void bindImageForReadWrite(GLuint image_unit, GLint mip_level);

	uint8_t calculateMipmapLevels();


private:
	GLuint _texture_id = 0;
	GLuint _fbo_id = 0;
	GLuint _rbo_id = 0;
	GLsizei _width = 0;
	GLsizei _height = 0;
	GLenum _internal_format;

	const uint8_t _downscale_limit = 10;
	const uint8_t _max_iterations = 16; // max mipmap levels
	uint8_t _mip_levels = 1;
};
