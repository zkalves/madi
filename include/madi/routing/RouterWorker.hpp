// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_ROUTING_ROUTER_WORKER_HPP
#define MADI_ROUTING_ROUTER_WORKER_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

#include "../core/Geometry.hpp"
#include "../core/GraphModel.hpp"

namespace madi::routing {

struct ObstacleRect {
  uint64_t id{0};
  core::Rect2d bounds;
  double padding{8.0};

  [[nodiscard]] core::Rect2d expanded_bounds() const noexcept {
    return bounds.expanded(padding);
  }
};

struct RouteNetRequest {
  uint64_t edge_id{0};
  core::Vec2d source_point;
  core::PortDirection source_direction{core::PortDirection::Omnidirectional};
  core::Vec2d target_point;
  core::PortDirection target_direction{core::PortDirection::Omnidirectional};
  core::RoutingStyle routing_style{core::RoutingStyle::Orthogonal};
};

struct RouteNetResult {
  uint64_t edge_id{0};
  std::vector<core::Vec2d> waypoints;
  bool success{false};
};

struct RouteBatchRequest {
  uint64_t batch_id{0};
  std::vector<ObstacleRect> obstacles;
  std::vector<RouteNetRequest> nets;
};

struct RouteBatchResult {
  uint64_t batch_id{0};
  std::vector<RouteNetResult> results;
};

class IRouterEngine {
public:
  virtual ~IRouterEngine() = default;
  virtual RouteBatchResult compute_routes(const RouteBatchRequest& request) = 0;
};

// Built-in orthogonal obstacle-avoidance router using A* visibility search
class OrthogonalAvoidanceEngine : public IRouterEngine {
public:
  RouteBatchResult compute_routes(const RouteBatchRequest& request) override;

private:
  std::vector<core::Vec2d> route_single_net(const RouteNetRequest& net,
                                            const std::vector<ObstacleRect>& obstacles);
};

class RouterWorker {
public:
  using ResultCallback = std::function<void(RouteBatchResult)>;

  explicit RouterWorker(std::unique_ptr<IRouterEngine> engine = nullptr);
  ~RouterWorker();

  // Submits a routing batch. If frozen, replaces pending batch.
  void dispatch_routing(RouteBatchRequest request);

  // Performance optimization: freeze routing during multi-node interactive drags
  void freeze();
  void unfreeze_and_dispatch(RouteBatchRequest request);
  [[nodiscard]] bool is_frozen() const noexcept {
    return is_frozen_.load(std::memory_order_relaxed);
  }

  // Result retrieval (polling or callback)
  [[nodiscard]] std::optional<RouteBatchResult> poll_result();
  void set_result_callback(ResultCallback callback);

  void cancel_all();
  void wait_until_idle();

private:
  void worker_loop(std::stop_token st);

  std::unique_ptr<IRouterEngine> engine_;
  std::atomic<bool> is_frozen_{false};
  std::atomic<bool> is_busy_{false};
  std::optional<RouteBatchRequest> deferred_request_;

  mutable std::mutex queue_mutex_;
  std::condition_variable cv_work_;
  std::condition_variable cv_idle_;
  std::queue<RouteBatchRequest> pending_batches_;
  std::queue<RouteBatchResult> completed_batches_;

  ResultCallback result_callback_;
  std::jthread worker_thread_;
};

}  // namespace madi::routing

#endif // MADI_ROUTING_ROUTER_WORKER_HPP
