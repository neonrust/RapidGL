#pragma once

#include <string_view>
#include <string>

#include <glm/fwd.hpp>

namespace RGL
{

class AssetManager;
class AnimationSystem;
class Scene;
class tag_file;


class SceneLoader
{
public:
	SceneLoader(AssetManager &assets, AnimationSystem &anims);

	uint32_t load(std::string_view name, Scene &scene);

private:
	bool read_mesh(tag_file &fp, Scene &scene);
	bool read_light(tag_file &fp, Scene &scene);
	bool read_entity(tag_file &fp, Scene &scene);
	bool read_control(tag_file &fp, Scene &scene);
	bool read_trigger(tag_file &fp, Scene &scene);
	bool read_walkable(tag_file &fp, Scene &scene);

private:
	std::string _roomName;
	AssetManager &_assets;
	AnimationSystem &_anims;
	std::string_view _filename;
};

} // RGL