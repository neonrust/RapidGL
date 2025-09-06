#include "texture.h"

#include <cstring>
#include <glm/glm.hpp>

#define TINYDDSLOADER_IMPLEMENTATION
#include <tinyddsloader.h>
#include "ktx_loader.h"

#include <ranges>
#include <print>
#include <chrono>
using namespace std::chrono;
using namespace std::literals;

namespace fs = std::filesystem;

using namespace tinyddsloader;

namespace
{

struct GLSwizzle
{
	GLenum m_r, m_g, m_b, m_a;
};

struct GLFormat
{
	DDSFile::DXGIFormat m_dxgiFormat;
	GLenum m_type;
	GLenum m_format;
	GLenum m_internal_format;
	GLSwizzle m_swizzle;
};

bool translateDdsFormat(DDSFile::DXGIFormat fmt, GLFormat* outFormat)
{
	static const GLSwizzle sws[] =
		{
		  { GL_RED,  GL_GREEN, GL_BLUE, GL_ALPHA },
		  { GL_BLUE, GL_GREEN, GL_RED,  GL_ALPHA },
		  { GL_BLUE, GL_GREEN, GL_RED,  GL_ONE   },
		  { GL_RED,  GL_GREEN, GL_BLUE, GL_ONE   },
		  { GL_RED,  GL_ZERO,  GL_ZERO, GL_ZERO  },
		  { GL_RED,  GL_GREEN, GL_ZERO, GL_ZERO  },
		  };

	using DXGIFmt = DDSFile::DXGIFormat;
	static const GLFormat formats[] =
		{
		  { DXGIFmt::R8G8B8A8_UNorm,     GL_UNSIGNED_BYTE, GL_RGBA,                                  GL_RGBA8UI,                               sws[0] },
		  { DXGIFmt::B8G8R8A8_UNorm,     GL_UNSIGNED_BYTE, GL_RGBA,                                  GL_RGBA8UI,                               sws[1] },
		  { DXGIFmt::B8G8R8X8_UNorm,     GL_UNSIGNED_BYTE, GL_RGBA,                                  GL_RGBA8UI,                               sws[2] },
		  { DXGIFmt::R32G32_Float,       GL_FLOAT,         GL_RG,                                    GL_RG32F,                                 sws[0] },
		  { DXGIFmt::R32G32B32A32_Float, GL_FLOAT,         GL_RGBA,                                  GL_RGBA32F,                               sws[0] },
		  { DXGIFmt::BC1_UNorm,          0,                GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,         GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,         sws[0] },
		  { DXGIFmt::BC2_UNorm,          0,                GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,         GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,         sws[0] },
		  { DXGIFmt::BC3_UNorm,          0,                GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,         GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,         sws[0] },
		  { DXGIFmt::BC4_UNorm,          0,                GL_COMPRESSED_RED_RGTC1_EXT,              GL_COMPRESSED_RED_RGTC1_EXT,              sws[0] },
		  { DXGIFmt::BC4_SNorm,          0,                GL_COMPRESSED_SIGNED_RED_RGTC1_EXT,       GL_COMPRESSED_SIGNED_RED_RGTC1_EXT,       sws[0] },
		  { DXGIFmt::BC5_UNorm,          0,                GL_COMPRESSED_RED_GREEN_RGTC2_EXT,        GL_COMPRESSED_RED_GREEN_RGTC2_EXT,        sws[0] },
		  { DXGIFmt::BC5_SNorm,          0,                GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT, GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT, sws[0] },
		  };

	for (const auto& format : formats)
	{
		if (format.m_dxgiFormat == fmt)
		{
			if (outFormat)
			{
				*outFormat = format;
			}
			return true;
		}
	}
	assert(0 && "Format not supported.");
	return false;
}

bool isDdsCompressed(GLenum fmt)
{
	switch (fmt)
	{
	case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
	case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
	case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
	case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
	case GL_COMPRESSED_RED_RGTC1_EXT:
	case GL_COMPRESSED_SIGNED_RED_RGTC1_EXT:
	case GL_COMPRESSED_RED_GREEN_RGTC2_EXT:
	case GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT:
		return true;
	default:
		return false;
	}
}

}

namespace RGL
{

// --------------------- Texture Sampler -------------------------
TextureSampler::TextureSampler() :
	_sampler_id(0),
	m_max_anisotropy(1.0f)
{
}

void TextureSampler::Create()
{
	assert(_sampler_id == 0);
	glCreateSamplers(1, &_sampler_id);

	SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::LinearMipLinear);
	SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
	SetWrapping (TextureWrappingAxis::U,    TextureWrappingParam::ClampToEdge);
	SetWrapping (TextureWrappingAxis::V,    TextureWrappingParam::ClampToEdge);
}

void TextureSampler::SetFiltering(TextureFiltering type, TextureFilteringParam filtering)
{
	if (type == TextureFiltering::Magnify && filtering > TextureFilteringParam::Linear)
	{
		filtering = TextureFilteringParam::Linear;
	}

	glSamplerParameteri(_sampler_id, GLenum(type), GLint(filtering));
}


void TextureSampler::SetMinLod(float lod)
{
	glSamplerParameterf(_sampler_id, GL_TEXTURE_MIN_LOD, lod);
}

void TextureSampler::SetMaxLod(float lod)
{
	glSamplerParameterf(_sampler_id, GL_TEXTURE_MAX_LOD, lod);
}

void TextureSampler::SetWrapping(TextureWrappingAxis axis, TextureWrappingParam wrapping)
{
	glSamplerParameteri(_sampler_id, GLenum(axis), GLint(wrapping));
}

void TextureSampler::SetBorderColor(float r, float g, float b, float a)
{
	float color[4] = { r, g, b, a };
	glSamplerParameterfv(_sampler_id, GL_TEXTURE_BORDER_COLOR, color);
}

void TextureSampler::SetCompareMode(TextureCompareMode mode)
{
	glSamplerParameteri(_sampler_id, GL_TEXTURE_COMPARE_MODE, GLint(mode));
}

void TextureSampler::SetCompareFunc(TextureCompareFunc func)
{
	glSamplerParameteri(_sampler_id, GL_TEXTURE_COMPARE_FUNC, GLint(func));
}

void TextureSampler::SetAnisotropy(float anisotropy)
{
	float max_anisotropy;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max_anisotropy);

	anisotropy = glm::clamp(anisotropy, 1.0f, max_anisotropy);
	glSamplerParameterf(_sampler_id, GL_TEXTURE_MAX_ANISOTROPY, anisotropy);
}

void TextureSampler::Bind(uint32_t texture_unit)
{
	glBindSampler(texture_unit, _sampler_id);
}

void TextureSampler::Release()
{
	glDeleteSamplers(1, &_sampler_id);
	_sampler_id = 0;
}

// --------------------- Texture -------------------------

bool Texture::Create(size_t width, size_t height, size_t depth, GLenum internalFormat, size_t num_mipmaps)
{
	if(_texture_id)
		Release();

	if(not num_mipmaps)
		num_mipmaps = calculateMipMapLevels(width, height, depth);

	if(height <= 1)
	{
		glCreateTextures(GLenum(TextureType::Texture1D), 1, &_texture_id);
		glTextureStorage1D(_texture_id, GLsizei(num_mipmaps), internalFormat, GLsizei(width));
	}
	if(depth <= 1)
	{
		glCreateTextures(GLenum(TextureType::Texture2D), 1, &_texture_id);
		glTextureStorage2D(_texture_id, GLsizei(num_mipmaps), internalFormat, GLsizei(width), GLsizei(height));
	}
	else
	{
		glCreateTextures(GLenum(TextureType::Texture3D), 1, &_texture_id);
		glTextureStorage3D(_texture_id, GLsizei(num_mipmaps), internalFormat, GLsizei(width), GLsizei(height), GLsizei(depth));
	}

	m_metadata.width = GLuint(width);
	m_metadata.height = GLuint(height);
	m_metadata.depth = GLuint(depth);
	m_metadata.channels = 0;
	m_metadata.channel_type = 0;
	m_metadata.channel_format = 0;

	return true;
}

void Texture::Bind(uint32_t unit) const
{
	glBindTextureUnit(unit, _texture_id);
}

void Texture::SetFiltering(TextureFiltering type, TextureFilteringParam filtering)
{
	if (type == TextureFiltering::Magnify and filtering > TextureFilteringParam::Linear)
		filtering = TextureFilteringParam::Linear;

	glTextureParameteri(_texture_id, GLenum(type), GLint(filtering));
}

void Texture::SetMinLod(float min)
{
	glTextureParameterf(_texture_id, GL_TEXTURE_MIN_LOD, min);
}

void Texture::SetMaxLod(float max)
{
	glTextureParameterf(_texture_id, GL_TEXTURE_MAX_LOD, max);
}

void Texture::SetWrapping(TextureWrappingAxis axis, TextureWrappingParam wrapping)
{
	glTextureParameteri(_texture_id, GLenum(axis), GLint(wrapping));
}

void Texture::SetBorderColor(float r, float g, float b, float a)
{
	float color[4] = { r, g, b, a };
	glTextureParameterfv(_texture_id, GL_TEXTURE_BORDER_COLOR, color);
}

void Texture::SetCompareMode(TextureCompareMode mode)
{
	glTextureParameteri(_texture_id, GL_TEXTURE_COMPARE_MODE, GLint(mode));
}

void Texture::SetCompareFunc(TextureCompareFunc func)
{
	glTextureParameteri(_texture_id, GL_TEXTURE_COMPARE_FUNC, GLint(func));
}

void Texture::SetAnisotropy(float anisotropy)
{
	float max_anisotropy;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max_anisotropy);

	anisotropy = glm::clamp(anisotropy, 1.0f, max_anisotropy);
	glTextureParameterf(_texture_id, GL_TEXTURE_MAX_ANISOTROPY, anisotropy);
}

void Texture::SetBaseLevel(uint32_t level) const
{
	glTextureParameteri(_texture_id, GL_TEXTURE_BASE_LEVEL, GLint(level));
}

void Texture::SetMaxLevel(uint32_t level) const
{
	glTextureParameteri(_texture_id, GL_TEXTURE_MAX_LEVEL, GLint(level));
}

void Texture::GenerateMipMaps()
{
	glGenerateTextureMipmap(_texture_id);
}

uint8_t Texture::calculateMipMapLevels(size_t width, size_t height, size_t depth, size_t min_size, size_t max_levels)
{
	if(not min_size and not max_levels)
	{
		const auto max_extent = std::max(width, std::max(height, depth));
		return uint8_t(1.f + std::floor(std::log2(float(max_extent))));
	}

	if(min_size == 0)
		min_size = 1;

	const auto use_height = height > 0;
	const auto use_depth = depth > 0;

	uint_fast8_t levels = 0;

	for (; levels < max_levels; ++levels)
	{
		width  >>= 1;
		height >>= 1;
		depth  >>= 1;

		if(width < min_size or (use_height and height < min_size) or (use_depth and depth < min_size))
			break;
	}

	return levels + 1;
}

void Texture::Release()
{
	if(_texture_id)
		glDeleteTextures(1, &_texture_id);
	_texture_id = 0;
}

void Texture::set(const TextureDescriptor &descr)
{
	m_metadata = descr.meta;
	m_type = descr.type;
	_texture_id = descr.texture_id;
}

bool Texture3D::Load(const std::filesystem::path& filepath)
{
	if(filepath.extension() == ".ktx2")
	{
		auto descr = ktx_load<Texture3D>(filepath);
		if(descr)
		{
			set(descr);
			return true;
		}
	}
	return false;
}

bool Texture1D::Create(size_t width, GLenum internalFormat, size_t num_mipmaps)
{
	return Texture::Create(width, 0, 0, internalFormat, num_mipmaps);
}

bool Texture2D::Create(size_t width, size_t height, GLenum internalFormat, size_t num_mipmaps)
{
	return Texture::Create(width, height, 0, internalFormat, num_mipmaps);
}

bool Texture3D::Create(size_t width, size_t height, size_t depth, GLenum internalFormat, size_t num_mipmaps)
{
	return Texture::Create(width, height, depth, internalFormat, num_mipmaps);
}

// --------------------- Texture2D -------------------------

bool Texture2D::Load(const std::filesystem::path& filepath, bool is_srgb, uint32_t num_mipmaps)
{
	if(filepath.extension() == ".ktx2")
	{
		auto descr = ktx_load<Texture2D>(filepath);
		if(not descr)
			return false;

		set(descr);
		return true;
	}

	auto data = Util::LoadTextureData(filepath, m_metadata);

	if (!data)
	{
		std::print(stderr, "Texture failed to load: {}\n", filepath.string());
		return false;
	}

	GLenum format          = 0;
	GLenum internal_format = 0;

	if (m_metadata.channels == 1)
	{
		format          = GL_RED;
		internal_format = GL_R8;
	}
	else if (m_metadata.channels == 3)
	{
		format          = GL_RGB;
		internal_format = is_srgb ? GL_SRGB8 : GL_RGB8;
	}
	else if (m_metadata.channels == 4)
	{
		format          = GL_RGBA;
		internal_format = is_srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
	}

	const GLuint max_num_mipmaps = calculateMipMapLevels(m_metadata.width, m_metadata.height);
	num_mipmaps     = num_mipmaps == 0 ? max_num_mipmaps : glm::clamp(num_mipmaps, 1u, max_num_mipmaps);

	glCreateTextures       (GLenum(TextureType::Texture2D), 1, &_texture_id);
	glTextureStorage2D     (_texture_id, GLsizei(num_mipmaps) /* levels */, internal_format, GLsizei(m_metadata.width), GLsizei(m_metadata.height));
	glTextureSubImage2D    (_texture_id, 0 /* level */, 0 /* xoffset */, 0 /* yoffset */, GLsizei(m_metadata.width), GLsizei(m_metadata.height), format, GL_UNSIGNED_BYTE, data.get());
	glGenerateTextureMipmap(_texture_id);

	SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::LinearMipLinear);
	SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
	SetWrapping (TextureWrappingAxis::U,    TextureWrappingParam::ClampToEdge);
	SetWrapping (TextureWrappingAxis::V,    TextureWrappingParam::ClampToEdge);

	Util::ReleaseTextureData(data);

	return true;
}

bool Texture2D::Load(unsigned char* memory_data, uint32_t data_size, bool is_srgb, uint32_t num_mipmaps)
{
	auto data = Util::LoadTextureData(memory_data, data_size, m_metadata);

	if (not data)
	{
		std::print(stderr, "Texture failed to load from the memory.\n");
		return false;
	}

	GLenum format          = 0;
	GLenum internal_format = 0;

	if (m_metadata.channels == 1)
	{
		format          = GL_RED;
		internal_format = GL_R8;
	}
	else if (m_metadata.channels == 3)
	{
		format          = GL_RGB;
		internal_format = is_srgb ? GL_SRGB8 : GL_RGB8;
	}
	else if (m_metadata.channels == 4)
	{
		format          = GL_RGBA;
		internal_format = is_srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
	}

	const GLuint max_num_mipmaps = calculateMipMapLevels(m_metadata.width, m_metadata.height);
	num_mipmaps     = num_mipmaps == 0 ? max_num_mipmaps : glm::clamp(num_mipmaps, 1u, max_num_mipmaps);


	glCreateTextures       (GLenum(TextureType::Texture2D), 1, &_texture_id);
	glTextureStorage2D     (_texture_id, GLsizei(num_mipmaps) /* levels */, internal_format, GLsizei(m_metadata.width), GLsizei(m_metadata.height));
	glTextureSubImage2D    (_texture_id, 0 /* level */, 0 /* xoffset */, 0 /* yoffset */, GLsizei(m_metadata.width), GLsizei(m_metadata.height), format, GL_UNSIGNED_BYTE, data.get());
	glGenerateTextureMipmap(_texture_id);

	SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::LinearMipLinear);
	SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
	SetWrapping (TextureWrappingAxis::U,    TextureWrappingParam::ClampToEdge);
	SetWrapping (TextureWrappingAxis::V,    TextureWrappingParam::ClampToEdge);

	Util::ReleaseTextureData(data);

	return true;
}

bool Texture2D::LoadHdr(const std::filesystem::path & filepath, uint32_t num_mipmaps)
{
	const auto T0 = steady_clock::now();
	// if (filepath.extension() != ".hdr")
	// {
	// 	std::print(stderr, "This function is meant for loading HDR images only.\n");
	// 	return false;
	// }

	auto data = Util::LoadTextureDataHdr(filepath, m_metadata);

	if (not data)
	{
		std::print(stderr, "Texture failed to load: {}\n", filepath.generic_string());
		return false;
	}

	GLenum format          = m_metadata.channel_format;
	GLenum type            = m_metadata.channel_type;
	GLenum internal_format = GL_RGB16F;

	const GLuint max_num_mipmaps = calculateMipMapLevels(m_metadata.width, m_metadata.height);
	num_mipmaps     = num_mipmaps == 0 ? max_num_mipmaps : glm::clamp(num_mipmaps, 1u, max_num_mipmaps);

	glCreateTextures       (GLenum(TextureType::Texture2D), 1, &_texture_id);
	glTextureStorage2D     (_texture_id, 1 /* levels */, internal_format, GLsizei(m_metadata.width), GLsizei(m_metadata.height));
	glTextureSubImage2D    (_texture_id, 0 /* level */, 0 /* xoffset */, 0 /* yoffset */, GLsizei(m_metadata.width), GLsizei(m_metadata.height), format, type, data.get());
	glGenerateTextureMipmap(_texture_id);

	SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::Linear);
	SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
	SetWrapping (TextureWrappingAxis::U,    TextureWrappingParam::ClampToEdge);
	SetWrapping (TextureWrappingAxis::V,    TextureWrappingParam::ClampToEdge);

	Util::ReleaseTextureData(data);

	std::print("Loaded HDR texture {}  ({} ms)\n", filepath.c_str(), duration_cast<milliseconds>(steady_clock::now() - T0));

	return true;
}

bool Texture2D::LoadDds(const std::filesystem::path& filepath)
{
	DDSFile dds;
	auto ret = dds.Load(filepath.string().c_str());

	if (Result::Success != ret)
	{
		std::print("Failed to load.[{}]\n", filepath.string());
		std::print("Result : {}\n", int(ret));

		std::print(stderr, "Texture failed to load: {}\n", filepath.string());
		std::print(stderr, "Result: {}\n", int(ret));
		return false;
	}

	if (dds.GetTextureDimension() == DDSFile::TextureDimension::Texture2D)
	{
		m_type = TextureType::Texture2D;
	}

	GLFormat format;
	if (!translateDdsFormat(dds.GetFormat(), &format))
	{
		return false;
	}

	glCreateTextures   (GLenum(m_type), 1, &_texture_id);
	glTextureParameteri(_texture_id, GL_TEXTURE_BASE_LEVEL, 0);
	glTextureParameteri(_texture_id, GL_TEXTURE_MAX_LEVEL, GLint(dds.GetMipCount() - 1));
	glTextureParameteri(_texture_id, GL_TEXTURE_SWIZZLE_R, GLint(format.m_swizzle.m_r));
	glTextureParameteri(_texture_id, GL_TEXTURE_SWIZZLE_G, GLint(format.m_swizzle.m_g));
	glTextureParameteri(_texture_id, GL_TEXTURE_SWIZZLE_B, GLint(format.m_swizzle.m_b));
	glTextureParameteri(_texture_id, GL_TEXTURE_SWIZZLE_A, GLint(format.m_swizzle.m_a));

	m_metadata.width  = dds.GetWidth();
	m_metadata.height = dds.GetHeight();

	glTextureStorage2D(_texture_id, GLsizei(dds.GetMipCount()), format.m_internal_format, GLsizei(m_metadata.width), GLsizei(m_metadata.height));
	dds.Flip();

	for (uint32_t level = 0; level < dds.GetMipCount(); level++)
	{
		auto imageData = dds.GetImageData(level, 0);
		switch (GLenum(m_type))
		{
		case GL_TEXTURE_2D:
		{
			auto w = imageData->m_width;
			auto h = imageData->m_height;

			if (isDdsCompressed(format.m_format))
			{
				glCompressedTextureSubImage2D(_texture_id, GLint(level), 0, 0, GLsizei(w), GLsizei(h), format.m_format, GLsizei(imageData->m_memSlicePitch), imageData->m_mem);
			}
			else
			{
				glTextureSubImage2D(_texture_id, GLint(level), 0, 0, GLsizei(w), GLsizei(h), format.m_format, format.m_type, imageData->m_mem);
			}
			break;
		}
		default:
			return false;
		}
	}

	return true;
}

// --------------------- Texture Array -------------------------

bool Texture2DArray::Create(size_t width, size_t height, size_t layers, GLenum internalFormat, size_t num_mipmaps)
{
	(void)width;
	(void)height;
	(void)layers;
	(void)internalFormat;
	(void)num_mipmaps;
	std::print(stderr, "Texture2DArray::Create - Not implenented!\n");
	return false;
}

bool Texture2DArray::Load(const fs::path &filepath, bool is_srgb)
{
	if(filepath.extension() == ".ktx2"sv)
	{
		auto descr = ktx_load<Texture2DArray>(filepath);
		if(not descr)
			return false;

		set(descr);
		return true;
	}
	else if(filepath.extension() == ".array"sv)
	{
		// read list of file names (absolute of relative to .array-file), one per line
		std::vector<fs::path> filepaths;

		std::ifstream fp;
		fp.open(filepath, std::ios::in);
		if(not fp.is_open())
		{
			std::print(stderr, "[{}] failed to open file: {}\n", filepath.string(), std::strerror(errno));
			return false;
		}

		const auto base_path = filepath.parent_path();
		std::string line;
		while(not fp.eof())
		{
			std::getline(fp, line);
			if(not line.empty())
			{
				auto icon_path = fs::path(line);
				if(not icon_path.is_absolute())
					icon_path = base_path / icon_path;
				filepaths.push_back(icon_path);
			}
		}
		fp.close();
		return LoadLayers(filepaths, is_srgb);
	}

	return false;
}

bool Texture2DArray::LoadLayers(const std::vector<fs::path> &paths, bool is_srgb)
{
	GLenum format          = 0;
	GLenum internal_format = 0;

	for(const auto &[layer_index, filepath]: std::views::enumerate(paths))
	{
		ImageMeta meta;
		auto data = Util::LoadTextureData(filepath, meta, 0);

		if(not data)
		{
			std::print(stderr, "Texture failed to load: {}\n", filepath.string());
			return false;
		}

		const auto num_mipmaps = calculateMipMapLevels(meta.width, meta.height);

		if(_texture_id == 0)
		{
			if (meta.channels == 1)
			{
				format          = GL_RED;
				internal_format = GL_R8;
			}
			else if (meta.channels == 3)
			{
				format          = GL_RGB;
				internal_format = is_srgb ? GL_SRGB8 : GL_RGB8;
			}
			else if (meta.channels == 4)
			{
				format          = GL_RGBA;
				internal_format = is_srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
			}

			glCreateTextures   (GLenum(TextureType::Texture2DArray), 1, &_texture_id);
			glTextureStorage3D (_texture_id, GLsizei(num_mipmaps) /* levels */, internal_format, GLsizei(meta.width), GLsizei(meta.height), GLsizei(paths.size()));

			m_metadata = meta;
		}
		else
		{
			assert(meta.width == m_metadata.width);
			assert(meta.height == m_metadata.height);
			assert(meta.channels == m_metadata.channels);
		}

		glTextureSubImage3D(_texture_id, 0 /* level */,
							0 /* xoffset */, 0 /* yoffset */, GLint(layer_index),
							GLsizei(meta.width), GLsizei(meta.height), 1,
							format, GL_UNSIGNED_BYTE, data.get());

		SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::LinearMipLinear);
		SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
		SetWrapping (TextureWrappingAxis::U,    TextureWrappingParam::ClampToEdge);
		SetWrapping (TextureWrappingAxis::V,    TextureWrappingParam::ClampToEdge);

		Util::ReleaseTextureData(data);
	}

	glGenerateTextureMipmap(_texture_id);

	return true;
}

bool Texture2DArray::LoadDds(const std::filesystem::path &filepath)
{
	DDSFile dds;
	auto ret = dds.Load(filepath.string().c_str());

	if (Result::Success != ret)
	{
		std::print("Failed to load.[{}]\n", filepath.string());
		std::print("Result : {}\n", int(ret));

		std::print(stderr, "Texture failed to load: {}\n", filepath.string());
		std::print(stderr, "Result: {}\n", int(ret));
		return false;
	}

	if (dds.GetTextureDimension() == DDSFile::TextureDimension::Texture2D)
	{
		m_type = TextureType::Texture2D;
	}

	GLFormat format;
	if (!translateDdsFormat(dds.GetFormat(), &format))
	{
		return false;
	}

	glCreateTextures   (GLenum(m_type), 1, &_texture_id);
	glTextureParameteri(_texture_id, GL_TEXTURE_BASE_LEVEL, 0);
	glTextureParameteri(_texture_id, GL_TEXTURE_MAX_LEVEL, GLint(dds.GetMipCount() - 1));
	glTextureParameteri(_texture_id, GL_TEXTURE_SWIZZLE_R, GLint(format.m_swizzle.m_r));
	glTextureParameteri(_texture_id, GL_TEXTURE_SWIZZLE_G, GLint(format.m_swizzle.m_g));
	glTextureParameteri(_texture_id, GL_TEXTURE_SWIZZLE_B, GLint(format.m_swizzle.m_b));
	glTextureParameteri(_texture_id, GL_TEXTURE_SWIZZLE_A, GLint(format.m_swizzle.m_a));

	m_metadata.width  = dds.GetWidth();
	m_metadata.height = dds.GetHeight();

	glTextureStorage2D(_texture_id, GLsizei(dds.GetMipCount()), format.m_internal_format, GLsizei(m_metadata.width), GLsizei(m_metadata.height));
	dds.Flip();

	for (uint32_t level = 0; level < dds.GetMipCount(); level++)
	{
		auto imageData = dds.GetImageData(level, 0);
		switch (GLenum(m_type))
		{
		case GL_TEXTURE_2D:
		{
			auto w = imageData->m_width;
			auto h = imageData->m_height;

			if (isDdsCompressed(format.m_format))
			{
				glCompressedTextureSubImage2D(_texture_id, GLint(level), 0, 0, GLsizei(w), GLsizei(h), format.m_format, GLsizei(imageData->m_memSlicePitch), imageData->m_mem);
			}
			else
			{
				glTextureSubImage2D(_texture_id, GLint(level), 0, 0, GLsizei(w), GLsizei(h), format.m_format, format.m_type, imageData->m_mem);
			}
			break;
		}
		default:
			return false;
		}
	}

	return true;
}

// --------------------- Texture CubeMap -------------------------

bool TextureCube::Create(size_t width, size_t height, GLenum internalFormat, size_t num_mipmaps)
{
	if(_texture_id)
		Release();

	if(not num_mipmaps)
		num_mipmaps = calculateMipMapLevels(width, height, 0);

	glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &_texture_id);
	assert(_texture_id);

	glTextureStorage2D(_texture_id, GLsizei(num_mipmaps), internalFormat, GLsizei(width), GLsizei(height));


	// create texture views for each face (useful for debugging, maybe nothing else?)
	createFaceViews(internalFormat);

	return true;
}

void TextureCube::createFaceViews(GLenum internalFormat)
{
	glGenTextures(6, _faceViews.data());

	for(auto face = 0u; face < 6; ++face)
		glTextureView(_faceViews[face], GL_TEXTURE_2D, _texture_id, internalFormat, 0, 1,  GLuint(face), 1);
}

void TextureCube::BindFace(CubeFace face, uint32_t unit)
{
	glBindTextureUnit(unit, _faceViews[uint32_t(face)]);
}

bool TextureCube::Load(const std::array<std::filesystem::path, 6> &filepaths, bool is_srgb, uint32_t num_mipmaps)
{
	constexpr int NUM_FACES = 6;

	Util::TextureData images_data[NUM_FACES];

	for (int idx = 0; idx < NUM_FACES; ++idx)
	{
		images_data[idx] = Util::LoadTextureData(filepaths[idx], m_metadata);

		if (not images_data[idx])
		{
			std::print(stderr, "Texture failed to load: {}\n", filepaths[idx].string());
			return false;
		}
	}

	GLuint m_format          = m_metadata.channels == 4 ? GL_RGBA         : GL_RGB;
	GLuint m_internal_format = is_srgb                  ? GL_SRGB8_ALPHA8 : GL_RGBA8;

	const GLuint max_num_mipmaps = calculateMipMapLevels(m_metadata.width, m_metadata.height);
	num_mipmaps     = num_mipmaps == 0 ? max_num_mipmaps : glm::clamp(num_mipmaps, 1u, max_num_mipmaps);

	glCreateTextures  (GLenum(TextureType::TextureCube), 1, &_texture_id);
	glTextureStorage2D(_texture_id, GLsizei(num_mipmaps), m_internal_format, GLsizei(m_metadata.width), GLsizei(m_metadata.height));

	for (int idx = 0; idx < NUM_FACES; ++idx)
	{
		glTextureSubImage3D(_texture_id,
							0,   // level
							0,   // xoffset
							0,   // yoffset
							idx, // zoffset
							GLsizei(m_metadata.width),
							GLsizei(m_metadata.height),
							1,   // depth
							m_format,
							GL_UNSIGNED_BYTE,
							images_data[idx].get());
	}

	glGenerateTextureMipmap(_texture_id);

	SetFiltering(TextureFiltering::Minify,  TextureFilteringParam::LinearMipLinear);
	SetFiltering(TextureFiltering::Magnify, TextureFilteringParam::Linear);
	SetWrapping  (TextureWrappingAxis::U,   TextureWrappingParam::ClampToEdge);
	SetWrapping  (TextureWrappingAxis::V,   TextureWrappingParam::ClampToEdge);
	SetWrapping  (TextureWrappingAxis::W,   TextureWrappingParam::ClampToEdge);

	for (int idx = 0; idx < NUM_FACES; ++idx)
	{
		/* Release images' data */
		Util::ReleaseTextureData(images_data[idx]);
	}

	createFaceViews(m_internal_format);

	return true;
}

void TextureCube::Release()
{
	glDeleteTextures(6, _faceViews.data());
	_faceViews = { 0, 0, 0, 0, 0, 0 };

	Texture::Release();
}

}
