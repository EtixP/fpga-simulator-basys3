"""Builds a photoreal Basys 3 in Blender for tools/board_render.py.

An original model in millimetres (see measured.py for the coordinates). build()
creates the scene; set_switch/set_led/set_button/set_segment change the live parts.
Logos of Digilent, AMD/Xilinx and others are deliberately not reproduced.
"""
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measured as M  # noqa: E402

TOP = M.TOP
SILK_Z = TOP + 0.03
OBJ = {}
MAT = {}
RNG = random.Random(3)


# ----------------------------------------------------------------------------- materials

def linear(hexcolor):
    h = hexcolor.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in c) + (1.0,)


def material(name, color, rough=0.5, metal=0.0, coat=0.0, coat_rough=0.08, emission=None,
             strength=0.0, transmission=0.0, spec=0.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value = linear(color)
    p.inputs['Roughness'].default_value = rough
    p.inputs['Metallic'].default_value = metal
    p.inputs['Specular IOR Level'].default_value = spec
    p.inputs['Coat Weight'].default_value = coat
    p.inputs['Coat Roughness'].default_value = coat_rough
    p.inputs['Transmission Weight'].default_value = transmission
    if emission:
        p.inputs['Emission Color'].default_value = linear(emission)
        p.inputs['Emission Strength'].default_value = strength
    MAT[name] = m
    return m


def grain(m, scale, amount):
    """Subtle unevenness in colour and gloss, as on a real solder mask."""
    nt = m.node_tree
    p = nt.nodes['Principled BSDF']
    coord = nt.nodes.new('ShaderNodeTexCoord')
    noise = nt.nodes.new('ShaderNodeTexNoise')
    noise.inputs['Scale'].default_value = scale
    noise.inputs['Detail'].default_value = 6.0
    nt.links.new(coord.outputs['Object'], noise.inputs['Vector'])
    tint = nt.nodes.new('ShaderNodeMix')
    tint.data_type = 'RGBA'
    tint.blend_type = 'MULTIPLY'
    tint.inputs['Factor'].default_value = 1.0
    tint.inputs['A'].default_value = p.inputs['Base Color'].default_value
    ramp = nt.nodes.new('ShaderNodeMapRange')
    ramp.inputs['To Min'].default_value = 1 - amount
    ramp.inputs['To Max'].default_value = 1 + amount
    nt.links.new(noise.outputs['Fac'], ramp.inputs['Value'])
    nt.links.new(ramp.outputs['Result'], tint.inputs['B'])
    nt.links.new(tint.outputs['Result'], p.inputs['Base Color'])
    rough = nt.nodes.new('ShaderNodeMapRange')
    rough.inputs['To Min'].default_value = 0.36
    rough.inputs['To Max'].default_value = 0.5
    nt.links.new(noise.outputs['Fac'], rough.inputs['Value'])
    nt.links.new(rough.outputs['Result'], p.inputs['Roughness'])
    return m


def make_materials():
    grain(material('mask', '#083c7c', rough=0.42, coat=0.15, coat_rough=0.12, spec=0.3), 0.6, 0.06)
    material('mask_cu', '#155096', rough=0.38, coat=0.15, coat_rough=0.12, spec=0.3)
    material('fr4', '#b8a978', rough=0.8)
    material('silk', '#f1f1ec', rough=0.6)
    material('gold', '#f0c05a', rough=0.22, metal=1.0)
    material('tin', '#d9d9d6', rough=0.28, metal=1.0)
    material('nickel', '#c9c8c4', rough=0.2, metal=1.0)
    material('steel_dark', '#8e9196', rough=0.35, metal=1.0)
    material('black', '#141415', rough=0.5)
    material('black_gloss', '#0d0d0e', rough=0.25)
    material('switch_black', '#171819', rough=0.55)
    material('switch_amber', '#7a4a1a', rough=0.5)
    material('chip', '#19191b', rough=0.42)
    material('ink', '#b9b9b6', rough=0.7)
    material('disp_body', '#d6d6d1', rough=0.5)
    material('disp_face', '#202122', rough=0.32)
    material('seg_off', '#a8a8a2', rough=0.35)
    material('seg_on', '#ff1a0a', rough=0.35, emission='#ff0c00', strength=4.0)
    material('led_off', '#efe9cf', rough=0.18, transmission=0.35)
    material('led_die', '#c99a3c', rough=0.4)
    material('led_green', '#5cff6a', rough=0.18, emission='#00ff12', strength=9.0)
    material('led_red', '#ff4030', rough=0.18, emission='#ff0a00', strength=9.0)
    material('jumper_blue', '#2457d6', rough=0.4)
    material('btn_body', '#22364c', rough=0.4)
    material('btn_white', '#e8e6df', rough=0.5)
    material('plunger', '#111112', rough=0.45)
    material('plunger_red', '#e3442f', rough=0.4)
    material('cap_tan', '#c9a76a', rough=0.5)
    material('res_black', '#1b1b1c', rough=0.45)
    material('rubber', '#d8dde0', rough=0.6, transmission=0.4)


# ----------------------------------------------------------------------------- geometry

def link(name, mesh_or_curve, mat, loc=(0, 0, 0), rot_z=0.0, smooth=False):
    ob = bpy.data.objects.new(name, mesh_or_curve)
    bpy.context.scene.collection.objects.link(ob)
    ob.location = loc
    ob.rotation_euler.z = math.radians(rot_z)
    if mat is not None:
        mesh_or_curve.materials.append(MAT[mat] if isinstance(mat, str) else mat)
    if smooth and isinstance(mesh_or_curve, bpy.types.Mesh):
        for poly in mesh_or_curve.polygons:
            poly.use_smooth = abs(poly.normal.z) < 0.99
    OBJ[name] = ob
    return ob


def bm_to_mesh(name, bm):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    return me


def box(name, cx, cy, z0, sx, sy, sz, mat, bevel=0.0, rot=0.0):
    """A box sitting on z0, centred on (cx, cy)."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * sx, v.co.y * sy, v.co.z * sz + sz / 2))
    if bevel:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=min(bevel, sx / 2.2, sy / 2.2, sz / 2.2),
                        segments=2, profile=0.5, affect='EDGES')
    return link(name, bm_to_mesh(name, bm), mat, (cx, cy, z0), rot, smooth=bevel > 0)


def cylinder(name, cx, cy, z0, r, h, mat, seg=32, r_top=None, axis='Z', bevel=0.0):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r,
                          radius2=r if r_top is None else r_top, depth=h)
    for v in bm.verts:
        v.co.z += h / 2
    if bevel:
        caps = [e for e in bm.edges if all(abs(abs(v.co.z - h / 2) - h / 2) < 1e-6 for v in e.verts)
                and abs(e.verts[0].co.z - e.verts[1].co.z) < 1e-6]
        bmesh.ops.bevel(bm, geom=caps, offset=bevel, segments=2, affect='EDGES')
    ob = link(name, bm_to_mesh(name, bm), mat, (cx, cy, z0), smooth=True)
    if axis == 'Y':
        ob.rotation_euler.x = math.radians(-90)
    elif axis == 'X':
        ob.rotation_euler.y = math.radians(90)
    return ob


def prism(name, pts, z0, h, mat, bevel=0.0):
    """Extrudes a counter-clockwise polygon (x, y) from z0 up by h."""
    bm = bmesh.new()
    bottom = [bm.verts.new((x, y, z0)) for x, y in pts]
    top = [bm.verts.new((x, y, z0 + h)) for x, y in pts]
    bm.faces.new(list(reversed(bottom)))
    bm.faces.new(top)
    n = len(pts)
    for i in range(n):
        bm.faces.new((bottom[i], bottom[(i + 1) % n], top[(i + 1) % n], top[i]))
    if bevel:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=2, affect='EDGES')
    return link(name, bm_to_mesh(name, bm), mat)


def ring(name, cx, cy, z, r_in, r_out, mat, h=0.035, seg=40):
    bm = bmesh.new()
    inner_b, outer_b, inner_t, outer_t = [], [], [], []
    for i in range(seg):
        a = 2 * math.pi * i / seg
        c, s = math.cos(a), math.sin(a)
        inner_b.append(bm.verts.new((r_in * c, r_in * s, 0)))
        outer_b.append(bm.verts.new((r_out * c, r_out * s, 0)))
        inner_t.append(bm.verts.new((r_in * c, r_in * s, h)))
        outer_t.append(bm.verts.new((r_out * c, r_out * s, h)))
    for i in range(seg):
        j = (i + 1) % seg
        bm.faces.new((outer_t[i], outer_t[j], inner_t[j], inner_t[i]))
        bm.faces.new((outer_b[j], outer_b[i], inner_b[i], inner_b[j]))
        bm.faces.new((outer_b[i], outer_b[j], outer_t[j], outer_t[i]))
        bm.faces.new((inner_b[j], inner_b[i], inner_t[i], inner_t[j]))
    return link(name, bm_to_mesh(name, bm), mat, (cx, cy, z), smooth=True)


def strip(name, points, width, z, h, mat):
    """A flat ribbon along a polyline (traces, silkscreen lines)."""
    bm = bmesh.new()
    prev = None
    for k in range(len(points) - 1):
        (x0, y0), (x1, y1) = points[k], points[k + 1]
        d = Vector((x1 - x0, y1 - y0, 0))
        if d.length < 1e-6:
            continue
        d.normalize()
        nrm = Vector((-d.y, d.x, 0)) * (width / 2)
        ext = d * (width / 2 if k == 0 else 0)
        ext2 = d * (width / 2)
        a = Vector((x0, y0, z)) - ext
        b = Vector((x1, y1, z)) + ext2
        quad = [a + nrm, a - nrm, b - nrm, b + nrm]
        vs = [bm.verts.new(p) for p in quad] + [bm.verts.new(p + Vector((0, 0, h))) for p in quad]
        bm.faces.new((vs[4], vs[5], vs[6], vs[7]))
        bm.faces.new((vs[0], vs[1], vs[5], vs[4]))
        bm.faces.new((vs[1], vs[2], vs[6], vs[5]))
        bm.faces.new((vs[2], vs[3], vs[7], vs[6]))
        bm.faces.new((vs[3], vs[0], vs[4], vs[7]))
    return link(name, bm_to_mesh(name, bm), mat)


FONT = None


def text(s, x, y, size, mat='silk', align='CENTER', rot=0.0, z=SILK_Z, bold=0.0, name=None):
    global FONT
    if FONT is None:
        fonts = bpy.utils.system_resource('DATAFILES', path='fonts')
        FONT = bpy.data.fonts.load(os.path.join(fonts, 'Inter.woff2'))
    cu = bpy.data.curves.new(name or ('t_' + s), 'FONT')
    cu.body = s
    cu.font = FONT
    cu.size = size
    cu.align_x = align
    cu.align_y = 'CENTER'
    cu.offset = bold if bold or mat != 'silk' else 0.012
    cu.extrude = 0.008
    ob = link(name or ('t_' + s), cu, mat, (x, y, z), rot)
    return ob


def silk_line(points, w=0.16, z=SILK_Z - 0.02):
    return strip('silk_line', points, w, z, 0.025, 'silk')


def silk_rect(cx, cy, w, h, lw=0.16):
    x0, x1, y0, y1 = cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2
    silk_line([(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)], lw)


def tag(s, x, y, size=1.05, rot=0.0):
    """White silkscreen box with the label knocked out in mask blue."""
    w = len(s) * size * 0.66 + size * 0.7
    h = size * 1.4
    vertical = abs(rot) == 90
    box('tag_' + s, x, y, SILK_Z - 0.02, h if vertical else w, w if vertical else h, 0.025, 'silk')
    text(s, x, y, size, mat='mask', rot=rot, z=SILK_Z + 0.02, bold=0.012)


# ----------------------------------------------------------------------------- the board

def pcb():
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * M.BOARD_W, v.co.y * M.BOARD_H, v.co.z * M.BOARD_T))
    me = bm_to_mesh('pcb', bm)
    ob = link('pcb', me, 'mask')
    me.materials.append(MAT['fr4'])
    for poly in me.polygons:
        poly.material_index = 0 if abs(poly.normal.z) > 0.5 else 1
    # mounting holes cut through
    cutters = bpy.data.collections.new('cutters')
    for i, (x, y) in enumerate(M.MOUNTING_HOLES):
        c = cylinder(f'cut{i}', x, y, -2, 1.905, 4, None, seg=48)
        bpy.context.scene.collection.objects.unlink(c)
        cutters.objects.link(c)
    mod = ob.modifiers.new('holes', 'BOOLEAN')
    mod.operation = 'DIFFERENCE'
    mod.operand_type = 'COLLECTION'
    mod.collection = cutters
    for i, (x, y) in enumerate(M.MOUNTING_HOLES):
        ring(f'mh_ring{i}', x, y, TOP - 0.01, 1.905, 2.35, 'tin', h=0.03)
    for i, (x, y) in enumerate(M.FEET):
        cylinder(f'foot{i}', x, y, -TOP - 3.69, 4.35, 3.69, 'rubber', seg=48, r_top=4.0)
    # plated pin holes with their pads
    for i, (x, y, r) in enumerate(M.HOLES):
        if r > 1.0:
            continue
        pad = r + (0.42 if r < 0.6 else 0.4)
        ring(f'pad{i}', x, y, TOP, r * 0.82, pad, 'gold', h=0.04)
        cylinder(f'hole{i}', x, y, TOP - 0.5, r * 0.82, 0.52, 'black', seg=20)


def slide_switch(name, x, y, on):
    """Through-hole slide switch: housing, slot, lever (rotated like the board's)."""
    box(name + '_housing', x, y, TOP, 5.7, 11.5, 7.5, 'switch_black', bevel=0.25)
    box(name + '_lip', x, y + 5.75 - 0.55, TOP + 7.5 - 0.02, 5.2, 0.9, 0.04, 'switch_amber', bevel=0.02)
    box(name + '_slot', x, y, TOP + 7.5 - 0.6, 3.6, 8.9, 0.62, 'black_gloss', bevel=0.1)
    lever_y = y - M.SWITCH_TRAVEL / 2 + (M.SWITCH_TRAVEL if on else 0)
    lever = box(name + '_lever', x, lever_y, TOP + 6.9, 3.2, 3.0, 4.6, 'switch_black', bevel=0.22)
    lever['base_y'] = y - M.SWITCH_TRAVEL / 2
    # the V groove on the lever's top
    box(name + '_groove', x, lever_y, TOP + 6.9 + 4.6 - 0.12, 3.0, 0.5, 0.16, 'black_gloss')
    OBJ[name + '_groove']['base_y'] = y - M.SWITCH_TRAVEL / 2
    return lever


def user_switches():
    for i, x in enumerate(M.SWITCH_X):
        slide_switch(f'sw{i}', x, M.SWITCH_Y, False)
        text(f'SW{i}', x - 1.25, -19.95, 1.05, bold=0.01)
        silk_line([(x - 3.15, -21.4), (x - 3.15, -20.75), (x + 3.15, -20.75), (x + 3.15, -21.4)])
        text(f'({M.SWITCH_PINS[i]})', x, -33.55, 0.95)


def led(name, x, y, color='green'):
    """0603 LED, long axis along y: tin terminals, a pale lens over the die."""
    box(name + '_t1', x, y + 0.66, TOP, 0.82, 0.32, 0.3, 'tin', bevel=0.04)
    box(name + '_t2', x, y - 0.66, TOP, 0.82, 0.32, 0.3, 'tin', bevel=0.04)
    box(name + '_body', x, y, TOP, 0.8, 1.04, 0.28, 'led_off', bevel=0.05)
    box(name + '_die', x, y + 0.05, TOP + 0.05, 0.32, 0.3, 0.2, 'led_die')
    lens = box(name + '_lens', x, y, TOP + 0.28, 0.72, 0.98, 0.06, 'led_off', bevel=0.03)
    lens['on_material'] = 'led_' + color
    return lens


def user_leds():
    for i, x in enumerate(M.LED_X):
        led(f'led{i}', x, M.LED_Y)
        text(f'LD{i}', x + 0.25, M.LED_Y + 2.15, 1.0)
        text(f'({M.LED_PINS[i]})', x + 0.95, M.LED_Y - 0.05, 0.95, align='LEFT')
        silk_line([(x - 0.5, M.LED_Y - 1.25), (x + 0.5, M.LED_Y - 1.25)], 0.14)


def tact_button(name, x, y, legs_along_x, plunger='plunger', body='btn_body'):
    rot = 0 if legs_along_x else 90
    # SMD legs
    for sx in (-1, 1):
        for sy in (-1, 1):
            if legs_along_x:
                box(f'{name}_leg{sx}{sy}', x + sx * 3.05, y + sy * 1.55, TOP, 1.4, 0.7, 0.22, 'tin', bevel=0.05)
            else:
                box(f'{name}_leg{sx}{sy}', x + sx * 1.55, y + sy * 3.05, TOP, 0.7, 1.4, 0.22, 'tin', bevel=0.05)
    box(name + '_base', x, y, TOP, 4.5, 4.5, 1.45, body, bevel=0.15)
    box(name + '_cover', x, y, TOP + 1.45, 4.3, 4.3, 0.22, body, bevel=0.12)
    for sx in (-1, 1):
        for sy in (-1, 1):
            cylinder(f'{name}_rivet{sx}{sy}', x + sx * 1.6, y + sy * 1.6, TOP + 1.6, 0.32, 0.14, 'steel_dark', seg=16)
    p = cylinder(name + '_plunger', x, y, TOP + 1.6, 1.25, 2.11, plunger, seg=40, bevel=0.2)
    p['base_z'] = TOP + 1.6
    return p


def buttons():
    labels = [('BTNC (U18)', 0, -3.55, 'CENTER'), ('BTNU\n(T18)', 4.4, 0.0, 'LEFT'),
              ('BTNL\n(W19)', -4.0, 0.0, 'RIGHT'), ('BTNR\n(T17)', 3.9, 0.0, 'LEFT'),
              ('BTND\n(U17)', -4.4, 0.0, 'RIGHT')]
    for i, (x, y, along_x) in enumerate(M.BUTTONS):
        tact_button(f'btn{i}', x, y, along_x)
        silk_rect(x, y, 5.5, 5.5)
        s, dx, dy, align = labels[i]
        text(s, x + dx, y + dy, 1.0, align=align)


def seven_segment():
    cx, cy, w, h = M.DISPLAY
    top = M.DISPLAY_TOP
    box('disp_body', cx, cy, TOP, w, h, top - TOP - 0.25, 'disp_body', bevel=0.12)
    box('disp_face', cx, cy, top - 0.3, w - 0.5, h - 0.5, 0.3, 'disp_face', bevel=0.08)
    for (digit, seg), pts in M.SEGMENTS.items():
        s = prism(f'seg{digit}_{seg}', pts, top - 0.02, 0.03, 'seg_off')
        s['on_material'] = 'seg_on'
    for digit, (x, y) in enumerate(M.DECIMAL_POINTS):
        d = cylinder(f'seg{digit}_dp', x, y, top - 0.02, M.DOT_RADIUS, 0.03, 'seg_off', seg=32)
        d['on_material'] = 'seg_on'
    for k, (x, y) in enumerate(M.COLON):
        cylinder(f'colon{k}', x, y, top - 0.02, M.DOT_RADIUS, 0.03, 'seg_off', seg=32)
    text('DISP1', cx - w / 2 + 1.8, cy + h / 2 + 1.25, 1.05)
    text('CLK100MHz', cx + w / 2 + 6.2, cy + 3.6, 1.0)
    text('(W5)', cx + w / 2 + 4.0, cy + 2.2, 1.0)


def chip(name, x, y, size, height, label_lines, pins_per_side=0):
    box(name, x, y, TOP, size, size, height, 'chip', bevel=0.08)
    if pins_per_side:
        pitch = (size - 1.2) / (pins_per_side - 1)
        for k in range(pins_per_side):
            o = -size / 2 + 0.6 + k * pitch
            for px, py, sx, sy in ((o, -size / 2 - 0.12, 0.22, 0.3), (o, size / 2 + 0.12, 0.22, 0.3),
                                   (-size / 2 - 0.12, o, 0.3, 0.22), (size / 2 + 0.12, o, 0.3, 0.22)):
                box(f'{name}_p', x + px, y + py, TOP, sx, sy, 0.08, 'tin')
    for k, (s, sz) in enumerate(label_lines):
        text(s, x, y + (len(label_lines) - 1) * 0.65 - k * 1.3, sz, mat='ink', z=TOP + height + 0.005,
             name=f'{name}_ink{k}')
    cylinder(name + '_dot', x - size / 2 + 0.9, y + size / 2 - 0.9, TOP + height - 0.02, 0.3, 0.03, 'ink', seg=16)


def chips():
    x, y, size, h = M.FPGA
    chip('fpga', x, y, size, h, [('XC7A35T', 1.05), ('CPG236', 0.85), ('1C', 0.85)])
    text('IC7', x, y - size / 2 - 1.1, 1.0)
    chip('ic1', *M.QFNS['IC1'], 9.0, 0.9, [('FT2232HQ', 0.9), ('USB-UART', 0.75)], pins_per_side=16)
    text('IC1', M.QFNS['IC1'][0] + 5.9, M.QFNS['IC1'][1] - 3.0, 1.0)
    chip('ic2', *M.QFNS['IC2'], 9.0, 0.9, [('PIC24', 0.9), ('USB HOST', 0.75)], pins_per_side=16)
    text('IC2', M.QFNS['IC2'][0] + 4.0, M.QFNS['IC2'][1] - 5.6, 1.0)


def vga():
    x = M.VGA_X
    # black plastic housing behind the front plate, with cut-outs for the board locks
    box('vga_body', x, 28.0, TOP, 30.9, 15.0, 11.8, 'black', bevel=0.3)
    for sx in (-1, 1):
        bx = x + sx * 12.55
        box(f'vga_well{sx}', bx, 24.0, TOP + 11.0, 4.6, 6.6, 0.9, 'black_gloss', bevel=0.1)
        box(f'vga_lock{sx}', bx, 23.2, TOP + 7.0, 2.4, 3.6, 5.0, 'nickel', bevel=0.2)
        cylinder(f'vga_post{sx}', bx, 25.6, TOP + 7.0, 1.0, 5.2, 'nickel', seg=24)
        for k in range(7):
            ring(f'vga_spring{sx}{k}', bx, 25.6, TOP + 8.6 + k * 0.45, 1.0, 1.25, 'nickel', h=0.22, seg=24)
    # front plate, shell and hex standoffs past the board edge
    box('vga_plate', x, 35.85, TOP, 30.9, 0.6, 12.4, 'nickel', bevel=0.1)
    d_top, d_bot = 16.9, 15.2
    z0, zh = TOP + 2.4, 8.2
    pts = [(-d_bot / 2, 36.15), (d_bot / 2, 36.15), (d_top / 2 - 0.4, 42.3), (-d_top / 2 + 0.4, 42.3)]
    prism('vga_shell', [(px + x, py) for px, py in pts], z0, zh, 'nickel', bevel=0.35)
    for sx in (-1, 1):
        bx = x + sx * 12.55
        box(f'vga_standoff{sx}', bx, 39.3, TOP + 3.95, 4.8, 6.2, 5.6, 'nickel', bevel=0.5)
        cylinder(f'vga_thread{sx}', bx, 42.4, TOP + 6.75, 1.25, 1.0, 'black_gloss', axis='Y', seg=24)
    tag('VGA', x + 0.2, 18.9, 1.1)
    text('J1', x - 14.6, 19.6, 1.0)


def usb_a():
    x, y = M.USB_A
    for k, px in enumerate((25.52, 28.02, 30.02, 32.52)):
        box(f'usb_pin{k}', px, 20.6, TOP, 0.6, 2.4, 0.3, 'tin', bevel=0.05)
    box('usb_shell', x, 28.6, TOP, 13.2, 13.9, 6.6, 'nickel', bevel=0.35)
    for sx in (-1, 1):
        box(f'usb_tab{sx}', x + sx * 7.0, 25.26, TOP, 1.2, 2.2, 1.2, 'nickel', bevel=0.2)
        lx = x + sx * 2.9
        prism(f'usb_spring{sx}', [(lx - 1.05, 33.8), (lx + 1.05, 33.8), (lx + 0.55, 29.3), (lx - 0.55, 29.3)],
              TOP + 6.6, 0.12, 'tin', bevel=0.05)
    box('usb_seam', x, 28.6, TOP + 6.6, 0.15, 13.6, 0.03, 'steel_dark')
    tag('USB', x - 6.9, 19.3, 1.05)
    text('J2', x - 8.9, 21.4, 1.0)


def micro_usb():
    x, y = M.MICRO_USB
    box('musb_shell', x, y + 0.4, TOP, 7.6, 5.4, 2.6, 'nickel', bevel=0.3)
    for sx in (-1, 1):
        box(f'musb_tab{sx}', x + sx * 4.0, y - 0.6, TOP, 1.1, 1.6, 0.9, 'nickel', bevel=0.1)
    for k in range(5):
        box(f'musb_pin{k}', x - 1.3 + k * 0.65, y - 2.5, TOP, 0.3, 1.1, 0.12, 'tin')
    tag('PROG', x - 5.2, y - 0.6, 1.0, rot=90)
    text('J4', x - 3.2, y - 3.5, 1.0)


def pmod(name, side, cy):
    """2x6 right-angle socket: black body past the board edge, gold pins bent into holes."""
    edge = side * M.BOARD_W / 2
    body_x = edge + side * (63.32 - 60.96) - side * 6.9 / 2
    box(name + '_body', body_x, cy, TOP, 6.9, 15.2, 5.07, 'black', bevel=0.25)
    inner = edge - side * 4.5          # where the body's back wall sits, inside the edge
    far_x = 50.79 if side > 0 else -50.67
    near_x = 53.33 if side > 0 else -53.21
    for k in range(6):
        py = cy - 6.35 + k * 2.54
        # upper row: long pins reaching the far holes
        x0, x1 = inner, far_x
        box(f'{name}_pin_hi{k}', (x0 + x1) / 2, py, TOP + 3.3, abs(x1 - x0) + 0.32, 0.64, 0.64, 'gold', bevel=0.08)
        box(f'{name}_pin_hiv{k}', x1, py, TOP, 0.64, 0.64, 3.94, 'gold', bevel=0.08)
        # lower row: shorter pins into the near holes
        x2 = near_x
        box(f'{name}_pin_lo{k}', (x0 + x2) / 2, py, TOP + 0.9, abs(x2 - x0) + 0.32, 0.64, 0.64, 'gold', bevel=0.08)
        box(f'{name}_pin_lov{k}', x2, py, TOP, 0.64, 0.64, 1.54, 'gold', bevel=0.08)
    lx, ly = {'JA': (-52.0, 7.9), 'JXADC': (-52.0, 2.2), 'JB': (52.0, 26.1), 'JC': (55.4, -14.4)}[name]
    text(name, lx, ly, 1.2, bold=0.02)
    one_y = cy - 6.35 if name in ('JA', 'JXADC') else cy + 6.35
    text('1', far_x + side * 1.8, one_y, 1.0)
    tag('GND', far_x - side * 3.6, cy + 6.35 - 2.54 * (1 if name in ('JA', 'JXADC') else 4), 0.95)
    tag('3V3', far_x - side * 3.6, cy + 6.35 - 2.54 * (0 if name in ('JA', 'JXADC') else 5), 0.95)


def pmods():
    for name, (side, cy) in M.PMODS.items():
        pmod(name, side, cy)


def header(name, x, y, n, axis):
    length = 2.54 * n
    if axis == 'y':
        box(name + '_base', x, y, TOP, 2.5, length - 0.04, 2.5, 'black', bevel=0.15)
    else:
        box(name + '_base', x, y, TOP, length - 0.04, 2.5, 2.5, 'black', bevel=0.15)
    for k in range(n):
        o = -length / 2 + 1.27 + k * 2.54
        px, py = (x, y + o) if axis == 'y' else (x + o, y)
        box(f'{name}_pin{k}', px, py, TOP + 2.5, 0.64, 0.64, 6.0, 'gold', bevel=0.12)


def jumper(name, x, y):
    box(name + '_shunt', x, y, TOP + 2.55, 2.35, 5.0, 6.6, 'jumper_blue', bevel=0.3)
    box(name + '_grip', x, y + 0.6, TOP + 2.55 + 6.6, 1.6, 2.6, 0.25, 'jumper_blue', bevel=0.1)


def top_edge_parts():
    for name, (x, y, n, axis) in M.HEADERS.items():
        header(name, x, y, n, axis)
    for name, (x, y) in M.JUMPERS.items():
        jumper(name, x, y)
    px, py = M.POWER_SWITCH
    slide_switch('power', px, py, True)
    tag('POWER', px, py - 7.4, 1.05)
    tag('ON', px + 4.3, py + 3.5, 1.0, rot=90)
    tag('OFF', px + 4.3, py - 3.6, 1.0, rot=90)
    x, y = M.PROG_BUTTON
    tact_button('prog', x, y, False, plunger='plunger_red', body='btn_white')
    tag('PROG', x, y - 5.4, 1.0)
    for name, (lx, ly) in M.STATUS_LEDS.items():
        color = 'red' if name == 'LD20' else 'green'
        led(name.lower(), lx, ly, color)
    tag('POWER', -55.0, 26.4, 1.0)
    text('LD20', -53.9, 32.9, 0.9)
    tag('RX', -22.96, 30.6, 1.0)
    tag('TX', -18.96, 30.6, 1.0)
    text('LD17', -24.6, 33.0, 0.9, rot=90)
    text('LD18', -20.6, 33.0, 0.9, rot=90)
    tag('BUSY', 18.74, 29.4, 1.0)
    text('LD16', 18.74, 34.2, 0.9)
    tag('DONE', 51.96, 27.5, 1.0)
    text('LD19', 53.7, 30.0, 0.9, rot=90)
    tag('MODE', 43.2, 31.2, 1.0, rot=90)
    text('JP1', 38.1, 29.4, 0.9, rot=90)
    for k, s in enumerate(('QSPI', 'JTAG', 'USB')):
        tag(s, 40.54, 22.4 - k * 1.55, 0.95)
    tag('EXT', -46.4, 33.0, 0.95, rot=90)
    tag('USB', -46.4, 28.6, 0.95, rot=90)
    text('JP2', -48.96, 25.9, 0.9)
    text('J6', -40.66, 20.1, 0.9)
    tag('EXT', -44.6, 17.74, 0.95)
    tag('GND', -36.6, 17.74, 0.95)
    text('5V Only!', -40.66, 15.6, 0.95)


def passive(name, x, y, kind='cap', vertical=True, size='0603'):
    L, W = (1.6, 0.8) if size == '0603' else (1.0, 0.5) if size == '0402' else (2.0, 1.25)
    sx, sy = (W, L) if vertical else (L, W)
    body = 'cap_tan' if kind == 'cap' else 'res_black'
    box(name, x, y, TOP, sx * 0.98, sy * 0.98, 0.5 if kind == 'cap' else 0.35, body, bevel=0.05)
    for s in (-1, 1):
        ex, ey = (0, s * (L / 2 - 0.15)) if vertical else (s * (L / 2 - 0.15), 0)
        box(f'{name}_e', x + ex, y + ey, TOP, (W if vertical else 0.32), (0.32 if vertical else W),
            0.52 if kind == 'cap' else 0.37, 'tin', bevel=0.04)


def passives():
    # groups around the FPGA (positions read from the reference photo)
    groups = [
        (-10.0, 12.8, 4, 1.25, True, 'cap', ['C46', 'C47', 'C48', 'C45']),
        (6.3, 12.8, 4, 1.25, True, 'cap', ['C61', 'C60', 'C62', 'C63']),
        (-13.7, 6.5, 5, 1.6, True, 'cap', ['C53', 'C102', 'C57', 'C55', 'C103']),
        (6.5, 6.5, 5, 1.5, True, 'cap', ['C86', 'C87', 'C88', 'C84', 'C85']),
        (-10.4, 0.6, 4, 1.45, True, 'cap', ['C78', 'C79', 'C76', 'C77']),
        (6.1, 0.6, 4, 1.45, True, 'cap', ['C95', 'C94', 'C97', 'C96']),
        (-7.9, 15.6, 2, 1.3, True, 'cap', ['C49', 'C50']),
        (6.3, 15.6, 2, 1.3, True, 'cap', ['C65', 'C64']),
        (-8.4, -2.8, 2, 1.6, True, 'cap', ['C80', 'C81']),
        (5.9, -2.8, 2, 1.6, True, 'cap', ['C99', 'C98']),
        (21.4, 14.2, 2, 1.3, True, 'cap', ['C11', 'C12']),
        (35.3, 12.0, 2, 1.3, True, 'cap', ['C8', 'C7']),
    ]
    for gx, gy, n, pitch, vertical, kind, names in groups:
        for k in range(n):
            passive(names[k], gx + k * pitch, gy, kind, vertical)
        w = (n - 1) * pitch + 1.6
        silk_rect(gx + (n - 1) * pitch / 2, gy, w, 2.6, 0.12)
        for k in range(n):
            text(names[k], gx + k * pitch, gy + 2.6, 0.75, rot=90)
    passive('C101', -16.3, 6.5, 'cap', False, '0805')
    passive('C83', 14.8, 6.5, 'cap', True, '0805')
    for k, ry in enumerate((18.3, 17.1, 15.9, 14.7)):
        passive(f'R{102 - k}', 36.3, ry, 'res', False, '0402')
        text(f'R{102 - k}', 38.6, ry, 0.75, align='LEFT')
    for name, (x, y, v) in {'C15': (-31.1, 29.2, False), 'C21': (-27.2, 29.2, False),
                            'C117': (-38.4, 10.6, False), 'C110': (-34.1, 10.6, False),
                            'C109': (-36.4, 9.2, True), 'C124': (-41.3, 8.3, False),
                            'C125': (-41.3, 7.2, False), 'C127': (-41.3, 6.1, False),
                            'C122': (-31.4, 8.3, False), 'C123': (-31.4, 7.2, False),
                            'C111': (-31.4, 6.1, False)}.items():
        passive(name, x, y, 'cap', v)
    for name, (x, y) in {'R162': (-36.2, 3.6), 'R165': (-33.6, 3.6)}.items():
        passive(name, x, y, 'res', True)


def traces():
    """Copper under the mask: short fan-outs to vias, and bundles toward the FPGA."""
    w = 0.2
    z = TOP + 0.001
    vias = []

    def route(points, width=w):
        strip('trace', points, width, z, 0.012, 'mask_cu')

    # FPGA fan-out on all four sides, ending in vias
    fx, fy = M.FPGA[0], M.FPGA[1]
    for side in range(4):
        for k in range(9):
            o = -4.0 + k * 1.0
            if side == 0:
                a, b = (fx + o, fy + 5.0), (fx + o, fy + 6.0 + (k % 3) * 0.7)
            elif side == 1:
                a, b = (fx + o, fy - 5.0), (fx + o, fy - 6.2 - (k % 3) * 0.7)
            elif side == 2:
                a, b = (fx - 5.0, fy + o), (fx - 6.2 - (k % 3) * 0.7, fy + o)
            else:
                a, b = (fx + 5.0, fy + o), (fx + 6.2 + (k % 3) * 0.7, fy + o)
            route([a, b])
            vias.append(b)
    # Pmod pins inward, as parallel bundles that turn 45 degrees
    for name, (side, cy) in M.PMODS.items():
        far = 50.67 if side < 0 else 50.79
        for k in range(6):
            py = cy - 6.35 + k * 2.54
            x0 = side * far
            x1 = side * (far - 3.0)
            jog = (k - 2.5) * 0.55
            route([(x0, py), (x1, py), (x1 - side * 2.0, py + jog * 0.3),
                   (side * (far - 9.5 - abs(k - 2.5) * 0.6), py + jog * 0.3)])
            vias.append((side * (far - 9.5 - abs(k - 2.5) * 0.6), py + jog * 0.3))
    # LED and switch connections to vias between the rows
    for i, x in enumerate(M.LED_X):
        route([(x, M.LED_Y + 0.66), (x, M.LED_Y + 1.4), (x - 1.3, M.LED_Y + 2.7)])
        vias.append((x - 1.3, M.LED_Y + 2.7))
    for i, x in enumerate(M.SWITCH_X):
        vias.append((x + 2.9, -20.0))
    # VGA pins down to the resistor area
    for k in range(5):
        x = M.VGA_X - 4.4 + k * 2.28
        route([(x, 22.49), (x, 20.3), (x - 1.2, 19.1), (x - 1.2, 17.0)])
        vias.append((x - 1.2, 17.0))
    # a quiet field of vias, kept clear of parts and labels
    blocked = [(-45, -15, -11, -3), (17, -16, 41, 4), (-6, 0, 6, 12), (-34, 18, -24, 28), (24, 8, 34, 18),
               (-18, 18, 18, 40), (16, 18, 42, 40), (-64, -36, 64, -15.5), (-64, 3, -46, 28),
               (46, -16, 64, 28), (-52, 24, -36, 36), (36, 20, 56, 36), (-34, 26, -16, 40)]
    for _ in range(90):
        x, y = RNG.uniform(-56, 56), RNG.uniform(-14, 32)
        if any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in blocked):
            continue
        vias.append((x, y))
    for k, (x, y) in enumerate(vias):
        ring(f'via{k}', x, y, TOP + 0.002, 0.15, 0.32, 'mask_cu', h=0.014, seg=20)


def marks():
    # The product name, as printed on the board, in a plain face
    box('logo_box', 4.4, -12.05, SILK_Z - 0.02, 21.5, 4.1, 0.03, 'silk', bevel=0.02)
    text('BASYS 3', 4.4, -12.05, 3.0, mat='mask', z=SILK_Z + 0.02, bold=0.16)
    # our own mark where vendor logos would be
    text('VIRTUALBASYS', 44.6, 5.6, 1.7, bold=0.03)
    text('FPGA TRAINER BOARD', 44.6, 3.4, 1.0)
    text('94V-0', -18.0, 18.0, 0.9)


# ----------------------------------------------------------------------------- scene

def build(resolution_scale=1.0, samples=256):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    make_materials()
    pcb()
    user_switches()
    user_leds()
    buttons()
    seven_segment()
    chips()
    vga()
    usb_a()
    micro_usb()
    pmods()
    top_edge_parts()
    passives()
    traces()
    marks()
    set_led_named('ld20', True)     # the board is powered

    # ground: catches the board's shadow on a transparent film
    ground = box('ground', 0, 0, -M.TOP - 3.69 - 0.01, 600, 600, 0.01, 'silk')
    ground.is_shadow_catcher = True

    # lights: an overhead softbox (as in product photos, it makes metal and gloss
    # read), a soft key from the top left for depth, the studio HDRI as fill
    box_light = bpy.data.lights.new('softbox', 'AREA')
    box_light.shape = 'RECTANGLE'
    box_light.size, box_light.size_y = 420, 300
    box_light.energy = 3.0e5
    softbox = bpy.data.objects.new('softbox', box_light)
    scene.collection.objects.link(softbox)
    softbox.location = (CANVAS_CX, CANVAS_CY, 260)
    softbox.visible_camera = False
    # only metal receives it: dielectrics keep their colour, as under a flagged softbox
    metal = bpy.data.collections.new('metal')
    for ob in scene.collection.objects:
        mats = getattr(ob.data, 'materials', None)
        if mats and mats[0] and mats[0].node_tree.nodes['Principled BSDF'].inputs['Metallic'].default_value > 0.5:
            metal.objects.link(ob)
    softbox.light_linking.receiver_collection = metal
    sun = bpy.data.lights.new('key', 'SUN')
    sun.energy = 3.0
    sun.angle = math.radians(9)
    key = bpy.data.objects.new('key', sun)
    scene.collection.objects.link(key)
    # from the top left: tilted 32 degrees off vertical, heading down-right
    key.rotation_euler = (math.radians(-32), 0, math.radians(40))
    world = bpy.data.worlds.new('studio')
    world.use_nodes = True
    nt = world.node_tree
    env = nt.nodes.new('ShaderNodeTexEnvironment')
    hdri = os.path.join(bpy.utils.system_resource('DATAFILES', path='studiolights/world'), 'studio.exr')
    env.image = bpy.data.images.load(hdri)
    nt.links.new(env.outputs['Color'], nt.nodes['Background'].inputs['Color'])
    nt.nodes['Background'].inputs['Strength'].default_value = 0.45
    scene.world = world

    cam_data = bpy.data.cameras.new('cam')
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = CANVAS_W
    cam_data.clip_start = 1
    cam_data.clip_end = 1000
    cam = bpy.data.objects.new('cam', cam_data)
    scene.collection.objects.link(cam)
    cam.location = (CANVAS_CX, CANVAS_CY, 300)
    scene.camera = cam

    scene.render.engine = 'CYCLES'
    prefs = bpy.context.preferences.addons['cycles'].preferences
    prefs.compute_device_type = 'METAL'
    prefs.refresh_devices()
    for d in prefs.devices:
        d.use = d.type == 'METAL'
    scene.cycles.device = 'GPU'
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.cycles.seed = 7
    scene.render.film_transparent = True
    scene.render.resolution_x = round(CANVAS_W * PX_PER_MM * resolution_scale)
    scene.render.resolution_y = round(CANVAS_H * PX_PER_MM * resolution_scale)
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.image_settings.color_depth = '8'
    # Standard keeps diffuse white white, as silkscreen photographs
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    scene.view_settings.exposure = 0.0
    scene.cycles.sample_clamp_indirect = 4.0
    return scene


# The rendered area: the board plus its overhanging connectors and shadow
CANVAS_W, CANVAS_H = 134.0, 89.0
CANVAS_CX, CANVAS_CY = 0.0, 4.5
PX_PER_MM = 16


# ----------------------------------------------------------------------------- live parts

def set_switch(i, on, prefix='sw'):
    for part in ('_lever', '_groove'):
        ob = OBJ[f'{prefix}{i}{part}']
        ob.location.y = ob['base_y'] + (M.SWITCH_TRAVEL if on else 0)


def set_led_named(name, on):
    lens = OBJ[name + '_lens']
    lens.data.materials[0] = MAT[lens['on_material']] if on else MAT['led_off']
    OBJ[name + '_body'].data.materials[0] = MAT[lens['on_material']] if on else MAT['led_off']


def set_led(i, on):
    set_led_named(f'led{i}', on)


def set_button(i, down):
    p = OBJ[f'btn{i}_plunger']
    p.location.z = p['base_z'] - (0.35 if down else 0)


def set_segment(digit, seg, on):
    ob = OBJ[f'seg{digit}_{seg}']
    ob.data.materials[0] = MAT['seg_on'] if on else MAT['seg_off']


def world_to_pixel(x, y, resolution_scale=1.0):
    s = PX_PER_MM * resolution_scale
    return ((x - (CANVAS_CX - CANVAS_W / 2)) * s, ((CANVAS_CY + CANVAS_H / 2) - y) * s)


if __name__ == '__main__':
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    out = argv[0] if argv else '/tmp/board.png'
    scale = float(argv[1]) if len(argv) > 1 else 0.5
    samples = int(argv[2]) if len(argv) > 2 else 64
    scene = build(scale, samples)
    scene.render.filepath = out
    bpy.ops.render.render(write_still=True)
