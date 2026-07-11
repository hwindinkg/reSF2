#!/usr/bin/env python3
"""Simulate render_body_model for legs to find the stretch issue."""
import struct
import re
import os
import math

ROOT = "/home/z/my-project"
SKEL_XML = os.path.join(ROOT, "assets/models/skeleton.xml")
BODY_XML = os.path.join(ROOT, "assets/models/body.xml")
BIN_FILE = os.path.join(ROOT, "assets/animations/binary/fists1_stance_idle.bin")

def parse_skeleton_nodes(path):
    with open(path, 'r', encoding='cp1251') as f:
        xml = f.read()
    m = re.search(r'<Nodes>(.*?)</Nodes>', xml, re.DOTALL)
    nodes_xml = m.group(1)
    nodes = {}
    ordered = []
    for m in re.finditer(r'<([\w\-]+)\s+([^>]+?)/>', nodes_xml, re.DOTALL):
        name = m.group(1)
        attrs_str = m.group(2)
        if 'X="' in attrs_str and 'Y="' in attrs_str:
            attrs = dict(re.findall(r'(\w+)="([^"]*)"', attrs_str))
            x = float(attrs['X'])
            y = float(attrs['Y'])
            z = float(attrs.get('Z', '0'))
            nodes[name] = (x, y, z)
            ordered.append(name)
    return nodes, ordered

def parse_edges(path):
    with open(path, 'r', encoding='cp1251') as f:
        xml = f.read()
    edges = {}
    for section in ['Edges']:
        m = re.search(rf'<{section}>(.*?)</{section}>', xml, re.DOTALL)
        if not m: continue
        for em in re.finditer(r'<([\w\-]+)\s+([^>]+?)/>', m.group(1), re.DOTALL):
            name = em.group(1)
            attrs = dict(re.findall(r'(\w+)="([^"]*)"', em.group(2)))
            if attrs.get('Type') in ('Edge', 'Muscle'):
                edges[name] = (attrs.get('End1', ''), attrs.get('End2', ''))
    return edges

def parse_body(path):
    with open(path, 'r', encoding='cp1251') as f:
        xml = f.read()
    # Get body's own Nodes
    body_nodes = {}
    m = re.search(r'<Nodes>(.*?)</Nodes>', xml, re.DOTALL)
    if m:
        for em in re.finditer(r'<([\w\-]+)\s+([^>]+?)/>', m.group(1), re.DOTALL):
            name = em.group(1)
            attrs_str = em.group(2)
            if 'X="' in attrs_str and 'Y="' in attrs_str:
                attrs = dict(re.findall(r'(\w+)="([^"]*)"', attrs_str))
                body_nodes[name] = (float(attrs['X']), float(attrs['Y']), float(attrs.get('Z', '0')))
    # Get body's own Edges
    body_edges = {}
    m = re.search(r'<Edges>(.*?)</Edges>', xml, re.DOTALL)
    if m:
        for em in re.finditer(r'<([\w\-]+)\s+([^>]+?)/>', m.group(1), re.DOTALL):
            name = em.group(1)
            attrs = dict(re.findall(r'(\w+)="([^"]*)"', em.group(2)))
            if attrs.get('Type') in ('Edge', 'Muscle'):
                body_edges[name] = (attrs.get('End1', ''), attrs.get('End2', ''))
    # Get body's MacroNodes
    body_macros = {}
    m = re.search(r'<MacroNodes>(.*?)</MacroNodes>', xml, re.DOTALL)
    if m:
        for em in re.finditer(r'<([\w\-]+)\s+([^>]+?)/>', m.group(1), re.DOTALL):
            name = em.group(1)
            attrs = dict(re.findall(r'(\w+)="([^"]*)"', em.group(2)))
            children = [attrs.get(f'ChildNode{i}', '') for i in range(1, 5)]
            lccs = [float(attrs.get(f'LCC{i}', '0')) for i in range(1, 5)]
            body_macros[name] = (children, lccs)
    # Get capsules
    capsules = []
    m = re.search(r'<Figures>(.*?)</Figures>', xml, re.DOTALL)
    if m:
        for em in re.finditer(r'<([\w\-]+)\s+([^>]+?)/>', m.group(1), re.DOTALL):
            attrs = dict(re.findall(r'(\w+)="([^"]*)"', em.group(2)))
            if attrs.get('Type') == 'Capsule':
                capsules.append({
                    'name': em.group(1),
                    'edge': attrs.get('Edge', ''),
                    'r1': float(attrs.get('Radius1', '0')),
                    'r2': float(attrs.get('Radius2', '0')),
                    'm1': float(attrs.get('Margin1', '0')),
                    'm2': float(attrs.get('Margin2', '0')),
                })
    return body_nodes, body_edges, body_macros, capsules

def parse_bin(path):
    with open(path, 'rb') as f:
        data = f.read()
    fc = struct.unpack('<I', data[:4])[0]
    offset = 4
    frames = []
    for fi in range(fc):
        skip = data[offset]
        nc = struct.unpack('<I', data[offset+1:offset+5])[0]
        offset += 5
        nodes = []
        for i in range(nc):
            x, y, z = struct.unpack('<fff', data[offset:offset+12])
            nodes.append((x, y, -z))
            offset += 12
        frames.append(nodes)
    return fc, frames

# Load everything
skel_nodes, skel_ordered = parse_skeleton_nodes(SKEL_XML)
skel_edges = parse_edges(SKEL_XML)
body_nodes, body_edges, body_macros, capsules = parse_body(BODY_XML)
fc, frames = parse_bin(BIN_FILE)

# Combined edges and nodes
all_edges = {**skel_edges, **body_edges}
all_nodes = {**skel_nodes, **body_nodes}

print(f"Loaded: skeleton {len(skel_nodes)} nodes, {len(skel_edges)} edges")
print(f"        body {len(body_nodes)} nodes, {len(body_edges)} edges, {len(body_macros)} macros, {len(capsules)} capsules")
print(f"        bin {fc} frames, {len(frames[0])} nodes/frame")

# Simulate anim_node_pos_ for frame 0
npivot_idx = skel_ordered.index('NPivot')
npivot_bin = frames[0][npivot_idx]  # (x, y, z)
npivot_rest = skel_nodes['NPivot']
npivot_rest_y = npivot_rest[1]

anim_pos = {}
for i, name in enumerate(skel_ordered):
    if i >= len(frames[0]):
        break
    abs_x, abs_y, abs_z = frames[0][i]
    local_x = abs_x - npivot_bin[0]
    local_y = abs_y - npivot_bin[1]
    anim_pos[name] = (local_x, local_y + npivot_rest_y)

print(f"\nNPivot bin pos: ({npivot_bin[0]:.2f}, {npivot_bin[1]:.2f}, {npivot_bin[2]:.2f})")
print(f"NPivot rest Y: {npivot_rest_y:.2f}")

# Player position
player_x, player_y = -167.0, -93.0
facing_right = True

# Resolve a node to world position
def resolve(name, world_cx, world_cy, face_right, pivot_local_y, anim_pos, depth=0):
    if depth > 5:
        return world_cx, world_cy
    if name in anim_pos:
        lx, ly = anim_pos[name]
        sx = (lx if face_right else -lx) * 0.9
        sy = world_cy + (ly - pivot_local_y) * 0.9
        return world_cx + sx, sy
    if name in body_nodes:
        lx, ly, lz = body_nodes[name]
        sx = (lx if face_right else -lx) * 0.9
        sy = world_cy + (ly - pivot_local_y) * 0.9
        return world_cx + sx, sy
    if name in skel_nodes:
        lx, ly, lz = skel_nodes[name]
        sx = (lx if face_right else -lx) * 0.9
        sy = world_cy + (ly - pivot_local_y) * 0.9
        return world_cx + sx, sy
    if name in body_macros:
        children, lccs = body_macros[name]
        sum_lcc = 0
        wx, wy = 0, 0
        for i, child in enumerate(children):
            if not child: continue
            cx, cy = resolve(child, world_cx, world_cy, face_right, pivot_local_y, anim_pos, depth+1)
            wx += cx * lccs[i]
            wy += cy * lccs[i]
            sum_lcc += lccs[i]
        if abs(sum_lcc) > 1e-6:
            return wx / sum_lcc, wy / sum_lcc
    return world_cx, world_cy

# Find leg capsules
leg_keywords = ['Thigh', 'Calf', 'Foot', 'Toe', 'Instep', 'Heel', 'Groin', 'Pelvis', 'Hip']
leg_caps = [c for c in capsules if any(k in c['edge'] for k in leg_keywords)]
print(f"\n=== Leg capsules: {len(leg_caps)} ===")
print(f"{'capsule':<30} {'edge':<20} {'end1':<15} {'end2':<15} {'(x1,y1)':<20} {'(x2,y2)':<20} {'len':>6}")
for c in leg_caps:
    edge = all_edges.get(c['edge'])
    if not edge:
        print(f"  {c['name']}: MISSING EDGE {c['edge']}")
        continue
    end1, end2 = edge
    x1, y1 = resolve(end1, player_x, player_y, facing_right, npivot_rest_y, anim_pos)
    x2, y2 = resolve(end2, player_x, player_y, facing_right, npivot_rest_y, anim_pos)
    # Apply margin
    m1, m2 = c['m1'], c['m2']
    mx1 = x1 + (x2 - x1) * m1
    my1 = y1 + (y2 - y1) * m1
    mx2 = x2 - (x2 - x1) * m2
    my2 = y2 - (y2 - y1) * m2
    length = math.hypot(mx2 - mx1, my2 - my1)
    r = (c['r1'] + c['r2']) * 0.5 * 0.9
    print(f"  {c['name']:<28} {c['edge']:<20} {end1:<15} {end2:<15} ({mx1:7.2f},{my1:7.2f}) ({mx2:7.2f},{my2:7.2f}) {length:6.2f}  r={r:.1f}")

# Also check: any capsule with length > 100 (suspiciously long)
print("\n=== Suspiciously long capsules (>100 units) ===")
for c in capsules:
    edge = all_edges.get(c['edge'])
    if not edge: continue
    end1, end2 = edge
    x1, y1 = resolve(end1, player_x, player_y, facing_right, npivot_rest_y, anim_pos)
    x2, y2 = resolve(end2, player_x, player_y, facing_right, npivot_rest_y, anim_pos)
    m1, m2 = c['m1'], c['m2']
    mx1 = x1 + (x2 - x1) * m1
    my1 = y1 + (y2 - y1) * m1
    mx2 = x2 - (x2 - x1) * m2
    my2 = y2 - (y2 - y1) * m2
    length = math.hypot(mx2 - mx1, my2 - my1)
    if length > 100:
        print(f"  {c['name']:<30} edge={c['edge']:<20} {end1}->{end2}  len={length:.2f}  ({mx1:.2f},{my1:.2f})->({mx2:.2f},{my2:.2f})")
