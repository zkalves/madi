// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_IO_STORAGE_ENGINE_HPP
#define MADI_IO_STORAGE_ENGINE_HPP

#include <string>
#include <string_view>
#include <vector>

#include "../core/GraphModel.hpp"
#include "../core/HierarchicalBlock.hpp"

namespace madi::io {

struct StorageResult {
  bool success{false};
  std::string error_message;
};

class StorageEngine {
public:
  StorageEngine() = default;

  // Primary serializer: RFC 8259 Standard JSON with .madi extension
  [[nodiscard]] std::string serialize(
      const core::GraphModel& graph, const core::BlockLibraryManager* lib_manager = nullptr) const;

  // Primary deserializer: Accepts RFC 8259 JSON, JSONC (with // and /* */ comments), and legacy YAML fallback
  [[nodiscard]] StorageResult deserialize(
      std::string_view text, core::GraphModel& out_graph,
      core::BlockLibraryManager* out_lib_manager = nullptr) const;

  // 1. Standard RFC 8259 JSON serializer & deserializer
  [[nodiscard]] std::string serialize_json(
      const core::GraphModel& graph, const core::BlockLibraryManager* lib_manager = nullptr) const;

  [[nodiscard]] StorageResult deserialize_json(
      std::string_view json_text, core::GraphModel& out_graph,
      core::BlockLibraryManager* out_lib_manager = nullptr) const;

  // 2. Line-oriented YAML serializer (retained for backward compatibility)
  [[nodiscard]] std::string serialize_yaml(
      const core::GraphModel& graph, const core::BlockLibraryManager* lib_manager = nullptr) const;

  [[nodiscard]] StorageResult deserialize_yaml(
      std::string_view yaml_text, core::GraphModel& out_graph,
      core::BlockLibraryManager* out_lib_manager = nullptr) const;

  // 3. SQLite Spatial Cache for partial paging of 10k-100k+ nodes
  [[nodiscard]] StorageResult save_sqlite_cache(const std::string& db_file_path,
                                                const core::GraphModel& graph) const;

  [[nodiscard]] StorageResult load_sqlite_spatial_region(const std::string& db_file_path,
                                                         core::Rect2d spatial_bounds,
                                                         core::GraphModel& out_graph) const;

  // 4. ZIP Archive Container (.diag bundle)
  [[nodiscard]] StorageResult pack_diag_archive(const std::string& diag_file_path,
                                                const core::GraphModel& root_graph,
                                                const core::BlockLibraryManager& lib_manager) const;

  [[nodiscard]] StorageResult unpack_diag_archive(const std::string& diag_file_path,
                                                  core::GraphModel& out_root_graph,
                                                  core::BlockLibraryManager& out_lib_manager) const;
};

}  // namespace madi::io

#endif // MADI_IO_STORAGE_ENGINE_HPP
