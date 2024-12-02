#pragma once

#include "shader.h"

struct Texture2DRenderTarget;


struct TonemappingFilter
{
	TonemappingFilter(uint32_t width, uint32_t height);
	~TonemappingFilter();

	void bindTexture(GLuint unit = 0);

	void bindFilterFBO(GLbitfield clear_mask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	void render(float exposure, float gamma);

	const Texture2DRenderTarget &renderTarget() const { return *_rt; }
	Texture2DRenderTarget &renderTarget() { return *_rt; }

private:
	std::shared_ptr<RGL::Shader> _shader;
	std::shared_ptr<Texture2DRenderTarget> _rt;

	GLuint _dummy_vao_id;
};
