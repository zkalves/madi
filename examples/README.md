# VECTOR Architecture & Schematic Examples

This directory contains reference diagrams and hierarchical subsystem examples in VECTOR's deterministic line-oriented YAML format.

## Available Examples

1. **`alu_subsystem.yaml`**:
   - Demonstrates a file-linked hierarchical subsystem with nested diagram view.
2. **`network_architecture.yaml`**:
   - A multi-tier network topology featuring router, switches, compute, and storage blocks connected via orthogonal obstacle-avoiding nets with connection ports.
3. **`hierarchical_schematic.yaml`**:
   - A sensor processing pipeline demonstrating hierarchical drill-down (`Enter` / `Ctrl+Down` to push into subsystem, `Alt+Up` / `Ctrl+Up` to pop back to parent).

## Usage

### In Graphical Editor (`vector-gui`)
- Open via GUI: **File -> Open Diagram...** (<kbd>Ctrl+O</kbd>) and select any `.yaml` file.
- Or pass via command line:
  ```bash
  vector-gui examples/network_architecture.yaml
  ```

### In Command-Line Tool (`vector-cli`)
- Inspect or convert between YAML and XML:
  ```bash
  vector-cli --convert examples/network_architecture.yaml network.xml
  ```
