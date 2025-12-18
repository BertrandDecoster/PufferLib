// Copyright 2024
// GenerationContext - shared state for procedural map generators

#ifndef COMPANIONS_CORE_GENERATION_CONTEXT_H_
#define COMPANIONS_CORE_GENERATION_CONTEXT_H_

#include "grid.h"
#include "pcg32.h"

namespace companions {

// Forward declaration
struct MapConfig;

// Shared context passed to all procedural generators.
// Provides access to the grid being generated, RNG state, and config.
struct GenerationContext {
  Grid& grid;
  pcg32& rng;
  const MapConfig& config;

  GenerationContext(Grid& g, pcg32& r, const MapConfig& c)
      : grid(g), rng(r), config(c) {}
};

}  // namespace companions

#endif  // COMPANIONS_CORE_GENERATION_CONTEXT_H_
