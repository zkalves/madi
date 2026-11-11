# Madí: Model, Align & Diagram Interconnects

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Build](https://img.shields.io/badge/Build-CMake%203.25%2B-brightgreen.svg)](https://cmake.org/)

**Madí** (/mah-DEE/) is an open-source, high-performance 2D diagramming, schematic, and interconnect engine engineered to scale smoothly to massive networks (10,000 to 100,000+ nodes and edges) on Linux (X11 and Wayland).

---

## The Madí Homage: Art & Engineering Alignment

The project is named in homage to **Arte Madí**, the 1946 Buenos Aires geometric art movement founded by Carmelo Arden Quin, Gyula Kosice, and Rhod Rothfuss:

1. **Built-in Wordplay & History**: Arte Madí was founded around intentional wordplay and debated acronyms (*MAterialismo DIaléctico*, *Movimiento de Arte De Invención*, or playful phonetic sound poetry).
2. **Conceptual Alignment**:
   - **Broken Canvas (*Marco Recortado*)**: Madí artists famously broke out of traditional rectangular canvases in favor of polygonal, irregular frames — directly mirroring modern infinite-canvas diagramming with freeform polygonal figures.
   - **Articulated Structures**: Madí pioneered transformable, hinged, modular physical structures — analogous to nested, hierarchical schematic subsystems with dynamic pin alignments.
3. **Tool Ergonomics**:
   - **CLI command**: `madi` (short, 4 letters, zero keyboard travel).
   - **Native file extension**: `.madi` (standard JSON format compliant with RFC 8259, with JSONC comment support and optimal Git diffs).
   - **Pronunciation**: /mah-DEE/ (rhymes with *body*, sounds like *Maddie*).
4. **Engineering Backronym**:
   **Model, Align & Diagram Interconnects**.

---

## Key Capabilities

- **Zero-Dependency Core Domain**: Pure ISO C++20 domain layer (`madi::core` / `madi::core`) with decoupled transactional undo/redo, R-Tree / BVH spatial indexing, and topological graph models.
- **Hierarchical Subsystems & Instantiation**: Built-in and file-linked subsystems (`.madi`), boundary port routing, prototype property overrides, and breadcrumb drill-down.
- **Parametric Shapes & Multi-Point Connectors**: Freeform polygons with interactive vertex handles, segmented arrows, and line physics.
- **Autonomous Obstacle Avoidance**: Background `RouterWorker` (`std::jthread`) with dynamic Manhattan routing, bridge crossings, and selectable snapping engine.
- **Deterministic Storage & Interop**: Standard RFC 8259 JSON `.madi` format with JSONC comments for Git version control, `.diag` archive bundler, and bidirectional Draw.io XML compatibility.

---

## Architectural Documentation

For full architectural blueprints, threading topology, scoping models, and class diagrams, see:
- [`ARCHITECTURE.md`](ARCHITECTURE.md)

---

## Building Madí

### Prerequisites
- GCC 12+ or Clang 16+ (ISO C++20 standard support)
- CMake 3.25+
- Qt 6 (Core, Gui, Widgets, OpenGLWidgets, Test)
- ZLIB

### Build Commands
Using the convenience `Makefile`:
```bash
# Display help and all available targets (default)
make

# Build all targets (libraries, CLI madi, and GUI madi-gui)
make all

# Build individual targets
make lib        # Build domain libraries
make cli        # Build madi CLI executable
make gui        # Build madi-gui Qt6 application
make run        # Build and launch madi-gui

# Run test suite
make test
```

Or using CMake directly:
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Running Tests
```bash
ctest --test-dir build --output-on-failure
```

---

## Command-Line Usage (`madi`)

```bash
# Inspect diagram statistics
madi --stats examples/counter.madi

# Convert between Draw.io XML and Madí format
madi --convert diagram.drawio diagram.madi

# Re-route all nets using obstacle avoidance
madi --route in.madi out.madi

# Export diagram directly to SVG
madi --export-svg examples/counter.madi counter.svg
```

---

## License

Madí is free and open-source software licensed under the [GNU General Public License v3 (GPLv3)](LICENSE).
