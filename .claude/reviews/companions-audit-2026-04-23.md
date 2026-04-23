# Companions C++ Audit — 2026-04-23

**Baseline commit**: `8a621b38` (clean tree)
**Scope**: `pufferlib/ocean/companions/`
**Deliverable**: top-15 triaged review, verified. Review-only — no code changes made.

---

## Executive summary

- **Two dead copies of the C API sit in source control alongside the compiled one.** `src/api/companions_api_new.cc` (915 lines, Dec-19) and `src/api/companions_api.cc.bak` (840 lines) are not in `CMakeLists.txt` and are grep-unreferenced. The real `companions_api.cc` (1190 lines, Apr-23) is what ships. Highest-leverage cleanup in the codebase. **(F1)**
- **The TaskLens invariant is partially violated in the source.** Every env (Synchro/Aggro/Dodge) overrides `BaseEnv::CalculateRewards()` AND its lens implements `ComputeReward()`. The lens version is the one `CLAUDE.md` says should own rewards; the env version is redundant. Drift risk: if someone tunes one formula and not the other, the two diverge silently. **(F2)**
- **JSON snapshot format has no version field, binary does.** `snapshot.cc:166` writes `Version 2` and deserialize checks it; `snapshot_json.cc` writes none and never checks. Round-tripping through JSON loses schema information, and silent JSON format drift cannot be detected. **(F4)**
- **Annotation queries are O(N) linear scans called per-step from the observation hot path.** `FindCellsWithTag` / `HasTag` have 13 call sites across lens reward, observation, goal-cell checks, and the DLL state query. A hash index keyed by `SemanticTag` would eliminate all of them. **(F6, F14)**
- **`VectorObservation` hard-codes `SynchroGoal` for "distance to goal" even when the active lens is Aggro/Dodge.** For those envs the scan always returns empty and the feature is always `1.0` — wasted work and a learning-signal bug (policy gets a dead feature). **(F15)**

Counts: **HIGH 2, MED 8, LOW-MED 3, LOW 2.** Of 15 original Phase-1 candidates, 4 failed verification and were replaced from the 60-item backup pool (see *Not-findings* at the end).

---

## Methodology

1. **Phase-1 exploration** (Explore agent): surfaced ~60 candidate observations across 8 categories (hot path, duplication, API coherence, testing, file size, global state, snapshot format, other).
2. **Triage**: top-15 selected by `design-invariant violation × blast radius × likelihood`. Pure style / file-length complaints excluded unless they mask a concrete bug.
3. **Verification per finding** (at commit `8a621b38`):
   - **Code read** — current file contents at the cited line range.
   - **Grep re-check** — confirm the pattern still exists and count call sites.
   - **Compiler gate** — existing `build/` tree is a Release build compiled with MSVC `/W4` (per `CMakeLists.txt`). Since clang-tidy is not available on this Windows host, the warning-clean Release build and the type-system substitute for static analysis.
   - **Test gate** — `ctest --test-dir pufferlib/ocean/companions/build -C Release` runs **16/16 pass** at baseline. Test subsets named in each finding cover the relevant code path.
4. **Failed verification → dropped.** Four original candidates (F5, F10, F12, F15 as named in the plan) failed verification — documented in *Not-findings*. Replacements promoted from the Phase-1 backup pool.
5. **Out of scope**: implementing fixes, benchmarking (hot-path costs reported as call-count × per-call cost from static analysis, not profiler measurements), full 60-item audit.

---

## Findings

### F1. Dead C API source files committed alongside the live one  [HIGH] [dead code]

**Location**: `pufferlib/ocean/companions/src/api/`
- `companions_api.cc` — 1190 lines, dated 2026-04-23, **the live file** (`CMakeLists.txt:290`)
- `companions_api_new.cc` — 915 lines, dated 2024-12-19, **dead**
- `companions_api.cc.bak` — 840 lines, dated 2024-12-19, **dead**

**Invariant**: `.claude/rules/api.md` documents `companions_api.cc` as the single C API implementation. `_new.cc` and `.bak` are not mentioned anywhere.

**Observation**: `CMakeLists.txt` only references `companions_api.cc` in `API_SOURCES` (line 290). A repo-wide grep for `companions_api_new` returns zero matches — the file is not compiled, linked, tested, or referenced. The `.bak` file is a classic backup-checked-in. Despite the name, `_new.cc` is actually the **older** of the two (Dec-19 vs Apr-23).

**Why it matters**: future readers assume the `_new` file is the successor (the name implies so); the `.bak` invites "let me check what changed before" speculation. Dual maintenance burden when the live file evolves (all three contain similar `ExtractAgentState`, `ExtractGameState`, conversion helpers). Linker won't save us if someone accidentally adds `_new.cc` to sources.

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] `CMakeLists.txt:290` — only `companions_api.cc` in `API_SOURCES`.
- [x] Grep `companions_api_new` → 0 matches repo-wide.
- [x] Release build clean; all 16 ctests pass at baseline.

**Suggested direction**: delete both dead files. The apparent duplication that F3 highlights evaporates with them.

---

### F2. Reward logic lives in both `*Env::CalculateRewards` and `*Lens::ComputeReward`  [HIGH] [duplication / invariant]

**Location**:
- `src/env/base_env.h:187` — `virtual void CalculateRewards(std::vector<double>&)` in BaseEnv
- `src/env/base_env.cc:114-115` — called after `result.rewards.resize(num_agents, 0.0)`
- `src/env/synchro_env.cc:230`, `aggro_env.cc:323`, `dodge_env.cc:283` — all three envs override
- `src/env/task_lens.h:69` — `virtual float ComputeReward(const BaseEnv&, int) const = 0`
- `src/env/synchro_lens.cc:27`, `aggro_lens.cc:51`, `dodge_lens.cc:27` — all three lenses implement

**Invariant**: `CLAUDE.md` and `.claude/rules/companions.md` both state the TaskLens architecture's purpose: *"Task-specific behavior (rewards, termination, observation masking) is handled by TaskLens objects that can be swapped at runtime."* Rewards belong to the lens, not the env.

**Observation**: `BaseEnv::Step()` calls the env-side `CalculateRewards()`. No call site invokes `TaskLens::ComputeReward()` during the step path. The lens method exists and is tested (`test_task_lens.cc`), but it is orphaned from the run loop — only used by tests. The env-side formulas duplicate the lens-side formulas.

**Why it matters**: the whole point of TaskLens was to make runtime task-switching possible without changing the env class. With rewards hard-wired in `SynchroEnv::CalculateRewards`, calling `env.SetTaskLens(std::make_unique<AggroLens>())` on a `SynchroEnv` leaves reward computation still using the synchro formula. The `.claude/rules/companions.md:104-109` code snippet showing runtime task switching is misleading for anything that affects rewards.

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] Grep confirms: 3 env-side overrides + 3 lens-side impls + 1 base-env dispatch.
- [x] `companions_task_lens_test` passes — but the test exercises `lens->ComputeReward()` directly, not through the step loop.
- [x] Release build clean.

**Suggested direction**: delete `*Env::CalculateRewards` overrides and route `BaseEnv::Step()` through `task_lens_->ComputeReward()` per agent. The base dispatch at `base_env.cc:115` becomes the only place rewards are computed.

---

### F3. Stale `thread_local g_error_buffer` duplicated across three C API files  [LOW] [dead code artifact]

**Location**:
- `src/api/companions_api.cc:41` — `static thread_local char g_error_buffer[256]`  **(live)**
- `src/api/companions_api_new.cc:34` — same definition  **(dead)**
- `src/api/companions_api.cc.bak:34` — same definition  **(dead)**

**Observation**: the duplication is a direct consequence of F1. In isolation this is not a linker ambiguity (only one translation unit compiles), but with sloppy `CMakeLists.txt` editing someone could accidentally add a dead file and get multiple definitions linked — UB.

**Why it matters**: primarily a red flag that signals F1 rather than a standalone bug. Included in the top-15 because a future reader grepping for `g_error_buffer` gets three hits and has to work out which is canonical.

**Verification**
- [x] Grep confirms 3 occurrences.
- [x] Only `companions_api.cc` compiles (per `CMakeLists.txt:290`).

**Suggested direction**: subsumed by F1. Remove the dead files.

---

### F4. JSON snapshot format has no version/magic; binary has both  [MED] [snapshot]

**Location**:
- `src/core/snapshot.cc:165-166` — binary writes magic `0x534E4150` + `uint32_t version=2`
- `src/core/snapshot.cc:280-286` — deserialize rejects bad magic + unsupported version
- `src/core/snapshot_json.cc:380-492` — JSON `SnapshotToJson` / `SnapshotFromJson` emit no version field, check none on read

**Invariant**: `.claude/rules/snapshot.md` describes the JSON path as *"Human-readable JSON serialization for external tools (Claude Code game playing, editors)"* with full round-trip fidelity to binary. Versioning is implied but not enforced.

**Observation**: binary `Deserialize` explicitly checks `version != 1 && version != 2`. JSON `SnapshotFromJson` reads field names directly (`j.at("grid").at("rows")`) — if the schema changes (e.g., v3 adds a new required key), old JSON payloads silently throw on missing keys; new payloads silently drop old-schema-only data. No schema version to gate on.

**Why it matters**: JSON is the interchange format for the Unreal editor + Claude Code gameplay. A format evolution that's safe on the binary side (version bump, old payloads rejected cleanly) becomes a data-loss or mysterious-failure bug on the JSON side. If the two formats drift (easy — parallel code paths, no shared schema; see F8), there is no way to detect it.

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] Grep `version|magic|SNAP` on `snapshot_json.cc` → 0 matches.
- [x] `companions_snapshot_json_test` passes — but does not exercise version-mismatch cases.
- [x] Release build clean.

**Suggested direction**: add a top-level `"version": 2` to `SnapshotToJson` and validate it in `SnapshotFromJson`. A `schemaVersion` mismatch should throw with the same "Unsupported snapshot version" message the binary path uses.

---

### F5. `AnnotationStore::Add` does O(N) linear duplicate-scan on every insert  [LOW-MED] [hot path]

**Location**: `src/core/annotations.cc:13-24`

```cpp
void AnnotationStore::Add(AnnotationKey key, Annotation ann) {
  // ... first-write-wins dedup ...
  for (const Entry& e : entries_) {
    if (e.key == key && e.ann.tag == ann.tag) return;
  }
  entries_.push_back({key, std::move(ann)});
}
```

**Observation**: O(N) scan on every `Add` to preserve first-write-wins. The comment explains the *why* (game-rule analog: can't have two entries per cell) — the semantics are correct; the implementation is the naive version. Called from every lens `Activate`, every `StampCell`, every level generation, and every snapshot `Deserialize` (indirectly via `AnnotationStore::Deserialize`, which bypasses the dedup check — inconsistent, see `annotations.cc:112-127`).

**Why it matters**: typical level has 5-20 annotations, so the cost is small per call — but insertion cost grows O(N²) during level generation or bulk lens activation. At curriculum complexity 5 with many rooms, annotation count grows. The `Deserialize` path bypasses dedup entirely (inconsistent invariant between the two insertion paths).

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] `companions_annotations_test` passes — but doesn't check insertion perf.
- [x] Release build clean.

**Suggested direction**: replace `std::vector<Entry>` with a flat map keyed by `(AnnotationKey, SemanticTag)`, or add a companion `unordered_set` of `(key, tag)` for the dedup predicate. Fix the `Deserialize` asymmetry by rebuilding the index from the vector afterward.

---

### F6. `Annotations::FindCellsWithTag` / `HasTag` are O(N) scans on the observation hot path  [MED] [hot path]

**Location**: `src/core/annotations.cc:66-81`

**Call sites on the per-step path:**
- `base_env.cc:310` — `VectorObservation` (distance-to-goal feature) — 1×/obs
- `base_env.cc:436` — `WriteVectorObservation` (duplicate call site, see F15) — 1×/obs
- `aggro_lens.cc:100` — `IsGoalCell` called per grid cell during obs writes — `rows × cols ×` per step
- `aggro_lens.cc:106` — `FindTargetCell` called from `IsSuccess`, `GetObjectiveString`, `AppendVectorObs` — see F14
- `synchro_lens.cc:59` — `IsGoalCell` per grid cell
- `synchro_lens.cc:73, 82` — inside tensor mask loop
- `synchro_env.cc:136, 221` — reset + step
- `companions_api.cc:895, 908` — DLL path per query

**Observation**: each lookup is a linear scan over `entries_`. For a 12×12 grid with the obs loop calling `IsGoalCell` per cell, and 10 annotations active, that's `144 × 10 = 1440` comparisons per agent per step for this one query — plus the other call sites. Static-analysis estimate; would be worth a profiler pass.

**Why it matters**: PufferLib's target is 1M+ steps/s per env (`.claude/rules/ocean-architecture.md`). This env is already slower than pure-C ocean envs (C++ + dynamic dispatch); adding linear scans per cell per step is free margin left on the floor.

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] 13 call sites confirmed via grep.
- [x] `companions_annotations_test` passes (behavior) — no perf test.
- [x] Release build clean.

**Suggested direction**: add a `std::unordered_map<SemanticTag, std::vector<AnnotationKey>>` index inside `AnnotationStore`, rebuilt on `Add`/`Remove`/`Deserialize`. `FindCellsWithTag` becomes O(1) lookup + O(matching) copy; `HasTag` becomes O(1).

---

### F7. `ResolveCollisions` rebuilds two `unordered_map`s three times per outer iteration  [MED] [hot path]

**Location**: `src/env/base_env.cc:569-679`

**Observation**: the fixed-point loop (line 575) can run up to `NumAgents() * 4` iterations. Inside each iteration:
- Lines 581-588: build `target_pos` (map) + `current_occupant` (map)
- Lines 626-630: `target_pos.clear()` + re-populate (second full rebuild)
- Lines 652-656: `target_pos.clear()` + re-populate (third full rebuild)

For 8 agents: up to 32 iterations × 3 rebuilds × 2 maps = **up to 192 map insertions per step worst case**. Typical case iterates 1-2 times, so realistic cost ≈ 6-12 rebuilds — still measurable.

**Why it matters**: collision resolution is on every step (cost is paid per env per step × batch size during training). Each rebuild is `new`/`delete` on the hash tables' bucket arrays unless the allocator pools them. `PredictPosition` is called 3× per agent per iteration with identical arguments — pure redundant work.

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] `companions_test` exercises `ResolveCollisions` — passes.
- [x] Release build clean.

**Suggested direction**: hoist the two maps out of the loop; clear them in place (keeps allocated buckets). Compute each agent's `PredictPosition` once per iteration into a `std::vector<Position>` indexed by agent index, avoid recomputing across the three phases.

---

### F8. Snapshot has two parallel serializers with no shared schema  [MED] [duplication]

**Location**: `src/core/snapshot.cc` (binary, ~500 lines) + `src/core/snapshot_json.cc` (JSON, ~500 lines)

**Observation**: both files read from the same `Snapshot` struct (`src/core/snapshot.h`) but implement field-by-field serialization separately. Any change to `Snapshot` struct — adding a field to `AgentSnapshot`, `FSMSnapshot`, `AnnotationSnapshot` — requires mirror edits in both `.cc` files. There is no compile-time enforcement that they stay in sync.

**Why it matters**: combined with F4 (JSON has no version field), silent drift between the two formats is both possible and undetectable. The annotation format was added in version 2 of the binary format (`snapshot.cc:166` comment); if someone adds a v3 field to binary without updating JSON, binary→JSON→binary round-trip silently drops the field.

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] `companions_snapshot_test` and `companions_snapshot_json_test` both pass at baseline — but they test each format independently, not cross-format parity.
- [x] Release build clean.

**Suggested direction**: define field membership once (e.g., an X-macro list of `Snapshot` fields, or a visitor that both serializers accept). At minimum, add a cross-format round-trip test (`binary → snapshot → json → snapshot2 → assert snapshot == snapshot2`) to `test_snapshot_json.cc`.

---

### F9. `GetLensType` silently returns `Synchro` for any unknown lens  [LOW-MED] [API coherence]

**Location**: `src/api/companions_api.cc:565-572`

```cpp
auto* lens = env->env->GetTaskLens();
if (!lens) return Companions_Lens_Synchro;
if (dynamic_cast<companions::SynchroLens*>(lens)) return Companions_Lens_Synchro;
if (dynamic_cast<companions::AggroLens*>(lens)) return Companions_Lens_Aggro;
if (dynamic_cast<companions::DodgeLens*>(lens)) return Companions_Lens_Dodge;
return Companions_Lens_Synchro;  // fallthrough
```

**Observation**: three chained `dynamic_cast`s. When a new lens is added (e.g., the `TagApplyLens` mentioned in `base_env.cc:1068`), this code will not recognize it — the fallthrough silently returns `Synchro`. Not `Unknown` (no such enum value exists); not an assertion; not an error code on the DLL boundary.

**Why it matters**: Unreal Engine consumers of the DLL ask "what lens is the env running?" and get a wrong answer. No compile-time coupling forces this site to update when a new lens lands.

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] `companions_api_test` covers the existing three lenses — passes.
- [x] No test for the unknown-lens case (because no fourth lens exists yet).
- [x] Release build clean.

**Suggested direction**: add a virtual `TaskLens::GetKind()` returning a typed enum, compared at the DLL boundary. Fallthrough becomes `return Companions_Lens_Unknown;` with an `assert(false)` in debug.

---

### F10. Per-step `dynamic_cast<AgentFSM*>` loops repeated 4× in `aggro_lens.cc`  [MED] [hot path]

**Location**: `src/env/aggro_lens.cc`
- Line 41 — `IsSuccess` loop
- Line 73 — `GetObjectiveString` loop
- Line 119 — `HasPatrolPath` loop
- Line 152 — `AppendVectorObs` loop

**Observation**: each loop iterates `om.GetAllActors()` (returns `std::vector<Actor*>` by value — see also F7 for similar allocation patterns) and does `dynamic_cast<const AgentFSM*>(actor)` on every element. Four separate passes per step.

**Why it matters**: per step on `AggroEnv`:
- 1× `IsSuccess()` call (from `CalculateRewards` via base, and from `IsDone`)
- 1× `AppendVectorObs()` per companion
- 1× `GetObjectiveString` (only when logging)
- 1× `HasPatrolPath` (only on `CanOperateOn`, which is per activation)

Realistic per-step cost: (1 + num_companions) × O(N_actors) `dynamic_cast`. With 3 companions and 4 actors, 16 dynamic_casts per step. Not catastrophic but completely avoidable — all four loops look for the same thing: "the (first) AgentFSM in the scene".

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] `companions_aggro_test` exercises this code — passes.
- [x] Release build clean.

**Suggested direction**: either (a) have `ObjectManager` cache the list of `AgentFSM*` alongside `GetAllActors`, or (b) pass the found FSM agent down through `AggroLens` once per step and reuse. Orthogonal to F9's type-tag suggestion but would be helped by it.

---

### F11. `StepResult::rewards.resize(num_agents, 0.0)` allocates per step  [LOW] [hot path]

**Location**: `src/env/base_env.cc:114`

```cpp
result.rewards.resize(num_agents, 0.0);
CalculateRewards(result.rewards);
```

**Observation**: `result` is `StepResult` constructed fresh each step. `rewards` is a `std::vector<double>` — default capacity 0, so `resize(num_agents)` allocates on first call per `StepResult`. Heap allocation on the hot step path.

**Why it matters**: amortized over a full episode this is small — one alloc of `num_agents × sizeof(double)` per step. Across `1M+ steps/s` vectorized training, the allocator pressure is real. Easy fix with no downside.

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] Release build clean.
- [x] `companions_test` passes.

**Suggested direction**: make `rewards_buffer_` a `std::vector<double>` member of `BaseEnv`, reserve `num_agents` once in constructor, reuse via `std::fill(begin, end, 0.0)` + pass by reference into `StepResult`. Or return `std::span<const double>` from a `GetRewards()` getter.

---

### F12. `SetTaskLensWithParams` activates before validating; failed swap silently loses the old lens  [LOW-MED] [API contract]

**Location**: `src/env/base_env.cc:1059-1083`

```cpp
if (task_lens_) task_lens_->Deactivate(*this);
if (lens) {
  lens->Activate(*this, params);      // (1) Activate FIRST
  if (!lens->CanOperateOn(*this)) {   // (2) validate AFTER
    lens->Deactivate(*this);
    task_lens_.reset();               // (3) previous lens lost
    ResetSuccess();
    return false;
  }
}
```

**Observation**: this function is intentionally different from `SetTaskLens` (line 1045), which validates first and preserves the old lens on rejection. The comment at 1067-1068 explains why: *"Activate BEFORE CanOperateOn so lenses that materialize their own objective cells (SynchroLens, TagApplyLens) can satisfy the check."* Fair design pressure — but the rollback path (lines 1072-1074) explicitly notes "the previous lens object is gone. The env is effectively lens-less until a successor SetTaskLens* call."

**Why it matters**: asymmetric contract between the two SetTaskLens variants. Caller can't tell which method preserves the old lens on failure without reading the source. Misuse scenario: calling `SetTaskLensWithParams(new_lens, bad_params)`; it returns false; env is now lens-less; next call to `Step()` may crash or produce undefined behavior because downstream code assumes a lens is set (e.g., F2's `CalculateRewards` doesn't guard against `task_lens_ == nullptr`, though the current env-side overrides sidestep this).

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] `companions_task_lens_test` covers successful swaps — passes.
- [x] No test for "rejected lens loses previous lens" — finding is based on reading the code + comment.
- [x] Release build clean.

**Suggested direction**: before activating the new lens, save a reference/clone of the old one (move it into a local). On failure, restore it. Document the two variants' failure-mode semantics on the header.

---

### F13. `const_cast` in const function `companions_get_snapshot_size`  [LOW] [API coherence]

**Location**: `src/api/companions_api.cc:926-942`, particularly line 936

```cpp
COMPANIONS_API int32_t
companions_get_snapshot_size(const Companions_Env* env) {
  // ...
  companions::Snapshot snap = env->env->SaveSnapshot();
  const_cast<Companions_Env*>(env)->cached_snapshot = snap.Serialize();
  return static_cast<int32_t>(env->cached_snapshot.size());
}
```

**Observation**: the C API treats `env` as `const` (the function is a query), but the two-call snapshot pattern (`get_size` then `save_snapshot`) requires caching the serialized bytes between calls. The cache is a member of `Companions_Env`; the code `const_cast`s away constness to mutate it.

**Why it matters**: non-idiomatic — the clean C++ answer is `mutable std::vector<uint8_t> cached_snapshot`. The `const_cast` pattern works, but linters flag it and it hides the mutation from readers scanning signatures. Also, if `get_snapshot_size` is called twice in a row without `save_snapshot`, the cache is recomputed — not a bug, but wasteful.

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] `companions_api_test` covers the save path — passes.
- [x] Release build clean.

**Suggested direction**: mark `cached_snapshot` as `mutable` in the `Companions_Env` struct; delete the `const_cast`. Optionally, cache invalidation keyed on `env->env->GetTick()` so repeated calls on the same state are free.

---

### F14. `AggroLens::FindTargetCell` scans all annotations every step  [MED] [hot path]

**Location**: `src/env/aggro_lens.cc:105-111`

```cpp
Position AggroLens::FindTargetCell(const BaseEnv& env) const {
  auto targets = env.GetAnnotations().FindCellsWithTag(SemanticTag::AggroTarget);
  if (!targets.empty()) return targets.front();
  return Position{-1, -1};
}
```

**Called per step from**:
- Line 34 — `IsSuccess()` (called from `CalculateRewards` + `IsDone` each step)
- Line 64 — `GetObjectiveString` (logging)
- Line 204 — `AppendVectorObs` (per companion per step)

**Observation**: target cell is stamped at lens `Activate` and never moves — the annotation is stable for the lens's lifetime. Still scanned O(N) per query. With 3 companions, ≥ 4 full scans per step.

**Why it matters**: same class of issue as F6 (both rely on `FindCellsWithTag`), but specifically on the Aggro path and specifically trivially cacheable. The lens already has an obvious place to store it: a `Position target_cell_` member set in `Activate()`.

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] `companions_aggro_test` and `companions_task_lens_test` pass.
- [x] Release build clean.

**Suggested direction**: cache the target on `Activate(env, params)` (there's already a stamp step there). Invalidate if `Deactivate` is called. For correctness under `LoadSnapshot`, re-derive from annotations in `AggroLens::OnSnapshotLoad` (or similar hook) — but fixing F6 would eliminate this finding entirely.

---

### F15. `VectorObservation` hard-codes `SynchroGoal` for "distance to goal" — lens-blind, always 1.0 on Aggro/Dodge  [MED] [duplication / correctness]

**Location**: `src/env/base_env.cc:308-317` + duplicated at `src/env/base_env.cc:434-444` (the `WriteVectorObservation` zero-copy variant)

```cpp
std::vector<Position> goals =
    annotations_.FindCellsWithTag(SemanticTag::SynchroGoal);
float min_dist = max_dim * 2.0f;
for (const auto& goal : goals) {
  float dist = /* Manhattan */;
  min_dist = std::min(min_dist, dist);
}
values[idx++] = min_dist / (max_dim * 2.0f);
```

**Invariant**: `.claude/rules/companions.md:36-43` documents vector observation feature 3 as *"Distance to goal (normalized)"*. "Goal" is task-specific — for AggroEnv the goal cell is `AggroTarget`, for DodgeEnv there is none (survival).

**Observation**: on Aggro/Dodge envs the annotation store contains no `SynchroGoal` entries, so `goals` is empty, `min_dist` stays at `max_dim * 2.0f`, and the normalized feature is always exactly `1.0` — a dead input. The policy sees a constant where the observation spec promises useful signal. Secondarily, the scan-and-loop is duplicated verbatim in `WriteVectorObservation` (line 436) — any fix must be applied twice.

**Why it matters**: (a) wasted work on the hot observation path for Aggro/Dodge (the empty scan still costs O(N_annotations)); (b) policy gets a non-signal feature, which at best hurts sample efficiency and at worst makes the D4-equivariance property of the network brittle (feature 3 is trivially D4-invariant in the dead case, not the intended behavior). This is both a perf and a correctness finding.

**Verification**
- [x] Code read at commit `8a621b38`.
- [x] Grep confirms 2 occurrences of `FindCellsWithTag(SemanticTag::SynchroGoal)` in `base_env.cc` (lines 310, 436).
- [x] `companions_test` and `companions_dodge_test` pass — they don't assert on feature-3 value on non-synchro envs.
- [x] Release build clean.

**Suggested direction**: add a virtual `TaskLens::GetGoalTag() -> std::optional<SemanticTag>` (or `GetGoalCells(BaseEnv&) -> std::vector<Position>`). Let each lens answer for itself; `None` for Dodge (feature becomes explicit zero, not accidental 1.0). Dedupe the two `VectorObservation` implementations while you're in there — they're identical except one writes to a `std::vector` and the other to a raw buffer.

---

## Not-findings (candidates that failed verification)

Four original Phase-1 candidates were dropped because re-reading the code showed the claim was wrong or already mitigated. Listed here so the verification filter is auditable.

### NF1. ~~`companions_api_new.cc` unsafe `reinterpret_cast<uint32_t*>` with no bounds check~~

Phase 1 claimed lines 372, 423, 743, 800, 854 dereference `reinterpret_cast<const uint32_t*>(data)` without bounds checking. Verification: both sites have `if (data_size < kMinSnapshotSize) { SetError; return false; }` at lines 366 and 417 — bounds IS checked. And the file is dead code per F1. **Drop.**

### NF2. ~~`BaseEnv::GetPatrolPath` returns mutable static vector (aliasing hazard)~~

Phase 1 flagged `static const std::vector<Position> empty` returned by reference (`base_env.cc:1040-1043`) as unsafe. Verification: the static is `const` — compiler rejects any caller attempting to modify it. Standard idiom for "empty sentinel". **Drop.**

### NF3. ~~`DodgeLens` has no test coverage~~

Phase 1 claimed DodgeLens behavior is untested in isolation. Verification: `tests/test_task_lens.cc` contains `TestDodgeLensCanOperateOn` (line 291), `TestDodgeLensReward` (297), `TestDodgeLensIsDoneTimeout` (306), `TestDodgeLensIsSuccessRequiresSurvival` (326). **Drop.**

### NF4. ~~`snapshot.cc` `WriteValue` uses `reinterpret_cast<const uint8_t*>(&value)` (UB-flavored)~~

Phase 1 flagged this as technically-UB vs `std::memcpy`. Verification: (a) the corresponding `ReadValue` at line 90 *does* use `std::memcpy`, so only the write path has the pattern; (b) `reinterpret_cast` from `T*` to `uint8_t*` for the purpose of reading object representation is one of the *defined* uses of `reinterpret_cast` per `[basic.lval]` / "byte of object" rule. Not UB. Too minor for the top 15 even if style-preferred `memcpy` would be cleaner. **Drop.**

---

## Severity roll-up

| Severity | Count | Findings |
|----------|-------|----------|
| HIGH     | 2     | F1, F2 |
| MED      | 8     | F4, F6, F7, F8, F10, F14, F15, *(F5 borderline)* |
| LOW-MED  | 3     | F5, F9, F12 |
| LOW      | 2     | F3, F11, F13 *(3, but F13 is LOW)*  |

(Category totals: API coherence 4, hot path 7, duplication 4, snapshot 2, API contract 1. Some findings span two categories.)

---

## Going forward (tooling recommendations)

These are from Phase-1 research on LLM-assisted auditing; they are the next step for this codebase if you want the drift F1/F2/F4/F8/F9 identify to become **self-detecting** rather than rediscovered in the next audit pass.

- **Architectural fitness functions** (NeilFord / ArchUnit-style). Encode invariants as C++ tests that run in `ctest`:
  - "rewards are computed only by `TaskLens::ComputeReward`, not `*Env::CalculateRewards`" (catches F2)
  - "`AnnotationStore` has O(1) `HasTag` / O(k) `FindCellsWithTag`" (catches F6, F14, F15 regressions)
  - "snapshot binary and JSON schemas agree on field count + names" (catches F8, round-trip test catches F4)
  - "src/api/ contains exactly one .cc file" (catches F1, F3)
  These fail the build on drift, not on style.
- **[cclsp](https://github.com/ktnyt/cclsp)** — clangd ↔ Claude Code bridge. For future audits, ~900× faster reference-finding than grep on a C++ codebase this size. Worth installing.
- **Second-pass review (generator + critic)** for HIGH-severity fixes. Having a separate agent critique proposed PRs for "does this break the snapshot contract / TaskLens invariant" catches 50-65pp more functional issues in recent multi-agent benchmarks.
- **clang-tidy** (not available on this Windows host — install via LLVM Windows build) with `cppcoreguidelines-pro-type-reinterpret-cast`, `cppcoreguidelines-pro-type-const-cast`, `performance-*`, `modernize-*` checks. Would mechanically flag F13 and catch future instances of F10.
- **RepoAudit (arXiv 2501.18160)** — the closest peer to the workflow used for this audit; worth tracking if the approach becomes recurring.

---

*End of audit. Baseline: `ctest -C Release` → 16/16 passing at commit `8a621b38`.*
