#include "scene_loader.h"

#include "asset/asset_manager.h"
#include "component/transform.h"
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
static constexpr std::string_view Name = "name"sv;
static constexpr std::string_view Type = "type"sv;
static constexpr std::string_view Position = "pos"sv;
static constexpr std::string_view Orientation = "ori"sv;
static constexpr std::string_view Animation = "anim"sv;
static constexpr std::string_view Grid = "grid"sv;
static constexpr std::string_view Color = "color"sv;
static constexpr std::string_view Effect1 = "fx1"sv;   // lights: wobble (o.e. live flame) - amplitude (float)
static constexpr std::string_view Effect2 = "fx2"sv;   // lights: intensity (float)
static constexpr std::string_view Effect3 = "fx3"sv;   // lights: fog (float)
static constexpr std::string_view Effect4 = "fx4"sv;   // TBD
static constexpr std::string_view Length = "len"sv;
static constexpr std::string_view Power = "pow"sv;
static constexpr std::string_view Flicker = "flicker"sv;
static constexpr std::string_view Radius = "radius"sv;
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

template<typename T=float>
static T pop_number(auto &buffer)
{
	// TODO: use extract_word() then convert?

	T number;
	auto result = std::from_chars(buffer.data(), buffer.data() + buffer.size(), number);
	if(result.ec == std::errc::invalid_argument)
		return std::numeric_limits<T>::min();

	if(result.ptr == buffer.data() + buffer.size())
		buffer.remove_prefix(buffer.size());
	else
	{
		auto *ptr = result.ptr;
		// advance 'p' until not whitespace
		while(*ptr == ' ' and ptr < buffer.data() + buffer.size())
			++ptr;
		buffer.remove_prefix(size_t(ptr - buffer.data()));
	}

	return number;
};

static std::string_view extract_word(std::string_view &buffer)
{
	// skip initial space (probably not needed)
	auto nonspace = buffer.find_first_not_of(" ");
	if(nonspace != std::string_view::npos)
		buffer.remove_prefix(nonspace);

	const auto word_end = buffer.find(' '); // might be 'npos', that's ok
	auto word = buffer.substr(0, word_end);
	buffer.remove_prefix(word_end == std::string_view::npos? buffer.size(): word_end);

	// skip trailing space
	nonspace = buffer.find_first_not_of(" ");
	if(nonspace != std::string_view::npos)
		buffer.remove_prefix(nonspace);

	return word;
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

	auto file_path = FileSystem::getResourcesPath() / "scene" / name;

	tag_file fp(file_path);

	enum class ItemType
	{
		None = 0,
		Mesh,
		// Animation,
		Light,
		Entity,      // player, npc, mob or item
		Control,    // button, lever, knob, wheel/valve, slider, etc
		Trigger,
		Walkable,
	};
	static const string_map<ItemType> item_name_type {
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
	auto item_start_line = 0u;
	bool first { true };

	while(fp)
	{
		const auto &[tag, value] = fp.next();
		if(tag.empty())
			break;

		if(first and tag == "room"sv)
		{
			_roomName = value;
			first = false;
			continue;
		}

		// capital latter means new item
		if(is_entry_start(tag))
		{
			item_start_line = fp.line_num();

			// TODO: read all fields directly in each if case??
			//   more clear, but less robust

			auto type_found = item_name_type.find(tag);
			if(type_found == item_name_type.end())
				Log::error("unknown item type: '{}'", tag);
			assert(type_found != item_name_type.end());

			item_type = type_found->second;

			// if(current.item_type == ItemType::Mesh)
			// 	current.model = _assets.staticMesh(value);
			// else if(current.item_type == ItemType::Entity)
			// 	current.properties[Property::Type] = std::string(value);
		}
		else
		{
			Log::error("[{}:{}] unknown tag file state, expected entry start, got: {} {}", _filename, fp.line_num(), tag, value);
			continue;
		}

		switch(item_type)
		{
		case ItemType::None:
			Log::warning("ignoring property of UNKNOWN item type: '{}'", tag);
			continue;
		case ItemType::Mesh:     read_mesh(fp, scene);     break;
		case ItemType::Light:    read_light(fp, scene);    break;
		case ItemType::Entity:   read_entity(fp, scene);   break;
		case ItemType::Control:  read_control(fp, scene);  break;
		case ItemType::Trigger:  read_trigger(fp, scene);  break;
		case ItemType::Walkable: read_walkable(fp, scene); break;
		}

		item_type = ItemType::None;
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
	auto word = extract_word(value);

	return word == "1"sv or word == "y"sv or word == "t"sv or word == "yes"sv or word == "true"sv or word == "on"sv;
}

static GridPos pop_grid(std::string_view &value)
{
	return {
		.x = pop_number<int32_t>(value),
		.y = pop_number<int32_t>(value),
		.z = pop_number<int32_t>(value)    // might be omitted
	};
};
static glm::vec2 pop_vec2(std::string_view &value)
{
	return {
		pop_number(value),
		pop_number(value),
	};
};
static glm::vec3 pop_vec3(std::string_view &value)
{
	return {
		pop_vec2(value),
		pop_number(value),
	};
};
static glm::vec3 pop_orientation(std::string_view &value)
{
	auto ori = pop_vec3(value);

	// y & z might be non-existent
	assert(ori.x != std::numeric_limits<float>::min());

		   // if only a single number was specified it's rotation around the Z-axis
	if(ori.y == std::numeric_limits<float>::min() and ori.z == std::numeric_limits<float>::min())
		return { 0, 0, ori.x };
	return ori;
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
		assert(false);
	};
	auto &curveset = select_property(descriptor[0]);

	switch(descriptor[1])
	{
	case 'x': return curveset[0]; break;
	case 'y': return curveset[1]; break;
	case 'z': return curveset[2]; break;
	default:
		Log::debug("Unknown animation curve: {} (expected: [pos][xyz])", descriptor);
		return std::nullopt;
	}
};

static bool add_curve_point(anim::curve_ref<> curve, std::string_view args)
{
	auto rest = args;
	const auto interp_type = extract_word(rest);
	// only one interpolation type supported at the moment (and probably every woll be)
	if(interp_type != "bz"sv)
	{
		Log::warning("Unknown curve point interpolation type: '{}'", interp_type);
		return false;
	}

	const auto in_time = seconds_f(pop_number(rest));
	const auto in_value = pop_number(rest);
	const auto cp_time = seconds_f(pop_number(rest));
	const auto cp_value = pop_number(rest);
	const auto out_time = seconds_f(pop_number(rest));
	const auto out_value = pop_number(rest);

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

static bool read_animation(tag_file &fp, AnimationSystem::AnimationSetup &anim)
{
	const auto &tag = fp.tag();

	if(tag == "anim_len"sv)
	{
		auto rest = fp.value();
		anim.end_time = seconds_f(pop_number(rest));
		return true;
	}
	else if(tag == "anim_end"sv)
	{
		anim.end_state = fp.value() == "reset"? anim::EndState::Reset: anim::EndState::Clamp;
	}
	else if(tag == "anim_lop"sv)
	{
		auto rest = fp.value();
		anim.total_loops = pop_number<uint32_t>(rest);
		if(anim.total_loops == 0)
			anim.total_loops = 1;
	}
	else if(tag.starts_with("kf-"sv) and tag.size() >= 4) // e.g. kf-px
	{
		auto opt_curve = select_curve(tag.substr(3), anim);
		if(opt_curve.has_value())
		{
			auto &curve = opt_curve.value();
			if(not curve) // not yet allocated
				curve.reset(new anim::animation_curve<>);

			add_curve_point(curve, fp.value());
		}
		return true;
	}
	return false;
}

static bool read_bounds(std::string_view value, bounds::AABB &bounds)
{
	// NOTE: the model itself contains a bounds, is this even needed?
	auto rest = value;
	auto min_x = pop_number(rest);
	auto min_y = pop_number(rest);
	auto min_z = pop_number(rest);
	auto to = extract_word(rest);
	if(to != "to"sv)
		Log::error("bounds: expected word 'to': '{}'", to);

	auto max_x = pop_number(rest);
	auto max_y = pop_number(rest);
	auto max_z = pop_number(rest);

	bounds.min() = { min_x, min_y, min_z };
	bounds.max() = { max_x, max_y, max_z };

	return bounds.volume() > 0;
}

static bool read_material(tag_file &fp, dense_map<uint32_t, MaterialRef> &materials)
{
	const auto prop = fp.tag().substr(4); // "mat_"

	// e.g. mat_alb >> 0 some_texture.png  (0 is material index)
	//   OR mat_alb >> 0 0.25

	auto rest = fp.value();
	const auto index = pop_number<uint32_t>(rest);
	assert(index >= 0);

	auto texture_name = rest;
	auto texture = AssetManager::the().texture(texture_name);
	assert(texture);

	auto &material = materials[index];
	if(not material)
		material.reset(new Material());

	// see material.h
	if(prop == "alb"sv) // albedo
		material->set(Material::TextureType::ALBEDO, texture_name, texture);
	else if(prop == "norm"sv) // normals
		material->set(Material::TextureType::NORMAL, texture_name, texture);
	else if(prop == "met"sv) // metallic
		material->set(Material::TextureType::METALLIC, texture_name, texture);
	else if(prop == "ruff"sv) // roughness  :)
		material->set(Material::TextureType::ROUGHNESS, texture_name, texture);
	else if(prop == "ao"sv) // ambient occlusion
		material->set(Material::TextureType::AO, texture_name, texture);
	else if(prop == "em"sv) // emissive
		material->set(Material::TextureType::EMISSIVE, texture_name, texture);
	else
	{
		Log::warning("Unknown mateiral property: {}", prop);
		return false;
	}

	return true;
}

bool SceneLoader::read_mesh(tag_file &fp, Scene &scene)
{
	// name (also.model file)

	const auto name = std::string(fp.value());

	prop_map_t props;

	ModelCRef model;
	MaterialCSet materials;
	bounds::AABB bounds;
	AnimationSystem::AnimationSetup animation;
	component::Transform transform;
	std::string anim_name;

	dense_map<uint32_t, MaterialRef> material_overrides;

	auto add_mesh = [&]() {
		// override materials (and constify)
		for(const auto &[index, material]: material_overrides)
		{
			assert(index < materials.size());
			materials[index] = material;
		}
		// store_mesh(model, materials, transform, bounds);
		if(_roomName.empty())
			scene.add(model, materials, transform);
		else
			scene.add(_roomName, model, materials, transform);
	};

	while(fp)
	{
		auto [tag, value] = fp.next();

		if(is_entry_start(tag))
		{
			add_mesh();
			return false;
		}
		if(tag == "mesh"sv)
		{
			auto [model_, materials_] = _assets.staticMesh(fp.value());
			model = std::move(model_);
			materials = std::move(materials_);
			assert(model);
		}
		if(tag == "position"sv)
		{
			transform.set_position(glm::vec4(pop_vec3(value), 0));
		}
		else if(tag == "orientat"sv)
		{
			transform.set_orientation_xyz(glm::vec4(pop_orientation(value), 0));
		}
		else if(tag == "scale"sv)
		{
			transform.set_scale(glm::vec4(pop_vec3(value), 0));
		}
		else if(tag == "bounds"sv and read_bounds(value, bounds))
			;
		else if(tag.starts_with("mat_"sv) and tag.size() > 5 and read_material(fp, material_overrides))
			;
		if(tag == "anim"sv)
		{
			if(not anim_name.empty())
			{
				_anims.add(anim_name, animation);
				anim_name.clear();
				animation.clear();
			}
			anim_name = fp.value();
		}
		else if(not anim_name.empty() and read_animation(fp, animation))
			;
	}

	// if we got here, we ran into EOF

	if(not anim_name.empty())
		_anims.add(anim_name, animation);

	add_mesh();

	return true;
}

bool SceneLoader::read_light(tag_file &fp, Scene &scene)
{
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

	if(fp.value() == "spot"sv)
		type = LightType::Spot;
	else if(fp.value()== "rect"sv)
		type = LightType::Rect;
	else if(fp.value() == "tube"sv)
		type = LightType::Tube;
	else if(fp.value() == "sphere"sv)
		type = LightType::Sphere;
	else if(fp.value() == "disc"sv)
		type = LightType::Disc;
	else
	{
		Log::warning("[{}:{}]: unknown light type: {} (using point)", _filename, start_line, fp.value());
		return false;
	}

	while(fp)
	{
		auto [tag, value] = fp.next();
		auto rest = value;

		if(tag == "enabled"sv) // bool
			enabled = pop_bool(value);
		else if(tag == "power"sv) // intensity, float
		{
			intensity = pop_number(value);
			intensity = std::max(0.f, intensity.value());
		}
		else if(tag == "position"sv)
			position = pop_vec3(rest);
		else if(tag == "orientat"sv)
			orientation = pop_vec3(rest);
		else if(tag == "color"sv)
		{
			color = pop_vec3(rest)/255.f;
			// TODO: saturate/normalize
		}
		else if(tag == "flicker"sv)
			flicker = pop_number(rest);
		else if(tag == "angle"sv)  // spots
			angles = { pop_number(rest), pop_number(rest) };
		else if(tag == "shadows"sv) // bool
			shadows = pop_bool(rest);
		else if(tag == "fog"sv) // float
			fog = pop_number(rest);
		else if(tag == "surface"sv) // bool
			surface = pop_bool(rest);
		else if(tag == "contacts"sv) // bool?
			contact_shadows = pop_bool(rest);
		else if(tag == "rangecmp"sv) // float
			shadow_range_comp = pop_number(rest);
		else if(tag == "radius"sv)
			radius = pop_number(rest);
		else if(tag == "thick"sv)
			thickness = pop_number(rest);
		else if(tag == "size"sv) // vec2
			size = pop_vec2(rest);
		else if(tag == "dblsided"sv)
			double_sided = pop_bool(rest);
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

	return true;
}

bool SceneLoader::read_entity(tag_file &fp, Scene &scene)
{
	// spawns (player, mobs)
	// "pickupables" / game items

	// name
	// type
	return false;
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
	// TODO
	return false;
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


	return true;
*/
}

bool SceneLoader::read_trigger(tag_file &fp, Scene &scene)
{
	// name
	// bounds

	auto name = std::string(fp.value());

	GridPos grid;
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
		else if(tag == "grid"sv)
			grid = pop_grid(value);
		else if(tag == "bounds"sv and read_bounds(value, bounds))
			;
	}

	add_trigger();

	return true;
}

bool SceneLoader::read_walkable(tag_file &fp, Scene &scene)
{
	GridPos grid;
	bounds::AABB bounds;


	auto add_walkable = [&scene, &grid]() {
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
		else if(tag == "grid"sv)
			grid = pop_grid(value);
	}

	add_walkable();

	return true;
}

} // RGL
