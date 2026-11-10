// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_CORE_SPATIAL_INDEX_HPP
#define VECTOR_CORE_SPATIAL_INDEX_HPP

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Geometry.hpp"

namespace vector::core {

struct SpatialItem {
  uint64_t id{0};
  Rect2d bounds;

  constexpr bool operator==(const SpatialItem& other) const noexcept {
    return id == other.id;
  }
};

class ISpatialIndex {
public:
  virtual ~ISpatialIndex() = default;

  virtual void insert(uint64_t id, const Rect2d& bounds) = 0;
  virtual bool remove(uint64_t id) = 0;
  virtual void update(uint64_t id, const Rect2d& new_bounds) = 0;
  [[nodiscard]] virtual std::vector<uint64_t> query_box(const Rect2d& box) const = 0;
  [[nodiscard]] virtual std::vector<uint64_t> query_point(Vec2d point) const = 0;
  [[nodiscard]] virtual std::vector<uint64_t> query_frustum(const Rect2d& frustum) const = 0;
  virtual void clear() = 0;
  [[nodiscard]] virtual size_t size() const noexcept = 0;
  [[nodiscard]] virtual bool empty() const noexcept = 0;
};

// High-performance, zero-dependency C++20 Linear R-Tree / Bounding Volume Hierarchy
class FlatBvhSpatialIndex final : public ISpatialIndex {
public:
  FlatBvhSpatialIndex() = default;
  ~FlatBvhSpatialIndex() override = default;

  void insert(uint64_t id, const Rect2d& bounds) override {
    remove(id);
    const size_t index = items_.size();
    items_.push_back(SpatialItem{.id = id, .bounds = bounds});
    id_to_index_[id] = index;
    is_dirty_ = true;
  }

  bool remove(uint64_t id) override {
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) {
      return false;
    }
    const size_t index = it->second;
    const size_t last_index = items_.size() - 1;

    if (index != last_index) {
      items_[index] = items_[last_index];
      id_to_index_[items_[index].id] = index;
    }

    items_.pop_back();
    id_to_index_.erase(it);
    is_dirty_ = true;
    return true;
  }

  void update(uint64_t id, const Rect2d& new_bounds) override {
    auto it = id_to_index_.find(id);
    if (it != id_to_index_.end()) {
      items_[it->second].bounds = new_bounds;
      is_dirty_ = true;
    } else {
      insert(id, new_bounds);
    }
  }

  [[nodiscard]] std::vector<uint64_t> query_box(const Rect2d& box) const override {
    std::vector<uint64_t> results;
    if (items_.empty() || box.is_empty()) {
      return results;
    }

    if (bvh_nodes_.empty() && !items_.empty()) {
      // Linear scan fallback when tree not built
      for (const auto& item : items_) {
        if (box.intersects(item.bounds)) {
          results.push_back(item.id);
        }
      }
      return results;
    }

    // Traversal of BVH tree nodes
    std::vector<size_t> stack;
    stack.reserve(64);
    stack.push_back(0);

    while (!stack.empty()) {
      const size_t node_idx = stack.back();
      stack.pop_back();

      if (node_idx >= bvh_nodes_.size())
        continue;
      const auto& node = bvh_nodes_[node_idx];

      if (!node.bounds.intersects(box)) {
        continue;
      }

      if (node.is_leaf()) {
        for (size_t i = 0; i < node.count; ++i) {
          const auto& item = items_[node.left_or_item_offset + i];
          if (box.intersects(item.bounds)) {
            results.push_back(item.id);
          }
        }
      } else {
        if (node.left_or_item_offset != kInvalidIndex) {
          stack.push_back(node.left_or_item_offset);
        }
        if (node.right_child != kInvalidIndex) {
          stack.push_back(node.right_child);
        }
      }
    }

    return results;
  }

  [[nodiscard]] std::vector<uint64_t> query_point(Vec2d point) const override {
    const Rect2d probe(point.x - 1e-4, point.y - 1e-4, 2e-4, 2e-4);
    std::vector<uint64_t> candidates = query_box(probe);
    std::vector<uint64_t> exact_hits;
    exact_hits.reserve(candidates.size());

    for (uint64_t id : candidates) {
      auto it = id_to_index_.find(id);
      if (it != id_to_index_.end() && items_[it->second].bounds.contains(point)) {
        exact_hits.push_back(id);
      }
    }
    return exact_hits;
  }

  [[nodiscard]] std::vector<uint64_t> query_frustum(const Rect2d& frustum) const override {
    return query_box(frustum);
  }

  void clear() override {
    items_.clear();
    id_to_index_.clear();
    bvh_nodes_.clear();
    is_dirty_ = false;
  }

  [[nodiscard]] size_t size() const noexcept override {
    return items_.size();
  }

  [[nodiscard]] bool empty() const noexcept override {
    return items_.empty();
  }

  void rebuild() {
    bvh_nodes_.clear();
    if (items_.empty()) {
      is_dirty_ = false;
      return;
    }

    bvh_nodes_.reserve(items_.size() * 2);
    build_bvh_recursive(0, items_.size());
    is_dirty_ = false;
  }

  [[nodiscard]] bool is_dirty() const noexcept {
    return is_dirty_;
  }

private:
  static constexpr size_t kInvalidIndex = static_cast<size_t>(-1);
  static constexpr size_t kMaxLeafItems = 4;

  struct BvhNode {
    Rect2d bounds;
    size_t left_or_item_offset{kInvalidIndex};
    size_t right_child{kInvalidIndex};
    size_t count{0};  // 0 if interior node, >0 if leaf

    [[nodiscard]] bool is_leaf() const noexcept {
      return count > 0;
    }
  };

  size_t build_bvh_recursive(size_t start, size_t end) {
    const size_t count = end - start;
    const size_t node_idx = bvh_nodes_.size();
    bvh_nodes_.push_back(BvhNode{});

    // Compute bounding box
    Rect2d node_bounds = items_[start].bounds;
    for (size_t i = start + 1; i < end; ++i) {
      node_bounds = node_bounds.united(items_[i].bounds);
    }
    bvh_nodes_[node_idx].bounds = node_bounds;

    if (count <= kMaxLeafItems) {
      bvh_nodes_[node_idx].left_or_item_offset = start;
      bvh_nodes_[node_idx].right_child = kInvalidIndex;
      bvh_nodes_[node_idx].count = count;
      return node_idx;
    }

    // Split along the largest axis
    const bool split_x = node_bounds.width >= node_bounds.height;
    const auto comparator = [split_x](const SpatialItem& a, const SpatialItem& b) {
      return split_x ? (a.bounds.center().x < b.bounds.center().x)
                     : (a.bounds.center().y < b.bounds.center().y);
    };

    const size_t mid = start + count / 2;
    std::nth_element(items_.begin() + start, items_.begin() + mid, items_.begin() + end,
                     comparator);

    // Update indices map because elements were swapped
    for (size_t i = start; i < end; ++i) {
      id_to_index_[items_[i].id] = i;
    }

    const size_t left_child = build_bvh_recursive(start, mid);
    const size_t right_child = build_bvh_recursive(mid, end);

    bvh_nodes_[node_idx].left_or_item_offset = left_child;
    bvh_nodes_[node_idx].right_child = right_child;
    bvh_nodes_[node_idx].count = 0;

    return node_idx;
  }

  std::vector<SpatialItem> items_;
  std::unordered_map<uint64_t, size_t> id_to_index_;
  std::vector<BvhNode> bvh_nodes_;
  bool is_dirty_{false};
};

}  // namespace vector::core

#endif  // VECTOR_CORE_SPATIAL_INDEX_HPP
