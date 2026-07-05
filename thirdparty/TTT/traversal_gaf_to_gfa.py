#!/usr/bin/env python3
"""Convert TTT traversal GAF to GFA format. Outputs:
- traversal.gfa: merged subgraph (all nodes+edges from all paths, no P-lines)
- traversal_path0.gfa, traversal_path1.gfa, ...: one GFA per path

When two consecutive nodes in a traversal path have no edge connecting them
in the original GFA, a synthetic 0M edge is added to preserve path continuity."""

import sys
import re

def parse_gfa(gfa_path):
    nodes = {}
    edges = []  # (from, from_orient, to, to_orient, line)
    for line in open(gfa_path):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split('\t')
        if parts[0] == 'S':
            nodes[parts[1]] = line
        elif parts[0] == 'L':
            edges.append((parts[1], parts[2], parts[3], parts[4], line))
    return nodes, edges

def parse_traversal_gaf(gaf_path):
    paths = []
    for line in open(gaf_path):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split('\t')
        if len(parts) >= 2:
            path_str = parts[1]
            segments = re.findall(r'([<>])([^<>]+)', path_str)
            paths.append([(name, orient == '>') for orient, name in segments])
    return paths

def find_edge(edges, n1, o1, n2, o2):
    """Find an L-line connecting n1(o1) to n2(o2). Returns line or None."""
    for from_n, from_o, to_n, to_o, line in edges:
        if from_n == n1 and from_o == o1 and to_n == n2 and to_o == o2:
            return line
        # reverse complement
        rev_o1 = '+' if o2 == '-' else '-'
        rev_o2 = '+' if o1 == '-' else '+'
        if from_n == n2 and from_o == rev_o1 and to_n == n1 and to_o == rev_o2:
            return line
    return None

def clone_node(line, suffix):
    """Clone a GFA S-line with suffix and halved DP depth."""
    parts = line.split('\t')
    new_name = parts[1] + suffix
    new_parts = [parts[0], new_name]
    # Copy sequence or placeholder
    if len(parts) > 2:
        new_parts.append(parts[2])
    # Copy tags, halving DP depth
    for tag in parts[3:]:
        if tag.startswith('DP:f:'):
            try:
                val = float(tag[5:])
                new_parts.append(f'DP:f:{val / 2.0}')
            except ValueError:
                new_parts.append(tag)
        elif tag.startswith('ll:f:'):
            try:
                val = float(tag[5:])
                new_parts.append(f'll:f:{val / 2.0}')
            except ValueError:
                new_parts.append(tag)
        elif tag.startswith('ll:i:'):
            try:
                val = int(tag[5:])
                new_parts.append(f'll:i:{val // 2}')
            except ValueError:
                new_parts.append(tag)
        elif tag.startswith('fc:f:'):
            try:
                val = float(tag[5:])
                new_parts.append(f'fc:f:{val / 2.0}')
            except ValueError:
                new_parts.append(tag)
        elif tag.startswith('RC:i:'):
            try:
                val = int(tag[5:])
                new_parts.append(f'RC:i:{val // 2}')
            except ValueError:
                new_parts.append(tag)
        else:
            new_parts.append(tag)
    return '\t'.join(new_parts)


def write_per_path_gfas(nodes, original_edges, per_path_defs, per_path_edges, out_base):
    """Write per-path GFA files. Shared nodes are cloned with _TTT<pi> suffix
    and halved depth. Non-shared nodes keep the original name."""
    path_count = len(per_path_defs)

    # Find shared nodes (nodes that appear in more than one path)
    node_paths = {}
    for pi, (_, pnodes) in enumerate(per_path_defs):
        for n, _ in pnodes:
            node_paths.setdefault(n, set()).add(pi)
    shared_nodes = {n for n, paths in node_paths.items() if len(paths) > 1}

    for pi in range(path_count):
        path_name, pnodes = per_path_defs[pi]
        pedges = per_path_edges[pi]
        suffix = f'_TTT{pi}'

        # Build node name mapping: shared nodes get _TTT<pi> suffix
        node_map = {}
        used_nodes = set()
        for n, o in pnodes:
            if n in shared_nodes:
                new_name = n + suffix
                node_map[(n, o)] = (new_name, o)
                used_nodes.add(new_name)
            else:
                node_map[(n, o)] = (n, o)
                used_nodes.add(n)

        # Build S-lines: clone shared nodes with _TTT<pi> suffix
        out_nodes = {}
        for n in used_nodes:
            if suffix in n and n.endswith(suffix):
                orig_name = n[:-len(suffix)]
                if orig_name in nodes:
                    out_nodes[n] = clone_node(nodes[orig_name], suffix)
            else:
                if n in nodes:
                    out_nodes[n] = nodes[n]

        # Build L-lines: remap shared node names (match by name only, direction may differ)
        out_edges = []
        seen_edges = set()
        # Build a name-only lookup for shared nodes
        name_map = {}
        for (n, o), (new_n, new_o) in node_map.items():
            name_map[n] = new_n
        for edge_line in pedges:
            parts = edge_line.split('\t')
            n1, o1 = parts[1], parts[2]
            n3, o3 = parts[3], parts[4]

            new_n1 = name_map.get(n1, n1)
            new_n3 = name_map.get(n3, n3)

            new_edge = f"L\t{new_n1}\t{o1}\t{new_n3}\t{o3}\t{parts[5] if len(parts) > 5 else '0M'}"
            if new_edge not in seen_edges:
                seen_edges.add(new_edge)
                out_edges.append(new_edge)

        out_path = f"{out_base}_path{pi}.gfa" if path_count > 1 else f"{out_base}.gfa"
        with open(out_path, 'w') as f:
            f.write("H\tVN:Z:1.0\n")
            for name in sorted(out_nodes):
                f.write(out_nodes[name] + '\n')
            for e in out_edges:
                f.write(e + '\n')
        print(f"  {out_path}: {len(out_nodes)} nodes, {len(out_edges)} edges"
              + (f" ({len(shared_nodes & set(n for n,_ in pnodes))} cloned as {suffix})" if shared_nodes else ""))

def write_merged_gfa_with_paths(nodes, all_edge_lines, per_path_edges, path_defs, out_path):
    """Write a single GFA with P-lines. Each path has its own ordered edge list.
    Missing edges are filled with 0M synthetic edges."""
    all_nodes = set()
    merged_edges = set()
    for pedges in per_path_edges:
        merged_edges |= set(pedges)
    # Also include any edge between used nodes from original GFA
    # (handled later via path nodes)

    # Collect all nodes referenced in edges
    for e in merged_edges:
        parts = e.split('\t')
        all_nodes.add(parts[1])
        all_nodes.add(parts[3])

    # Also include path nodes (some may not appear in edges if isolated)
    for pdef in path_defs:
        for n, _ in pdef[1]:
            all_nodes.add(n)

    with open(out_path, 'w') as f:
        f.write("H\tVN:Z:1.0\n")
        for name in sorted(all_nodes):
            if name in nodes:
                f.write(nodes[name] + '\n')
        seen_edges = set()
        for pedges in per_path_edges:
            for e in pedges:
                if e not in seen_edges:
                    seen_edges.add(e)
                    f.write(e + '\n')
        for pname, pnodes in path_defs:
            # Build comma-separated segment list for P-line
            segs = []
            for i, (n, o) in enumerate(pnodes):
                orient = '>' if o == '+' else '<'
                segs.append(orient + n)
            f.write("P\t" + pname + "\t" + ",".join(segs) + "\t*\n")
    print(f"  {out_path}: {len(all_nodes)} nodes, {len(seen_edges)} edges, {len(path_defs)} paths")

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 traversal_gaf_to_gfa.py <input.gfa> <traversal.gaf> [--merged|--per-path|--concatenated|--all] [output_basename]")
        print("  --merged:       single GFA with P-lines (0M edges for gaps)")
        print("  --per-path:     one GFA per traversal, shared nodes cloned as _TTT<N>")
        print("  --concatenated: single GFA with all paths as separate components (_TTT<N> clones)")
        print("  --all:          all of the above")
        sys.exit(1)

    gfa_path = sys.argv[1]
    gaf_path = sys.argv[2]
    all_mode = "--all" in sys.argv
    merged_mode = "--merged" in sys.argv or all_mode
    per_path_mode = "--per-path" in sys.argv or all_mode
    cat_mode = "--concatenated" in sys.argv or all_mode

    # Default: if no mode flag, use --all
    if not any(f in sys.argv for f in ["--merged", "--per-path", "--concatenated", "--all"]):
        all_mode = True
        merged_mode = True
        per_path_mode = True
        cat_mode = True

    out_base = gaf_path.replace('.gaf', '')
    if len(sys.argv) > 3 and sys.argv[3] not in ("--merged", "--per-path", "--concatenated", "--all"):
        out_base = sys.argv[3]

    nodes, original_edges = parse_gfa(gfa_path)
    print(f"GFA: {len(nodes)} nodes, {len(original_edges)} edges")

    traversals = parse_traversal_gaf(gaf_path)
    print(f"GAF: {len(traversals)} traversal path(s)")

    all_nodes = set()
    per_path_edges = []    # list of lists of L-line strings (ordered)
    per_path_defs = []     # list of (name, [(node, orient), ...])
    synthetic_count = 0

    for pi, trav in enumerate(traversals):
        path_name = f"traversal_{pi}" if len(traversals) > 1 else "traversal"
        path_edges = []
        path_nodes_with_orient = []

        for i in range(len(trav)):
            name, pos = trav[i]
            o = '+' if pos else '-'
            path_nodes_with_orient.append((name, o))
            all_nodes.add(name)

        for i in range(len(trav) - 1):
            n1, pos1 = trav[i]
            n2, pos2 = trav[i + 1]
            o1 = '+' if pos1 else '-'
            o2 = '+' if pos2 else '-'

            edge = find_edge(original_edges, n1, o1, n2, o2)
            if edge:
                path_edges.append(edge)
            else:
                # Synthetic 0M edge
                synth = f"L\t{n1}\t{o1}\t{n2}\t{o2}\t0M"
                path_edges.append(synth)
                synthetic_count += 1

        per_path_edges.append(path_edges)
        per_path_defs.append((path_name, path_nodes_with_orient))

    if synthetic_count > 0:
        print(f"Added {synthetic_count} synthetic 0M edge(s)")

    if merged_mode:
        # Single merged GFA with P-lines
        out_path = f"{out_base}.gfa" if not out_base.endswith('.gfa') else out_base
        write_merged_gfa_with_paths(nodes, original_edges, per_path_edges, per_path_defs, out_path)

    if per_path_mode or cat_mode:
        # Per-path GFA files with cloned shared nodes (needed by concatenated mode too)
        write_per_path_gfas(nodes, original_edges, per_path_defs, per_path_edges, out_base)

    if cat_mode:
        # Concatenated GFA: all per-path GFAs in one file with _TTT<N> clones
        # (no cross-path edges — each path is its own connected component)
        cat_path = f"{out_base}_concatenated.gfa"
        with open(cat_path, 'w') as fcat:
            fcat.write("H\tVN:Z:1.0\n")
            seen_s = set()
            seen_l = set()
            for pi in range(len(per_path_defs)):
                path_gfa = f"{out_base}_path{pi}.gfa"
                for line in open(path_gfa):
                    line = line.strip()
                    if line.startswith('H') or not line:
                        continue
                    if line.startswith('S'):
                        name = line.split('\t')[1]
                        if name not in seen_s:
                            seen_s.add(name)
                            fcat.write(line + '\n')
                    elif line.startswith('L'):
                        if line not in seen_l:
                            seen_l.add(line)
                            fcat.write(line + '\n')
        print(f"  {cat_path}: {len(seen_s)} nodes, {len(seen_l)} edges (concatenated)")

if __name__ == '__main__':
    main()
