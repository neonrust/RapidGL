#pragma once
#include "util.h"

#include <glad/glad.h>
#include <string_view>
#include <filesystem>

namespace RGL
{

enum class TextureType
{
	Invalid        = 0,
	Texture1D      = GL_TEXTURE_1D,
	Texture2D      = GL_TEXTURE_2D,
	Texture2DArray = GL_TEXTURE_2D_ARRAY,
	Texture3D      = GL_TEXTURE_3D,
	TextureCube    = GL_TEXTURE_CUBE_MAP
};

enum class TextureFiltering
{
	Magnify  = GL_TEXTURE_MAG_FILTER,
	Minify  = GL_TEXTURE_MIN_FILTER
};

enum class TextureFilteringParam
{
	Nearest              = GL_NEAREST,
	Linear               = GL_LINEAR,
	NearestMipNearest    = GL_NEAREST_MIPMAP_NEAREST,
	LinearMipNearest     = GL_LINEAR_MIPMAP_NEAREST,
	NearestMipLinear     = GL_NEAREST_MIPMAP_LINEAR,
	LinearMipLinear      = GL_LINEAR_MIPMAP_LINEAR
};

enum class TextureWrappingAxis
{
	U = GL_TEXTURE_WRAP_S,
	V = GL_TEXTURE_WRAP_T,
	W = GL_TEXTURE_WRAP_R,
};

enum class TextureWrappingParam
{
	Repeat               = GL_REPEAT,
	MirroredRepeat       = GL_MIRRORED_REPEAT,
	ClampToEdge          = GL_CLAMP_TO_EDGE,
	ClampToBorder        = GL_CLAMP_TO_BORDER,
	MirrorClampToEdge    = GL_MIRROR_CLAMP_TO_EDGE
};

enum class TextureCompareMode
{
	None = GL_NONE,
	Ref  = GL_COMPARE_REF_TO_TEXTURE }
;

enum class TextureCompareFunc
{
	Never        = GL_NEVER,
	Always       = GL_ALWAYS,
	LessEqual    = GL_LEQUAL,
	GreaterEqual = GL_GEQUAL,
	Less         = GL_LESS,
	Greater      = GL_GREATER,
	Equal        = GL_EQUAL,
	NotEqual     = GL_NOTEQUAL
};

enum class CubeFace : uint32_t
{
	PlusX  = 0,
	MinutX = 1,
	PlusY  = 2,
	MinutY = 3,
	PlusZ  = 4,
	MinutZ = 5,
};

struct TextureDescriptor
{
	ImageMeta meta;
	GLuint texture_id { 0 };
	TextureType type { TextureType::Invalid };

	inline operator bool () const
	{
		return texture_id > 0 \
			and type != TextureType::Invalid \
			and meta.width > 0;
	}
};

class TextureSampler final
{
public:
	TextureSampler();
	~TextureSampler() { Release(); };

	TextureSampler(const TextureSampler&) = delete;
	TextureSampler& operator = (const TextureSampler&) = delete;

	TextureSampler(TextureSampler&& other) noexcept : _sampler_id(other._sampler_id), m_max_anisotropy(other.m_max_anisotropy)
	{
		other._sampler_id = 0;
	}

	TextureSampler& operator = (TextureSampler&& other) noexcept
	{
		if (this != &other)
		{
			Release();
			std::swap(_sampler_id, other._sampler_id);
			std::swap(m_max_anisotropy, other.m_max_anisotropy);
		}

		return *this;
	}

	void Create();
	inline GLuint sampler_id() const { return _sampler_id; }
	void SetFiltering(TextureFiltering type, TextureFilteringParam filtering);
	void SetMinLod(float lod);
	void SetMaxLod(float lod);
	void SetWrapping(TextureWrappingAxis axis, TextureWrappingParam wrapping);
	void SetBorderColor(float r, float g, float b, float a);
	void SetCompareMode(TextureCompareMode mode);
	void SetCompareFunc(TextureCompareFunc func);
	void SetAnisotropy(float anisotropy);

	void Bind(uint32_t texture_unit=0);

	inline operator bool () { return _sampler_id > 0; }

private:
	void Release();

	GLuint _sampler_id;
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
		_texture_id(other._texture_id)
	{
		other._texture_id = 0;
	}

	Texture& operator = (Texture&& other) noexcept
	{
		if (this != &other)
		{
			Release();

			std::swap(m_metadata, other.m_metadata);
			std::swap(m_type,     other.m_type);
			std::swap(_texture_id, other._texture_id);
		}

		return *this;
	}

	inline GLuint texture_id() const { return _texture_id; }
	inline TextureType texture_type() const { return m_type; }

	virtual void Bind(uint32_t unit=0) const;

	virtual void SetFiltering(TextureFiltering type, TextureFilteringParam filtering);
	virtual void SetMinLod(float min);
	virtual void SetMaxLod(float max);
	virtual void SetWrapping(TextureWrappingAxis axis, TextureWrappingParam wrapping);
	virtual void SetBorderColor(float r, float g, float b, float a);
	virtual void SetCompareMode(TextureCompareMode mode);
	virtual void SetCompareFunc(TextureCompareFunc func);
	virtual void SetAnisotropy(float anisotropy);

	void SetBaseLevel(uint32_t level) const;
	void SetMaxLevel(uint32_t level) const;

	void GenerateMipMaps();

	virtual const ImageMeta &GetMetadata() const { return m_metadata; };

	static uint8_t calculateMipMapLevels(size_t width, size_t height=0, size_t depth=0, size_t min_size=0, size_t max_levels=64);

	inline operator bool () const { return _texture_id; }

	void Release();

	void set(const TextureDescriptor &descr);

protected:
	bool Create(size_t width, size_t height, size_t depth, GLenum internalFormat, size_t num_mipmaps);

	Texture() : m_type(TextureType::Invalid), _texture_id(0) {}

	ImageMeta   m_metadata;
	TextureType m_type;
	GLuint      _texture_id;
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

class Texture2DArray : public Texture
{
public:
	Texture2DArray() = default;

	void BindLayer(uint32_t layer, uint32_t unit=0);

	bool Create(size_t width, size_t height, size_t layers, GLenum internalFormat, size_t num_mipmaps=DefaultMipmaps);
	void Release();

	bool Load(const std::filesystem::path &filepath, bool is_srgb=true);
	bool LoadLayers(const std::vector<std::filesystem::path> &paths, bool is_srgb=true);
	bool LoadDds(const std::filesystem::path &filepath);

private:
	void createLayerViews(GLenum internalFormat);

	std::vector<GLuint> _layerViews;
};

class TextureCube : public Texture
{
public:
	TextureCube() = default;

	bool Create(size_t width, size_t height, GLenum internalFormat, size_t num_mipmaps=DefaultMipmaps);
	void Release();

	void BindFace(CubeFace face, uint32_t unit=0);

	// TODO: convert to factory function
	bool Load(const std::array<std::filesystem::path, 6> &filepaths, bool is_srgb = false, uint32_t num_mipmaps=DefaultMipmaps);

	inline uint32_t texture_face_id(CubeFace face) const { return _faceViews[uint32_t(face)]; }

private:
	void createFaceViews(GLenum internalFormat);

private:
	std::array<GLuint, 6> _faceViews;
};

class Texture3D : public Texture
{
public:
	Texture3D() = default;

	bool Load(const std::filesystem::path &filepath);
	bool Create(size_t width, size_t height, size_t depth, GLenum internalFormat, size_t num_mipmaps=DefaultMipmaps);
};


} // RGL
