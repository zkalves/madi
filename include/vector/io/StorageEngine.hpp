// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_IO_STORAGE_ENGINE_HPP
#define VECTOR_IO_STORAGE_ENGINE_HPP

#include <string>
#include <string_view>
#include <vector>

#include "../core/GraphModel.hpp"
#include "../core/HierarchicalBlock.hpp"

namespace vector::io {

struct StorageResult {
  bool success{false};
  std::string error_message;
};

class StorageEngine {
public:
  StorageEngine() = default;

  // 1. Line-oriented, deterministic YAML serializer for clean Git versioning and diffs
  [[nodiscard]] std::string serialize_yaml(
      const core::GraphModel& graph, const core::BlockLibraryManager* lib_manager = nullptr) const;

  [[nodiscard]] StorageResult deserialize_yaml(
      std::string_view yaml_text, core::GraphModel& out_graph,
      core::BlockLibraryManager* out_lib_manager = nullptr) const;

  // JSON serializer
  [[nodiscard]] std::string serialize_json(
      const core::GraphModel& graph, const core::BlockLibraryManager* lib_manager = nullptr) const;

  [[nodiscard]] StorageResult deserialize_json(
      std::string_view json_text, core::GraphModel& out_graph,
      core::BlockLibraryManager* out_lib_manager = nullptr) const;

  // 2. SQLite Spatial Cache for partial paging of 10k-100k+ nodes
  [[nodiscard]] StorageResult save_sqlite_cache(const std::string& db_file_path,
                                                const core::GraphModel& graph) const;

  [[nodiscard]] StorageResult load_sqlite_spatial_region(const std::string& db_file_path,
                                                         core::Rect2d spatial_bounds,
                                                         core::GraphModel& out_graph) const;

  // 3. ZIP Archive Container (.diag bundle)
  [[nodiscard]] StorageResult pack_diag_archive(const std::string& diag_file_path,
                                                const core::GraphModel& root_graph,
                                                const core::BlockLibraryManager& lib_manager) const;

  [[nodiscard]] StorageResult unpack_diag_archive(const std::string& diag_file_path,
                                                  core::GraphModel& out_root_graph,
                                                  core::BlockLibraryManager& out_lib_manager) const;
};

}  // namespace vector::io

#endif  // VECTOR_IO_STORAGE_ENGINE_HPP
