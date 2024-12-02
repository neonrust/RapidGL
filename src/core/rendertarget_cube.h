#pragma once

#include "glad/glad.h"
#include "glm/ext/matrix_float4x4.hpp"


struct CubeMapRenderTarget
{
	void create(uint32_t width, uint32_t height, bool gen_mip_levels = false);

	~CubeMapRenderTarget() { cleanup(); }

	void set_position(const glm::vec3 pos);

	inline GLsizei width() const { return _width; }
	inline GLsizei height() const { return _height; }
	inline GLuint texture_id() const { return _cubemap_texture_id; }
	inline const glm::mat4 &projection() const { return _projection; }
	// TODO: use enum: left, top, etc.
	inline const glm::mat4 &view_transform(size_t index) { return _view_transforms[index]; }

	void bindTexture(GLuint unit = 0);
	void bindRenderBuffer();
	void bindRenderTarget(GLbitfield clear_mask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	void cleanup();

private:
	glm::mat4 _view_transforms[6];
	glm::mat4 _projection;

	GLuint    _cubemap_texture_id = 0;
	GLuint    _fbo_id             = 0;
	GLuint    _rbo_id             = 0;
	glm::vec3 _position           = glm::vec3(0.0f);
	GLsizei   _width;
	GLsizei   _height;
};
