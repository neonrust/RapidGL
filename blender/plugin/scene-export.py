import bpy
from bpy_extras import anim_utils
from mathutils import Matrix, Vector
from sys import float_info
import math

PI = math.pi
float_max = float_info.max
float_min = float_info.min

bl_info = {
	"name": "Game Scene Exporter",
	"author": "André Jonsson",
	"version": (0, 3, 2),
	"blender": (5, 0, 0),
	"location": "File > Export > Game Scene",
	"category": "Import-Export",
}

GRID_SIZE = 2
NAME_LENGTH = 22  # max length std::string can store on the stack

option_y_up = False

def to_engine_space(point):
	if option_y_up:
		return Vector((point.x, point.z, -point.y))
	return point

def fv(value, decimals=None):
	if isinstance(value, float):
		return '%g' % (value if decimals is None else round(value, decimals))
	else:
		return ' '.join(( fv(v, decimals) for v in value ))

def iv(value, scale=1):
	return ' '.join(( '%d' % round(v*scale) for v in value ))

def wtag(fp, label, s):
	s = str(s)
	print('%-08s%02x%s' % (label[:8], len(s), s), file=fp)

def wbounds(fp, obj):
    # bbmin = [float_max, float_max, float_max]
    # bbmax = [float_min, float_min, float_min]
    # obj_pos = obj.location
    # vertices_world = [
    #     obj.matrix_world @ vertex.co - obj_pos
    #     for vertex in obj.data.vertices
    # ]
    # for corner in vertices_world:
    #     for axis in range(3):
    #         bbmin[axis] = min(bbmin[axis], corner[axis])
    #         bbmax[axis] = max(bbmax[axis], corner[axis])
    # wtag(fp, 'bounds', '%s to %s' % (fv(bbmin, 3), fv(bbmax, 3) ))
	return


def wgrid_pos(fp, obj):
	x, y, z = grid_of(obj)
	wtag(fp, 'grid', '%d %d %d' % (x, y, z))

def grid_of(obj, check_align=True):
	loc = to_engine_space(obj.location)
	if check_align:
		if not is_on_grid(loc.x) or not is_on_grid(loc.y) or not is_on_grid(loc.z):
			print('NOT GRID ALIGNED:', obj.name, loc.x, loc.y, loc.z)

	# truncate location to grid squares (well, cubes)
	x = round(loc.x)//GRID_SIZE
	y = round(loc.y)//GRID_SIZE
	z = round(loc.z)//GRID_SIZE
	return x, y, z

def is_on_grid(pos):
	diff = abs(round(pos/GRID_SIZE, 4)*GRID_SIZE - pos)
	return diff < 0.05

array_index_names = [ 'x',  'y', 'z' ]
tag_prefix = { 'location': 'p', 'scale': 's', 'rotation_euler': 'o' }

def curve_data_name(curve):
	name = curve.data_path
	idx = curve.array_index
	if name not in tag_prefix:
		return None, None
	return (tag_prefix[name], array_index_names[idx])

def engine_channel(category, axis):
	'''map a Blender curve channel to the engine's: (axis, value transform)'''
	unit = degrees if category == 'o' else (lambda v: v)
	if not option_y_up or axis == 'x':
		return axis, unit
	# (x, y, z) -> (x, z, -y), for positions and euler angles alike (scale isn't negated)
	#   Blender's XYZ euler Rz(c)*Ry(b)*Rx(a) then becomes Ry(c)*Rz(-b)*Rx(a), i.e. the engine
	#   needs to compose the curves as glm::eulerAngleYZX(oy, oz, ox)
	if axis == 'z':
		return 'y', unit
	if category == 's':
		return 'z', unit
	return 'z', lambda v: -unit(v)

anim_serial = 0

def action_fcurves(adata):
	# Blender 5.x: F-curves live in the channelbag of the action's assigned slot
	if not adata or not adata.action:
		return []
	channelbag = anim_utils.action_get_channelbag_for_slot(adata.action, adata.action_slot)
	return channelbag.fcurves if channelbag else []

def wanim_keys(fp, obj):
	adata = obj.animation_data
	if not adata:
		return False

	anim_name = obj.get('animation', '').strip()
	if not anim_name:
		if not obj.get('button'):
			print('WARNING: %s has no "animation" property (animation will not work)' % obj.name)
		else:
			# generate a unique name
			global anim_serial
			anim_serial += 1
			anim_name = '%d%d' % (id(obj), anim_serial)

	wtag(fp, 'anim', anim_name)
	wtag(fp, 'animend', 'clamp')  # TODO: read from blender
	wtag(fp, 'animloop', 1)
	wtag(fp, 'animlen', anim_lengths[anim_name])

	if adata.action_influence != 1:
		print('WARNING: animation action influence is not 1.0: %s' % adata.action_influence)
	if adata.action_blend_type != 'REPLACE':
		print('WARNING: animation action blend type is not REPLACE: %s' % adata.action_blend_type)

	interpolation = 'BEZIER'

	for curve in action_fcurves(adata):
		category, axis = curve_data_name(curve)
		if category is None:
			print('WARNING: %s: unsupported animation curve: %s' % (obj.name, curve.data_path))
			continue
		if category == 'o' and obj.rotation_mode != 'XYZ':
			print('WARNING: %s: rotation mode must be XYZ euler, got %s' % (obj.name, obj.rotation_mode))
		axis, transform = engine_channel(category, axis)
		key = 'kf-%s%s' % (category, axis)

		for keyframe in curve.keyframe_points:
			if keyframe.interpolation != interpolation:
				print('WARNING: keyframe interpolation != %s' % interpolation)

			wanim_key_bezier(fp, key, keyframe, transform)

	return True

def wanim_key_bezier(fp, key, keyframe, transform):
	T, v = anim_timeline_value(keyframe.co)
	lhT, lhv = anim_timeline_value(keyframe.handle_left)
	rhT, rhv = anim_timeline_value(keyframe.handle_right)
	v = transform(v)
	lhv = transform(lhv)
	rhv = transform(rhv)

	wtag(fp, key, 'bz %g %s %g %s %g %s' % (
        lhT, fv(lhv, 3),
        T, fv(v, 3),
        rhT, fv(rhv, 3),
    ))


def anim_timeline_value(v):
    fps = bpy.context.scene.render.fps
    frame, value = v
    return round(frame/fps, 3), value

# ----------------------------------------------------------------------------

import re
serial_ptn = re.compile(r'\.\d{3}$')

walkable = set()
anim_lengths = {}

def degrees(rad):
	return rad*180/PI

def ori_angle(angle):
	return str(round(degrees(angle)) % 360)

# Blender (Z up) -> engine (Y up): (x, y, z) -> (x, z, -y)
Y_UP_CONVERSION = Matrix.Rotation(-PI/2, 3, 'X')

def engine_orientation(obj):
	'''euler angles (degrees) as expected by glm::eulerAngleXYZ(), i.e. Rx*Ry*Rz'''
	rot = obj.matrix_basis.decompose()[1].to_matrix()
	if option_y_up:
		rot = Y_UP_CONVERSION @ rot @ Y_UP_CONVERSION.transposed()
	# Blender calls the Rx*Ry*Rz product 'ZYX' order
	e = rot.to_euler('ZYX')
	return '%s %s %s' % (ori_angle(e.x), ori_angle(e.y), ori_angle(e.z))

def dump_mesh(fp, obj):
	linked_source = obj.data.library.name.replace('.blend', '')
	# don't know how to avoid serial numbering of the library name in Blender
	# chopping it off should result in the correct name
	linked_source = serial_ptn.sub('', linked_source)
	wtag(fp, 'MESH', linked_source)
	wgrid_pos(fp, obj)
	if obj.rotation_euler.x == 0 and obj.rotation_euler.y == 0:
		if obj.rotation_euler.z != 0:
			wtag(fp, 'ori', ori_angle(obj.rotation_euler.z))
	else:
		wtag(fp, 'ori', engine_orientation(obj))
	wbounds(fp, obj)
	if wanim_keys(fp, obj):
		# write initial values for 'position'
		# but! already written via 'grid' above
		#wtag(fp, 'position', fv(to_engine_space(obj.location), 2))
		# TODO: write initial "orientation" as well... but we can't use the same tag "orientation"
		pass


def dump_control(fp, obj):
	'''interactable objects'''
	linked_source = obj.data.library.name.replace('.blend', '')
	linked_source = serial_ptn.sub('', linked_source)
	wtag(fp, 'CONTROL', linked_source)
	wgrid_pos(fp, obj)
	wtag(fp, 'type', 'button')  # TODO: lever, knob, wheel, slider, etc...
	wtag(fp, 'position', fv(obj.location, 2))
	wtag(fp, 'ori', fv(obj.rotation_euler, 3))
	wbounds(fp, obj)
	if obj.get('animation'):
		wtag(fp, 'anim', obj.get('animation'))
		wanim_keys(fp, obj)
	if obj.get('action'):
		wtag(fp, 'action', obj.get('action'))


def dump_light(fp, obj):
	wtag(fp, 'LIGHT', obj.name)
	light_type = obj.data.type
	wtag(fp, 'type', light_type)
	wtag(fp, 'position', fv(obj.location, 2))
	# for non-point lights:
	if light_type != 'POINT':
		wtag(fp, 'ori', fv(obj.rotation_euler, 3))
	elif light_type == 'SPOT':
		wtag(fp, 'angle', str(fv(obj.data.spot_size, 2)))
	wtag(fp, 'color', iv(obj.data.color, scale=255))
	# some made-up formula to make the light power similar in the enginge :|
	wtag(fp, 'power', str(round(math.pow(obj.data.energy/100, 0.5), 2)))
	wtag(fp, 'shadows', str(1 if obj.data.use_shadow else 0))
	# TODO: animation(s)


def dump_spawn(fp, obj):
	spawn = obj.get('spawn')
	if not spawn:
		print('Object %s has empty "spawn" property' % obj.name)
		return

	wtag(fp, 'ENTITY', 'spawn')
	wtag(fp, 'type', spawn)
	wgrid_pos(fp, obj)
	wtag(fp, 'rotation', str(round(obj.rotation_euler.z*180/PI) % 360))


def dump_walkable(fp, obj):
	wtag(fp, 'WALKABLE', '')
	wgrid_pos(fp, obj)


def dump_empty(fp, obj):
	name = obj.name
	if name[:5].lower() == 'spawn':
		dump_spawn(fp, obj)


def verify_walkable():
	if not walkable:
		print('NO WALKABLE')
		return False

	visited = set()

	def visit_all(walkable, start):
		w = set(walkable)
		try:
			w.remove(start)
		except KeyError:
			return

		visited.add(start)
		# visit neighbours
		visit_all(walkable, (w[0] + 1, w[1],     w[2]    ))
		visit_all(walkable, (w[0] - 1, w[1],     w[2]    ))
		visit_all(walkable, (w[0],     w[1] + 1, w[2]    ))
		visit_all(walkable, (w[0],     w[1] - 1, w[2]    ))
		visit_all(walkable, (w[0],     w[1],     w[2] + 1))
		visit_all(walkable, (w[0],     w[1],     w[2] - 1))

	start = list(walkable.pop())[0]
	visit_all(walkable, start)
	# TODO: also visit all at a teleporter destination
	unvisited = len(walkable) - len(visited)
	if unvisited:
		print('%d unvisited grids' % unvisited)
	return unvisited > 0

def collect_anim_lengths(obj):
	adata = obj.animation_data
	if not adata:
		return

	anim_name = obj.get('animation')
	if not anim_name:
		return

	last_time = 0
	for curve in action_fcurves(adata):
		for keyframe in curve.keyframe_points:
			last_time, _ = anim_timeline_value(keyframe.co)

	anim_lengths[anim_name] = max(anim_lengths.get(anim_name, 0), last_time)


def write_scene_data(fp, context, options=None):
	scene = bpy.context.scene
	current_frame = scene.frame_current
	scene.frame_set(0)

	try:
		return write_scene_data_wrapped(fp, context, options)
	finally:
		scene.frame_set(current_frame)

def write_scene_data_wrapped(fp, context, options=None):
	options = options or {}

	global walkable
	walkable = set()

	for obj in bpy.context.scene.objects:
		if obj.type == 'MESH':
			# only linked objects
			if obj.get('walkable') == True:
				walkable.add(grid_of(obj))
			else:
				is_linked = obj.data.library is not None
				if is_linked:
					collect_anim_lengths(obj)
	verify_walkable()

	only_visible = options.get('only-visible')

	hidden_skipped = 0

	for obj in bpy.context.scene.objects:
		if only_visible and not obj.visible_get():
			hidden_skipped += 1
			continue

		if obj.type == 'MESH':
			# only linked objects
			if obj.get('walkable') == True:
				dump_walkable(fp, obj)
			elif obj.get('button') == True:
				dump_control(fp, obj)
			else:
				is_linked = obj.data.library is not None
				if is_linked:
					dump_mesh(fp, obj)
				else:
					print('non-linked mesh ignored:', obj.name)
					# export as separate file?

		elif obj.type == 'LIGHT':
			print('dump_light:', obj.name)
			dump_light(fp, obj)

		elif obj.type == 'EMPTY':
			print('dump_empty:', obj.name)
			dump_empty(fp, obj)

	if hidden_skipped:
		print('Hidden objects skipped:', hidden_skipped)

	return {'FINISHED'}


# ExportHelper is a helper class, defines filename and
# invoke() function which calls the file selector.
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty, BoolProperty, EnumProperty
from bpy.types import Operator


class ExportGameScene(Operator, ExportHelper):
	bl_idname = "export_scene.game"
	bl_label = "Export Game Scene"

	# ExportHelper mix-in class uses this.
	filename_ext = ".scene"

	filter_glob: StringProperty(
		default="*%s" % filename_ext,
		options={'HIDDEN'},
		maxlen=255,  # Max internal buffer length, longer would be clamped.
	)

	# List of operator properties, the attributes will be assigned
	# to the class instance from the operator settings before calling.
	only_visible_setting: BoolProperty(
		name="Only visible",
		description="Export only visible objects",
		default=False,
	)
	y_up_setting: BoolProperty(
		name="Y up",
		description="Swap Y & Z axes",
		default=True,
	)

#	type: EnumProperty(
#		name="Example Enum",
#		description="Choose between two items",
#		items=(
#			('OPT_A', "First Option", "Description one"),
#			('OPT_B', "Second Option", "Description two"),
#		),
#		default='OPT_A',
#	)

	def execute(self, context):
		options = {
			'only-visible': self.only_visible_setting,
		}
		global option_y_up
		option_y_up = self.y_up_setting

		with open(self.filepath, 'w') as fp:
			return write_scene_data(fp, context, options)


# Only needed if you want to add into a dynamic menu
def menu_func_export(self, context):
	self.layout.operator(ExportGameScene.bl_idname, text="Game Scene (.scene)")

# Register and add to the "file selector" menu (required to use fv for quick access).
def register():
	bpy.utils.register_class(ExportGameScene)
	bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
	bpy.utils.unregister_class(ExportGameScene)
	bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)

if __name__ == "__main__":
	register()
