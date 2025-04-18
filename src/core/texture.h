#pragma once
#include "util.h"

#include <glad/glad.h>
#include <string_view>
#include <filesystem>

namespace RGL
{

enum class TextureType
{
	NONE = 0,
	Texture1D = GL_TEXTURE_1D,
	Texture2D = GL_TEXTURE_2D,
	Texture3D = GL_TEXTURE_3D,
	TextureCubeMap = GL_TEXTURE_CUBE_MAP
};

enum class TextureFiltering
{
	Magnify  = GL_TEXTURE_MAG_FILTER,
	Minify  = GL_TEXTURE_MIN_FILTER
};

enum class TextureFilteringParam
{
	NEAREST              = GL_NEAREST,
	LINEAR               = GL_LINEAR,
	NEAREST_MIP_NEAREST  = GL_NEAREST_MIPMAP_NEAREST,
	LINEAR_MIP_NEAREST   = GL_LINEAR_MIPMAP_NEAREST,
	NEAREST_MIP_LINEAR   = GL_NEAREST_MIPMAP_LINEAR,
	LINEAR_MIP_LINEAR    = GL_LINEAR_MIPMAP_LINEAR
};

enum class TextureWrappingAxis
{
	S = GL_TEXTURE_WRAP_S,
	T = GL_TEXTURE_WRAP_T,
	R = GL_TEXTURE_WRAP_R
};

enum class TextureWrappingParam
{
	REPEAT              = GL_REPEAT,
	MIRRORED_REPEAT      = GL_MIRRORED_REPEAT,
	CLAMP_TO_EDGE        = GL_CLAMP_TO_EDGE,
	CLAMP_TO_BORDER      = GL_CLAMP_TO_BORDER,
	MIRROR_CLAMP_TO_EDGE = GL_MIRROR_CLAMP_TO_EDGE
};

enum class TextureCompareMode
{
	NONE = GL_NONE,
	REF  = GL_COMPARE_REF_TO_TEXTURE }
;

enum class TextureCompareFunc
{
	NEVER    = GL_NEVER,
	ALWAYS   = GL_ALWAYS,
	LEQUAL   = GL_LEQUAL,
	GEQUAL   = GL_GEQUAL,
	LESS     = GL_LESS,
	GREATER  = GL_GREATER,
	EQUAL    = GL_EQUAL,
	NOTEQUAL = GL_NOTEQUAL
};

class TextureSampler final
{
public:
	TextureSampler();
	~TextureSampler() { Release(); };

	TextureSampler(const TextureSampler&) = delete;
	TextureSampler& operator = (const TextureSampler&) = delete;

	TextureSampler(TextureSampler&& other) noexcept : m_so_id(other.m_so_id), m_max_anisotropy(other.m_max_anisotropy)
	{
		other.m_so_id = 0;
	}

	TextureSampler& operator = (TextureSampler&& other) noexcept
	{
		if (this != &other)
		{
			Release();
			std::swap(m_so_id, other.m_so_id);
			std::swap(m_max_anisotropy, other.m_max_anisotropy);
		}

		return *this;
	}

	void Create();
	void SetFiltering(TextureFiltering type, TextureFilteringParam param);
	void SetMinLod(float min);
	void SetMaxLod(float max);
	void SetWrapping(TextureWrappingAxis coord, TextureWrappingParam param);
	void SetBorderColor(float r, float g, float b, float a);
	void SetCompareMode(TextureCompareMode mode);
	void SetCompareFunc(TextureCompareFunc func);
	void SetAnisotropy(float anisotropy);

	void Bind(uint32_t texture_unit) { glBindSampler(texture_unit, m_so_id); }

private:
	void Release();

	GLuint m_so_id;
	float m_max_anisotropy;
};

class Texture
{
public:
	static constexpr size_t DefaultMipmaps = 0;

	virtual ~Texture() { Release(); };

	Texture             (const Texture&) = delete;
	Texture& operator = (const Texture&) = delete;

	Texture(Texture&& other) noexcept :
		m_metadata(other.m_metadata),
		m_type(other.m_type),
		m_obj_name(other.m_obj_name)
	{
		other.m_obj_name = 0;
	}

	Texture& operator = (Texture&& other) noexcept
	{
		if (this != &other)
		{
			Release();

			std::swap(m_metadata, other.m_metadata);
			std::swap(m_type,     other.m_type);
			std::swap(m_obj_name, other.m_obj_name);
		}

		return *this;
	}

	inline GLuint texture_id() const { return m_obj_name; }
	inline TextureType texture_type() const { return m_type; }

	virtual inline void Bind(uint32_t unit=0) const { glBindTextureUnit(unit, m_obj_name); }

	virtual void SetFiltering(TextureFiltering type, TextureFilteringParam param);
	virtual void SetMinLod(float min);
	virtual void SetMaxLod(float max);
	virtual void SetWrapping(TextureWrappingAxis axis, TextureWrappingParam param);
	virtual void SetBorderColor(float r, float g, float b, float a);
	virtual void SetCompareMode(TextureCompareMode mode);
	virtual void SetCompareFunc(TextureCompareFunc func);
	virtual void SetAnisotropy(float anisotropy);

	virtual ImageData GetMetadata() const { return m_metadata; };

	static uint8_t calculateMipMapLevels(size_t width, size_t height=0, size_t depth=0, size_t min_size=0, size_t max_levels=0);

	inline operator bool () const { return m_obj_name; }

protected:
	bool Create(size_t width, size_t height, size_t depth, GLenum internalFormat, size_t num_mipmaps);

	Texture() : m_type(TextureType::NONE), m_obj_name(0) {}

	void Release()
	{
		glDeleteTextures(1, &m_obj_name);
		m_obj_name = 0;
	}

	ImageData   m_metadata;
	TextureType m_type;
	GLuint      m_obj_name;
};

class Texture1D : public Texture
{
public:
	Texture1D() = default;

	bool Create(size_t width, GLenum internalFormat, size_t num_mipmaps=DefaultMipmaps);
};

class Texture2D : public Texture
{
public:
	Texture2D() = default;

	bool Create(size_t width, size_t height, GLenum internalFormat, size_t num_mipmaps=DefaultMipmaps);

	// TODO: convert to factory function
	//   also, these should access a shared storage,
	//   if the texture is already loaded, return the existing (a shread_ptr)
	bool Load(const std::filesystem::path & filepath, bool is_srgb = false, uint32_t num_mipmaps=DefaultMipmaps);
	bool Load(unsigned char* memory_data, uint32_t data_size, bool is_srgb = false, uint32_t num_mipmaps=DefaultMipmaps);
	bool LoadHdr(const std::filesystem::path& filepath, uint32_t num_mipmaps=DefaultMipmaps);
	bool LoadDds(const std::filesystem::path& filepath);
};

class TextureCubeMap : public Texture
{
public:
	TextureCubeMap() = default;

	// TODO: convert to factory function
	bool Load(const std::filesystem::path * filepaths, bool is_srgb = false, uint32_t num_mipmaps=DefaultMipmaps);
};

class Texture3D : public Texture
{
public:
	Texture3D() = default;

	bool Create(size_t width, size_t height, size_t depth, GLenum internalFormat, size_t num_mipmaps=DefaultMipmaps);
};


} // RGL
