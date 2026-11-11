// Madí - Model, Align & Diagram Interconnects
// Homage to Arte Madí (Buenos Aires, 1946) — Broken Canvas, Linear Grids & Articulated Spatial Structures
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_MADI_HPP
#define MADI_MADI_HPP

#include "vector/core/Geometry.hpp"
#include "vector/core/GraphModel.hpp"
#include "vector/core/HierarchicalBlock.hpp"
#include "vector/core/SpatialIndex.hpp"
#include "vector/core/Command.hpp"
#include "vector/core/CommandHistory.hpp"
#include "vector/core/ParametricShape.hpp"
#include "vector/core/SymbolGenerator.hpp"
#include "vector/grid/GridManager.hpp"
#include "vector/routing/OrthogonalRouter.hpp"
#include "vector/routing/RouterWorker.hpp"
#include "vector/routing/SnappingEngine.hpp"
#include "vector/io/StorageEngine.hpp"
#include "vector/io/DrawioParser.hpp"

namespace madi {
  // Alias vector namespace components
  using namespace vector;
}

#endif // MADI_MADI_HPP
