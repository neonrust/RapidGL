#pragma once

#include "shader.h"

struct Texture2DRenderTarget;


struct TonemappingFilter
{
	std::shared_ptr<RGL::Shader> m_shader;
	std::shared_ptr<Texture2DRenderTarget> rt;

	GLuint m_dummy_vao_id;

	TonemappingFilter(uint32_t width, uint32_t height);

	~TonemappingFilter();

	void bindTexture(GLuint unit = 0);

	void bindFilterFBO(GLbitfield clear_mask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	void render(float exposure, float gamma);
};
