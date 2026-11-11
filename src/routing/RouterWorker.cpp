// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "madi/routing/RouterWorker.hpp"

#include <algorithm>
#include <cmath>

namespace madi::routing {

namespace {

inline bool segment_intersects_box(core::Vec2d a, core::Vec2d b, const core::Rect2d& r) {
  // Fast check: bounding boxes overlap
  const double min_x = std::min(a.x, b.x);
  const double max_x = std::max(a.x, b.x);
  const double min_y = std::min(a.y, b.y);
  const double max_y = std::max(a.y, b.y);

  if (max_x < r.x || min_x > r.x + r.width || max_y < r.y || min_y > r.y + r.height) {
    return false;
  }

  // If vertical segment
  if (std::abs(a.x - b.x) < 1e-4) {
    return (a.x >= r.x && a.x <= r.x + r.width && min_y <= r.y + r.height && max_y >= r.y);
  }
  // If horizontal segment
  if (std::abs(a.y - b.y) < 1e-4) {
    return (a.y >= r.y && a.y <= r.y + r.height && min_x <= r.x + r.width && max_x >= r.x);
  }

  return true;
}

inline void simplify_collinear(std::vector<core::Vec2d>& pts) {
  if (pts.size() < 3) return;
  std::vector<core::Vec2d> result;
  result.push_back(pts[0]);

  for (size_t i = 1; i + 1 < pts.size(); ++i) {
    const auto& prev = result.back();
    const auto& curr = pts[i];
    const auto& next = pts[i + 1];

    const bool col_x = (std::abs(prev.x - curr.x) < 1e-3 && std::abs(curr.x - next.x) < 1e-3);
    const bool col_y = (std::abs(prev.y - curr.y) < 1e-3 && std::abs(curr.y - next.y) < 1e-3);
    if (!col_x && !col_y) {
      result.push_back(curr);
    }
  }
  result.push_back(pts.back());
  pts = std::move(result);
}

} // namespace

std::vector<core::Vec2d> OrthogonalAvoidanceEngine::route_single_net(
    const RouteNetRequest& net, const std::vector<ObstacleRect>& obstacles) {
  const core::Vec2d p_src = net.source_point;
  const core::Vec2d p_tgt = net.target_point;

  if (net.routing_style == core::RoutingStyle::Direct) {
    return {p_src, p_tgt};
  }

  const double stub = 20.0;
  const core::Vec2d v_src = core::direction_to_vector(net.source_direction);
  const core::Vec2d v_tgt = core::direction_to_vector(net.target_direction);

  const core::Vec2d s_stub = (net.source_direction != core::PortDirection::Omnidirectional)
                                 ? (p_src + v_src * stub)
                                 : p_src;
  const core::Vec2d t_stub = (net.target_direction != core::PortDirection::Omnidirectional)
                                 ? (p_tgt + v_tgt * stub)
                                 : p_tgt;

  // Check if any obstacle intersects direct orthogonal path
  const ObstacleRect* hit_obs = nullptr;
  for (const auto& obs : obstacles) {
    const core::Rect2d eb = obs.expanded_bounds();
    // Test direct segment between stubs
    if (segment_intersects_box(s_stub, t_stub, eb) ||
        segment_intersects_box(s_stub, {s_stub.x, t_stub.y}, eb) ||
        segment_intersects_box({s_stub.x, t_stub.y}, t_stub, eb)) {
      hit_obs = &obs;
      break;
    }
  }

  std::vector<core::Vec2d> wps;
  wps.push_back(p_src);
  if (s_stub.distance_sq_to(p_src) > 1e-3) {
    wps.push_back(s_stub);
  }

  if (hit_obs) {
    const core::Rect2d eb = hit_obs->expanded_bounds();
    // Determine whether to bypass horizontally or vertically
    const bool route_horizontal = (std::abs(p_src.x - p_tgt.x) >= std::abs(p_src.y - p_tgt.y));
    if (route_horizontal) {
      // Bypass vertically (above or below the obstacle)
      const double bypass_y = (s_stub.y <= eb.center().y) ? (eb.y - 12.0) : (eb.y + eb.height + 12.0);
      wps.push_back({s_stub.x, bypass_y});
      wps.push_back({t_stub.x, bypass_y});
    } else {
      // Bypass horizontally (left or right of obstacle)
      const double bypass_x = (s_stub.x <= eb.center().x) ? (eb.x - 12.0) : (eb.x + eb.width + 12.0);
      wps.push_back({bypass_x, s_stub.y});
      wps.push_back({bypass_x, t_stub.y});
    }
  } else {
    // Standard clean 2-bend orthogonal routing
    if (std::abs(v_src.x) > 0.5) {
      const double mid_x = (s_stub.x + t_stub.x) * 0.5;
      wps.push_back({mid_x, s_stub.y});
      wps.push_back({mid_x, t_stub.y});
    } else {
      const double mid_y = (s_stub.y + t_stub.y) * 0.5;
      wps.push_back({s_stub.x, mid_y});
      wps.push_back({t_stub.x, mid_y});
    }
  }

  if (t_stub.distance_sq_to(p_tgt) > 1e-3) {
    wps.push_back(t_stub);
  }
  wps.push_back(p_tgt);

  simplify_collinear(wps);
  return wps;
}

RouteBatchResult OrthogonalAvoidanceEngine::compute_routes(const RouteBatchRequest& request) {
  RouteBatchResult result;
  result.batch_id = request.batch_id;
  result.results.reserve(request.nets.size());

  for (const auto& net : request.nets) {
    RouteNetResult nr;
    nr.edge_id = net.edge_id;
    nr.waypoints = route_single_net(net, request.obstacles);
    nr.success = !nr.waypoints.empty();
    result.results.push_back(std::move(nr));
  }

  return result;
}

RouterWorker::RouterWorker(std::unique_ptr<IRouterEngine> engine)
    : engine_(engine ? std::move(engine) : std::make_unique<OrthogonalAvoidanceEngine>()),
      worker_thread_([this](std::stop_token st) { worker_loop(st); }) {}

RouterWorker::~RouterWorker() {
  cancel_all();
  if (worker_thread_.joinable()) {
    worker_thread_.request_stop();
    cv_work_.notify_all();
    worker_thread_.join();
  }
}

void RouterWorker::dispatch_routing(RouteBatchRequest request) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  if (is_frozen_.load(std::memory_order_relaxed)) {
    deferred_request_ = std::move(request);
  } else {
    pending_batches_.push(std::move(request));
    cv_work_.notify_one();
  }
}

void RouterWorker::freeze() {
  is_frozen_.store(true, std::memory_order_relaxed);
}

void RouterWorker::unfreeze_and_dispatch(RouteBatchRequest request) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  is_frozen_.store(false, std::memory_order_relaxed);
  deferred_request_.reset();
  pending_batches_.push(std::move(request));
  cv_work_.notify_one();
}

std::optional<RouteBatchResult> RouterWorker::poll_result() {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  if (completed_batches_.empty()) {
    return std::nullopt;
  }
  auto res = std::move(completed_batches_.front());
  completed_batches_.pop();
  return res;
}

void RouterWorker::set_result_callback(ResultCallback callback) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  result_callback_ = std::move(callback);
}

void RouterWorker::cancel_all() {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  deferred_request_.reset();
  while (!pending_batches_.empty()) pending_batches_.pop();
}

void RouterWorker::wait_until_idle() {
  std::unique_lock<std::mutex> lock(queue_mutex_);
  cv_idle_.wait(lock, [&]() {
    return pending_batches_.empty() && !is_busy_.load(std::memory_order_relaxed);
  });
}

void RouterWorker::worker_loop(std::stop_token st) {
  while (!st.stop_requested()) {
    RouteBatchRequest current_job;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      cv_work_.wait(lock, [&]() {
        return st.stop_requested() || (!pending_batches_.empty() && !is_frozen_.load(std::memory_order_relaxed));
      });

      if (st.stop_requested()) {
        break;
      }

      if (!pending_batches_.empty()) {
        current_job = std::move(pending_batches_.front());
        pending_batches_.pop();
        is_busy_.store(true, std::memory_order_relaxed);
      } else {
        continue;
      }
    }

    if (engine_) {
      RouteBatchResult res = engine_->compute_routes(current_job);
      ResultCallback cb;
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        completed_batches_.push(res);
        cb = result_callback_;
        is_busy_.store(false, std::memory_order_relaxed);
        cv_idle_.notify_all();
      }
      if (cb) {
        cb(std::move(res));
      }
    } else {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      is_busy_.store(false, std::memory_order_relaxed);
      cv_idle_.notify_all();
    }
  }
}

} // namespace madi::routing
