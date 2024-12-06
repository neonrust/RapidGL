#include "rendertarget_cube.h"

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"


void RenderTargetCube::create(uint32_t width, uint32_t height, bool gen_mip_levels)
{
	_width  = GLsizei(width);
	_height = GLsizei(height);

	glGenTextures(1, &_cubemap_texture_id);
	glBindTexture(GL_TEXTURE_CUBE_MAP, _cubemap_texture_id);

	for (uint8_t i = 0; i < 6; ++i)
	{
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, _width, _height, 0, GL_RGB, GL_FLOAT, 0);
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, gen_mip_levels ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	if (gen_mip_levels)
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	glGenFramebuffers(1, &_fbo_id);
	glBindFramebuffer(GL_FRAMEBUFFER, _fbo_id);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, _cubemap_texture_id, 0);

	glGenRenderbuffers(1, &_rbo_id);
	glBindRenderbuffer(GL_RENDERBUFFER, _rbo_id);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, _width, _height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _rbo_id);

	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTargetCube::set_position(const glm::vec3 pos)
{
	_position = pos;
	_view_transforms[0] = glm::lookAt(pos, pos + glm::vec3( 1,  0,  0), glm::vec3(0, -1,  0));
	_view_transforms[1] = glm::lookAt(pos, pos + glm::vec3(-1,  0,  0), glm::vec3(0, -1,  0));
	_view_transforms[2] = glm::lookAt(pos, pos + glm::vec3( 0,  1,  0), glm::vec3(0,  0,  1));
	_view_transforms[3] = glm::lookAt(pos, pos + glm::vec3( 0, -1,  0), glm::vec3(0,  0, -1));
	_view_transforms[4] = glm::lookAt(pos, pos + glm::vec3( 0,  0,  1), glm::vec3(0, -1,  0));
	_view_transforms[5] = glm::lookAt(pos, pos + glm::vec3( 0,  0, -1), glm::vec3(0, -1,  0));

	_projection = glm::perspective(glm::radians(90.f), 1.f, 0.1f, 10.f);
}

void RenderTargetCube::bindTexture(GLuint unit)
{
	glBindTextureUnit(unit, _cubemap_texture_id);
}

void RenderTargetCube::bindRenderBuffer()
{
	glBindRenderbuffer(GL_RENDERBUFFER, _rbo_id);
}

void RenderTargetCube::bindRenderTarget(GLbitfield clear_mask)
{
	glBindFramebuffer(GL_FRAMEBUFFER, _fbo_id);
	glViewport(0, 0, _width, _height);
	glClear(clear_mask);
}

void RenderTargetCube::cleanup()
{
	if (_cubemap_texture_id != 0)
	{
		glDeleteTextures(1, &_cubemap_texture_id);
	}

	if (_fbo_id != 0)
	{
		glDeleteFramebuffers(1, &_fbo_id);
	}

	if (_rbo_id != 0)
	{
		glDeleteRenderbuffers(1, &_rbo_id);
	}
}
