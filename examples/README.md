# Madí Architecture & Schematic Examples

This directory contains reference diagrams and hierarchical subsystem examples in Madí's native format (`.madi`, compliant with standard RFC 8259 JSON and JSONC comments).

## Available Examples

1. **`counter.madi`**:
   - A synchronous 4-bit counter showing clock generation, hierarchical counter core subsystem, and 7-segment decoder interconnects.
2. **`hierarchical_schematic.madi`** / **`hierarchical_schematic.yaml`**:
   - A sensor processing pipeline demonstrating hierarchical drill-down (`Enter` / `Ctrl+Down` to push into subsystem, `Alt+Up` / `Ctrl+Up` to pop back to parent).
3. **`network_architecture.yaml`**:
   - A multi-tier network topology featuring router, switches, compute, and storage blocks connected via orthogonal obstacle-avoiding nets with connection ports.
4. **`alu_subsystem.yaml`**:
   - Demonstrates a file-linked hierarchical subsystem with nested diagram view.

## Usage

### In Graphical Editor (`madi-gui`)
- Open via GUI: **File -> Open Diagram...** (<kbd>Ctrl+O</kbd>) and select any `.madi` or `.yaml` file.
- Or pass via command line:
  ```bash
  madi-gui examples/counter.madi
  ```

### In Command-Line Tool (`madi`)
- Display statistics of a diagram:
  ```bash
  madi --stats examples/counter.madi
  ```
- Convert between Draw.io XML and Madí format:
  ```bash
  madi --convert diagram.drawio diagram.madi
  ```
- Re-route nets with obstacle avoidance:
  ```bash
  madi --route in.madi out.madi
  ```
- Export directly to SVG:
  ```bash
  madi --export-svg examples/counter.madi counter.svg
  ```
