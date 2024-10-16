#pragma once

#include "glad/glad.h"

#include <cstdint>


struct Texture2DRenderTarget
{
	void create(uint32_t width, uint32_t height, GLenum internalformat);

	~Texture2DRenderTarget() { cleanup(); }

	void cleanup();

	void bindTexture(GLuint unit = 0);
	void bindRenderTarget(GLbitfield clear_mask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	void bindImageForRead(GLuint image_unit, GLint mip_level);
	void bindImageForWrite(GLuint image_unit, GLint mip_level);
	void bindImageForReadWrite(GLuint image_unit, GLint mip_level);

	uint8_t calculateMipmapLevels();

	GLuint m_texture_id = 0;
	GLuint m_fbo_id = 0;
	GLuint m_rbo_id = 0;
	GLsizei m_width = 0;
	GLsizei m_height = 0;
	GLenum m_internalformat;

	const uint8_t m_downscale_limit = 10;
	const uint8_t m_max_iterations = 16; // max mipmap levels
	uint8_t m_mip_levels = 1;
};
