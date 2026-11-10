// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vector/routing/RouterWorker.hpp"

#include <cassert>
#include <iostream>

using namespace vector;

int main() {
  std::cout << "===> Running RoutingSmokeTest...\n";

  // 1. Direct Orthogonal Routing without Obstacles
  routing::RouteBatchRequest req1;
  req1.batch_id = 1;
  req1.nets.push_back(routing::RouteNetRequest{
    .edge_id = 101,
    .source_point = {100.0, 100.0},
    .source_direction = core::PortDirection::East,
    .target_point = {300.0, 200.0},
    .target_direction = core::PortDirection::West,
    .routing_style = core::RoutingStyle::Orthogonal
  });

  routing::OrthogonalAvoidanceEngine router;
  auto res1 = router.compute_routes(req1);
  assert(!res1.results.empty());
  assert(res1.results[0].success);
  assert(res1.results[0].waypoints.size() >= 3);

  // 2. Obstacle-avoiding Routing around Intervening Obstacle
  routing::RouteBatchRequest req2;
  req2.batch_id = 2;
  req2.obstacles.push_back(routing::ObstacleRect{
    .id = 999,
    .bounds = {180.0, 80.0, 60.0, 60.0},
    .padding = 10.0
  });
  req2.nets.push_back(routing::RouteNetRequest{
    .edge_id = 102,
    .source_point = {100.0, 100.0},
    .source_direction = core::PortDirection::East,
    .target_point = {300.0, 100.0},
    .target_direction = core::PortDirection::West,
    .routing_style = core::RoutingStyle::Orthogonal
  });

  auto res2 = router.compute_routes(req2);
  assert(!res2.results.empty());
  assert(res2.results[0].success);
  assert(res2.results[0].waypoints.size() >= 4);

  // 3. Concurrent RouterWorker (std::jthread & freeze testing)
  routing::RouterWorker worker;
  worker.freeze();
  assert(worker.is_frozen());

  worker.unfreeze_and_dispatch(req2);
  assert(!worker.is_frozen());
  worker.wait_until_idle();

  auto poll_res = worker.poll_result();
  assert(poll_res.has_value());
  assert(poll_res->batch_id == 2);
  assert(poll_res->results.size() == 1);

  std::cout << "===> RoutingSmokeTest passed successfully!\n";
  return 0;
}
