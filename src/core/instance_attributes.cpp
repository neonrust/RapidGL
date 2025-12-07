#include "instance_attributes.h"

using namespace std::literals;

namespace RGL
{

InstanceAttributes::InstanceAttributes() :
	_buf("inst-attrs"sv)
{
}

InstanceAttributes::~InstanceAttributes()
{
	if(_vao_owner and _vao)
	{
		glDeleteVertexArrays(1, &_vao);
		_vao = 0;
	}
}

void InstanceAttributes::config(size_t stride, GLuint bindingIndex)
{
	_bind_index = bindingIndex;
	_stride = GLsizei(stride);
}

void InstanceAttributes::config_with_vao(GLuint vao, size_t stride, GLuint bindingIndex)
{
	assert(not _vao);
	config(stride, bindingIndex);
	_vao = vao;
	_vao_owner = false;
}

void InstanceAttributes::skip(uint32_t loc, uint32_t offset)
{
	_attrib_location += loc;
	assert(_offset + offset <= uint32_t(_stride));
	_offset += offset;
}

void InstanceAttributes::bind_vao()
{
	assert(_vao > 0);

	glBindVertexArray(_vao);
}

void InstanceAttributes::ensureCreated()
{
	if(not _buf)
	{
		_buf.create();

		if(_vao_owner)
			glCreateVertexArrays(1, &_vao);

		glVertexArrayVertexBuffer(
			_vao,
			_bind_index,
			_buf.id(),
			0,               // offset
			_stride
		);
	}
}


} // RGL
