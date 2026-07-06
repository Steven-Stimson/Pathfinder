# Pathfinder

## Intro

**Pathfinder** is a powerful tool for interactive assembly graph visualization and manipulation. Built as a successor to [Bandage](https://github.com/rrwick/Bandage), Pathfinder carries forward the core philosophy of interactive assembly graph visualization while introducing substantial enhancements to graph editing and usability.

## Features

### Graph Visualization
- Interactive visualization of assembly graphs in GFA format
- Multiple layout algorithms for optimal graph rendering
- Customizable node colors, labels, and styles
- BLAST search integration for sequence queries

### Graph Editing
- **Link 2 Nodes**: Connect nodes with custom edges
- **Merge Nodes**: Combine selected nodes
- **Duplicate Nodes**: Create copies of selected nodes
- **Change Node Properties**: Modify node names and depths
- **Full Undo/Redo Stack**: Multi-level undo/redo for all graph operations
  - `Ctrl+Z` - Undo
  - `Ctrl+Shift+Z` - Redo

### TTT Module
- Tangle Termination Tool for resolving complex graph tangles
- Automatic multiplicity detection
- Path optimization with alignment scoring
- Multiple output formats (merged, per-path, concatenated)

## Prerequisites (for building from source)

- Qt 6
- CMake 3.28+
- C++17-compliant compiler
- Python 3 (for TTT module)

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## Pre-built Binaries

Pre-built binaries for Linux, macOS, and Windows are available from the [Releases](https://github.com/Steven-Stimson/Pathfinder/releases) page.

## Usage

### Loading a Graph
```
File → Load Graph
```

### Drawing the Graph
1. Set the graph scope (Entire graph, Around nodes, etc.)
2. Configure display options (zoom, node width, colors)
3. Click "Draw graph"

### Using TTT
1. Load a GFA graph
2. Go to Pathfinder → TTT
3. Configure boundary nodes and options
4. Run TTT to resolve tangles

### Linking Nodes
1. Select Edit → Link 2 nodes (Ctrl+L)
2. Click on the head or tail of the first node
3. Click on the head or tail of the second node
4. An edge will be created between the nodes

## Contributing

New contributors are welcome! If you're interested or have ideas, please use the Issues section in the repo.

## License

GNU General Public License, version 3
