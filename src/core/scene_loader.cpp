#include "scene_loader.h"

#include "asset/asset_manager.h"
#include "component/transform.h"
#include "formatters_glm.h"  // IWYU pragma: keep
#include "animation/animation_system.h"
#include "bounds.h"
// #include "component/model.h"
#include "container_types.h"
#include "game_time.h"
#include "lights.h"
#include "log.h"
#include "scene.h"
#include "filesystem.h"
#include "tag_file.h"
#include "light_type.h"
#include "light_manager.h"

#include <variant>
#include <chrono>
#include <array>

#include <component/light_general.h>

using namespace std::chrono;
using namespace std::literals;

namespace RGL
{

using gaxis_t = int32_t;
struct GridPos
{
	gaxis_t x;
	gaxis_t y;
	gaxis_t z { 0 };
};

namespace Property
{
static constexpr auto Name = "name"sv;
static constexpr auto Type = "type"sv;
static constexpr auto Position = "position"sv;
static constexpr auto Grid = "grid"sv;
static constexpr auto Orientation = "ori"sv;
static constexpr auto Scale = "scale"sv;
static constexpr auto Bounds = "bounds"sv;
static constexpr auto Animation = "anim"sv;
static constexpr auto Color = "color"sv;
static constexpr auto Effect1 = "fx1"sv;   // lights: wobble (o.e. live flame) - amplitude (float)
static constexpr auto Effect2 = "fx2"sv;   // lights: intensity (float)
static constexpr auto Effect3 = "fx3"sv;   // lights: fog (float)
static constexpr auto Effect4 = "fx4"sv;   // TBD
static constexpr auto Length = "len"sv;
static constexpr auto Power = "pow"sv;
static constexpr auto Flicker = "flicker"sv;
static constexpr auto Radius = "radius"sv;

namespace Material
{
// suffixes (max 4 characters)
static constexpr auto Albedo = "alb"sv;
static constexpr auto Normals = "norm"sv;
static constexpr auto Metallic = "met"sv;
static constexpr auto Roughness = "ruff"sv;
static constexpr auto Ambient = "ao"sv;
static constexpr auto Emissive = "em"sv;
} // Material

} // Property

enum class Control
{
	None,
	Button,
	Switch,
	Axis
};

using prop_value_t = std::variant<float, int64_t, std::string, glm::vec4, GridPos>;
using prop_map_t = string_map<prop_value_t>;

static glm::vec3 to_vec3(const prop_value_t &p)
{
	assert(std::holds_alternative<glm::vec4>(p));
	auto pv = std::get<glm::vec4>(p);
	return { pv.x, pv.y, pv.z };
}

static GridPos to_grid(const prop_value_t &p)
{
	assert(std::holds_alternative<GridPos>(p));
	return std::get<GridPos>(p);
}

static std::string_view pop_word(std::string_view &buffer)
{
	// skip initial space, if any
	auto nonspace = buffer.find_first_not_of(" ");
	if(nonspace != std::string_view::npos)
		buffer.remove_prefix(nonspace);

	const auto word_end = buffer.find(' '); // might be 'npos', that's ok
	auto word = buffer.substr(0, word_end);
	buffer.remove_prefix(word_end == std::string_view::npos? buffer.size(): word_end);

	// skip space after word (as a favor)
	nonspace = buffer.find_first_not_of(" ");
	if(nonspace != std::string_view::npos)
		buffer.remove_prefix(nonspace);

	return word;
}

template<typename T=float>
static std::optional<T> pop_number_optional(auto &buffer)
{
	auto word = pop_word(buffer);
	if(word.empty())
		return std::nullopt;

	T number;
	auto result = std::from_chars(word.data(), word.data() + word.size(), number);
	if(result.ec != std::errc(0))
	{
		if constexpr (std::is_integral_v<T>)
			Log::error("Failed to parse '{}' as integer value, error: {}", word, uint32_t(result.ec));
		else if constexpr (std::is_floating_point_v<T>)
			Log::error("Failed to parse '{}' as float value, error: {}", word, uint32_t(result.ec));
		else
			Log::error("Failed to parse '{}' (not int, nor float), error: {}", word, uint32_t(result.ec));
		assert(result.ec == std::errc(0));
		return std::nullopt;
	}

	// skip space after word (as a favor)
	auto nonspace = buffer.find_first_not_of(" ");
	if(nonspace != std::string_view::npos)
		buffer.remove_prefix(nonspace);

	return number;
};

template<typename T=float>
static T pop_number(auto &buffer)
{
	return pop_number_optional<T>(buffer).value();
}

SceneLoader::SceneLoader(AssetManager &assets, AnimationSystem &anims) :
	_assets(assets),
	_anims(anims)
{
}

static bool is_entry_start(std::string_view tag)
{
	assert(not tag.empty());
	return tag[0] >= 'A' and tag[0] <= 'Z';
}

uint32_t SceneLoader::load(std::string_view name, Scene &scene)
{
	_filename = name;

	const auto T0 = steady_clock::now();

	auto file_path = FileSystem::getResourcesPath() / "scenes" / name;
	if(file_path.extension().empty())
		file_path.replace_extension(".scene");

	tag_file fp(file_path);
	if(not fp)
		return 0;

	enum class ItemType
	{
		None = 0,
		Config,
		Mesh,
		// Animation,
		Light,
		Entity,      // player, npc, mob or item
		Control,    // button, lever, knob, wheel/valve, slider, etc
		Trigger,
		Walkable,
	};
	static const string_map<ItemType> item_name_type {
		{ "CONFIG",   ItemType::Config },
		{ "MESH",     ItemType::Mesh },
		// { "ANIM",     ItemType::Animation },
		{ "LIGHT",    ItemType::Light },
		{ "ENTITY",   ItemType::Entity },    // player spawn, monster, etc, w/ "type" porperty dictates which
		{ "CONTROL",  ItemType::Control },   // interactible "thing"
		{ "TRIGGER",  ItemType::Trigger },
		{ "WALKABLE", ItemType::Walkable },
	};

	auto entry_num { 0u };
	auto num_brushes { 0u };
	auto num_lights { 0u };
	auto num_entities { 0u };
	auto num_walkables { 0u };

	ItemType item_type { ItemType::None };
	bool first { true };

	fp.next();

	while(fp)
	{
		const auto &[tag, value] = fp.current();

		if(tag.empty())
			return 0;

		if(tag == "room"sv)
		{
			assert(first);
			if(not first)
			{
				Log::error("[{}:{}] unexpected '{}', must be first tag in the file", _filename, fp.line_num(), tag);
				return 0;
			}
			_roomName = value;
			first = false;
			continue;
		}

		// capital latter means new item
		if(is_entry_start(tag))
		{
			_item_start_line = fp.line_num();

			// TODO: read all fields directly in each if case??
			//   more clear, but less robust

			auto type_found = item_name_type.find(tag);
			if(type_found == item_name_type.end())
			{
				Log::error("unknown item type: '{}'", tag);
				fp.next();
				assert(type_found != item_name_type.end());
				continue;
			}

			item_type = type_found->second;

			// if(current.item_type == ItemType::Mesh)
			// 	current.model = _assets.staticMesh(value);
			// else if(current.item_type == ItemType::Entity)
			// 	current.properties[Property::Type] = std::string(value);
		}
		else
		{
			Log::warning("[{}:{}] unknown file state: expected entry start, got: {} {}", _filename, fp.line_num(), tag, value);
			fp.next();
			continue;
		}

		bool eat_next = false;

		switch(item_type)
		{
		case ItemType::None:
			Log::warning("ignoring property of UNKNOWN item type: '{}'", tag);
			continue;
		case ItemType::Config:   eat_next = read_config(fp, scene);   break;
		case ItemType::Mesh:     eat_next = read_mesh(fp, scene);     break;
		case ItemType::Light:    eat_next = read_light(fp, scene);    break;
		case ItemType::Entity:   eat_next = read_entity(fp, scene);   break;
		case ItemType::Control:  eat_next = read_control(fp, scene);  break;
		case ItemType::Trigger:  eat_next = read_trigger(fp, scene);  break;
		case ItemType::Walkable: eat_next = read_walkable(fp, scene); break;
		}

		item_type = ItemType::None;

		if(eat_next and fp)
			fp.next();
	}

	const auto T1 = steady_clock::now();
	const auto tD = duration_cast<milliseconds>(T1 - T0);

	Log::info("LEVEL [{}]: constructed entires [{} ms]", _filename, entry_num, tD.count());
	Log::info("  brushes:  {}", num_brushes);
	Log::info("  lights:   {}", num_lights);
	Log::info("  entities: {}", num_entities);
	Log::info("  walkable: {} grids", num_walkables);

	// TODO: sort so entities in same-grid are kept together
	// FIXME: assert fails!?!? "Set does not contain entity" in entt/entity/sparse_set.hpp
	// level->_entities.sort<GridPos>([&](const GridPos &A, const GridPos &B) {
	// 	// don't care about Z axis... maybe later
	// 	return A.x < B.x or A.y < B.y;
	// });

	return 0;
}

static bool pop_bool(std::string_view &value)
{
	auto word = pop_word(value);

	return word == "1"sv or word == "y"sv or word == "t"sv or word == "yes"sv or word == "true"sv or word == "on"sv;
}

glm::vec3 SceneLoader::pop_grid_pos(std::string_view &value) const
{
	glm::vec3 grid;
	grid.x = float(pop_number<int32_t>(value));
	grid.y = float(pop_number<int32_t>(value));
	grid.z = float(pop_number<int32_t>(value));

	return grid * _gridSize;
};
static glm::vec2 pop_vec2(std::string_view &value)
{
	glm::vec2 v;
	v.x = pop_number(value);
	v.y = pop_number(value);
	return v;
};
static glm::vec3 pop_vec3(std::string_view &value)
{
	glm::vec3 v(pop_vec2(value), 0);
	v.z = pop_number(value);
	return v;
};
static glm::vec3 pop_orientation(std::string_view &value)
{
	glm::vec3 ori;
	ori.x = pop_number(value);
	auto y = pop_number_optional(value);
	auto z = pop_number_optional(value);

	// if only a single number was specified it's rotation around the Y-axis
	if(not y.has_value() and not z.has_value())
		return { 0, ori.x, 0 };
	assert(y.has_value() and z.has_value());
	return { ori.x, y.value(), z.value() };
};

static std::optional<anim::curve_ref<>> select_curve(std::string_view descriptor, AnimationSystem::AnimationSetup &anim)
{
	if(descriptor.size() != 2 or descriptor.substr(0, 1).find_first_not_of("pos") != std::string::npos or descriptor.substr(1).find_first_not_of("xyz") != std::string::npos)
	{
		Log::debug("Unknown animation curve: {} (expected: [pos][xyz])", descriptor);
		return std::nullopt;
	}

	auto select_property = [&anim](char p) -> std::array<anim::curve_ref<>, 3> & {
		switch(p)
		{
		case 'p': return anim.position; break;
		case 'o': return anim.orientation; break;
		case 's': return anim.scale; break;
		}
		Log::error("Invalid axis property: {}", p);
		assert(false);
		static std::array<anim::curve_ref<>, 3> sentinel;
		return sentinel;
	};
	auto &curveset = select_property(descriptor[0]);

	auto ensure_created = [&](size_t idx) {
		if(not curveset[idx])
			curveset[idx].reset(new anim::animation_curve<>());
	};

	switch(descriptor[1])
	{
	case 'x': ensure_created(0); return curveset[0];
	case 'y': ensure_created(1); return curveset[1];
	case 'z': ensure_created(2); return curveset[2];
	default:
		Log::debug("Unknown animation curve: {} (expected: [pos][xyz])", descriptor);
		return std::nullopt;
	}
};

static bool add_curve_point(anim::curve_ref<> curve, std::string_view args)
{
	const auto interp_type = pop_word(args);
	// only one interpolation type supported at the moment (and probably every woll be)
	if(interp_type != "bz"sv)
	{
		Log::warning("Unknown curve point interpolation type: '{}'", interp_type);
		return false;
	}

	const auto in_time = seconds_f(pop_number(args));
	const auto in_value = pop_number(args);
	const auto cp_time = seconds_f(pop_number(args));
	const auto cp_value = pop_number(args);
	const auto out_time = seconds_f(pop_number(args));
	const auto out_value = pop_number(args);

	curve->add({ cp_time, cp_value }, { in_time, in_value }, { out_time, out_value });

	return true;
}

struct animation_params
{
	std::array<anim::animation_curve<>, 3> position_curves;
	std::array<anim::animation_curve<>, 3> orientation_curves;
	std::array<anim::animation_curve<>, 3> scale_curves;
	seconds_f end_time;
};

bool SceneLoader::read_anim_tag(tag_file &fp, AnimationSystem::AnimationSetup &anim)
{
	const auto &tag = fp.tag();
	auto value = fp.value();

	if(tag == "animlen"sv)
	{
		anim.end_time = seconds_f(pop_number(value));
		return true;
	}
	else if(tag == "animend"sv)
	{
		anim.end_state = value == "reset"? anim::EndState::Reset: anim::EndState::Clamp;
		return true;
	}
	else if(tag == "animloop"sv)
	{
		anim.total_loops = pop_number<uint32_t>(value);
		if(anim.total_loops == 0) // TODO: or infinite? useful?
			anim.total_loops = 1;
		return true;
	}
	else if(tag.starts_with("kf-"sv) and tag.size() >= 4) // keyframe of a channel, e.g. kf-px
	{
		auto opt_curve = select_curve(tag.substr(3), anim);
		if(opt_curve.has_value())
			add_curve_point(opt_curve.value(), fp.value());
		return true;
	}
	else
		unexpected_tag(fp, "anim");

	return false;
}

static bool pop_bounds(std::string_view &value, bounds::AABB &bounds)
{
	// NOTE: the model itself contains a bounds, is this even needed?
	auto min_x = pop_number(value);
	auto min_y = pop_number(value);
	auto min_z = pop_number(value);
	auto to = pop_word(value);
	if(to != "to"sv)
		Log::error("bounds: expected word 'to': '{}'", to);

	auto max_x = pop_number(value);
	auto max_y = pop_number(value);
	auto max_z = pop_number(value);

	bounds.min() = { min_x, min_y, min_z };
	bounds.max() = { max_x, max_y, max_z };

	return bounds.volume() > 0;
}

static bool read_material_override(tag_file &fp, dense_map<uint32_t, MaterialRef> &materials)
{
	const auto prop = fp.tag().substr(4); // "mtl_"

	// e.g. mtl_alb >> 0 some_texture.png  (0 is material index)
	//   OR mtl_alb >> 0 0.25

	auto value = fp.value();
	const auto index = pop_number<uint32_t>(value);
	assert(index >= 0);

	auto texture_name = value;
	auto loaded = AssetManager::the().texture(texture_name);
	if(not loaded)
		return false;

	auto texture = loaded.value();

	auto &material = materials[index];
	if(not material)
		material.reset(new Material());

	// see material.h
	if(prop == Property::Material::Albedo)
		material->set(Material::TextureType::ALBEDO, texture_name, texture);
	else if(prop == Property::Material::Normals)
		material->set(Material::TextureType::NORMAL, texture_name, texture);
	else if(prop == Property::Material::Metallic)
		material->set(Material::TextureType::METALLIC, texture_name, texture);
	else if(prop == Property::Material::Roughness)
		material->set(Material::TextureType::ROUGHNESS, texture_name, texture);
	else if(prop == Property::Material::Ambient)
		material->set(Material::TextureType::AO, texture_name, texture);
	else if(prop == Property::Material::Emissive)
		material->set(Material::TextureType::EMISSIVE, texture_name, texture);
	else
	{
		Log::warning("Unknown mateiral property: {}", prop);
		return false;
	}
	return true;
}

bool SceneLoader::read_config(tag_file &fp, Scene &)
{
	// Log::debug("[{}:{}] read_config", fp.file_path().filename().native(), fp.line_num());

	while(fp)
	{
		auto [tag, value] = fp.next();
		if(is_entry_start(tag))
			return false;
		else if(tag == Property::Grid)
		{
			// grid size (not position)
			const auto width = float(pop_number<uint32_t>(value));
			const auto depth = float(pop_number<uint32_t>(value));
			const auto height = width;
			assert(width > 0 and depth > 0 and height > 0);
			_gridSize = glm::vec3(width, height, depth);
		}
		else
			unexpected_tag(fp, "config");
	}

	return false;
}

bool SceneLoader::read_mesh(tag_file &fp, Scene &scene)
{
	// Log::debug("[{}:{}] read_mesh: {}", fp.file_path().filename().native(), fp.line_num(), fp.value());

	// name (also.model file)

	prop_map_t props;

	ModelCRef model;
	MaterialCSet materials;
	bounds::AABB bounds;
	AnimationSystem::AnimationSetup animation;
	component::Transform transform;
	std::string object_name; // only required if it needs to be referred to by name
	std::string anim_name;

	auto mesh_name = std::string(fp.value());
	// if no extension, default to ".gltf"  (this decision should probably be in AssetManager)
	if(std::string_view(mesh_name).substr(mesh_name.size() - 5).find('.') == std::string::npos)
		mesh_name += ".gltf"sv;

	auto loaded = _assets.staticMesh(mesh_name);
	if(loaded)
	{
		const auto &[model_, materials_] = loaded.value();
		model = std::move(model_);
		materials = std::move(materials_);
	}
	else
	{
		// skip until next start tag or EOF
		while(fp)
			if(is_entry_start(fp.next().first))
				break;
		return false;
	}

	dense_map<uint32_t, MaterialRef> material_overrides;

	auto add_mesh = [&]() -> entt::entity {
		// override materials (and constify)
		if(model)
		{
			for(const auto &[index, material]: material_overrides)
			{
				assert(index < materials.size());
				materials[index] = material;
			}
			if(_roomName.empty())
				return scene.add(model, materials, transform);
			else
				return scene.add(_roomName, model, materials, transform);
		}
		return NO_ENTITY_ID;
	};
	auto add_anim = [&](auto mesh_id) {
		if(not anim_name.empty())
		{
			assert(animation);
			_anims.add(anim_name, mesh_id, animation);
			anim_name.clear();
			animation.clear();
		}
	};

	entt::entity mesh_id { NO_ENTITY_ID };

	while(fp)
	{
		auto [tag, value] = fp.next();
		if(not fp)
			break;

		if(is_entry_start(tag))
		{
			mesh_id = add_mesh();
			add_anim(mesh_id);
			return false;
		}
		else if(tag == Property::Name)
			object_name = fp.value();
		else if(tag == Property::Position)
			transform.set_position(pop_vec3(value));
		else if(tag == Property::Grid)
			transform.set_position(pop_grid_pos(value));
		else if(tag == Property::Orientation)
			transform.set_orientation_xyz(pop_orientation(value));
		else if(tag == Property::Scale)
			transform.set_scale(pop_vec3(value));
		else if(tag == Property::Bounds and pop_bounds(value, bounds))
			;
		else if(tag.starts_with("mtl_"sv) and tag.size() > 5 and read_material_override(fp, material_overrides))
			;
		else if(tag == Property::Animation)
		{
			add_anim(mesh_id);

			anim_name = fp.value();
		}
		else if(not anim_name.empty() and read_anim_tag(fp, animation))
			;
		else
			unexpected_tag(fp, "mesh");
	}

	// if we got here, we ran into EOF

	mesh_id = add_mesh();
	add_anim(mesh_id);

	return true;
}

bool SceneLoader::read_light(tag_file &fp, Scene &scene)
{
	// Log::debug("[{}:{}] read_light: {}", fp.file_path().filename().native(), fp.line_num(), fp.value());

	// intensity
	// fog
	// shadow caster
	// contact shadows
	// shadow range compression

	auto name = std::string(fp.value());
	const auto start_line = fp.line_num();

	// first tag must always be "type" (many parameters are type-specific)
	fp.next();
	assert(fp.tag() == "type"sv);

	LightType type = LightType::Point;

	bool enabled { true };
	std::optional<float> intensity { 10.f };
	std::optional<glm::vec3> position;
	std::optional<glm::vec3> orientation(0);
	glm::vec3 color{1};
	std::optional<std::array<float, 2>> angles;
	std::optional<float> radius;
	std::optional<float> thickness;
	std::optional<glm::vec2> size;
	bool shadows { true };
	bool surface { true };
	bool contact_shadows { false };
	float shadow_range_comp { 1.f };
	float fog { 1.f };
	float flicker { 0.f };
	std::optional<bool> double_sided;

	const auto type_name = fp.value();

	if(type_name == "point"sv)
		type = LightType::Point;
	else if(type_name == "spot"sv)
		type = LightType::Spot;
	else if(type_name== "rect"sv)
		type = LightType::Rect;
	else if(type_name == "tube"sv)
		type = LightType::Tube;
	else if(type_name == "sphere"sv)
		type = LightType::Sphere;
	else if(type_name == "disc"sv)
		type = LightType::Disc;
	else
	{
		Log::warning("[{}:{}]: unknown light type: {} (using point)", _filename, start_line, fp.value());
		type = LightType::Point;
		return false;
	}

	// true if we stopped at the start of the next entry (i.e. it must not be skipped by the caller)
	bool at_next_entry = false;

	while(fp)
	{
		auto [tag, value] = fp.next();
		if(not fp)
			break;  // EOF; still add the light

		if(is_entry_start(tag))
		{
			at_next_entry = true;
			break;
		}
		else if(tag == "enabled"sv) // bool
			enabled = pop_bool(value);
		else if(tag == "power"sv) // intensity, float
		{
			intensity = std::sqrt(pop_number(value)) * 2.f;
			intensity = std::max(0.f, intensity.value());
		}
		else if(tag == Property::Position)
			position = pop_vec3(value);
		else if(tag == Property::Orientation)
			orientation = pop_vec3(value);
		else if(tag == Property::Color)
		{
			color = pop_vec3(value)/255.f;
			// TODO: saturate/normalize
		}
		else if(tag == "flicker"sv)
			flicker = pop_number(value);
		else if(tag == "angle"sv)  // spots
		{
			auto outer = pop_number(value);
			auto inner_o = pop_number_optional(value);
			auto inner = outer;
			if(inner_o)
				inner = inner_o.value();

			angles = { outer, inner };
		}
		else if(tag == "shadows"sv) // bool
			shadows = pop_bool(value);
		else if(tag == "fog"sv) // float
			fog = pop_number(value);
		else if(tag == "surface"sv) // bool
			surface = pop_bool(value);
		else if(tag == "contacts"sv) // bool?
			contact_shadows = pop_bool(value);
		else if(tag == "rangecmp"sv) // float
			shadow_range_comp = pop_number(value);
		else if(tag == "radius"sv)
			radius = pop_number(value);
		else if(tag == "thick"sv)
			thickness = pop_number(value);
		else if(tag == "size"sv) // vec2
			size = pop_vec2(value);
		else if(tag == "dblsided"sv)
			double_sided = pop_bool(value);
		else
			unexpected_tag(fp, "light");
	}

	assert(type == LightType::Directional or position.has_value());
	assert(type != LightType::Spot or angles.has_value());
	// TODO: validate other required properties

	component::LightGeneral general;
	general.light_type = type;
	general.intensity = intensity.value();
	general.color = color;
	general.fog = fog;
	general.shadow_caster = shadows;
	general.shadow_compression = shadow_range_comp;
	general.contact_shadows = contact_shadows;
	general.has_surface = true;
	// TODO flicker support in the shading
	// general.flicker = flicker;

	component::Transform transform;
	transform.set_position(position.value());
	if(type != LightType::Directional and type != LightType::Sphere)
		transform.set_orientation_xyz(orientation.value());

	auto light_id = NO_LIGHT_ID;

	auto &lights = scene.lights();

	switch(type)
	{
	case LightType::Point:
	{
		PointLightParams p;
		p.color = general.color;
		p.intensity = general.intensity;
		p.fog = general.fog;
		p.shadow_caster = general.shadow_caster;
		p.contact_shadows = general.contact_shadows;
		p.position = transform.position();

		if(auto res = lights.add(p); res)
			light_id = res.value();
	}
	break;
	case LightType::Directional:
	{
		DirectionalLightParams d;
		d.color = general.color;
		d.intensity = general.intensity;
		d.fog = general.fog;
		d.shadow_caster = general.shadow_caster;
		d.contact_shadows = general.contact_shadows;
		d.direction = transform.direction();

		if(auto res = lights.add(d); res)
			light_id = res.value();
	}
	break;
	case LightType::Spot:
	{
		SpotLightParams s;
		s.position = position.value();
		s.color = general.color;
		s.intensity = general.intensity;
		s.fog = general.fog;
		s.shadow_caster = general.shadow_caster;
		s.contact_shadows = general.contact_shadows;
		s.direction = transform.direction();
		s.outer_angle = angles.value()[0];
		s.inner_angle = angles.value()[1];

		if(auto res = lights.add(s); res)
			light_id = res.value();
	}
	break;
	case LightType::Rect:
	{
		RectLightParams r;
		r.position = position.value();
		r.intensity = general.intensity;
		r.fog = general.fog;
		r.shadow_caster = general.shadow_caster;
		r.contact_shadows = general.contact_shadows;
		r.size = size.value();
		r.double_sided = double_sided.value();
		r.visible_surface = surface;
		r.orientation = orientation.value();

		if(auto res = lights.add(r); res)
			light_id = res.value();
	}
	break;
	case LightType::Tube:
	{
		TubeLightParams t;
		t.position = position.value();
		t.intensity = general.intensity;
		t.fog = general.fog;
		t.shadow_caster = general.shadow_caster;
		t.contact_shadows = general.contact_shadows;
		t.half_extent = transform.orientation() * glm::vec3{radius.value(), 0, 0};
		t.thickness = thickness.value();
		t.visible_surface = surface;

		if(auto res = lights.add(t); res)
			light_id = res.value();
	}
	break;
	case LightType::Sphere:
	{
		SphereLightParams s;
		s.position = position.value();
		s.intensity = general.intensity;
		s.fog = general.fog;
		s.shadow_caster = general.shadow_caster;
		s.contact_shadows = general.contact_shadows;
		s.radius = radius.value();
		s.visible_surface = surface; // TODO

		if(auto res = scene.lights().add(s); res)
			light_id = res.value();
	}
	break;
	case LightType::Disc:
	{
		DiscLightParams d;
		d.position = position.value();
		d.intensity = general.intensity;
		d.fog = general.fog;
		d.shadow_caster = general.shadow_caster;
		d.contact_shadows = general.contact_shadows;
		d.radius = radius.value();
		d.visible_surface = surface;
		d.double_sided = double_sided.value();
		d.direction = transform.direction();

		if(auto res = scene.lights().add(d); res)
			light_id = res.value();
	}
	break;
	}

	if(light_id != NO_LIGHT_ID)
		Log::info("added light {}", light_id);

	return not at_next_entry;
}

bool SceneLoader::read_entity(tag_file &fp, Scene &scene)
{
	// Log::debug("[{}:{}] read_entity: {}", fp.file_path().filename().native(), fp.line_num(), fp.value());

	// skip until next start tag or EOF
	while(fp)
		if(is_entry_start(fp.next().first))
			break;
	return false;

	// spawns (player, mobs)
	// "pickupables" / game items

	// name
	// type
/*
	if(not current.got(Property::Grid) or not current.got(Property::Orientation))
	{
		Log::warning("[{}:{}]: invalid SPAWN entry (missing grid or rotation)", _filename, current.item_start_line);
		break;
	}

	auto spawn_ent = level->_entities.create();

	auto facing = DIR_NORTH;
	auto rot_z = to_vec3(current.properties[Property::Orientation]).z;
	// snap rotation to closest cardinal direction
	if(rot_z >= 90 - 45 and rot_z < 90 + 45)
		facing = DIR_EAST;
	else if(rot_z >= 180 - 45 and rot_z < 180 + 45)
		facing = DIR_SOUTH;
	else if(rot_z >= 270 - 45 and rot_z < 270 + 45)
		facing = DIR_WEST;

		   // get spawn type before we move it all into the spawn component
	const auto spawn_type = std::get<std::string>(current.properties[Property::spawn_type]);

	component::Spawn spawn_comp {
		.type = SpawnType::Player,
		.name = "",                 // only for 'type' != "player"
		.grid = to_grid(current.properties[Property::grid]),
		.facing = facing,
		.properties = std::move(current.properties),
	};
	if(spawn_type != "player")
	{
		spawn_comp.type = SpawnType::Entity;
		spawn_comp.name = spawn_type;
		// TODO: check that 'spawn_type' is a known (and spawnable) entity
	}

	level->_entities.emplace<component::Spawn>(spawn_ent, spawn_comp);
*/
	return true;
}

bool SceneLoader::read_control(tag_file &fp, Scene &scene)
{
	Log::debug("[{}:{}] read_control: {}", fp.file_path().filename().native(), fp.line_num(), fp.value());

	// skip until next start tag or EOF
	while(fp)
		if(is_entry_start(fp.next().first))
			break;
	return false;

	// TODO
/*
	// name
	// type
	// bounds?

	else if(current.item_type == ItemType::Control and tag == "type"sv)
	{
		auto type = Control::None;
		if(value == "button"sv)  // single-choice (only once)
			type = Control::Button;
		else if(value == "switch"sv) // multi-choice
			type = Control::Switch;
		else if(value == "axis"sv)  // value "slider"
			type = Control::Axis;
		else
			Log::warning("[{}:{}]: unknown control 'type': {}", _filename, current.item_start_line, value);

		current.properties[Property::Type] = int64_t(type);
	}

	if(not current.got(Property::Grid) or not current.got(Property::Orientation) or not current.got(Property::control_type))
	{
		Log::debug("[{}:{}]: invalid CONTROL entry (missing grid or orientation)", _filename, current.item_start_line);
		break;
	}

	auto ctrl_ent = level->_entities.create();

	auto ctrl_type = control::Type(std::get<int64_t>(current.properties[Property::control_type]));

	component::Control ctrl_comp {
								   .type = ctrl_type,
								   .value = { 0 },
								   };

	level->_entities.emplace<component::Control>(ctrl_ent, ctrl_comp);


*/
	return true;
}

bool SceneLoader::read_trigger(tag_file &fp, Scene &scene)
{
	// Log::debug("[{}:{}] read_trigger: {}", fp.file_path().filename().native(), fp.line_num(), fp.value());

	// name
	// bounds

	auto name = std::string(fp.value());

	glm::vec3 position(0);
	bounds::AABB bounds;


	auto add_trigger = [&name]() {
		Log::warning("Add trigger NOT IMPLEMENTED: {}", name);
	};

	while(fp)
	{
		auto [tag, value] = fp.next();

		if(is_entry_start(tag))
		{
			add_trigger();
			return false;
		}
		else if(tag == Property::Grid)
			position = pop_grid_pos(value);
		else if(tag == "bounds"sv and pop_bounds(value, bounds))
			;
		else
			unexpected_tag(fp, "trigger");
	}

	add_trigger();

	return true;
}

bool SceneLoader::read_walkable(tag_file &fp, Scene &scene)
{
	// Log::debug("[{}:{}] read_walkable: {}", fp.file_path().filename().native(), fp.line_num(), fp.value());

	glm::vec3 position(0);
	bounds::AABB bounds;


	auto add_walkable = [&scene, &position]() {
		Log::warning("Add walkable NOT IMPLEMENTED");
		// scene.addWalkable(grid);
	};

	while(fp)
	{
		auto [tag, value] = fp.next();

		if(is_entry_start(tag))
		{
			add_walkable();
			return false;
		}
		else if(tag == Property::Grid)
			position = pop_grid_pos(value);
		else
			unexpected_tag(fp, "walkable");
	}

	add_walkable();

	return true;
}

void SceneLoader::unexpected_tag(const tag_file &fp, std::string_view context)
{
	Log::error("[{}:{}] (in {}) Unexpected tag: {} (item started @ {})", _filename, fp.line_num(), context, fp.tag(), _item_start_line);
}

} // RGL
