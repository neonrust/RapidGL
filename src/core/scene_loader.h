#pragma once

#include "animation/animation_system.h"
#include <string_view>
#include <string>

#include <glm/fwd.hpp>
#include <glm/vec3.hpp>

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
	bool read_config(tag_file &fp, Scene &scene);
	bool read_mesh(tag_file &fp, Scene &scene);
	bool read_light(tag_file &fp, Scene &scene);
	bool read_entity(tag_file &fp, Scene &scene);
	bool read_control(tag_file &fp, Scene &scene);
	bool read_trigger(tag_file &fp, Scene &scene);
	bool read_walkable(tag_file &fp, Scene &scene);
	bool read_anim_tag(tag_file &fp, AnimationSystem::AnimationSetup &anim);
	void unexpected_tag(const tag_file &fp, std::string_view context);

	glm::vec3 pop_grid_pos(std::string_view &value) const;

private:
	std::string _roomName;
	uint32_t _item_start_line { 0 };
	AssetManager &_assets;
	AnimationSystem &_anims;
	std::string_view _filename;

	glm::vec3 _gridSize { 2, 2, 2 };
};

} // RGL