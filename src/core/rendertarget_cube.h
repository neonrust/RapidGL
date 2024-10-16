#pragma once

#include "glad/glad.h"
#include "glm/ext/matrix_float4x4.hpp"


struct CubeMapRenderTarget
{
	glm::mat4 m_view_transforms[6];
	glm::mat4 m_projection;

	GLuint    m_cubemap_texture_id = 0;
	GLuint    m_fbo_id             = 0;
	GLuint    m_rbo_id             = 0;
	glm::vec3 m_position           = glm::vec3(0.0f);
	GLsizei m_width, m_height;

	~CubeMapRenderTarget() { cleanup(); }

	void set_position(const glm::vec3 pos);

	void bindTexture(GLuint unit = 0);

	void cleanup();

	void generate_rt(uint32_t width, uint32_t height, bool gen_mip_levels = false);
};
