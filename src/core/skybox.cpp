#include "skybox.h"

#include "filesystem.h"
#include "camera.h"
#include "texture.h"

#include <glm/vec3.hpp>

using namespace std::literals;

namespace RGL
{

SkyBox::SkyBox() :
	_vao(0),
	_vbo(0)
{

}

SkyBox::~SkyBox()
{
	if(_vao)
	{
		glDeleteVertexArrays(1, &_vao);
		_vao = 0;
	}

	if(_vbo)
	{
		glDeleteBuffers(1, &_vbo);
		_vbo = 0;
	}
}

bool SkyBox::create()
{
	glCreateVertexArrays(1, &_vao);
	glCreateBuffers(1, &_vbo);

	makeGeometry();

	const auto shader_dir = FileSystem::getResourcesPath() / "shaders";

	new (&_shader) Shader(shader_dir / "background.vert", shader_dir / "background.frag");
	_shader.link();
	assert(_shader);

	return bool(*this);
}

void SkyBox::render(const Camera &camera, const TextureCube &texture)
{
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDepthFunc(GL_LEQUAL);

	_shader.bind();
	camera.setUniforms(_shader);
	_shader.setUniform("u_view_orientation"sv, glm::mat4(glm::mat3(camera.viewTransform())));  // only rotational part
	_shader.setUniform("u_mip_level"sv,        _mip_level);
	texture.Bind();

	drawBox();
}

void SkyBox::drawBox()
{
	glBindVertexArray(_vao);
	glDrawArrays(GL_TRIANGLES, 0, 36);
}

SkyBox::operator bool() const
{
	return _vao != 0 and _vbo != 0 and bool(_shader);
}

void SkyBox::makeGeometry()
{
	// vertex positions for cube faces
	glm::vec3 vertices[] = {
		// back face
		{ -1, -1, -1 },
		{  1, -1, -1 },
		{  1,  1, -1 },
		{  1,  1, -1 },
		{ -1,  1, -1 },
		{ -1, -1, -1 },
		// front face
		{ -1, -1,  1 },
		{  1,  1,  1 },
		{  1, -1,  1 },
		{  1,  1,  1 },
		{ -1, -1,  1 },
		{ -1,  1,  1 },
		// left face
		{ -1,  1,  1 },
		{ -1, -1, -1 },
		{ -1,  1, -1 },
		{ -1, -1, -1 },
		{ -1,  1,  1 },
		{ -1, -1,  1 },
		// right face
		{ 1,  1,  1 },
		{ 1,  1, -1 },
		{ 1, -1, -1 },
		{ 1, -1, -1 },
		{ 1, -1,  1 },
		{ 1,  1,  1 },
		// bottom face
		{ -1, -1, -1 },
		{  1, -1,  1 },
		{  1, -1, -1 },
		{  1, -1,  1 },
		{ -1, -1, -1 },
		{ -1, -1,  1 },
		// top face
		{ -1,  1, -1 },
		{  1,  1, -1 },
		{  1,  1 , 1 },
		{  1,  1,  1 },
		{ -1,  1,  1 },
		{ -1,  1, -1 },
	};

	glNamedBufferStorage(_vbo, sizeof(vertices), &vertices[0], 0 /*flags*/);

	glEnableVertexArrayAttrib(_vao, 0 /*index*/);

	glVertexArrayAttribFormat(_vao, 0 /*index*/, 3 /*size*/, GL_FLOAT, GL_FALSE, 0 /*relativeoffset*/);
	glVertexArrayAttribBinding(_vao, 0 /*index*/, 0 /*bindingindex*/);
	glVertexArrayVertexBuffer(_vao, 0 /*bindingindex*/, _vbo, 0 /*offset*/, sizeof(glm::vec3) /*stride*/);
}


} // RGL