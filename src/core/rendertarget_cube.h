#pragma once

#include "glad/glad.h"
#include "glm/mat4x4.hpp"  // IWYU pragma: keep
#include "texture.h"
#include "rendertarget_common.h"

namespace RGL::RenderTarget
{

// TODO: inherit from TextureCubeMap
struct Cube
{
	Cube();

	void create(const char *name, uint32_t width, uint32_t height, Color::Config color_cfg = Color::Default, Depth::Config depth_cfg = Depth::Default);

	~Cube() { release(); }

	void set_position(const glm::vec3 pos);

	inline GLsizei width() const { return _width; }
	inline GLsizei height() const { return _height; }

	inline const glm::mat4 &projection() const { return _projection; }
	// TODO: use enum: left, top, etc.
	inline const glm::mat4 &view_transform(size_t index) { return _view_transforms[index]; }


	inline bool has_color() const { return _has_color; }
	inline bool has_depth() const { return _has_depth; }

	// might be invalid (if not a texture)
	inline TextureCube &color_texture() { return _color_texture; }
	void bindTexture(uint32_t unit = 0);
	void bindTextureFace(CubeFace face, uint32_t unit = 0);

	// might be invalid (if not a texture)
	inline TextureCube &depth_texture() { return _depth_texture; }
	void bindDepthTexture(GLuint unit = 0);

	// useful when rendering into mip levels and depth is not a texture
	void resizeDepth(size_t width, size_t height);

	inline void bindRenderTarget(uint32_t face, BufferMask clear_buffers=ColorBuffer | DepthBuffer) { bindRenderTargetMip(face, 0, clear_buffers); }
	void bindRenderTargetMip(uint32_t face, uint32_t mip_level, BufferMask clear_buffers=ColorBuffer | DepthBuffer);

	void release();

private:
	void attach(GLenum attachment, GLenum internal_format, GLuint texture_id, GLuint &rbo_id);

private:
	glm::vec3 _position;
	glm::mat4 _view_transforms[6];
	glm::mat4 _projection;

	GLsizei   _width;
	GLsizei   _height;

	GLuint _fbo_id { 0 };

	bool _has_color { false };
	GLenum _color_format { 0 };
	TextureCube _color_texture;
	GLuint _color_rbo_id { 0 };

	bool _has_depth { false };
	GLenum _depth_format { 0 };
	TextureCube _depth_texture;
	GLuint _depth_rbo_id { 0 };

	uint_fast8_t _mip_levels { 1 };
	const char *_name;
};

} // RGL::RenderTarget
