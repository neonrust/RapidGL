#pragma once

#include "shader.h"

namespace RGL::RenderTarget
{
struct Texture2d;
}


struct TonemappingFilter
{
	TonemappingFilter(uint32_t width, uint32_t height);
	~TonemappingFilter();

	void bindTexture(GLuint unit = 0);

	void bindRenderTarget(GLbitfield clear_mask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	void render(float exposure, float gamma);

	const RGL::RenderTarget::Texture2d &renderTarget() const { return *_rt; }
	RGL::RenderTarget::Texture2d &renderTarget() { return *_rt; }

private:
	std::shared_ptr<RGL::Shader> _shader;
	std::shared_ptr<RGL::RenderTarget::Texture2d> _rt;

	GLuint _dummy_vao_id;
};
