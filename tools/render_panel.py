"""
Photoreal render of the OL-4 faceplate (our own design on the LA-4 control layout).
Cycles, orthographic front view, real studio HDRI + sun.
Outputs (in OUT dir):
  plate.png            full panel, default state, inner knobs removed (they are a separate layer)
  crop_<name>.png      region renders for every switch / skirt / LED state
  knob/####.png        inner knob film strip, 0.0 .. 10.0 in 0.1 steps, transparent with shadow
  layout.json          pixel geometry for the web UI
Units: 1 Blender unit = 1 mm. Layout coordinates are given from the faceplate's top-left (x right, y down).
"""
import bpy, bmesh, math, json, os, sys, random
from mathutils import Vector, Matrix

ARGS = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = ARGS[0] if ARGS else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'render-out')
PARTS = ARGS[1].split(',') if len(ARGS) > 1 else ['plate', 'crops', 'knob']
SAMPLES = int(ARGS[2]) if len(ARGS) > 2 else 160
PPM = float(ARGS[3]) if len(ARGS) > 3 else 10.0     # pixels per mm
os.makedirs(OUT, exist_ok=True)
os.makedirs(os.path.join(OUT, 'knob'), exist_ok=True)

FONT_BOLD = '/usr/share/fonts/truetype/freefont/FreeSansBold.ttf'
FONT_REG = '/usr/share/fonts/truetype/freefont/FreeSans.ttf'
HDRI = os.path.join(os.path.dirname(bpy.app.binary_path or ''), '')  # resolved below

W, H, EAR = 216.0, 89.0, 20.0           # faceplate 8.5" x 3.5", rack ears
TOTAL_W = W + 2 * EAR

def P(x, y, z=0.0):
    """faceplate coords (mm from top-left, y down) -> world"""
    return Vector((x - W / 2, H / 2 - y, z))

# ------------------------------------------------------------------ layout (mm, faceplate coords)
L = {
    'border': (6.0, 5.0, 194.0, 84.0, 3.0),
    'screws': [(11.0, 15.6), (11.0, 73.2), (205.0, 16.3), (205.0, 73.3)],
    'meter_bezel': (20.1, 16.6, 100.2, 60.2),
    'meter_face': (22.4, 19.3, 98.2, 49.6),
    'meter_open': (22.4, 19.3, 98.2, 57.4),
    'meter_screw': (60.3, 53.4),
    'badge': (20.0, 65.5, 43.5, 78.5),
    'zero_hole': (111.0, 65.6),
    'knob_thr': (128.0, 38.4),
    'knob_out': (172.4, 38.4),
    'skirt_r': 15.6, 'knob_r': 9.6, 'cap_r': 4.3, 'scale_r': 19.2,
    'tog_st': (126.6, 73.1), 'tog_pwr': (201.6, 51.4),
    'led_pwr': (201.6, 36.0), 'led_ovl': (170.0, 73.4),
}
ARC = 146.0                                  # knob scale: 0 at -146 deg, 10 at +146 deg (clockwise from top)
RATIO_LEG = ['2', '4', '8', '12', '20']
METER_LEG = ['GR', '+4', '+8']
LEG_STEP = 30.0                              # degrees between skirt legends

def knob_angle(v):                           # value 0..10 -> clockwise degrees from 12 o'clock
    return -ARC + v * (2 * ARC / 10.0)

# ------------------------------------------------------------------ scene
bpy.ops.wm.read_factory_settings(use_empty=True)
scn = bpy.context.scene
scn.render.engine = 'CYCLES'
scn.cycles.device = 'CPU'
scn.cycles.samples = SAMPLES
scn.cycles.use_denoising = True
try:
    scn.cycles.denoiser = 'OPENIMAGEDENOISE'
except Exception:
    pass
scn.cycles.max_bounces = 10
scn.cycles.transmission_bounces = 10
scn.cycles.glossy_bounces = 6
scn.cycles.seed = 7
scn.render.film_transparent = True
scn.render.image_settings.file_format = 'PNG'
scn.render.image_settings.color_mode = 'RGBA'
scn.render.image_settings.color_depth = '8'
for vt in ('Khronos PBR Neutral', 'AgX', 'Standard'):
    try:
        scn.view_settings.view_transform = vt
        break
    except Exception:
        continue
scn.view_settings.exposure = 0.0

# world: real studio HDRI
import glob
hdr = None
for root in sys.path:
    c = glob.glob(os.path.join(root, 'bpy', '*', 'datafiles', 'studiolights', 'world', 'studio.exr'))
    if c: hdr = c[0]; break
world = bpy.data.worlds.new('W'); scn.world = world; world.use_nodes = True
wn = world.node_tree.nodes; wl = world.node_tree.links; wn.clear()
bg = wn.new('ShaderNodeBackground'); out = wn.new('ShaderNodeOutputWorld')
env = wn.new('ShaderNodeTexEnvironment'); env.image = bpy.data.images.load(hdr)
tc = wn.new('ShaderNodeTexCoord'); mp = wn.new('ShaderNodeMapping')
mp.inputs['Rotation'].default_value = (math.radians(-62), 0, math.radians(18))
wl.new(tc.outputs['Generated'], mp.inputs['Vector']); wl.new(mp.outputs['Vector'], env.inputs['Vector'])
wl.new(env.outputs['Color'], bg.inputs['Color']); bg.inputs['Strength'].default_value = 0.85
wl.new(bg.outputs['Background'], out.inputs['Surface'])

# key light: soft sun from top-left
sun = bpy.data.lights.new('Sun', 'SUN'); sun.energy = 2.6; sun.angle = math.radians(9)
so = bpy.data.objects.new('Sun', sun); scn.collection.objects.link(so)
so.rotation_euler = (math.radians(38), math.radians(-24), math.radians(-20))

# large softbox just above the camera: what flat brushed metal mirrors in a straight-on view
fb = bpy.data.lights.new('Softbox', 'AREA'); fb.shape = 'RECTANGLE'; fb.size = 520; fb.size_y = 300
fb.energy = float(os.environ.get('SOFTBOX', '330000'))
fo = bpy.data.objects.new('Softbox', fb); scn.collection.objects.link(fo)
fo.location = (-40, 170, 620); fo.rotation_euler = (math.radians(-15), math.radians(-4), 0)
# low fill from the right to lift shadows a little
fl = bpy.data.lights.new('Fill', 'AREA'); fl.size = 260; fl.energy = 90000
flo = bpy.data.objects.new('Fill', fl); scn.collection.objects.link(flo)
flo.location = (420, -120, 260); flo.rotation_euler = (math.radians(30), math.radians(55), 0)

# camera
cam = bpy.data.cameras.new('Cam'); cam.type = 'ORTHO'; cam.ortho_scale = TOTAL_W
co = bpy.data.objects.new('Cam', cam); scn.collection.objects.link(co); scn.camera = co
co.location = (0, 0, 300); co.rotation_euler = (0, 0, 0)
cam.clip_end = 1000
scn.render.resolution_x = int(round(TOTAL_W * PPM)); scn.render.resolution_y = int(round(H * PPM))
scn.render.resolution_percentage = 100
cam.sensor_fit = 'HORIZONTAL'

# ------------------------------------------------------------------ materials
def mat(name, **kw):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; b = nt.nodes['Principled BSDF']
    for k, v in kw.items():
        if k in b.inputs: b.inputs[k].default_value = v
    return m

def brushed_aluminium(name, base=(0.80, 0.805, 0.80, 1), rough=(0.20, 0.33), dirx=True, wear=0.6, seed=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; n = nt.nodes; l = nt.links
    b = n['Principled BSDF']
    b.inputs['Base Color'].default_value = base
    b.inputs['Metallic'].default_value = 1.0
    tc = n.new('ShaderNodeTexCoord')
    # streaks: noise stretched along the brushing direction
    mp = n.new('ShaderNodeMapping'); mp.inputs['Scale'].default_value = (0.012, 2.4, 1) if dirx else (2.4, 0.012, 1)
    mp.inputs['Location'].default_value = (seed, seed * 3.1, 0)
    l.new(tc.outputs['Object'], mp.inputs['Vector'])
    nz = n.new('ShaderNodeTexNoise'); nz.inputs['Scale'].default_value = 6.0; nz.inputs['Detail'].default_value = 15.0; nz.inputs['Roughness'].default_value = 0.72
    l.new(mp.outputs['Vector'], nz.inputs['Vector'])
    mp2 = n.new('ShaderNodeMapping'); mp2.inputs['Scale'].default_value = (0.05, 14, 1) if dirx else (14, 0.05, 1)
    l.new(tc.outputs['Object'], mp2.inputs['Vector'])
    nz2 = n.new('ShaderNodeTexNoise'); nz2.inputs['Scale'].default_value = 9.0; nz2.inputs['Detail'].default_value = 6.0
    l.new(mp2.outputs['Vector'], nz2.inputs['Vector'])
    mixs = n.new('ShaderNodeMix'); mixs.data_type = 'FLOAT'; mixs.inputs['Factor'].default_value = 0.5
    l.new(nz.outputs['Fac'], mixs.inputs['A']); l.new(nz2.outputs['Fac'], mixs.inputs['B'])
    # broad wear / handling smudges
    nz3 = n.new('ShaderNodeTexNoise'); nz3.inputs['Scale'].default_value = 0.035; nz3.inputs['Detail'].default_value = 4.0
    l.new(tc.outputs['Object'], nz3.inputs['Vector'])
    ramp = n.new('ShaderNodeMapRange'); ramp.inputs['From Min'].default_value = 0.3; ramp.inputs['From Max'].default_value = 0.7
    ramp.inputs['To Min'].default_value = rough[0]; ramp.inputs['To Max'].default_value = rough[1]
    add = n.new('ShaderNodeMath'); add.operation = 'MULTIPLY_ADD'; add.inputs[1].default_value = 0.10 * wear; add.inputs[2].default_value = -0.05 * wear
    l.new(nz3.outputs['Fac'], add.inputs[0])
    mixr = n.new('ShaderNodeMath'); mixr.operation = 'ADD'
    l.new(mixs.outputs['Result'], ramp.inputs['Value']); l.new(ramp.outputs['Result'], mixr.inputs[0]); l.new(add.outputs['Value'], mixr.inputs[1])
    l.new(mixr.outputs['Value'], b.inputs['Roughness'])
    # slight colour variation
    cm = n.new('ShaderNodeMapRange'); cm.inputs['To Min'].default_value = 0.93; cm.inputs['To Max'].default_value = 1.03
    l.new(mixs.outputs['Result'], cm.inputs['Value'])
    rgb = n.new('ShaderNodeRGB'); rgb.outputs[0].default_value = base
    mc = n.new('ShaderNodeVectorMath'); mc.operation = 'SCALE'
    l.new(rgb.outputs[0], mc.inputs[0]); l.new(cm.outputs['Result'], mc.inputs['Scale'])
    l.new(mc.outputs['Vector'], b.inputs['Base Color'])
    # micro-relief of the brushing
    bump = n.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.10; bump.inputs['Distance'].default_value = 0.02
    l.new(mixs.outputs['Result'], bump.inputs['Height']); l.new(bump.outputs['Normal'], b.inputs['Normal'])
    # anisotropy along the grain (UV tangent)
    try:
        b.inputs['Anisotropic'].default_value = 0.55
        tg = n.new('ShaderNodeTangent'); tg.direction_type = 'RADIAL'; tg.axis = 'Y' if dirx else 'X'
        l.new(tg.outputs['Tangent'], b.inputs['Tangent'])
    except Exception:
        pass
    return m

def spun_aluminium(name):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; n = nt.nodes; l = nt.links; b = n['Principled BSDF']
    b.inputs['Base Color'].default_value = (0.86, 0.865, 0.87, 1); b.inputs['Metallic'].default_value = 1.0
    tc = n.new('ShaderNodeTexCoord')
    wave = n.new('ShaderNodeTexWave'); wave.wave_type = 'RINGS'; wave.inputs['Scale'].default_value = 40.0
    wave.inputs['Distortion'].default_value = 0.0; wave.inputs['Detail'].default_value = 2.0
    l.new(tc.outputs['Object'], wave.inputs['Vector'])
    mr = n.new('ShaderNodeMapRange'); mr.inputs['To Min'].default_value = 0.30; mr.inputs['To Max'].default_value = 0.36
    l.new(wave.outputs['Fac'], mr.inputs['Value']); l.new(mr.outputs['Result'], b.inputs['Roughness'])
    bump = n.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.03; bump.inputs['Distance'].default_value = 0.005
    l.new(wave.outputs['Fac'], bump.inputs['Height']); l.new(bump.outputs['Normal'], b.inputs['Normal'])
    try:
        b.inputs['Anisotropic'].default_value = 0.35
        tg = n.new('ShaderNodeTangent'); tg.direction_type = 'RADIAL'; tg.axis = 'Z'
        l.new(tg.outputs['Tangent'], b.inputs['Tangent'])
    except Exception:
        pass
    return m

M = {
    'plate': brushed_aluminium('Plate', base=(0.86, 0.862, 0.855, 1), rough=(0.26, 0.40), wear=0.8),
    'ear': brushed_aluminium('Ear', base=(0.66, 0.665, 0.665, 1), rough=(0.34, 0.5), dirx=False, wear=1.0, seed=4.0),
    'ink': mat('Ink', **{'Base Color': (0.012, 0.012, 0.013, 1), 'Roughness': 0.55}),
    'inkred': mat('InkRed', **{'Base Color': (0.55, 0.05, 0.03, 1), 'Roughness': 0.5}),
    'skirtink': mat('SkirtInk', **{'Base Color': (0.50, 0.52, 0.54, 1), 'Roughness': 0.6}),
    'chrome': mat('Chrome', **{'Base Color': (0.90, 0.90, 0.91, 1), 'Metallic': 1.0, 'Roughness': 0.10}),
    'nickel': mat('Nickel', **{'Base Color': (0.80, 0.79, 0.76, 1), 'Metallic': 1.0, 'Roughness': 0.22}),
    'blackplastic': mat('BlackPlastic', **{'Base Color': (0.018, 0.018, 0.02, 1), 'Roughness': 0.38, 'Specular IOR Level': 0.55}),
    'knobplastic': mat('KnobPlastic', **{'Base Color': (0.012, 0.012, 0.013, 1), 'Roughness': 0.30, 'Specular IOR Level': 0.6}),
    'darkgrey': mat('DarkGrey', **{'Base Color': (0.035, 0.036, 0.038, 1), 'Roughness': 0.6}),
    'hole': mat('Hole', **{'Base Color': (0.0, 0.0, 0.0, 1), 'Roughness': 1.0}),
    'acrylic': mat('Acrylic', **{'Base Color': (0.96, 0.97, 0.975, 1), 'Roughness': 0.24, 'IOR': 1.49, 'Transmission Weight': 1.0}),
    'badge': mat('Badge', **{'Base Color': (0.055, 0.06, 0.068, 1), 'Metallic': 0.85, 'Roughness': 0.32}),
    'badgeink': mat('BadgeInk', **{'Base Color': (0.88, 0.89, 0.9, 1), 'Metallic': 1.0, 'Roughness': 0.3}),
    'ledoff': mat('LedOff', **{'Base Color': (0.22, 0.012, 0.01, 1), 'Roughness': 0.05, 'Coat Weight': 1.0}),
    'ledon': mat('LedOn', **{'Base Color': (1.0, 0.15, 0.06, 1), 'Roughness': 0.05, 'Coat Weight': 1.0,
                             'Emission Color': (1.0, 0.13, 0.05, 1), 'Emission Strength': 28.0}),
    'meterhole': mat('MeterHole', **{'Base Color': (0, 0, 0, 1), 'Roughness': 1.0}),
}
spun = spun_aluminium('Spun')

# ------------------------------------------------------------------ geometry helpers
def link(obj):
    scn.collection.objects.link(obj); return obj

def mesh_obj(name, bm, material, smooth=False, sharp_deg=None):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    if smooth:
        for p in me.polygons: p.use_smooth = True
    ob = bpy.data.objects.new(name, me); link(ob)
    if material: ob.data.materials.append(material)
    if sharp_deg is not None:
        try:
            mod = ob.modifiers.new('Smooth', 'NODES')
            ob.modifiers.remove(mod)
        except Exception:
            pass
        me.set_sharp_from_angle(angle=math.radians(sharp_deg)) if hasattr(me, 'set_sharp_from_angle') else None
    return ob

def prism(name, pts, z0, z1, material, smooth_sides=False, bevel=0.0, seg=3):
    bm = bmesh.new()
    bot = [bm.verts.new((x, y, z0)) for x, y in pts]
    top = [bm.verts.new((x, y, z1)) for x, y in pts]
    n = len(pts)
    bm.faces.new(list(reversed(bot))); bm.faces.new(top)
    for i in range(n):
        f = bm.faces.new((bot[i], bot[(i + 1) % n], top[(i + 1) % n], top[i]))
        f.smooth = smooth_sides
    for e in bm.edges:
        lf = e.link_faces
        if len(lf) == 2 and (len(lf[0].verts) > 4 or len(lf[1].verts) > 4 or n <= 4):
            e.smooth = False
    ob = mesh_obj(name, bm, material)
    if bevel > 0:
        md = ob.modifiers.new('Bevel', 'BEVEL'); md.width = bevel; md.segments = seg
        md.limit_method = 'ANGLE'; md.angle_limit = math.radians(35); md.harden_normals = False
    return ob

def circle(r, n=96, cx=0.0, cy=0.0):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]

def rrect(x0, y0, x1, y1, r, n=10):
    pts = []
    for (cx, cy, a0) in ((x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180), (x1 - r, y0 + r, 270)):
        for k in range(n + 1):
            a = math.radians(a0 + 90 * k / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts

def tube_curve(name, pts, radius, z, material, closed=True, flatten=0.08):
    cu = bpy.data.curves.new(name, 'CURVE'); cu.dimensions = '3D'
    sp = cu.splines.new('POLY'); sp.points.add(len(pts) - 1)
    for i, (x, y) in enumerate(pts): sp.points[i].co = (x, y, 0, 1)
    sp.use_cyclic_u = closed
    cu.bevel_depth = radius; cu.bevel_resolution = 2
    ob = bpy.data.objects.new(name, cu); link(ob); ob.location.z = z; ob.scale.z = flatten
    ob.data.materials.append(material)
    return ob

FONTS = {}
def font(path):
    if path not in FONTS: FONTS[path] = bpy.data.fonts.load(path)
    return FONTS[path]

def text(name, s, x, y, cap, material, z=0.004, bold=True, align='CENTER', extrude=0.004, rot=0.0, parent=None, spacing=1.0):
    cu = bpy.data.curves.new(name, 'FONT'); cu.body = s
    cu.font = font(FONT_BOLD if bold else FONT_REG)
    cu.size = cap / 0.73; cu.align_x = align; cu.align_y = 'CENTER'
    cu.extrude = extrude; cu.space_character = spacing
    ob = bpy.data.objects.new(name, cu); link(ob)
    if parent is None:
        ob.location = P(x, y, z)
    else:
        ob.parent = parent; ob.location = (x, y, z)
    ob.rotation_euler.z = rot
    ob.data.materials.append(material)
    return ob

# ------------------------------------------------------------------ faceplate + ears
plate = prism('Plate', rrect(-W / 2, -H / 2, W / 2, H / 2, 0.6, 3), -3.0, 0.0, M['plate'], bevel=0.35, seg=3)
for side in (-1, 1):
    x0 = side * (W / 2 + 0.35); x1 = side * (W / 2 + EAR)
    xa, xb = min(x0, x1), max(x0, x1)
    ear = prism('Ear%d' % side, rrect(xa, -H / 2, xb, H / 2, 0.6, 3), -3.2, -0.25, M['ear'], bevel=0.3)
    for sy in (-1, 1):
        cx = (xa + xb) / 2 + side * 1.5; cy = sy * 35.5
        cut = prism('Slot', rrect(cx - 6.2, cy - 2.4, cx + 6.2, cy + 2.4, 2.4, 8), -6, 3, None)
        md = ear.modifiers.new('Slot', 'BOOLEAN'); md.operation = 'DIFFERENCE'; md.object = cut
        try: md.solver = 'EXACT'
        except Exception: pass
        cut.hide_render = True; cut.hide_viewport = True
    # move bevel after booleans
    bv = ear.modifiers['Bevel']; ear.modifiers.move(ear.modifiers.find('Bevel'), len(ear.modifiers) - 1)

# printed inset border
bx0, by0, bx1, by1, br = L['border']
pts = [(p[0], p[1]) for p in rrect(bx0 - W / 2, H / 2 - by1, bx1 - W / 2, H / 2 - by0, br, 8)]
tube_curve('Border', pts, 0.19, 0.0, M['ink'])

# screws (Phillips pan head, random slot angles)
random.seed(3)
def screw(x, y, r=2.55):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=48, v_segments=24, radius=r)
    for v in bm.verts:
        if v.co.z < 0: v.co.z = 0
        else: v.co.z *= 0.34
    ob = mesh_obj('Screw', bm, M['nickel'], smooth=True)
    ob.location = P(x, y, 0.0)
    a = random.uniform(0, math.pi)
    for k in range(2):
        c = prism('Cross', [(-r * 0.62, -0.30), (r * 0.62, -0.30), (r * 0.62, 0.30), (-r * 0.62, 0.30)], 0.35, 3.0, None)
        c.location = ob.location; c.rotation_euler.z = a + k * math.pi / 2
        md = ob.modifiers.new('X', 'BOOLEAN'); md.operation = 'DIFFERENCE'; md.object = c
        c.hide_render = True; c.hide_viewport = True
    return ob
for (x, y) in L['screws']: screw(x, y)

# VU meter: bezel, face hole, lower lip, zero screw
mx0, my0, mx1, my1 = L['meter_bezel']; ox0, oy0, ox1, oy1 = L['meter_open']
bez = prism('Bezel', rrect(mx0 - W / 2, H / 2 - my1, mx1 - W / 2, H / 2 - my0, 1.4, 6), -0.5, 2.4, M['blackplastic'], bevel=0.7, seg=4)
cut = prism('BezelCut', rrect(ox0 - W / 2, H / 2 - oy1, ox1 - W / 2, H / 2 - oy0, 0.5, 4), 0.2, 4, None)
md = bez.modifiers.new('Open', 'BOOLEAN'); md.operation = 'DIFFERENCE'; md.object = cut; cut.hide_render = True; cut.hide_viewport = True
bez.modifiers.move(bez.modifiers.find('Open'), 0)
fx0, fy0, fx1, fy1 = L['meter_face']
face = prism('MeterFace', [(fx0 - W / 2, H / 2 - fy1), (fx1 - W / 2, H / 2 - fy1), (fx1 - W / 2, H / 2 - fy0), (fx0 - W / 2, H / 2 - fy0)], 0.0, 0.35, M['meterhole'])
lip = prism('MeterLip', rrect(ox0 - W / 2 + 0.05, H / 2 - oy1 + 0.05, ox1 - W / 2 - 0.05, H / 2 - fy1 - 0.05, 0.3, 3), 0.0, 1.0, M['darkgrey'], bevel=0.25)
ms = screw(L['meter_screw'][0], L['meter_screw'][1], r=1.25); ms.location.z = 1.0
ms.data.materials[0] = M['blackplastic']

# badge + legends
bx0, by0, bx1, by1 = L['badge']
badge = prism('Badge', rrect(bx0 - W / 2, H / 2 - by1, bx1 - W / 2, H / 2 - by0, 0.8, 4), 0.0, 0.9, M['badge'], bevel=0.35, seg=3)
text('BadgeOL', 'OL', (bx0 + bx1) / 2, (by0 + by1) / 2 + 0.2, 8.0, M['badgeink'], z=0.9, extrude=0.18, spacing=0.92)
text('Model', 'OL-4', 49.4, 68.7, 3.3, M['ink'], align='LEFT', spacing=1.25)
text('Desc', 'COMPRESSOR/LIMITER', 49.4, 74.6, 3.4, M['ink'], align='LEFT', spacing=1.0)

def label_with_bullet(s, x, y, cap, hollow):
    ob = text('Lbl_' + s, s, x + 1.2, y, cap, M['ink'], spacing=1.06)
    bpy.context.view_layer.update()
    w = ob.dimensions.x
    bx = x + 1.2 - w / 2 - 2.0
    pts = circle(0.55, 32)
    if hollow:
        tube_curve('Ring', [(px + P(bx, y).x, py + P(bx, y).y) for px, py in circle(0.62, 40)], 0.13, 0.0, M['ink'])
    else:
        prism('Dot', [(px + P(bx, y).x, py + P(bx, y).y) for px, py in pts], 0.0, 0.01, M['ink'])

for key, top, bot in (('knob_thr', 'THRESHOLD', 'RATIO'), ('knob_out', 'OUTPUT LEVEL', 'METER')):
    kx, ky = L[key]
    label_with_bullet(top, kx, 11.3, 2.35, False)
    label_with_bullet(bot, kx, 16.0, 2.35, True)
    for v in range(11):
        a = math.radians(knob_angle(v))
        r = L['scale_r']
        text('N%d' % v, str(v), kx + r * math.sin(a), ky - r * math.cos(a), 2.25, M['ink'], bold=False)

text('Stereo', 'STEREO', L['tog_st'][0], 66.6, 2.3, M['ink'], spacing=1.06)
text('Off', 'OFF', L['tog_st'][0] - 9.2, 73.1, 2.1, M['ink'], spacing=1.06)
text('On', 'ON', L['tog_st'][0] + 8.2, 73.1, 2.1, M['ink'], spacing=1.06)
text('Ovl', 'OVERLOAD', L['led_ovl'][0], 66.9, 2.3, M['ink'], spacing=1.06)
text('Pwr', 'POWER', L['tog_pwr'][0], 44.1, 2.1, M['ink'], spacing=1.04)

# zero-adjust hole
zh = prism('ZeroHole', circle(1.05, 40, P(*L['zero_hole']).x, P(*L['zero_hole']).y), -1.5, 0.005, M['hole'])

# ------------------------------------------------------------------ concentric controls
def skirt(name, kx, ky, legends):
    root = bpy.data.objects.new(name, None); link(root); root.location = P(kx, ky, 0)
    d = prism(name + '_disc', circle(L['skirt_r'], 128), 0.55, 2.05, M['acrylic'], smooth_sides=True, bevel=0.35, seg=3)
    d.parent = root; d.location = (0, 0, 0)
    for i, s in enumerate(legends):
        a = math.radians((i - (len(legends) - 1) / 2) * LEG_STEP)
        r = 12.55
        t = text(name + '_leg' + s, s, r * math.sin(a), r * math.cos(a), 1.95, M['skirtink'], z=2.055, parent=root, rot=-a, spacing=1.0, bold=False)
    return root

def knob(name, kx, ky):
    root = bpy.data.objects.new(name, None); link(root); root.location = P(kx, ky, 2.05)
    N = 36; R = L['knob_r']; pts = []
    for i in range(N * 2):
        a = 2 * math.pi * i / (N * 2)
        rr = R if i % 2 == 0 else R - 0.42
        pts.append((rr * math.cos(a), rr * math.sin(a)))
    body = prism(name + '_body', pts, 0.0, 7.6, M['knobplastic'], bevel=0.12, seg=2)
    body.parent = root
    collar = prism(name + '_top', circle(R - 0.45, 96), 7.6, 8.3, M['knobplastic'], smooth_sides=True, bevel=0.45, seg=4)
    collar.parent = root
    cap = prism(name + '_cap', circle(L['cap_r'], 96), 8.3, 8.95, spun, smooth_sides=True, bevel=0.3, seg=4)
    cap.parent = root
    ring = tube_curve(name + '_mark', [(x, y + 2.15) for x, y in circle(0.78, 40)], 0.17, 0.0, M['ink'], flatten=0.3)
    ring.parent = root; ring.location = (0, 0, 8.97)
    return root

sk_thr = skirt('SkirtThr', *L['knob_thr'], RATIO_LEG)
sk_out = skirt('SkirtOut', *L['knob_out'], METER_LEG)
kn = knob('Knob', *L['knob_thr'])            # the one rendered knob (film strip); hidden for the plate

def set_skirt(root, idx, n):
    a = (idx - (n - 1) / 2) * LEG_STEP        # put legend idx at 12 o'clock
    root.rotation_euler.z = math.radians(a)

def set_knob(root, v):
    root.rotation_euler.z = -math.radians(knob_angle(v))

# ------------------------------------------------------------------ toggles and LEDs
def toggle(name, x, y, axis):
    root = bpy.data.objects.new(name, None); link(root); root.location = P(x, y, 0)
    wsh = prism(name + '_washer', circle(3.4, 64), 0.0, 0.35, M['nickel'], smooth_sides=True, bevel=0.1)
    wsh.parent = root
    hexp = [(3.0 * math.cos(math.radians(30 + 60 * i)), 3.0 * math.sin(math.radians(30 + 60 * i))) for i in range(6)]
    nut = prism(name + '_nut', hexp, 0.35, 1.75, M['chrome'], bevel=0.2, seg=2); nut.parent = root
    bush = prism(name + '_bush', circle(2.05, 64), 1.75, 3.4, M['chrome'], smooth_sides=True, bevel=0.15); bush.parent = root
    piv = bpy.data.objects.new(name + '_pivot', None); link(piv); piv.parent = root; piv.location = (0, 0, 2.9)
    bm = bmesh.new()
    segs = 48; L0 = 8.6
    for k in (0, 1):
        r = 1.35 if k == 0 else 0.95
        for i in range(segs):
            a = 2 * math.pi * i / segs
            bm.verts.new((r * math.cos(a), r * math.sin(a), k * L0))
    bm.verts.ensure_lookup_table()
    for i in range(segs):
        f = bm.faces.new((bm.verts[i], bm.verts[(i + 1) % segs], bm.verts[segs + (i + 1) % segs], bm.verts[segs + i])); f.smooth = True
    lever = mesh_obj(name + '_lever', bm, M['chrome']); lever.parent = piv
    bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=1.45)
    tip = mesh_obj(name + '_tip', bm, M['chrome'], smooth=True); tip.parent = piv; tip.location = (0, 0, L0)
    root['axis'] = axis
    return root, piv

def set_toggle(piv, axis, on):
    t = math.radians(28)
    if axis == 'h':   # stereo: ON = lever to the right
        piv.rotation_euler = (0, t if on else -t, 0)
    else:             # power: ON = lever up
        piv.rotation_euler = (-t if on else t, 0, 0)

tog_st, piv_st = toggle('TogSt', *L['tog_st'], 'h')
tog_pw, piv_pw = toggle('TogPw', *L['tog_pwr'], 'v')

def led(name, x, y):
    root = bpy.data.objects.new(name, None); link(root); root.location = P(x, y, 0)
    pts_o = circle(2.45, 64); pts_i = circle(1.7, 64)
    bez = prism(name + '_bez', pts_o, 0.0, 1.0, M['blackplastic'], smooth_sides=True, bevel=0.25); bez.parent = root
    bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=40, v_segments=20, radius=1.65)
    for v in bm.verts:
        if v.co.z < 0: v.co.z = 0
        else: v.co.z *= 0.62
    lens = mesh_obj(name + '_lens', bm, M['ledoff'], smooth=True); lens.parent = root; lens.location = (0, 0, 0.95)
    return lens

led_pw = led('LedPw', *L['led_pwr'])
led_ov = led('LedOv', *L['led_ovl'])
def set_led(lens, on):
    lens.data.materials[0] = M['ledon'] if on else M['ledoff']

# ------------------------------------------------------------------ states
DEFAULT = {'ratio': 1, 'meter': 0, 'power': True, 'stereo': True, 'pwrled': True, 'ovl': False}
def apply(state):
    set_skirt(sk_thr, state['ratio'], 5)
    set_skirt(sk_out, state['meter'], 3)
    set_toggle(piv_pw, 'v', state['power'])
    set_toggle(piv_st, 'h', state['stereo'])
    set_led(led_pw, state['pwrled']); set_led(led_ov, state['ovl'])

def objs_under(root):
    res = [root]
    for c in root.children: res += objs_under(c)
    return res
knob_objs = objs_under(kn)

def px(x, y):
    """faceplate mm -> image pixels (origin top-left of the render)"""
    return ((x + EAR) * PPM, y * PPM)

def render_to(path, border=None):
    r = scn.render
    if border:
        x0, y0, x1, y1 = border  # pixels, y down
        Wp, Hp = r.resolution_x, r.resolution_y
        r.use_border = True; r.use_crop_to_border = True
        r.border_min_x = x0 / Wp; r.border_max_x = x1 / Wp
        r.border_min_y = 1 - y1 / Hp; r.border_max_y = 1 - y0 / Hp
    else:
        r.use_border = False
    r.filepath = path
    bpy.ops.render.render(write_still=True)

def crop_box(cx, cy, half):
    x, y = px(cx, cy); h = half * PPM
    return (int(math.floor(x - h)), int(math.floor(y - h)), int(math.ceil(x + h)), int(math.ceil(y + h)))

layout = {'ppm': PPM, 'width': scn.render.resolution_x, 'height': scn.render.resolution_y, 'crops': {}, 'mm': L,
          'ear': EAR, 'arc': ARC}
fx0, fy0, fx1, fy1 = L['meter_face']
layout['face'] = [px(fx0, fy0)[0], px(fx0, fy0)[1], px(fx1, fy1)[0], px(fx1, fy1)[1]]
for k in ('knob_thr', 'knob_out', 'tog_st', 'tog_pwr', 'led_pwr', 'led_ovl'):
    layout[k] = list(px(*L[k]))

CROPS = []
for i in range(5): CROPS.append(('skirt_thr_%d' % i, 'knob_thr', 17.2, {'ratio': i}))
for j in range(3): CROPS.append(('skirt_out_%d' % j, 'knob_out', 17.2, {'meter': j}))
for on in (0, 1):
    CROPS.append(('tog_pwr_%d' % on, 'tog_pwr', 8.5, {'power': bool(on)}))
    CROPS.append(('tog_st_%d' % on, 'tog_st', 8.5, {'stereo': bool(on)}))
    CROPS.append(('led_pwr_%d' % on, 'led_pwr', 4.8, {'pwrled': bool(on)}))
    CROPS.append(('led_ovl_%d' % on, 'led_ovl', 4.8, {'ovl': bool(on)}))

for o in knob_objs: o.hide_render = True

if 'plate' in PARTS:
    apply(DEFAULT)
    render_to(os.path.join(OUT, 'plate.png'))
if 'crops' in PARTS:
    for name, at, half, change in CROPS:
        st = dict(DEFAULT); st.update(change); apply(st)
        box = crop_box(*L[at], half)
        layout['crops'][name] = box
        render_to(os.path.join(OUT, 'crop_%s.png' % name), box)
    apply(DEFAULT)
else:
    for name, at, half, change in CROPS: layout['crops'][name] = crop_box(*L[at], half)

if 'knob' in PARTS:
    # inner knob film strip: only the knob + a shadow catcher on the skirt surface
    hidden = []
    for o in scn.objects:
        if o.type in ('MESH', 'CURVE', 'FONT') and o not in knob_objs and not o.hide_render:
            o.hide_render = True; hidden.append(o)
    for o in knob_objs: o.hide_render = False
    bm = bmesh.new(); bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=30)
    catcher = mesh_obj('Catcher', bm, M['plate']); catcher.location = P(*L['knob_thr'], 2.05)
    catcher.is_shadow_catcher = True
    # the skirt under the knob is clear acrylic over aluminium: catcher stands in for it
    box = crop_box(*L['knob_thr'], 11.6)
    layout['knob_box'] = [box[0] - px(*L['knob_thr'])[0], box[1] - px(*L['knob_thr'])[1], box[2] - box[0], box[3] - box[1]]
    scn.cycles.samples = max(48, SAMPLES // 3)
    steps = int(ARGS[4]) if len(ARGS) > 4 else 101
    for f in range(steps):
        v = 10.0 * f / (steps - 1)
        set_knob(kn, v)
        render_to(os.path.join(OUT, 'knob', '%04d.png' % f), box)
    layout['knob_frames'] = steps
    for o in hidden: o.hide_render = False

with open(os.path.join(OUT, 'layout.json'), 'w') as fh:
    json.dump(layout, fh, indent=1)
print('DONE', PARTS)
