#!/usr/bin/env python3
"""Check if body.xml references any nodes not in skeleton.xml."""
import re
import os
import sys

ROOT = "/home/z/my-project"
SKEL_XML = os.path.join(ROOT, "assets/models/skeleton.xml")
BODY_XML = os.path.join(ROOT, "assets/models/body.xml")

def parse_xml_tags(xml, section_name):
    """Find <section>...</section> and return list of (tag_name, attrs_dict)."""
    m = re.search(rf'<{section_name}>(.*?)</{section_name}>', xml, re.DOTALL)
    if not m:
        return []
    section = m.group(1)
    tags = []
    for m in re.finditer(r'<([\w\-]+)\s+([^>]+?)/>', section, re.DOTALL):
        name = m.group(1)
        attrs_str = m.group(2)
        attrs = dict(re.findall(r'(\w+)="([^"]*)"', attrs_str))
        tags.append((name, attrs))
    return tags

# Parse skeleton.xml
with open(SKEL_XML, 'r', encoding='cp1251') as f:
    skel_xml = f.read()
skel_nodes = parse_xml_tags(skel_xml, 'Nodes')
skel_node_names = set(n[0] for n in skel_nodes)
print(f"skeleton.xml: {len(skel_nodes)} nodes")
print(f"  Names: {sorted(skel_node_names)[:10]}...")

skel_edges = parse_xml_tags(skel_xml, 'Edges')
skel_edge_map = {n: a for n, a in skel_edges}
print(f"skeleton.xml: {len(skel_edges)} edges")

# Parse body.xml
with open(BODY_XML, 'r', encoding='cp1251') as f:
    body_xml = f.read()
body_nodes = parse_xml_tags(body_xml, 'Nodes')
body_node_names = set(n[0] for n in body_nodes)
print(f"body.xml: {len(body_nodes)} nodes")

body_edges = parse_xml_tags(body_xml, 'Edges')
body_edge_map = {n: a for n, a in body_edges}
print(f"body.xml: {len(body_edges)} edges")

# Combined edge map
all_edges = {}
all_edges.update(skel_edge_map)
all_edges.update(body_edge_map)
print(f"Combined edges: {len(all_edges)}")

# Parse body.xml capsules and check edge endpoints
body_capsules = parse_xml_tags(body_xml, 'Figures')
print(f"body.xml: {len(body_capsules)} capsules (Figures)")

# All node names available
all_node_names = skel_node_names | body_node_names

# Check each capsule's edge endpoints
missing_nodes = set()
stretched_capsules = []
for cap_name, cap_attrs in body_capsules:
    if cap_attrs.get('Type') != 'Capsule':
        continue
    edge_name = cap_attrs.get('Edge', '')
    if not edge_name:
        continue
    edge = all_edges.get(edge_name)
    if not edge:
        print(f"  MISSING EDGE: capsule {cap_name} references edge '{edge_name}' not found")
        continue
    end1 = edge.get('End1', '')
    end2 = edge.get('End2', '')
    for end in [end1, end2]:
        if end and end not in all_node_names:
            missing_nodes.add(end)
            stretched_capsules.append((cap_name, edge_name, end))

print(f"\nMissing node references: {len(missing_nodes)}")
for n in sorted(missing_nodes):
    print(f"  - {n}")

print(f"\nStretched capsules (referencing missing nodes): {len(stretched_capsules)}")
for cap, edge, end in stretched_capsules[:20]:
    print(f"  - {cap} (edge {edge}) -> {end}")

# Also check: do all skeleton.xml edges reference existing skeleton nodes?
print("\n=== Skeleton edges with missing endpoints ===")
for edge_name, edge_attrs in skel_edges:
    end1 = edge_attrs.get('End1', '')
    end2 = edge_attrs.get('End2', '')
    for end in [end1, end2]:
        if end and end not in all_node_names:
            print(f"  - edge {edge_name}: endpoint {end} not found")
