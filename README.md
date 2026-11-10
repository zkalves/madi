# VECTOR: Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Build](https://img.shields.io/badge/Build-CMake%203.25%2B-brightgreen.svg)](https://cmake.org/)

**VECTOR** is an open-source, high-performance 2D diagramming and schematic engine engineered to scale smoothly to massive networks (10,000 to 100,000+ nodes and edges) on Linux (X11 and Wayland).

---

## Key Capabilities

- **Zero-Dependency Core Domain**: Pure C++20 domain layer (`vector::core`) with decoupled transactional undo/redo, R-Tree spatial indexing, and topological graph models.
- **Hierarchical Blocks & Instantiation**: `BlockDefinition` vs `BlockInstance` scoping, prototype inheritance for property overrides, boundary `InterfacePort` pin mapping, and breadcrumb drill-down.
- **Dual-Backend Viewport**: Skia 2D rendering pipeline (GPU OpenGL/Vulkan with multi-threaded SIMD CPU software fallback) integrated into Qt6 (`CanvasWidget`), featuring subpixel antialiasing and Level-of-Detail (LOD) degradation.
- **Dynamic Obstacle Avoidance**: Concurrent `RouterWorker` (`std::jthread`) with Adaptagrams `libavoid` interface, built-in orthogonal A* avoidance, and freeze-during-drag optimization.
- **Tri-Model Storage & Interop**: Deterministic line-oriented YAML/JSON format for Git diffing, SQLite spatial caching for partial diagram paging, `.diag` ZIP archive bundles, and bidirectional Draw.io XML (`mxGraphModel`) compatibility.

---

## Architectural Documentation

For full architectural blueprints, threading topology, scoping models, and class diagrams, see:
- [`ARCHITECTURE.md`](ARCHITECTURE.md)

---

## Building VECTOR

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

# Build all targets
make all

# Build individual targets
make lib        # Build libvector static library
make cli        # Build vector-cli
make gui        # Build vector-gui (Qt6)

# Run test suite
make test
make smoke-test # Run standalone smoke test directly
```

Or using CMake directly:
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Running Tests
```bash
./build/bin/vector_smoke_test
# Or via CTest:
ctest --test-dir build --output-on-failure
```

---

## License

VECTOR is free and open-source software licensed under the [GNU General Public License v3 (GPLv3)](LICENSE).
