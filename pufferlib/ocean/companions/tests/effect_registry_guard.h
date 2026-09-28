// Copyright 2024
// ScopedEffectRegistry - test guard for the global EffectConfigRegistry

#ifndef COMPANIONS_TESTS_EFFECT_REGISTRY_GUARD_H_
#define COMPANIONS_TESTS_EFFECT_REGISTRY_GUARD_H_

#include "../src/core/effect_config.h"

namespace companions {

// Clears the global EffectConfigRegistry (back to the builtins) when created
// and again when destroyed, so the configs a test registers never leak into
// the next test, even when an assertion throws halfway. Declare it before any
// env whose effects point into the registry: locals are destroyed in reverse
// order, so the envs go first.
class ScopedEffectRegistry {
 public:
  ScopedEffectRegistry() { EffectConfigRegistry::Instance().Clear(); }
  ~ScopedEffectRegistry() { EffectConfigRegistry::Instance().Clear(); }
  ScopedEffectRegistry(const ScopedEffectRegistry&) = delete;
  ScopedEffectRegistry& operator=(const ScopedEffectRegistry&) = delete;
};

}  // namespace companions

#endif  // COMPANIONS_TESTS_EFFECT_REGISTRY_GUARD_H_
