#!/usr/bin/env python3
import sys
import argparse
import logging
import math
import os
from src.path_optimizer import PathOptimizer
from src.path_supplementary import get_gaf_string
from src.alignment_scorer import AlignmentScorer
from src.logging_utils import setup_logging, print_warnings_summary
from src.node_id_mapper import NodeIdMapper
from src.MIP_optimizer import MIPOptimizer
from src.tangle import Tangle
from src.input_parsing import (
    parse_gfa, parse_gaf, parse_pairs, read_tangle_nodes, get_oriented_boundaries, identify_tangle_nodes, new_identify_tangle_nodes, read_coverage_file,
    coverage_from_graph, verify_coverage, calculate_median_coverage, clean_tips, parse_boundary_file,
    DETECTED_LOW_MEDIAN_COVERAGE_VARIATION, DETECTED_HIGH_MEDIAN_COVERAGE_VARIATION
)
from src.graph_transformation import (
    get_canonical_rc_vertex,
    create_dual_graph, create_multi_dual_graph,
    get_traversable_subgraph
)

# Create global instances
node_id_mapper = NodeIdMapper()

def optimize_paths(tangle, alignment_scorer: AlignmentScorer, args):
    """Optimize Eulerian paths."""
    best_path = None
    best_score = -1
    logging.info(f"Starting optimization with {args.num_initial_paths} initial paths, max {args.max_iterations} iterations per path.")
    rc_vertex_map = {}
    for vertex in tangle.multi_graph.nodes():
        rc_vertex_map[vertex] = get_canonical_rc_vertex(vertex, tangle.original_graph, tangle.node_id_mapper)
    subgraph_to_traverse, start_vertex = get_traversable_subgraph(tangle)    

    if not subgraph_to_traverse:
        logging.error(f"No Eulerian path found.")        
        sys.exit(1)
    pathOptimizer = PathOptimizer(subgraph_to_traverse, start_vertex, tangle.node_id_mapper, rc_vertex_map)

    for seed in range(args.num_initial_paths):
        logging.info(f"Generating initial path with seed {seed}.")
        pathOptimizer.generate_random_eulerian_path(seed)
        #TODO: most of this logic should go to PathOptimizer
        current_path = pathOptimizer.get_path()
        current_score = alignment_scorer.score_corasick(current_path)
        logging.info(f"Initial path score for seed {seed}: {current_score}.")

        iterations_since_improvement = 0
        for i in range(args.max_iterations):
            if iterations_since_improvement >= args.early_stopping_limit:
                logging.info(f"Early stopping for seed {seed} on iteration {i} after {iterations_since_improvement} iterations without improvement.")
                break
            
            new_path = pathOptimizer.get_random_change(seed * args.max_iterations + i)
            if new_path is None:
                logging.info(f"Not able to change a path at all, stopping")
                break
            new_score = alignment_scorer.score_corasick(new_path)
            if new_score > current_score:
                logging.info(f"Improved score for seed {seed} at iteration {i}: {current_score} -> {new_score}.")
                current_path = new_path
                pathOptimizer.set_path(new_path)
                current_score = new_score
                iterations_since_improvement = 0
            else:
                iterations_since_improvement += 1
        
        logging.info(f"Final score for seed {seed}: {current_score}.")
        if current_score > best_score:
            logging.info(f"New best path found for seed {seed} with score {current_score}.")
            logging.info(f"Path: {get_gaf_string(current_path, node_id_mapper)}")
            best_path = current_path
            best_score = current_score
    
    logging.info(f"Optimization completed. Best score: {best_score}.")        
    if best_path:
        logging.info("Path optimizing finished.")
        return best_path, best_score, pathOptimizer        
    else:
        logging.error("No valid path found during optimization.")
        sys.exit(1)

def print_final_path_info(best_path, pathOptimizer, tangle, alignment_scorer: AlignmentScorer, args):
    output_fasta = os.path.join(args.outdir, args.basename + ".hpc.fasta")
    output_gaf = os.path.join(args.outdir, args.basename + ".gaf")
    
    pathOptimizer.set_path(best_path)
    pathOptimizer.get_synonymous_changes(alignment_scorer)
    best_path_str = get_gaf_string(best_path, tangle.node_id_mapper)
    logging.info(f"Found traversal\t{best_path_str}")   
    # Output FASTA file
    logging.info(f"Writing best path to {output_fasta} and gaf to {output_gaf}")
    pathOptimizer.output_path(tangle.original_graph, output_fasta, output_gaf)
    alignment_scorer.not_satisfied_fraction(best_path)

    coverage_fraction = MIPOptimizer.ALWAYS_INCLUDE_COVERAGE_FRACTION

    cutoff = coverage_fraction * tangle.detected_coverage
    path_abs_ids = {abs(edge.original_node) for edge in best_path}
    tips_abs_ids = {abs(node) for node in tangle.cleaned_tips}
    non_tips_abs_ids = {abs(node) for node in tangle.nodes} - tips_abs_ids

    missing_tip_ids = {
        node_id for node_id in tips_abs_ids
        if tangle.coverage_dict.get(node_id, 0) >= cutoff and node_id not in path_abs_ids
    }
    missing_non_tip_ids = {
        node_id for node_id in non_tips_abs_ids
        if tangle.coverage_dict.get(node_id, 0) >= cutoff and node_id not in path_abs_ids
    }

    missing_tips_count = len(missing_tip_ids)
    missing_non_tips_count = len(missing_non_tip_ids)
    missing_abs_ids = sorted(missing_tip_ids | missing_non_tip_ids)

    missing_total = missing_tips_count + missing_non_tips_count
    if missing_total > 0:
        missing_names = ", ".join([tangle.node_id_mapper.node_id_to_unoriented_name(node_id) for node_id in missing_abs_ids])
        logging.warning(
            f"Missing high-coverage edges in final path: total={missing_total} (tips={missing_tips_count}, non_tips={missing_non_tips_count}), "
            f"threshold={coverage_fraction * tangle.detected_coverage:.2f}, edges={missing_names}"            
        )
        logging.warning(f"TTT was not able to include high-covered tips or some tip-like structures in the path because of the graph structure.")
    else:
        logging.info(
            f"No missing high-coverage edges in the final path, "
            f"threshold={coverage_fraction * tangle.detected_coverage:.2f}"
        )
        
    logging.info(f"Detected coverage for the path: {tangle.detected_coverage}")
    logging.info(f"Final path: {get_gaf_string(best_path, tangle.node_id_mapper)}")

def parse_arguments():
    parser = argparse.ArgumentParser(description="Solve for integer multiplicities in a GFA tangle graph based on coverage.")
    parser.add_argument("--graph", required=False, help="Path to the GFA graph.")
    parser.add_argument("--alignment", required=False, nargs='+',
                        help="Path(s) to alignment files. Supports .gaf, .pairs formats and .gz compression. "
                             "Format is auto-detected by extension: .gaf/.gaf.gz for GAF format, .pairs/.pairs.gz for Hi-C pairs format.")
    parser.add_argument("--outdir", required=True, type=str, help="Output directory for all result files (will be created if it doesn't exist)")
    parser.add_argument("--boundary-nodes", required=True, type=str, help="Path to a file listing boundary node pairs, tab-separated (required for 2-2 tangles).")

    parser.add_argument("--coverage", help="Path to a file with node coverages (verkko's format; newline separated pairs node_id coverage). If not provided, coverage will be filled from the GFA file.")
    parser.add_argument("--median-unique", type=float, help="Median coverage for reliable unique nodes in tangle. If not provided, autodetected.")
    parser.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"], help="Set the logging level for log file (default: INFO).")

    parser.add_argument("--num-initial-paths", type=int, default=10, help="Number of initial paths to generate (default: 10).")
    parser.add_argument("--max-iterations", type=int, default=100000, help="Maximum iterations for path optimization (default: 100000).")
    #TODO: for quality 0 use random of alignments with highest score?
    parser.add_argument("--early-stopping-limit", type=int, default=15000, help="Early stopping limit for optimization (default: 15000).")
    parser.add_argument("--quality-threshold", type=int, default=20, help="Alignments with quality less than this will be filtered out, default 20")
    parser.add_argument("--basename", required=False, default="traversal", type=str, help="Basename for most of the output files, default `traversal`")
    parser.add_argument("--milp-time-limit", type=int, default=7200, help="Time limit for MILP solver in seconds (default: 7200 seconds = 2 hour).")
    parser.add_argument("--output-gfa", action="store_true", default=False, help="Output traversal paths as GFA files.")
    parser.add_argument("--output-mode", type=str, default="all", choices=["all", "merged", "per-path", "concatenated"],
                        help="GFA output mode: all, merged, per-path, or concatenated (default: all).")

    # GFA format options (mutually exclusive)
    gfa_format_group = parser.add_mutually_exclusive_group()
    gfa_format_group.add_argument("--gfa1", action="store_true", default=True,
                                  help="Output P-lines in GFA1 format with >/< prefixes (default)")
    gfa_format_group.add_argument("--gfa2", action="store_true", default=False,
                                  help="Output P-lines in GFA2 format with +/- suffixes")

    args = parser.parse_args()
    if not args.graph:
        sys.stderr.write("--graph is required\n")
        return

    args.alt_coverage = None
    return args

def process_single_tangle(args, original_graph, dual_graph, cov, alt_cov, boundary_pairs, tangle_idx, total_tangles):
    """Process a single tangle defined by boundary_pairs.

    Args:
        args: Command line arguments
        original_graph: The full GFA graph
        dual_graph: The dual graph
        cov: Coverage dictionary
        alt_cov: Alternative coverage dictionary (or None)
        boundary_pairs: List of [node_name1, node_name2] pairs for this tangle
        tangle_idx: Index of this tangle (0-based)
        total_tangles: Total number of tangles

    Returns:
        Path to the merged GFA file for this tangle, or None if failed
    """
    import tempfile
    from src.input_parsing import new_identify_tangle_nodes

    logging.info(f"{'='*60}")
    logging.info(f"Processing tangle {tangle_idx + 1}/{total_tangles} ({len(boundary_pairs)} boundary pairs)")
    logging.info(f"{'='*60}")

    # Create temporary boundary file for this tangle
    tangle_outdir = os.path.join(args.outdir, f"tangle_{tangle_idx}")
    os.makedirs(tangle_outdir, exist_ok=True)

    boundary_file = os.path.join(tangle_outdir, "boundary_nodes.tsv")
    with open(boundary_file, 'w') as f:
        for pair in boundary_pairs:
            f.write(f"{pair[0]}\t{pair[1]}\n")

    # Create a modified args for this tangle
    tangle_args = argparse.Namespace(**vars(args))
    tangle_args.outdir = tangle_outdir
    tangle_args.boundary_nodes = boundary_file

    # Identify tangle nodes
    tangle_nodes, boundary_nodes = new_identify_tangle_nodes(tangle_args, original_graph, dual_graph, node_id_mapper)

    # Auto-detect ploidy from boundary node pairs
    ploidy = len(boundary_nodes)
    logging.info(f"Detected ploidy: {ploidy} (from {ploidy} boundary node pairs)")

    nor_nodes = {abs(node) for node in tangle_nodes}

    # Create Tangle object
    tangle = Tangle(
        nodes=tangle_nodes,
        nor_nodes=nor_nodes,
        boundary_nodes=boundary_nodes,
        original_graph=original_graph,
        dual_graph=dual_graph,
        node_id_mapper=node_id_mapper,
        ploidy=ploidy
    )

    # Clean tips and rebuild dual graph
    tangle.cleaned_tips = clean_tips(tangle, node_id_mapper)
    tangle.dual_graph = create_dual_graph(original_graph, node_id_mapper)

    used_or_nodes = tangle.nodes.copy()
    for b in tangle.boundary_nodes:
        used_or_nodes.add(b)
        used_or_nodes.add(tangle.boundary_nodes[b])
        used_or_nodes.add(-b)
        used_or_nodes.add(-tangle.boundary_nodes[b])

    tangle.coverage_dict = cov
    tangle.coverage_range = calculate_median_coverage(tangle, tangle_args)
    median_unique = (tangle.coverage_range[0] * DETECTED_LOW_MEDIAN_COVERAGE_VARIATION)
    tangle.median_unique_coverage = median_unique

    # Load alignments
    if tangle_args.alignment:
        filtered_alignment_file = os.path.join(tangle_outdir, f"{tangle_args.basename}.q{tangle_args.quality_threshold}.used_alignments.gaf")
        all_alignments = []

        for align_file in tangle_args.alignment:
            base_name = os.path.basename(align_file).lower()
            if base_name.endswith('.pairs.gz') or base_name.endswith('.pairs'):
                logging.info(f"Detected pairs format: {align_file}")
                file_alignments = parse_pairs(align_file, used_or_nodes, None, tangle_args.quality_threshold, node_id_mapper, original_graph)
            elif base_name.endswith('.gaf.gz') or base_name.endswith('.gaf'):
                logging.info(f"Detected GAF format: {align_file}")
                file_alignments = parse_gaf(align_file, used_or_nodes, None, tangle_args.quality_threshold, node_id_mapper)
            else:
                logging.warning(f"Unknown alignment file format: {align_file}, skipping")
                continue

            logging.info(f"  Loaded {len(file_alignments)} paths from {align_file}")
            all_alignments.extend(file_alignments)

        if filtered_alignment_file and all_alignments:
            with open(filtered_alignment_file, 'w') as f:
                for i, path in enumerate(all_alignments):
                    path_str = ''.join(f"{'>' if n > 0 else '<'}{node_id_mapper.node_id_to_name_safe(abs(n))}" for n in path)
                    f.write(f"alignment_{i}\t0\t0\t0\t+\t{path_str}\t0\t0\t0\t0\t0\t60\n")

        alignments = all_alignments
        alignment_scorer = AlignmentScorer(alignments, original_graph, node_id_mapper)
        logging.info(f"Total alignments loaded: {len(alignments)}")
    else:
        logging.info("No alignment file provided, skipping alignment-based scoring")
        alignments = []
        alignment_scorer = AlignmentScorer(alignments, original_graph, node_id_mapper)

    # MIP optimization
    logging.info("Starting multiplicity counting...")
    mip_optimizer = MIPOptimizer(node_id_mapper, time_limit=tangle_args.milp_time_limit)
    equations, nonzeros, a_values, boundary_values = mip_optimizer.generate_MIP_equations(tangle, directed=True)
    best_solution, score, detected_coverage = mip_optimizer.solve_MIP(equations, nonzeros, boundary_values, a_values, tangle.coverage_range, original_graph)

    if score > 0.2 and alt_cov is not None:
        logging.warning(f"High coverage inconsistency score {score}, trying alternative coverage")
        tangle.coverage_dict = alt_cov
        tangle.coverage_range = calculate_median_coverage(tangle, tangle_args)
        median_unique = (tangle.coverage_range[0] * DETECTED_LOW_MEDIAN_COVERAGE_VARIATION)
        tangle.median_unique_coverage = median_unique
        equations, nonzeros, a_values, boundary_values = mip_optimizer.generate_MIP_equations(tangle, directed=True)
        best_solution_hifi, score_hifi, detected_coverage_hifi = mip_optimizer.solve_MIP(equations, nonzeros, boundary_values, a_values, tangle.coverage_range, original_graph)
        if score_hifi * 1.2 < score:
            logging.warning(f"Alternative coverage produced significantly better MIP score {score_hifi} vs {score}, using it")
            best_solution = best_solution_hifi.copy()
            score = score_hifi
            detected_coverage = detected_coverage_hifi
        else:
            logging.warning(f"Alternative coverage did not significantly improve MIP score {score_hifi} vs {score}, keeping original")
            tangle.coverage_dict = cov
            tangle.coverage_range = calculate_median_coverage(tangle, tangle_args)
            tangle.median_unique_coverage = (tangle.coverage_range[0] * DETECTED_LOW_MEDIAN_COVERAGE_VARIATION)

    tangle.multiplicities = best_solution
    tangle.detected_coverage = detected_coverage

    output_csv = os.path.join(tangle_outdir, tangle_args.basename + ".multiplicities.csv")
    mip_optimizer.write_multiplicities(output_csv, best_solution, cov)

    # Path optimization
    tangle.multi_graph = create_multi_dual_graph(tangle)
    best_path, best_score, pathOptimizer = optimize_paths(tangle, alignment_scorer, tangle_args)
    print_final_path_info(best_path, pathOptimizer, tangle, alignment_scorer, tangle_args)
    print_warnings_summary(tangle_args)

    # Write GFA output
    merged_gfa_path = None
    if tangle_args.output_gfa:
        write_gfa_output(tangle_args, best_path, pathOptimizer, tangle)
        merged_gfa_path = os.path.join(tangle_outdir, f"{tangle_args.basename}_path.merged.gfa")

    return merged_gfa_path


def merge_gfa_files(gfa_files, output_path, original_graph_path, tangle_nodes=None):
    """Merge multiple merged.gfa files into a single file, preserving non-tangle nodes.

    Args:
        gfa_files: List of paths to merged.gfa files
        output_path: Path to write the combined GFA
        original_graph_path: Path to original GFA for header info and non-tangle nodes
        tangle_nodes: Set of node names that are part of tangles (to exclude from original)
    """
    if tangle_nodes is None:
        tangle_nodes = set()

    all_s_lines = {}  # node_name -> S-line
    all_l_lines = []  # L-lines
    all_p_lines = []  # P-lines

    seen_s_lines = set()
    seen_l_lines = set()
    path_counter = 0

    # First, read all tangle GFA files
    for gfa_file in gfa_files:
        if not gfa_file or not os.path.exists(gfa_file):
            continue

        with open(gfa_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue

                if line.startswith('S'):
                    parts = line.split('\t')
                    if len(parts) >= 3:
                        name = parts[1]
                        if name not in seen_s_lines:
                            seen_s_lines.add(name)
                            all_s_lines[name] = line

                elif line.startswith('L'):
                    if line not in seen_l_lines:
                        seen_l_lines.add(line)
                        all_l_lines.append(line)

                elif line.startswith('P'):
                    parts = line.split('\t')
                    if len(parts) >= 3:
                        old_name = parts[1]
                        new_name = f"tangle_{path_counter}_{old_name}"
                        parts[1] = new_name
                        all_p_lines.append('\t'.join(parts))
                        path_counter += 1

    # Then, read original GFA and add non-tangle nodes/edges
    if original_graph_path and os.path.exists(original_graph_path):
        with open(original_graph_path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('H'):
                    continue

                if line.startswith('S'):
                    parts = line.split('\t')
                    if len(parts) >= 3:
                        name = parts[1]
                        # Only add if not already added (from tangle output) and not a tangle node
                        if name not in seen_s_lines and name not in tangle_nodes:
                            seen_s_lines.add(name)
                            all_s_lines[name] = line

                elif line.startswith('L'):
                    # Add link if not already added
                    if line not in seen_l_lines:
                        # Check if this link involves tangle nodes
                        parts = line.split('\t')
                        if len(parts) >= 5:
                            from_node = parts[1]
                            to_node = parts[3]
                            # Only add if both nodes are not in tangle
                            if from_node not in tangle_nodes and to_node not in tangle_nodes:
                                seen_l_lines.add(line)
                                all_l_lines.append(line)

    # Write merged GFA
    with open(output_path, 'w') as f:
        # Write header
        f.write("H\tVN:Z:1.0\n")

        # Write S-lines (sorted by name for consistency)
        for name in sorted(all_s_lines.keys()):
            f.write(all_s_lines[name] + '\n')

        # Write L-lines
        for l_line in all_l_lines:
            f.write(l_line + '\n')

        # Write P-lines
        for p_line in all_p_lines:
            f.write(p_line + '\n')

    logging.info(f"Merged {len(gfa_files)} GFA files into {output_path}")
    logging.info(f"  Total segments: {len(all_s_lines)}")
    logging.info(f"  Total links: {len(all_l_lines)}")
    logging.info(f"  Total paths: {len(all_p_lines)}")


def main():
    # When running as a PyInstaller bundle, configure paths for bundled glpsol
    if hasattr(sys, '_MEIPASS'):
        os.environ['PATH'] = sys._MEIPASS + os.pathsep + os.environ.get('PATH', '')
        glpsol_name = 'glpsol.exe' if sys.platform == 'win32' else 'glpsol'
        glpsol_path = os.path.join(sys._MEIPASS, glpsol_name)
        if os.path.exists(glpsol_path):
            os.environ['GLPK_CMD'] = glpsol_path

    args = parse_arguments()
    os.makedirs(args.outdir, exist_ok=True)
    setup_logging(args)
    logging.debug(f"args: {args}")
    logging.info("Reading files...")

    # Parse graph and coverage (shared across all tangles)
    original_graph = parse_gfa(args.graph, node_id_mapper)
    if args.coverage:
        cov = read_coverage_file(args.coverage, node_id_mapper)
    else:
        cov = coverage_from_graph(original_graph)

    alt_cov = None
    if args.alt_coverage:
        alt_cov = read_coverage_file(args.alt_coverage, node_id_mapper)
        verify_coverage(alt_cov, original_graph, node_id_mapper)

    verify_coverage(cov, original_graph, node_id_mapper)

    # Create dual graph (shared across all tangles)
    dual_graph = create_dual_graph(original_graph, node_id_mapper)

    # Parse boundary file - may contain multiple tangles
    all_tangles = parse_boundary_file(args.boundary_nodes, original_graph, node_id_mapper)
    total_tangles = len(all_tangles)

    if total_tangles == 0:
        logging.error("No tangles found in boundary file")
        sys.exit(1)

    logging.info(f"Found {total_tangles} tangle(s) in boundary file")

    # Process each tangle
    merged_gfa_files = []
    concatenated_gfa_files = []
    all_tangle_node_names = set()  # Collect all node names that are part of any tangle

    for tangle_idx, boundary_pairs in enumerate(all_tangles):
        try:
            merged_gfa = process_single_tangle(
                args, original_graph, dual_graph, cov, alt_cov,
                boundary_pairs, tangle_idx, total_tangles
            )
            if merged_gfa:
                merged_gfa_files.append(merged_gfa)

                # Also collect concatenated GFA path
                tangle_outdir = os.path.join(args.outdir, f"tangle_{tangle_idx}")
                concat_gfa = os.path.join(tangle_outdir, f"{args.basename}_path.concatenated.gfa")
                if os.path.exists(concat_gfa):
                    concatenated_gfa_files.append(concat_gfa)

                # Collect all node names from the tangle's merged GFA
                if os.path.exists(merged_gfa):
                    with open(merged_gfa, 'r') as f:
                        for line in f:
                            if line.startswith('S'):
                                parts = line.strip().split('\t')
                                if len(parts) >= 2:
                                    all_tangle_node_names.add(parts[1])

        except Exception as e:
            logging.error(f"Failed to process tangle {tangle_idx + 1}: {e}")
            import traceback
            logging.error(traceback.format_exc())
            continue

    # Merge all GFA files into final output, preserving non-tangle nodes
    if merged_gfa_files and args.output_gfa:
        final_merged = os.path.join(args.outdir, f"{args.basename}_path.merged.gfa")
        merge_gfa_files(merged_gfa_files, final_merged, args.graph, all_tangle_node_names)
        logging.info(f"Final merged GFA: {final_merged}")

        # Also merge concatenated GFA files
        if concatenated_gfa_files:
            final_concatenated = os.path.join(args.outdir, f"{args.basename}_path.concatenated.gfa")
            merge_gfa_files(concatenated_gfa_files, final_concatenated, args.graph, all_tangle_node_names)
            logging.info(f"Final concatenated GFA: {final_concatenated}")

    # Copy boundary nodes file to output directory
    if args.boundary_nodes and os.path.exists(args.boundary_nodes):
        import shutil
        dest = os.path.join(args.outdir, "boundary_nodes.tsv")
        shutil.copy2(args.boundary_nodes, dest)
        logging.info(f"Copied boundary nodes file to {dest}")

    successful_count = len(merged_gfa_files)
    failed_count = total_tangles - successful_count
    if failed_count > 0:
        logging.warning(f"Completed: {successful_count}/{total_tangles} tangles succeeded, {failed_count} failed")
    else:
        logging.info(f"All {total_tangles} tangle(s) processed successfully")

def write_gfa_output(args, best_path, pathOptimizer, tangle):
    """Write traversal paths as GFA files for Pathfinder integration.

    Output modes:
      - all / merged: writes {basename}_path.merged.gfa (single GFA with P-lines, shared nodes NOT duplicated)
      - concatenated: writes {basename}_path.concatenated.gfa (one segment per original edge with links, shared nodes duplicated)
      - per-path: writes {basename}_path0.gfa, {basename}_path1.gfa, ... (one per segment)

    Node naming convention:
      - Non-shared nodes keep original name (e.g., utg001259l)
      - Shared nodes get _TTT<index> suffix (e.g., utg001259l_TTT0, utg001259l_TTT1)
    """
    mode = args.output_mode
    basename = args.basename
    outdir = args.outdir

    # Read original GFA to get full S-line information
    original_slines = {}  # node_name -> full S-line parts
    with open(args.graph, 'r') as f:
        for line in f:
            if line.startswith('S'):
                parts = line.strip().split('\t')
                if len(parts) >= 3:
                    original_slines[parts[1]] = parts

    # Split path at AUX marker(s) (same logic as output_path)
    aux_positions = []
    for i in range(len(best_path)):
        name = tangle.node_id_mapper.node_id_to_unoriented_name(abs(best_path[i].original_node))
        if name.startswith("AUX"):
            aux_positions.append(i)
    aux_positions.sort()
    if aux_positions:
        paths = []
        prev = 0
        for pos in aux_positions:
            paths.append(best_path[prev:pos])
            prev = pos + 1
        paths.append(best_path[prev:])
    else:
        paths = [best_path]

    # Get original node names for each path
    path_node_names = []  # list of lists of (original_name, orientation)
    for path in paths:
        node_names = []
        for edge in path:
            node_id = abs(edge.original_node)
            orientation = '+' if edge.original_node > 0 else '-'
            name = tangle.node_id_mapper.node_id_to_unoriented_name(node_id)
            node_names.append((name, orientation))
        path_node_names.append(node_names)

    # Find shared nodes (nodes that appear in more than one path)
    node_paths = {}
    for pi, pnodes in enumerate(path_node_names):
        for name, _ in pnodes:
            node_paths.setdefault(name, set()).add(pi)
    shared_nodes = {name for name, path_indices in node_paths.items() if len(path_indices) > 1}

    logging.info(f"Found {len(shared_nodes)} shared nodes across {len(paths)} paths")

    # Helper function to get node name with suffix if shared
    def get_node_name(original_name, path_idx):
        if original_name in shared_nodes:
            return f"{original_name}_TTT{path_idx}"
        return original_name

    # In merged mode: single GFA with all paths, shared nodes NOT duplicated, P-lines for each path
    if mode in ("all", "merged"):
        # Collect all unique nodes and edges across all paths
        all_nodes = {}  # node_id -> (name, seq)
        all_edges = []  # list of (from_name, from_orient, to_name, to_orient, overlap)
        path_lines = []  # list of P-line strings

        for path_idx, path in enumerate(paths):
            path_segments = []  # for P-line
            prev_seg_name = None
            prev_edge = None
            prev_orientation = None

            for edge in path:
                node_id = abs(edge.original_node)
                orientation = '+' if edge.original_node > 0 else '-'
                original_name = tangle.node_id_mapper.node_id_to_unoriented_name(node_id)
                seq = tangle.original_graph.nodes[node_id].get('sequence', '*')

                if seq == "*":
                    logging.warning("Cannot write GFA: assembly graph has no sequences")
                    return

                # Store node with original sequence (orientation handled by P-line)
                if node_id not in all_nodes:
                    all_nodes[node_id] = original_name

                # Add edge if we have a previous segment
                if prev_seg_name is not None and prev_edge is not None:
                    overlap = tangle.original_graph.get_edge_data(prev_edge, edge.original_node)
                    if overlap:
                        overlap_val = max(0, overlap.get('overlap', 0))
                        overlap_str = f"{overlap_val}M"
                    else:
                        # Try reverse complement edge
                        rc_prev = -prev_edge
                        rc_curr = -edge.original_node
                        overlap = tangle.original_graph.get_edge_data(rc_curr, rc_prev)
                        if overlap:
                            overlap_val = max(0, overlap.get('overlap', 0))
                            overlap_str = f"{overlap_val}M"
                        else:
                            overlap_str = "0M"
                    edge_tuple = (prev_seg_name, prev_orientation, original_name, orientation, overlap_str)
                    if edge_tuple not in all_edges:
                        all_edges.append(edge_tuple)

                # Build path segment for P-line based on format
                if args.gfa2:
                    # GFA2 format: seg+ or seg-
                    orient_suffix = '+' if orientation == '+' else '-'
                    path_segments.append(f"{original_name}{orient_suffix}")
                else:
                    # GFA1 format: >seg or <seg (default)
                    orient_prefix = '>' if orientation == '+' else '<'
                    path_segments.append(f"{orient_prefix}{original_name}")
                prev_seg_name = original_name
                prev_edge = edge.original_node
                prev_orientation = orientation

            # Create P-line for this path
            path_name = f"traversal_{path_idx}" if len(paths) > 1 else "traversal"
            path_lines.append(f"P\t{path_name}\t{','.join(path_segments)}\t*")

        # Write merged GFA
        gfa_path = os.path.join(outdir, f"{basename}_path.merged.gfa")
        with open(gfa_path, 'w') as f:
            f.write("H\tVN:Z:1.0\n")
            # Write all unique S-lines with full tags
            for node_id, name in sorted(all_nodes.items()):
                # Get original S-line from input GFA
                orig_name = tangle.node_id_mapper.node_id_to_unoriented_name(node_id)
                if orig_name in original_slines:
                    parts = original_slines[orig_name]
                    # Copy parts, keeping original name (shared nodes not duplicated in merged mode)
                    new_parts = [parts[0], name] + parts[2:]
                    f.write('\t'.join(new_parts) + '\n')
                else:
                    # Fallback: write minimal S-line
                    seq = tangle.original_graph.nodes[node_id].get('sequence', '*')
                    f.write(f"S\t{name}\t{seq}\n")
            # Write all unique L-lines
            for from_name, from_orient, to_name, to_orient, overlap in all_edges:
                f.write(f"L\t{from_name}\t{from_orient}\t{to_name}\t{to_orient}\t{overlap}\n")
            # Write P-lines for each path
            for p_line in path_lines:
                f.write(f"{p_line}\n")
        logging.info(f"Wrote merged GFA to {gfa_path}: {len(all_nodes)} nodes, {len(all_edges)} edges, {len(path_lines)} paths")

    # Helper function to write S-line with proper tags
    num_paths = len(paths)
    def write_sline(f, seg_name, original_name, node_id, is_shared, path_idx):
        """Write S-line with full tags from original GFA. For shared nodes, divide depth values by number of paths."""
        if original_name in original_slines:
            parts = original_slines[original_name]
            new_parts = [parts[0], seg_name] + parts[2:]
            # For shared nodes, divide depth-related tags by number of paths
            if is_shared:
                final_parts = [new_parts[0], new_parts[1], new_parts[2]]  # S, name, seq
                for tag in new_parts[3:]:
                    if tag.startswith('DP:f:'):
                        try:
                            val = float(tag[5:])
                            final_parts.append(f'DP:f:{val / num_paths}')
                        except ValueError:
                            final_parts.append(tag)
                    elif tag.startswith('ll:f:'):
                        try:
                            val = float(tag[5:])
                            final_parts.append(f'll:f:{val / num_paths}')
                        except ValueError:
                            final_parts.append(tag)
                    elif tag.startswith('ll:i:'):
                        try:
                            val = int(tag[5:])
                            final_parts.append(f'll:i:{val // num_paths}')
                        except ValueError:
                            final_parts.append(tag)
                    elif tag.startswith('fc:f:'):
                        try:
                            val = float(tag[5:])
                            final_parts.append(f'fc:f:{val / num_paths}')
                        except ValueError:
                            final_parts.append(tag)
                    elif tag.startswith('RC:i:'):
                        try:
                            val = int(tag[5:])
                            final_parts.append(f'RC:i:{val // num_paths}')
                        except ValueError:
                            final_parts.append(tag)
                    else:
                        final_parts.append(tag)
                f.write('\t'.join(final_parts) + '\n')
            else:
                f.write('\t'.join(new_parts) + '\n')
        else:
            # Fallback: write minimal S-line
            seq = tangle.original_graph.nodes[node_id].get('sequence', '*')
            f.write(f"S\t{seg_name}\t{seq}\n")

    # In concatenated mode: one segment per original edge with links, shared nodes duplicated
    if mode in ("all", "concatenated"):
        gfa_path = os.path.join(outdir, f"{basename}_path.concatenated.gfa")
        with open(gfa_path, 'w') as f:
            f.write("H\tVN:Z:1.0\n")
            for path_idx, path in enumerate(paths):
                prev_seg = None
                for edge in path:
                    node_id = abs(edge.original_node)
                    original_name = tangle.node_id_mapper.node_id_to_unoriented_name(node_id)
                    seg_name = get_node_name(original_name, path_idx)
                    is_shared = original_name in shared_nodes
                    write_sline(f, seg_name, original_name, node_id, is_shared, path_idx)
                    if prev_seg is not None:
                        overlap = tangle.original_graph.get_edge_data(prev_edge, edge.original_node)
                        if overlap:
                            overlap_val = max(0, overlap.get('overlap', 0))
                            overlap_str = f"{overlap_val}M"
                        else:
                            overlap_str = "0M"
                        f.write(f"L\t{prev_seg}\t+\t{seg_name}\t+\t{overlap_str}\n")
                    prev_seg = seg_name
                    prev_edge = edge.original_node
        logging.info(f"Wrote concatenated GFA to {gfa_path}")

    # In per-path mode: one file per segment
    if mode in ("all", "per-path"):
        for path_idx, path in enumerate(paths):
            gfa_path = os.path.join(outdir, f"{basename}_path{path_idx}.gfa")
            with open(gfa_path, 'w') as f:
                f.write("H\tVN:Z:1.0\n")
                prev_seg = None
                prev_edge = None
                for edge in path:
                    node_id = abs(edge.original_node)
                    original_name = tangle.node_id_mapper.node_id_to_unoriented_name(node_id)
                    seg_name = get_node_name(original_name, path_idx)
                    is_shared = original_name in shared_nodes
                    write_sline(f, seg_name, original_name, node_id, is_shared, path_idx)
                    if prev_seg is not None and prev_edge is not None:
                        overlap = tangle.original_graph.get_edge_data(prev_edge, edge.original_node)
                        if overlap:
                            overlap_val = max(0, overlap.get('overlap', 0))
                            overlap_str = f"{overlap_val}M"
                        else:
                            overlap_str = "0M"
                        f.write(f"L\t{prev_seg}\t+\t{seg_name}\t+\t{overlap_str}\n")
                    prev_seg = seg_name
                    prev_edge = edge.original_node
            logging.info(f"Wrote per-path GFA to {gfa_path}")

if __name__ == "__main__":
    main()
