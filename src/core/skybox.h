#pragma once

#include "glad/glad.h"

#include "shader.h"


namespace RGL
{

class Camera;
class TextureCube;

class SkyBox
{
public:
	SkyBox();
	~SkyBox();

	bool create();

	void setMipLevel(float level);
	inline float mipLevel() const { return _mip_level; }

	void render(const Camera &camera, const TextureCube &texture);
	void drawBox();

	operator bool () const;

private:
	void makeGeometry();

private:
	Shader _shader;
	float _mip_level { 1.2f };
	GLuint _vao;
	GLuint _vbo;
};

} // RGL