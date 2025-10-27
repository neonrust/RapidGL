#pragma once

#include "glad/glad.h"

namespace RGL::RenderTarget
{

namespace Color
{
	using Config = uint32_t;

	static constexpr Config None      = 0;
	static constexpr Config Byte      = 0x0100'0000;
	static constexpr Config Float     = 0x0400'0000;
	static constexpr Config Float2    = 0x0800'0000 | Float;   // RG16F or use a n extra bit for 16/32 bit?
	static constexpr Config HalfFloat = 0x1000'0000;
	static constexpr Config Texture   = 0x2000'0000;
	static constexpr Config Default   = Float | Texture;

	static constexpr Config custom_mask = 0x00ff'ffff; // anything in the lower bits is treated as a custom format
	inline bool is_custom(Config f)
	{
		return (f & custom_mask) > 0;
	}
};

namespace Depth
{
	using Config = uint32_t;

	static constexpr Config None     = 0;
	static constexpr Config Float    = 0x0400'0000;
	static constexpr Config Texture  = 0x2000'0000;
	static constexpr Config Default  = Float;
}

using BufferMask = uint32_t;
static constexpr BufferMask NoBuffer = 0;
static constexpr BufferMask ColorBuffer = GL_COLOR_BUFFER_BIT;
static constexpr BufferMask DepthBuffer = GL_DEPTH_BUFFER_BIT;

bool check_fbo(GLuint fbo_id);
#if !defined(NDEBUG)
void dump_config(const char *fbo_name, GLuint fbo);
#endif

} // RGL::RenderTarget
