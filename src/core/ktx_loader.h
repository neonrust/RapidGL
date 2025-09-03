#pragma once

#include "texture.h"
#include "util.h"

#include <filesystem>


namespace RGL
{

// TODO: return a 'Texture' (i.e. undimensioned)
GLuint ktx_load(const std::filesystem::path &filepath, ImageMeta &meta, size_t  dimensions=2, bool array=false);

template<typename TX>  requires (std::same_as<TX, Texture1D> or std::same_as<TX, Texture2D> or std::same_as<TX, Texture3D> or std::same_as<TX, Texture2DArray>)
TextureDescriptor ktx_load(const std::filesystem::path &filepath)
{
	auto ttype = TextureType::Texture2D;
	size_t dimensions = 2;
	bool array = false;

	if constexpr (std::same_as<TX, Texture1D>)
	{
		dimensions = 1;
		ttype = TextureType::Texture1D;
	}
	else if constexpr (std::same_as<TX, Texture3D>)
	{
		dimensions = 3;
		ttype = TextureType::Texture3D;
	}
	else if constexpr (std::same_as<TX, Texture2DArray>)
	{
		ttype = TextureType::Texture2DArray;
		array = true;
	}

	TextureDescriptor descr;

	GLuint texture_id = ktx_load(filepath, descr.meta, dimensions, array);

	if(texture_id == 0)  // load failed
	{
		// TODO: return a fallback texture?
		return descr;
	}

	descr.type = ttype;
	descr.texture_id = texture_id;

	return descr;
}

} // RGL
