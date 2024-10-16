#include "rendertarget_cube.h"

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"


void CubeMapRenderTarget::generate_rt(uint32_t width, uint32_t height, bool gen_mip_levels)
{
	m_width  = GLsizei(width);
	m_height = GLsizei(height);

	glGenTextures(1, &m_cubemap_texture_id);
	glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubemap_texture_id);

	for (uint8_t i = 0; i < 6; ++i)
	{
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, m_width, m_height, 0, GL_RGB, GL_FLOAT, 0);
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, gen_mip_levels ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	if (gen_mip_levels)
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	glGenFramebuffers(1, &m_fbo_id);
	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo_id);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, m_cubemap_texture_id, 0);

	glGenRenderbuffers(1, &m_rbo_id);
	glBindRenderbuffer(GL_RENDERBUFFER, m_rbo_id);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_width, m_height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_rbo_id);

	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CubeMapRenderTarget::set_position(const glm::vec3 pos)
{
	m_position = pos;
	m_view_transforms[0] = glm::lookAt(pos, pos + glm::vec3( 1,  0,  0), glm::vec3(0, -1,  0));
	m_view_transforms[1] = glm::lookAt(pos, pos + glm::vec3(-1,  0,  0), glm::vec3(0, -1,  0));
	m_view_transforms[2] = glm::lookAt(pos, pos + glm::vec3( 0,  1,  0), glm::vec3(0,  0,  1));
	m_view_transforms[3] = glm::lookAt(pos, pos + glm::vec3( 0, -1,  0), glm::vec3(0,  0, -1));
	m_view_transforms[4] = glm::lookAt(pos, pos + glm::vec3( 0,  0,  1), glm::vec3(0, -1,  0));
	m_view_transforms[5] = glm::lookAt(pos, pos + glm::vec3( 0,  0, -1), glm::vec3(0, -1,  0));

	m_projection = glm::perspective(glm::radians(90.f), 1.f, 0.1f, 10.f);
}

void CubeMapRenderTarget::bindTexture(GLuint unit)
{
	glBindTextureUnit(unit, m_cubemap_texture_id);
}

void CubeMapRenderTarget::cleanup()
{
	if (m_cubemap_texture_id != 0)
	{
		glDeleteTextures(1, &m_cubemap_texture_id);
	}

	if (m_fbo_id != 0)
	{
		glDeleteFramebuffers(1, &m_fbo_id);
	}

	if (m_rbo_id != 0)
	{
		glDeleteRenderbuffers(1, &m_rbo_id);
	}
}
