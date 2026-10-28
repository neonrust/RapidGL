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
} // RGL

class SceneLoader
{
public:
	SceneLoader(RGL::AssetManager &assets, RGL::AnimationSystem &anims);

	uint32_t load(std::string_view name, RGL::Scene &scene);

private:
	bool read_config(RGL::tag_file &fp, RGL::Scene &scene);
	bool read_mesh(RGL::tag_file &fp, RGL::Scene &scene);
	bool read_light(RGL::tag_file &fp, RGL::Scene &scene);
	bool read_entity(RGL::tag_file &fp, RGL::Scene &scene);
	bool read_control(RGL::tag_file &fp, RGL::Scene &scene);
	bool read_trigger(RGL::tag_file &fp, RGL::Scene &scene);
	bool read_walkable(RGL::tag_file &fp, RGL::Scene &scene);
	bool read_anim_tag(RGL::tag_file &fp, RGL::AnimationSetup &anim);
	void unexpected_tag(const RGL::tag_file &fp, std::string_view context);

	glm::vec3 pop_grid_pos(std::string_view &value) const;

private:
	std::string _roomName;
	uint32_t _item_start_line { 0 };
	RGL::AssetManager &_assets;
	RGL::AnimationSystem &_anims;
	std::string_view _filename;

	glm::vec3 _gridSize { 2, 2, 2 };
};
