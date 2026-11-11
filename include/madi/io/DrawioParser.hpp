// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_IO_DRAWIO_PARSER_HPP
#define MADI_IO_DRAWIO_PARSER_HPP

#include <string>
#include <string_view>
#include <vector>

#include "../core/GraphModel.hpp"

namespace madi::io {

struct DrawioParseResult {
  bool success{false};
  std::string error_message;
  size_t imported_nodes{0};
  size_t imported_edges{0};
};

class DrawioParser {
public:
  DrawioParser() = default;

  // Imports a Draw.io XML string or raw .drawio file contents into the given GraphModel
  [[nodiscard]] DrawioParseResult import_xml(std::string_view xml_data,
                                             core::GraphModel& out_graph);

  // Exports the GraphModel to a valid Draw.io XML string
  [[nodiscard]] std::string export_xml(const core::GraphModel& graph,
                                       const std::string& diagram_name = "Page-1");

  // Base64 & Deflate helpers for compressed <diagram> tags
  [[nodiscard]] static std::string decompress_diagram_payload(std::string_view base64_payload);
  [[nodiscard]] static std::string compress_diagram_payload(std::string_view raw_xml);

private:
  bool parse_mx_graph_model(std::string_view xml_data, core::GraphModel& out_graph,
                            DrawioParseResult& result);
};

}  // namespace madi::io

#endif // MADI_IO_DRAWIO_PARSER_HPP
