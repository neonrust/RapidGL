#include "rendertarget_common.h"
#include "container_types.h"

#include <iomanip>
#include <iostream>
#include <cstdio>
#include <string>
#include <fstream>
#include <regex>

using namespace std::literals;

namespace RGL::RenderTarget
{

bool check_fbo(GLuint fbo_id)
{
	const auto fbo_status = glCheckNamedFramebufferStatus(fbo_id, GL_FRAMEBUFFER);

	if(fbo_status != GL_FRAMEBUFFER_COMPLETE)
	{
		const char *msg = nullptr;
		// see https://registry.khronos.org/OpenGL-Refpages/gl4/html/glCheckFramebufferStatus.xhtml
		switch(fbo_status)
		{
		case GL_FRAMEBUFFER_UNDEFINED:
			msg = "Default read/draw framebuffer does not exist.";
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
			msg = "Some framebuffer attachments are framebuffer incomplete.";
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
			msg = "Framebuffer does not have at least one image attached to it.";
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
			msg = "GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE is GL_NONE for any color attachment point(s) named by GL_DRAW_BUFFERi.";
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
			msg = "GL_READ_BUFFER is not GL_NONE and the value of GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE is GL_NONE for the color attachment point named by GL_READ_BUFFER.";
			break;
		case GL_FRAMEBUFFER_UNSUPPORTED:
			msg = "The combination of internal formats of the attached images violates an implementation-dependent set of restrictions.";
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
			msg = "(complicated combination of conditions related to multisample was not satisfied, see https://registry.khronos.org/OpenGL-Refpages/gl4/html/glCheckFramebufferStatus.xhtml)";
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
			msg = "A framebuffer attachment is layered, and any populated attachment is not layered, or if all populated color attachments are not from textures of the same target.";
			break;
		default:
			msg = "(unknown status code)";
		}
		if(msg)
			std::fprintf(stderr, "FBO INVALID: %s (%d)\n", msg, fbo_status);
		return false;
	}

	return true;
}

#if !defined(NDEBUG)
std::string valueEnum(uint32_t value)
{
	static dense_map<uint32_t, std::string> enumMap;
	static bool tried_read = false;

		   // Parse once
	if (not tried_read)
	{
		tried_read = true;

		static const auto gl_h = "thirdparty/glad/include/glad/glad.h"s; // TODO: should be defined by build system

		std::ifstream file(gl_h);
		if (!file.is_open())
		{
			std::cerr << "Failed to open: " << gl_h << "\n";
			return {};
		}

		std::string line;
		std::regex defineRegex(R"-(#define\s+(GL_[A-Za-z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+))-");

		while (std::getline(file, line))
		{
			std::smatch match;
			if (std::regex_search(line, match, defineRegex))
			{
				std::string name = match[1];
				std::string valStr = match[2];

				uint32_t val = 0;
				if (valStr.find("0x") == 0 || valStr.find("0X") == 0)
					val = uint32_t(std::stoul(valStr, nullptr, 16));
				else
					val = uint32_t(std::stoul(valStr));

					   // Only keep first match (many names may share the same value)
				enumMap.emplace(val, name);
			}
		}
	}

	auto found = enumMap.find(value);
	if (found != enumMap.end())
		return found->second;
	else
		return "(unknown: " + std::to_string(value) + ")";
}

void dump_config(const char *fbo_name, GLuint fbo)
{
	auto printAttachment = [&](GLenum attachment) {
		GLenum type;
		GLuint obj;
		glGetNamedFramebufferAttachmentParameteriv(fbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, reinterpret_cast<GLint*>(&type));
		glGetNamedFramebufferAttachmentParameteriv(fbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, reinterpret_cast<GLint*>(&obj));
		// GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL

		if (type == GL_NONE)
			return;

		std::string label;
		switch (attachment)
		{
		case GL_DEPTH_ATTACHMENT:         label = "   Depth"; break;
		case GL_STENCIL_ATTACHMENT:       label = " Stencil"; break;
		// case GL_DEPTH_STENCIL_ATTACHMENT: label = "Depth+Stencil"; break;
		default:
			if (attachment >= GL_COLOR_ATTACHMENT0 and attachment <= GL_COLOR_ATTACHMENT31)
				label = " Color." + std::to_string(attachment - GL_COLOR_ATTACHMENT0);
			else
				label = valueEnum(attachment);
		}

		std::cout << "  " << label << ": ";

		GLint fmt = 0;
		GLint w = 0;
		GLint h = 0;

		if (type == GL_RENDERBUFFER)
		{
			std::cout << "Renderb.(" << obj << ")";

			glGetNamedRenderbufferParameteriv(obj, GL_RENDERBUFFER_INTERNAL_FORMAT, &fmt);
			glGetNamedRenderbufferParameteriv(obj, GL_RENDERBUFFER_WIDTH, &w);
			glGetNamedRenderbufferParameteriv(obj, GL_RENDERBUFFER_HEIGHT, &h);
		}
		else if (type == GL_TEXTURE)
		{
			std::cout << "Texture (" << obj << ")";

			glGetTextureLevelParameteriv(obj, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
			glGetTextureLevelParameteriv(obj, 0, GL_TEXTURE_WIDTH, &w);
			glGetTextureLevelParameteriv(obj, 0, GL_TEXTURE_HEIGHT, &h);
		}
		std::cout << "  " << w << " x " << h << "  " << valueEnum(uint32_t(fmt)) << " (0x" << std::hex << fmt << std::dec << ")\n";
	};

	std::cout << "FBO \"" << fbo_name << "\" (" << fbo << ")\n";

		   // Check for standard attachments
	for (int i = 0; i < 8; ++i)
		printAttachment(GLenum(GL_COLOR_ATTACHMENT0 + i));

	printAttachment(GL_DEPTH_ATTACHMENT);
	printAttachment(GL_STENCIL_ATTACHMENT);
	//	printAttachment(GL_DEPTH_STENCIL_ATTACHMENT);
}
#endif

} // RGL::RenderTarget
