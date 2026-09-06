#include "asset_manager.h"

#include "filesystem.h"
#include "log.h"
#include "texture.h"

#include <string_view>
using namespace std::literals;

namespace RGL
{

AssetManager::AssetManager()
{
	// TODO: prepare maps and stuff
	_textures.reserve(256);
	_static_meshes.reserve(64);
}

AssetManager &AssetManager::the()
{
	static AssetManager *instance = new AssetManager();
	return *instance;
}

void AssetManager::shut_down()
{
	the()._static_mesh_default_materials.clear();
	the()._textures.clear();
	the()._static_meshes.clear();
}

std::shared_ptr<const Texture2D> AssetManager::texture(std::string_view name, bool is_srgb)
{
	static constexpr auto textures_prefix = "/textures/"sv;

	// cut anything up to (and including) "/textures/"
	if(auto ptex = name.find(textures_prefix); ptex != std::string_view::npos)
		name = name.substr(ptex + textures_prefix.size());

	if(auto found = _textures.find(name); found != _textures.end())
		return found->second.lock();

	// TODO: pool?
	auto *tex = new Texture2D();

	const auto texture_path = FileSystem::getResourcesPath() / "textures" / name;
	Log::debug("Loading texture: {}", texture_path.string());
	// TODO: immediately return a default texture while loading in the background,
	//   then replace it
	tex->Load(texture_path, is_srgb); // TODO: use 'textures_prefix'
	if(not *tex)
	{
		// TODO: use the default texture
		assert(*tex);
	}

	tex->SetWrapping(TextureWrappingAxis::U, TextureWrappingParam::Repeat);
	tex->SetWrapping(TextureWrappingAxis::V, TextureWrappingParam::Repeat);

	auto tex_ref = std::shared_ptr<Texture2D>(tex, [this, name=std::string(name)](auto *tex) {
		delete_texture2d(name, tex);
	});

	_textures[std::string(name)] = tex_ref;

	return tex_ref;
}

void AssetManager::delete_texture2d(std::string_view name, Texture2D *tex)
{
	auto found = _textures.find(name);
	if(found != _textures.end())
	{
		delete tex;
		_textures.erase(found);
		Log::info("Deleted texture: {}", name);
	}
}



} // RGL