// Copyright 2024
// BaseEnv for The Companions game

#ifndef COMPANIONS_ENV_BASE_ENV_H_
#define COMPANIONS_ENV_BASE_ENV_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../core/annotations.h"
#include "../core/context_skill.h"
#include "../core/d4_transform.h"
#include "../core/pcg32.h"
#include "../core/reaction.h"
#include "../core/effect_config.h"
#include "../core/grid.h"
#include "../core/object_manager.h"
#include "../core/skill_config.h"
#include "../core/snapshot.h"
#include "../core/tag_table.h"
#include "../core/types.h"
#include "task_lens.h"

namespace companions {

// Forward declaration
class EffectSystem;

// =============================================================================
// Rectangle - axis-aligned bounding box for spatial filtering
// =============================================================================
struct Rectangle {
  int top;
  int left;
  int width;
  int height;

  bool Contains(Position pos) const {
    return pos.row >= top && pos.row < top + height &&
           pos.col >= left && pos.col < left + width;
  }
};

// =============================================================================
// StepResult - returned by Step()
// =============================================================================
struct StepResult {
  bool done = false;
  std::vector<double> rewards;
  std::string info;
};

// Why an episode ended (BaseEnv::GetEndReason). The values are those of
// Companions_EndReason in the C API.
enum class EndReason : int {
  None = 0,        // Not done
  Success = 1,     // The latched success
  Horizon = 2,     // The horizon was reached without an outcome
  TaskFailed = 3,  // The task can no longer succeed (e.g. Aggro's enemy is dead)
  TeamDown = 4,    // The team is down: max_downs reached, or every companion down at once (the level is lost)
};

// =============================================================================
// BaseEnv - RL-style environment base class
// =============================================================================
class BaseEnv {
 public:
  explicit BaseEnv(int rows = kDefaultGridSize, int cols = kDefaultGridSize,
                   int d4_transform = 0);
  virtual ~BaseEnv();

  // Copyable (for OpenSpiel State::Clone()). Assignment replaces the TagTable
  // (ids may change): the C API relies on its env's table only growing, so it
  // never assigns over an env it wraps.
  BaseEnv(const BaseEnv& other);
  BaseEnv& operator=(const BaseEnv& other);

  // RL interface
  virtual void Reset() = 0;
  virtual void Reset(unsigned int seed) = 0;
  // A step that throws is aborted (AbortStep): the env is between two steps
  // again, but keeps what the step did before the throw. One that throws
  // after its timers ticked (TickZones: the downs report, PostStep, the
  // lens's rewards and outcome latch) keeps the incremented tick too; only
  // what came after the throw is missing.
  virtual StepResult Step(const std::vector<Action>& actions);
  // Done: the env's own rule (IsEnvDone: success, horizon, a failure it
  // honours), or the team is down (whatever the env). The cheap term first.
  bool IsDone() const { return IsEnvDone() || IsTeamDown(); }

  // Downs. Every companion going down counts (revived or not); the level is
  // lost (EndReason::TeamDown) once the count reaches max_downs, or when every
  // companion is down at once. max_downs is level data (default 3, >= 1):
  // snapshots carry it (LoadSnapshot sets it), a generated Reset keeps it.
  static constexpr int kDefaultMaxDowns = companions::kDefaultMaxDowns;  // core/types.h
  int GetDowns() const;
  int GetMaxDowns() const { return max_downs_; }
  // False below 1. Level data: meant to be set at load or between episodes.
  // A mid-episode change re-evaluates the verdict: raised above the downs, it
  // can make IsDone() false again, and the next latch (LatchEndReason) then
  // clears a latched end reason.
  bool SetMaxDowns(int max_downs);
  bool IsTeamDown() const;

  // Latched success flag. BaseEnv::Step sets it once the active TaskLens
  // reports IsSuccess; it stays set until ResetSuccess is called (e.g. by
  // SetTaskLens). Subclasses normally should not override.
  virtual bool IsSuccess() const { return success_; }
  virtual void ResetSuccess() { success_ = false; }
  // Latched task failure: BaseEnv::Step sets it once the active TaskLens
  // reports IsFailed (the task can no longer succeed, e.g. Aggro's enemy is
  // dead). An outcome, once latched, is final: success and failure exclude
  // each other. SetTaskLens, LoadSnapshot and Reset clear both (ResetOutcome).
  bool IsTaskFailed() const { return failed_; }
  void ResetOutcome() {
    ResetSuccess();
    failed_ = false;
    end_reason_ = EndReason::None;
  }
  // Why the episode is done, i.e. what ended it: None while IsDone() is
  // false, else Success (the latched success), else TeamDown (IsTeamDown),
  // else TaskFailed when the env is done even without the horizon
  // (IsDoneWithoutHorizon: a latched failure the env's IsEnvDone honours, as
  // AggroEnv's under the Aggro lens, or the env's own end rule: a Dodge
  // companion died, an Aggro enemy killed between steps), else Horizon
  // (tick >= horizon). A latched failure the env's
  // IsEnvDone ignores (a Dodge lens on SynchroEnv / AggroEnv) did not end the
  // episode: it ends at the horizon, as Horizon. Hosts that keep playing
  // past a task failure tell it from a time out with this.
  // The reason is fixed when done first becomes true (latched by Step,
  // SetTaskLens* and LoadSnapshot): a kill after the horizon keeps Horizon.
  // Between those calls (e.g. a kill between steps) it is evaluated live.
  virtual EndReason GetEndReason() const;

  // Task lens management
  bool SetTaskLens(std::unique_ptr<TaskLens> lens);

  // Set a lens AND hand it parameters to materialize objective cells.
  // Activates before CanOperateOn so lenses that stamp their own objective
  // cells can satisfy the readiness check.
  bool SetTaskLensWithParams(std::unique_ptr<TaskLens> lens,
                              const LensParams& params);

  TaskLens* GetTaskLens() const { return task_lens_.get(); }

  // Clone this environment (virtual for polymorphic copy in OpenSpiel)
  virtual std::unique_ptr<BaseEnv> Clone() const = 0;

  // Observation
  // Returns a 5-plane observation tensor [5 × rows × cols]:
  //   Plane 0: Floor cells (1.0 if walkable, including synchro cells)
  //   Plane 1: Wall cells (1.0 if wall)
  //   Plane 2: Synchro cells (1.0 if synchro/goal cell)
  //   Plane 3: Current player position (1.0 at player's location)
  //   Plane 4: Other agents positions (1.0 at other agent locations)
  virtual std::string ToString() const;
  virtual void ObservationTensor(std::vector<float>& values, int player = 0) const;
  virtual std::vector<int> ObservationShape() const;
  std::vector<int> ObservationTensorShape() const { return ObservationShape(); }

  // Vector Observation - flat feature vector alternative to tensor
  // Returns a 1D vector with hand-crafted features suitable for MLP-based RL.
  // Base features (per player view):
  //   - Own position (row, col) normalized to [0,1]
  //   - Own health / max_health
  //   - Distance to nearest goal cell (normalized)
  //   - Relative positions of other companions (dx, dy per companion)
  // Subclasses may extend with environment-specific features.
  virtual void VectorObservation(std::vector<float>& values, int player = 0) const;
  virtual int VectorObservationSize() const;

  // Direct-write observation methods (zero-copy for C bindings)
  // These write directly to a pre-allocated buffer, avoiding std::vector allocation
  void WriteObservationTensor(float* buffer, int player = 0) const;
  void WriteVectorObservation(float* buffer, int player = 0) const;

  // Utility bounds (for MCTS and planning algorithms)
  // Pure virtual - each environment defines its own reward structure
  virtual double MinUtility() const = 0;
  virtual double MaxUtility() const = 0;
  int GetHorizon() const { return horizon_; }

  // Action space
  int NumAgents() const;
  int NumActions() const { return kNumMovementActions; }
  // Stay, each move onto an in-bounds walkable cell (occupancy is for
  // collisions), all with interact None; for a companion, also each enabled
  // slot's skill it can use (CanUseSkill; slot < kEnabledSkillSlots: Skill1,
  // then Skill2 once enabled) with every aim (Stay + 4 directions). A dead or
  // stunned agent (any kind): Stay only. A rooted one: no moves (Stay and its
  // skills only).
  std::vector<Action> LegalActions(int agent_idx) const;

  // Find empty cells (Floor cells with no actor)
  // - count: number of cells to return (-1 = return all matching)
  // - include: if set, cells must be inside this rectangle
  // - exclude: if set, cells must NOT be inside this rectangle
  // Returns shuffled cells, throws if count > 0 and not enough available
  std::vector<Position> FindEmptyCells(
      int count, pcg32& rng,
      std::optional<Rectangle> include = std::nullopt,
      std::optional<Rectangle> exclude = std::nullopt);

  // Accessors
  const Grid& GetGrid() const { return *grid_; }
  Grid& GetMutableGrid() { return *grid_; }
  const ObjectManager& GetObjectManager() const { return *object_manager_; }
  ObjectManager& GetMutableObjectManager() { return *object_manager_; }
  int GetTick() const { return tick_; }
  int GetRows() const { return rows_; }
  int GetCols() const { return cols_; }
  int GetD4Transform() const { return d4_transform_; }

  // Patrol path accessor (override in AggroEnv)
  virtual const std::vector<Position>& GetPatrolPath() const;

  // Semantic annotations (task tags on cells and agents, independent of
  // physical CellKind). Lenses populate on Activate and clean up via
  // owner_lens_id on Deactivate; persistent annotations (owner_lens_id == -1)
  // come from the map generator or external HTN input.
  const AnnotationStore& GetAnnotations() const { return annotations_; }
  AnnotationStore& GetMutableAnnotations() { return annotations_; }

  // Effect system access (delegates to EffectSystem)
  const std::vector<ActiveEffect>& GetActiveEffects() const;
  void ClearEffects();
  EffectSystem& GetEffectSystem() { return *effect_system_; }
  const EffectSystem& GetEffectSystem() const { return *effect_system_; }

  // Spawn a new effect at a target location (public for testing)
  // Delegates to EffectSystem::SpawnEffect
  void SpawnEffect(const std::string& effect_name, EffectTarget target,
                   Direction direction = Direction::Up,
                   ObjectId source_id = kInvalidObjectId);

  // ==========================================================================
  // Skills and tags (data-driven; names are opaque to the env)
  // ==========================================================================
  // A companion's Skill1 uses its slot 0's (effective) skill: the companion stays put
  // (the movement only aims) and the skill resolves after movement. Slots are
  // never empty: one with nothing else in it holds kDefaultSkill ("attack",
  // a strike on the faced cell), so every companion, RL envs' included,
  // strikes on Skill1 unless it holds another skill. A skill that cannot be
  // used (cooldown, disabled slot, a name the book lacks, rooted for a
  // self-moving skill) is dropped and the movement applies as if no interact
  // had been given.
  const TagTable& GetTagTable() const { return tags_; }
  // Intern only: the C API relies on the table only growing and never
  // renaming (it keeps stable copies of the names by id).
  TagTable& GetMutableTagTable() { return tags_; }
  const SkillBook& GetSkillBook() const { return skills_; }
  SkillBook& GetMutableSkillBook() { return skills_; }

  // Put `skill` in a companion's slot (0-based; "" puts kDefaultSkill back)
  // and reset the slot's cooldown. False for an unknown skill, slot or
  // companion, or a name longer than kMaxNameLength.
  bool SetCompanionSkill(ObjectId companion, int slot, const std::string& skill);

  // Context skills (core/context_skill.h). A slot holds its equipped skill
  // (SetCompanionSkill, snapshots) and an effective one: the skill of the
  // first rule for that slot whose condition holds for the companion, else
  // the equipped one. Nothing is swapped. A skill a rule gives neither reads
  // nor spends the slot's cooldown (cooldowns belong to the equipped skill;
  // a rule's skill has none). The effective skill of a use is fixed when the
  // step reads the intentions (GatherIntentions): an ally revived or downed
  // later in the step does not change it.
  // The rules are level data, like max_downs: a fresh env has
  // DefaultContextSkills() (next to a downed ally, slot 0 is revive),
  // snapshots carry them (LoadSnapshot sets the snapshot's, absent = the
  // default ones, validated against the snapshot's book), and a generated
  // Reset keeps the env's rules. A rule the current book no longer allows
  // (IsUsableWith: its skill gone or given a cooldown behind the rules' back
  // through GetMutableSkillBook, a generated Reset reloading the builtins) is
  // skipped, and SaveSnapshot leaves it out (so a saved state always loads).
  const std::vector<ContextSkillRule>& GetContextSkills() const { return context_skills_; }
  // Replaces the rules ({} = no override). False, rules unchanged and the
  // reason in `error` (when given), unless ValidateContextSkills accepts them
  // with the current skill book.
  bool SetContextSkills(std::vector<ContextSkillRule> rules, std::string* error = nullptr);
  // The skill `comp`'s slot uses now. A companion that cannot act (downed,
  // dead) has no context: its equipped skill. A slot outside
  // [0, kMaxSkillSlots) has none: "" (a static empty string). The reference
  // points into the rules or the companion's slot: SetContextSkills,
  // SetCompanionSkill and a load invalidate it.
  const std::string& EffectiveSkill(const Companion& comp, int slot) const;
  // Whether a rule gives that slot its effective skill now (false for a
  // companion that cannot act and for a slot out of range)
  bool IsContextSkill(const Companion& comp, int slot) const;

  // What a skill use does to one agent it affects (bit flags): the skill's
  // tags land on it (at least one: an agent immune to all of them gets no
  // Tags), its damage hits it, it is revived, rooted, or moved by
  // the area motion (Motion: it really changes cell; a push against a wall
  // moves nothing). The caster, affected with friendly fire, gets only what
  // its self_* flags allow.
  // In a SkillPreview the flags are what the use would do now, predicted
  // before its damage. In a SkillUse they are what the use DID: an agent its
  // own damage downed or killed is neither rooted nor moved (no Root, no
  // Motion), and a pull that then took the next thing of its ring reports
  // that one with Motion. Tags and Damage are the same in both, and Revive
  // too unless the ally got up before (an earlier caster).
  enum SkillEffect : unsigned {
    kSkillEffectTags = 1u << 0,
    kSkillEffectDamage = 1u << 1,
    kSkillEffectRoot = 1u << 2,
    kSkillEffectMotion = 1u << 3,
    kSkillEffectRevive = 1u << 4,
  };
  struct AffectedAgent {
    ObjectId id = kInvalidObjectId;
    unsigned effects = 0;  // SkillEffect flags; 0: affected, but nothing applies to it
    bool operator==(const AffectedAgent& o) const { return id == o.id && effects == o.effects; }
    bool operator!=(const AffectedAgent& o) const { return !(*this == o); }
  };

  // What `caster` using its slot `slot` aimed `aim` would do NOW, before the
  // step: a pure query (nothing moves, no tag lands, no damage, no revive,
  // nothing is interned). The skill is the slot's effective one
  // (EffectiveSkill, context rules included); its centre, landing and
  // affected agents come from the same code as the step's UseSkill
  // (ResolveSkillTargets), so the preview and the use cannot drift.
  // The step itself may differ: it resolves movement first (everyone's
  // walks, the enemies' included), then the skills one caster at a time in
  // agent-index order, so an earlier caster's push / pull / damage / revive
  // (or a walk into the line) changes what a later one reaches. A use whose
  // movement is Stay keeps the caster's facing: preview it with that facing.
  // The preview says whom the skill affects and how (SkillEffect), not
  // where a push / pull then moves them.
  struct SkillPreview {
    // The step would use it: a caster that is not stunned (GatherIntentions
    // makes it stay) and CanUseSkill (not cooling down, not rooted for a
    // skill that moves its caster, an affectable caster, an enabled slot)
    bool usable = false;
    // The effective skill ("" for a slot outside [0, kMaxSkillSlots)). When
    // it is not in the book, the rest describes no use (centre and landing =
    // the caster's cell, nobody affected).
    std::string skill;
    Position centre;                  // Its SkillUse::target
    std::vector<AffectedAgent> affected;  // Its SkillUse::affected, same order and effects
    Position caster_landing;          // Where a dash / teleport puts the caster, else its cell
  };
  // Computed whatever `usable` says (what the skill would do if it could).
  SkillPreview PreviewSkill(const Companion& caster, int slot, Direction aim) const;

  // What `caster` using its slot `slot` aimed `aim` would DO now, reports
  // included: the use is resolved on a clone of the env (Clone(); nothing in
  // this env changes, nothing is interned here), by the step's own code for a
  // use (ResolveSkillUse), as the next step would resolve it were it the
  // step's only change: no movement, no zones landing on those who stand on
  // them, no enemy acting, no end-of-step timers. The clone is mid-step
  // around the use (in_step_, Agent::BeginStep), so what the use sets (a
  // zone_becomes zone, a tag status, a root) is stored as the step stores it;
  // the zones its reactions set (their reports' cells) are committed after
  // the use, as the end of the step would (their timers not ticked).
  // The clone's reports are the use's: GetLastSkillUses (the use, or none when
  // it is not usable), GetLastTagsApplied, GetLastReactions, GetLastDefeats,
  // GetLastRevives and GetLastDowns (the downs the use caused, not those
  // between steps still to report). Tag ids in them are the clone's (a skill's
  // tags may not be interned in this env yet): read their names in the
  // clone's GetTagTable().
  // Each call returns its own world (the C API keeps one: its last
  // preview's). Costs a copy of the env: meant for a UI, not the RL hot
  // path. The real
  // step may differ as PreviewSkill says (everyone moves first, the zones
  // land on those standing on them, the casters resolve in agent order).
  struct SkillOutcome {
    // As SkillPreview::usable; false: nothing was resolved (empty reports)
    bool usable = false;
    // The clone, as the use left it: mid-step, its timers not ticked (read
    // its reports and state; do not step or save it). Null for an id naming
    // no companion.
    std::unique_ptr<BaseEnv> world;
  };
  SkillOutcome PreviewSkillOutcome(ObjectId caster, int slot, Direction aim) const;

  // Host primitives: land / remove a tag outside of a step. `duration` is a
  // step count (1..kMaxTimerSteps) or kPermanentTag; ApplyTagTo returns false
  // for any other (IsValidTimer), for an empty tag or one longer than
  // kMaxNameLength, for an agent that is not affectable (downed or dead:
  // the tag is then not interned, like LandTag), and for an agent immune to
  // it (nothing lands). Durations are step timers (see Agent::BeginStep): a tag
  // applied between two steps with duration d is there for the d next steps.
  // A host landing is a landing like any other (TagSource::Host, cause
  // "host"): its tag status, the agent's weaknesses and the reactions apply,
  // and it is reported (GetLastTagsApplied, and GetLastReactions /
  // GetLastDefeats for what it caused) until the next Step clears the reports.
  bool ApplyTagTo(ObjectId agent, const std::string& tag, int duration);
  bool RemoveTagFrom(ObjectId agent, const std::string& tag);

  struct SkillUse {
    ObjectId caster = kInvalidObjectId;
    std::string skill;
    Position target;  // The skill's centre: landing cell for Self skills
    int slot = 0;     // The caster's slot it was used from (0-based)
    // The agents it affected, in the order it processed them: those on its
    // area (cell order: the centre, then up, right, down, left for a Cross),
    // then those on a tag_path dash's path, each with what the use did to it
    // (SkillEffect). The same agents PreviewSkill gives before the step
    // (ResolveSkillTargets); their Root / Motion / Revive may differ from the
    // preview's prediction (see SkillEffect).
    std::vector<AffectedAgent> affected;
  };
  // What landed a tag (TagApplication::kind)
  enum class TagSource : int {
    Skill = 0,     // A skill's tags (source = the caster, cause = the skill)
    Zone = 1,      // A zone (source kInvalidObjectId, cause "zone")
    Reaction = 2,  // A reaction's result (source and cause: the landing that triggered it)
    Host = 3,      // ApplyTagTo (source kInvalidObjectId, cause "host")
  };
  struct TagApplication {
    // No default kind: every landing path says what landed it
    explicit TagApplication(TagSource source_kind) : kind(source_kind) {}
    ObjectId agent = kInvalidObjectId;
    TagId tag = kInvalidTag;
    // Steps, or kPermanentTag. A step timer (see Agent::BeginStep): landed
    // during step t with duration d, the tag is there until the end of step
    // t + d (read after steps t .. t+d-1).
    int duration = kPermanentTag;
    // Caster, or kInvalidObjectId for a zone or the host. A reaction's result:
    // the source of the landing that triggered it.
    ObjectId source = kInvalidObjectId;
    // Skill name, "zone" or "host". A reaction's result: the cause of the
    // landing that triggered it (its kind says it is a result).
    std::string cause;
    // The agent did not have the tag before (a result: before its reaction
    // removed the originals, so a result that is one of them is not fresh)
    bool fresh = false;
    // The zone damage this landing dealt (CellTag::damage as given, before
    // Marked, like SkillConfig::damage), after the tag. 0 when none was
    // dealt: a skill's landing (a skill's damage is its SkillUse's Damage
    // effect), a harmless zone, an agent no longer affectable once the tag
    // landed (a weakness defeated it, a reaction's damage downed or killed it).
    int damage = 0;
    TagSource kind;
    int reaction = -1;  // A result: its reaction's index in GetLastReactions(); else -1
  };
  struct Revival {
    ObjectId reviver = kInvalidObjectId;  // The skill's caster
    ObjectId revived = kInvalidObjectId;
    int health = 0;  // The HP it got up with (it may lose them later in the step)
  };
  // What the last Step did (cleared at the start of every Step, and by
  // LoadSnapshot, hence by every Reset).
  // Skills resolve one caster at a time in agent-index order, each from its
  // current cell: an earlier push / pull can move a later caster (ResolveSkills).
  // Report order in a step: the zone phase first (its zone landings, every
  // one, in agent-index order; its landing defeats; its reactions in trigger
  // agent-index order, their result landings in that firing order, then the
  // results' defeats, in first-hit order: the firing, then its affected
  // order), then the skill phase's, in resolution order. The C API's
  // report_index fields follow these orders.
  const std::vector<SkillUse>& GetLastSkillUses() const { return last_skill_uses_; }
  const std::vector<TagApplication>& GetLastTagsApplied() const { return last_tags_applied_; }
  // Companions that went down since the last report, one entry per down:
  // this step's, and any between steps (a host effect), reported once.
  const std::vector<ObjectId>& GetLastDowns() const { return last_downs_; }
  // Downed companions a skill revived this step, in resolution order (each
  // revive is also one of the step's skill uses). Revived and downed again in
  // the same step: a revive here, then a down in GetLastDowns.
  const std::vector<Revival>& GetLastRevives() const { return last_revives_; }

  // What a reaction did to one agent it affected
  struct ReactionOutcome {
    ObjectId agent = kInvalidObjectId;
    bool result_landed = false;  // False: immune to the result
    bool defeated = false;       // The result landed on one of its weaknesses
    int damage = 0;  // The rule's damage it dealt (before Marked); 0 when none: a defeated agent
  };
  // One reaction that fired (in the order they fired)
  struct ReactionReport {
    // No default kind: the triggering landing says what landed it
    explicit ReactionReport(TagSource trigger_kind) : kind(trigger_kind) {}
    int rule = -1;                        // Index in GetReactions()
    ObjectId trigger = kInvalidObjectId;  // The agent the triggering tag landed on
    TagId tag = kInvalidTag;              // The triggering tag (the rule's a or b)
    // The triggering landing's source, cause and kind (never Reaction: results
    // do not trigger reactions)
    ObjectId source = kInvalidObjectId;
    std::string cause;
    TagSource kind;
    bool spread = false;  // Over the trigger's zone region (else the trigger alone)
    // Every agent it affected, in agent-index order (the trigger included,
    // unless a weakness defeated it: then possibly none)
    std::vector<ReactionOutcome> affected;
    // The cells the reaction (re)set to the rule's zone_becomes, in
    // row-major order (empty without a spread or a zone_becomes; a
    // zone_becomes equal to the region's zone still lists them: the tag
    // stays, its lifetime starts again), so a host knows the zone changed
    // without diffing the grid. During a step they change at its end (a
    // later reaction writing the same cell wins); between steps, at once.
    std::vector<Position> cells;
  };
  // `rule` indexes the current reactions: a SetReactions between the step and
  // the read makes it stale.
  const std::vector<ReactionReport>& GetLastReactions() const { return last_reactions_; }
  // One agent defeated by a weakness (P, S): S landed on it while it stood on
  // a zone providing P
  struct DefeatReport {
    // No default kind: the landing of S says what landed it
    explicit DefeatReport(TagSource landing_kind) : kind(landing_kind) {}
    ObjectId agent = kInvalidObjectId;
    TagId zone = kInvalidTag;  // P
    TagId tag = kInvalidTag;   // S
    // The landing of S: its source, cause and kind (Reaction for a result)
    ObjectId source = kInvalidObjectId;
    std::string cause;
    TagSource kind;
    int reaction = -1;  // S a result: its reaction's index in GetLastReactions(); else -1
  };
  const std::vector<DefeatReport>& GetLastDefeats() const { return last_defeats_; }

  // ==========================================================================
  // Reactions, weaknesses, immunities, tag statuses (core/reaction.h)
  // ==========================================================================
  // Every tag landing (a skill's, a zone's, a reaction's result, the host's)
  // resolves in this order (LandTag):
  //   1. immunity: an agent immune to the tag gets nothing (no report, and no
  //      zone damage for a zone's landing);
  //   2. the tag lands (reported), with the status tag_statuses binds to it;
  //   3. weakness: S landing while the agent stands on a zone providing P,
  //      for one of its (P, S): defeated (Agent::Defeat, reported), and it
  //      gets nothing more (no reaction outcome, no zone damage);
  //   4. reaction (not for a result: results never trigger one): the first
  //      rule, in level order, pairing the tag with one the agent carries
  //      (a defeated agent keeps its tags, so it still triggers one). Its
  //      affected agents (the region's affectable ones when it spreads, in
  //      agent-index order, else the agent alone) each lose the originals not
  //      kept, get the result (a permanent tag, through steps 1-3) and the
  //      damage (if still affectable); then a spread region becomes
  //      zone_becomes (at the end of the step, see below). A defeated agent
  //      alone affects nobody: nothing fires (not reported); spreading, it
  //      fires without it;
  //   5. a zone's landing then deals the zone's damage, if the agent is still
  //      affectable.
  // A step reads ONE zone map, the map as it began: a reaction's
  // zone_becomes during a step waits (pending_zones_) and the map changes at
  // the end of the step (CommitPendingZones, before the timers tick), so a
  // zone a reaction creates first lands NEXT step; weaknesses (P), spread
  // regions and skill motions landing on a zone all read the unchanged map.
  // The zone phase (ApplyZoneTags) runs the steps above as sub-phases, each
  // over every zone landing (ResolveZoneLandings: 1-2 for all, 3 for all,
  // 4 gathered then applied per agent: every result, then one weakness check,
  // then each firing's damage; 5 for all), so the order of the agents does
  // not change an outcome, only the order of the reports.
  // One exception, an outcome: two firings writing different zone_becomes
  // to the same cells, the later one (firing order: trigger agent index)
  // wins at the end of the step. Report-only effects of the order: a
  // result's DefeatReport::reaction names the first firing whose result
  // defeated, only the first of identical result landings on an agent is
  // `fresh`, and the damage an agent downed by an earlier firing's damage no
  // longer takes is reported 0 by the later firings.
  // The skill phase resolves each landing at once, casters in agent-index
  // order. Between two steps (the host's ApplyTagTo) there is no phase: a
  // landing resolves at once and its zone_becomes applies at once.
  // Level data (reactions, tag statuses): like the zone table, copied with the
  // env, kept across a generated Reset, saved in snapshots (v7) and replaced
  // by LoadSnapshot with the snapshot's (none in older ones). Per-agent data
  // (weaknesses, immunities) lives on the agents: copied and saved with them
  // (v7), gone when a generated Reset re-creates them.
  // Each setter validates (core/reaction.h) before any change: false, data
  // unchanged, nothing interned and the reason in `error` (when given).
  bool SetReactions(std::vector<ReactionRule> rules, std::string* error = nullptr);
  const std::vector<ReactionRule>& GetReactions() const { return reactions_; }
  bool SetTagStatuses(std::vector<TagStatusRule> rules, std::string* error = nullptr);
  const std::vector<TagStatusRule>& GetTagStatuses() const { return tag_statuses_; }
  // False as well for an id naming no agent
  bool SetWeaknesses(ObjectId agent, const std::vector<TagWeakness>& weak_to,
                     std::string* error = nullptr);
  std::vector<TagWeakness> GetWeaknesses(ObjectId agent) const;  // {} for no agent
  // An immunity blocks landings only: a tag the agent already carries stays
  // until it expires or a reaction removes it.
  bool SetImmunities(ObjectId agent, const std::vector<std::string>& immune,
                     std::string* error = nullptr);
  std::vector<std::string> GetImmunities(ObjectId agent) const;  // {} for no agent

  // Zones: a cell may carry one tag, landed (with `duration`, cause "zone",
  // source kInvalidObjectId) on every affectable agent standing on it after the
  // regular movement of every Step (before skills), and on any
  // agent a skill moves onto it. Each landing is reported; its `fresh` is that
  // of TagApplication (the agent did not carry the tag just before this
  // landing), so it does not tell arrivals apart: an agent standing on a
  // zone still carries its tag (it ticks at the END of Step, after the
  // landing), so it is re-landed not fresh, and so is one already carrying
  // the tag from any source when it arrives.
  // Each landing then deals the zone's `damage` (Agent::TakeDamage: Marked
  // applies, a companion goes down), if the agent is still affectable once
  // the tag landed and the weaknesses and reactions resolved; reported in
  // the landing's TagApplication::damage. A step reads one map (see
  // SetReactions): a zone a reaction sets during it first lands next step.
  // A zone lives `steps` steps (a step timer, like tags: set between two
  // steps it lands during the n next steps; set by a host call during a step
  // (e.g. a PreStep hook) it also covers the rest of that step, kept as n + 1
  // and read n after it; a reaction's zone_becomes, kept as n + 1 too, is
  // written at the end of the step: its n next steps), or forever
  // (kPermanentTag). Zone timers tick at the end of Step, right after the
  // agents' (Agent::EndStep); an expired zone becomes its successor `then`
  // (a zone by name: the table's fields; it lands from the next step and
  // lasts its steps from there), or nothing. Cycles (a -> b -> a, a -> a)
  // are legal: one zone per tick.
  // Cell tags are world state: copied with the env, saved in snapshots (by tag
  // name) and replaced by LoadSnapshot with the snapshot's own (a generated
  // level has none, so every Reset clears them). Like cell annotations, they
  // follow the snapshot's D4 transform. SaveSnapshot writes each zone's
  // fields as the cell holds them (v7: its remaining steps, successor and
  // damage too), so an override, a zone mid-life or one created before its
  // tag was redefined loads as it was; a field a snapshot's cell lacks (a
  // hand-written level's {row, col, tag}, a file before v7) is the
  // snapshot's zone table's (Snapshot::CellZone).
  struct CellTag {
    TagId tag = kInvalidTag;
    int duration = kPermanentTag;  // The duration landed on agents
    // Steps the zone still lasts (the timer as stored: n + 1 during the step
    // that set it), or kPermanentTag
    int steps = kPermanentTag;
    TagId then = kInvalidTag;  // Its successor's tag, or kInvalidTag (none)
    int damage = 0;            // Per landing
  };
  // A zone by name: the zone table's fields for `tag` (GetZoneDef). ""
  // clears the cell (the other fields are then ignored). False (cell
  // unchanged, nothing interned) out of bounds, or for an invalid tag or
  // fields (see DefineZone).
  bool SetCellTag(Position cell, const std::string& tag);
  // By name, with an explicit landing duration (the other fields: the table's)
  bool SetCellTag(Position cell, const std::string& tag, int duration);
  // An explicit per-cell override: these fields, whatever the table says
  bool SetCellTag(Position cell, const std::string& tag, const ZoneDef& zone);
  CellTag GetCellTag(Position cell) const;  // {} when none / out of bounds
  void ClearCellTags() { cell_tags_.clear(); }

  // The level's zone table: what a zone of each tag is, used whenever a zone
  // is created by name (SetCellTag without explicit fields, a successor, a
  // reaction's zone_becomes).
  // Each cell keeps its own resolved copy: (re)defining a tag changes the
  // zones created after it, not those already there. A tag the table does not
  // define gets the ZoneDef defaults. DefineZone (re)defines `tag`; false
  // (table unchanged) for an empty tag, a tag or `then` longer than
  // kMaxNameLength, a duration or steps that is not IsValidTimer (0, below
  // kPermanentTag or above kMaxTimerSteps, as ApplyTagTo), or a negative
  // damage (IsValidZoneDef, core/reaction.h).
  // Level data like max_downs: copied with the env, kept across a generated
  // Reset, saved in snapshots (v7) and replaced by LoadSnapshot with the
  // snapshot's (none in older ones), before its zone cells load.
  bool DefineZone(const std::string& tag, const ZoneDef& zone);
  const std::map<std::string, ZoneDef>& GetZoneDefs() const { return zone_defs_; }
  ZoneDef GetZoneDef(const std::string& tag) const;  // The defaults when undefined
  void ClearZoneDefs() { zone_defs_.clear(); }

  // ==========================================================================
  // Snapshot Support - Save/Load complete world state
  // ==========================================================================

  // Save current state to a snapshot (grid, agents, effects, timing, the skill
  // book but the fixed kDefaultSkill, agent tags / skill slots / cooldowns,
  // zones with every field as their cell holds it; tags by name; downs,
  // max_downs; the context skills, always explicit, but for those the book
  // no longer allows: see GetContextSkills; the zone table, the reactions,
  // the tag statuses and each agent's weaknesses / immunities). Between two
  // steps (a zone's steps are then the steps to come): std::logic_error
  // inside one (a Step that threw is not: it aborts, AbortStep).
  virtual Snapshot SaveSnapshot() const;

  // Load state from a snapshot. The skill book is reset to the builtins, then
  // gets the snapshot's skills; the TagTable is kept (ids stay stable). An
  // empty or missing slot loads as kDefaultSkill. max_downs and the context
  // skills are the snapshot's (absent rules: DefaultContextSkills()), and so
  // are the zone table (set before the zone cells, which take its fields
  // for those they lack), the reactions, the tag statuses and each agent's
  // weaknesses / immunities (none when the snapshot has none).
  // Throws std::runtime_error if snapshot is incompatible (e.g., wrong
  // dimensions) or its skills / tags / zones are invalid, before any change.
  virtual void LoadSnapshot(const Snapshot& snapshot);

  // Validate that snapshot has required cell types for this environment
  // Override in subclasses to check for required cells (e.g., Synchro cells)
  // Throws std::runtime_error if validation fails
  virtual void ValidateSnapshot(const Snapshot& snapshot) const;

 protected:
  // Subclass hooks for custom step logic
  virtual void PreStep();
  virtual void PostStep() {}

  // Update all agents with FSM AI (called in PreStep)
  void UpdateAgentFSM();

  // A generated Reset's load: LoadSnapshot keeping the env's level data
  // (max_downs, context skills, the zone table, reactions and tag statuses)
  // in place of the generated level's defaults / none. The context rules
  // are not checked against the generated book. The generated agents have no
  // weaknesses or immunities (per-agent data).
  void LoadGeneratedLevel(Snapshot snapshot);

  // The env's own end rule (IsDone's other term, besides IsTeamDown): the
  // latched success, the horizon, a failure it honours
  virtual bool IsEnvDone() const = 0;

  // IsEnvDone()'s terms other than the latched success and the horizon: true
  // when the env is done even without the horizon (a latched failure its
  // IsEnvDone honours, or its own end rule). GetEndReason reports TaskFailed
  // then. An env whose IsEnvDone is only success || horizon (SynchroEnv) keeps
  // the default; the others build IsEnvDone on their override so both agree.
  virtual bool IsDoneWithoutHorizon() const { return false; }

  // GetEndReason's rules, for a done env
  EndReason ComputeEndReason() const;
  // Fixes the end reason once done becomes true; None while not done
  void LatchEndReason();
  // Latches the end reason of a state just loaded, afresh: for a derived
  // Reset / LoadSnapshot that changes state after BaseEnv::LoadSnapshot
  // latched (AggroEnv spawns its enemy then). Not after a Step: the reason
  // fixed then must stay.
  void RelatchEndReasonAfterLoad();

  // A copy's FSM agents still point at the copied env's RNG
  // (FSMContext::rng, a raw pointer copied with them): re-points those at
  // `from` to `to`, the copy's own, so a copy (a Clone, an outcome preview's
  // world) never draws from, or advances, the original's. The derived envs,
  // which own the RNG, call it from their copy constructor and assignment.
  void RepointFsmRng(const pcg32* from, pcg32* to);

  // D4 symmetry transform - call at end of Reset() in subclasses
  // Transforms grid cells, actor positions, cell annotations and zones
  // according to d4_transform_
  void ApplyD4Transform();

  // Collision resolution (the new system)
  void GatherIntentions(const std::vector<Action>& actions);
  void CaptureOriginalIntentions();  // Save intentions before collision resolution
  void ResolveCollisions();
  Position PredictPosition(const Agent* agent) const;
  bool ValidateMovement(const Agent* agent, Position target) const;

  // Movement execution
  void ExecuteValidatedMovements();

  // Interaction resolution, after movement: companion skills (FSM attacks go
  // through the effect system)
  void ResolveInteractions();

  // Skills (see GetSkillBook)
  // Empties the per-step reports (skill uses, tags applied, reactions,
  // defeats, downs, revives): their ObjectIds are re-issued by a new world.
  void ClearStepReports();
  // Not rooted. Walking and caster-moving skills both need it (being pushed /
  // pulled does not).
  bool CanMoveItself(const Agent& agent) const;
  // The slot's effective skill is usable: an affectable caster, an enabled
  // slot, a skill of the book, not cooling down (only an equipped skill reads
  // the cooldown), and a rooted caster only for a skill that does not move it.
  bool CanUseSkill(const Companion& comp, int slot) const;
  // The first rule for `slot` usable with the book (IsUsableWith) whose
  // condition holds for `comp`, or nullptr
  const ContextSkillRule* ActiveContextRule(const Companion& comp, int slot) const;
  // The one evaluation of a condition (a new condition: one more case)
  bool ContextHolds(ContextCondition condition, const Companion& comp) const;
  // Uses the skill fixed by GatherIntentions (intended_skills_) through
  // ResolveSkillUse.
  void ResolveSkills();         // After movement, in agent-index order
  // One use, as the step resolves it (ResolveSkills, PreviewSkillOutcome):
  // the skill of context rule `rule` (index in context_skills_), or the
  // slot's equipped one for -1; UseSkill; the slot's cooldown only for an
  // equipped skill; the SkillUse report (the effective skill's name). Nothing
  // for a skill the book lacks.
  void ResolveSkillUse(Companion& comp, int slot, int rule);
  // Where a skill use lands and whom it affects: steps 1-2 of UseSkill, the
  // one implementation of targeting (UseSkill and PreviewSkill both use it).
  struct SkillTargets {
    Position landing;                // The caster's cell after its own motion
    Position centre;                 // By the skill's targeting
    // On the area (cell order), then on a tag_path dash's path, with the
    // effects decided before the use (UseSkill then reports what it did)
    std::vector<AffectedAgent> affected;
  };
  // Pure: reads the world as it is, with the caster already on its landing
  // cell (AgentAfterMotion), for `aim` (UseSkill passes the caster's facing).
  SkillTargets ResolveSkillTargets(const Companion& caster, const SkillConfig& skill,
                                   Direction aim) const;
  // The actor on `p` once `caster` moved from its cell to `landing` (the
  // caster on `landing`, nobody on the cell it left), as GetActorAt gives it
  const Actor* ActorAfterMotion(Position p, const Agent& caster, Position landing) const;
  // The same, when it is an agent (nullptr for none, or a non-agent actor)
  const Agent* AgentAfterMotion(Position p, const Agent& caster, Position landing) const;
  // What a skill's area motion may move on `p` (caster on `landing`): a
  // living actor, an agent only if the skill affects it, the caster only
  // with self_motion. Shared by ResolveSkillTargets and AreaMotion.
  const Actor* MotionThingAt(Position p, const SkillConfig& skill, const Agent& caster,
                             Position landing) const;
  // Where a PushOut moves `mover`, on ring cell `p`, away from `centre`
  // (ResolveDash's rule, landings read with the caster on `landing`, as the
  // step reads them once the caster moved). Shared by ResolveSkillTargets
  // and AreaMotion.
  Position PushLanding(const SkillConfig& skill, Position p, Position centre, ObjectId mover,
                       const Agent& caster, Position landing) const;
  // The ring cell a PullIn takes its one thing from (caster on `landing`):
  // the first MotionThingAt by ring priority (up, right, down, left), only
  // into a walkable centre no living actor holds; nullopt for none.
  std::optional<Position> PullFrom(const SkillConfig& skill, Position centre, const Agent& caster,
                                   Position landing) const;
  // Resolves one skill (caster motion, area, tags, damage, revive, root, area
  // motion); returns its targets (centre and affected, for the SkillUse).
  SkillTargets UseSkill(Companion& caster, const SkillConfig& skill);
  // `centre`, then its in-bounds orthogonal ring (up, right, down, left) for Cross.
  std::vector<Position> AreaCells(Position centre, SkillArea area) const;
  // The one definition of "affected": a standing agent (alive, not downed),
  // or for an affects_downed skill a downed one, passing the skill's filter
  // and, without friendly fire, not of the caster's faction (so never the
  // caster). With friendly fire the caster itself can be affected; each
  // effect then checks its self_* flag.
  bool Affects(const SkillConfig& skill, const Agent& caster, const Agent& agent) const;
  // Appends the agents on `cells` the skill affects (with the caster on
  // `landing`, see AgentAfterMotion), each once, in cell order, with the
  // cell each was found on (`found_on`, parallel), effects not set.
  void CollectAffected(const std::vector<Position>& cells, const SkillConfig& skill,
                       const Agent& caster, Position landing,
                       std::vector<AffectedAgent>& affected,
                       std::vector<Position>& found_on) const;
  // One tag landing on an agent: steps 1-4 of the landing order (see
  // SetReactions), all at once (a skill's, a result's, the host's; a zone's
  // goes through the phases of ResolveZoneLandings instead): false when
  // nothing landed (an agent not affectable, or immune to the tag), else true
  // (even if a weakness defeated it). `reaction` is the index in
  // last_reactions_ of the reaction whose result this is (kind Reaction),
  // else -1.
  bool LandTag(Agent& agent, TagId tag, int duration, ObjectId source,
               const std::string& cause, TagSource kind, int reaction = -1);
  bool LandTag(Agent& agent, const std::string& tag, int duration, ObjectId source,
               const std::string& cause, TagSource kind);  // Interns, forwards
  // Steps 1-2: immunity, then the tag (reported) and its tag status. False
  // when nothing landed, as LandTag.
  bool PutTag(Agent& agent, TagId tag, int duration, ObjectId source, const std::string& cause,
              TagSource kind, int reaction);
  // Step 3: `tag` just landed on `agent`; defeats it on a matching weakness
  // (reported, with `reaction`: the landing's, see LandTag). P is read on the
  // map, which a step never changes before its end (see pending_zones_).
  void ResolveWeakness(Agent& agent, TagId tag, ObjectId source, const std::string& cause,
                       TagSource kind, int reaction);
  // Defeats `agent` (Agent::Defeat) by `weakness`, reported (DefeatReport,
  // the landing of S: source / cause / kind / reaction)
  void DefeatBy(Agent& agent, const Agent::WeakTo& weakness, ObjectId source,
                const std::string& cause, TagSource kind, int reaction);
  // Step 4 at once: FindReaction, then FireReaction.
  void ResolveReaction(Agent& agent, TagId tag, ObjectId source, const std::string& cause,
                       TagSource kind);
  // The first rule, in level order, pairing `tag` (just landed on `agent`)
  // with a tag it carries (the reverse too), or -1
  int FindReaction(const Agent& agent, TagId tag) const;
  // Fires rule `rule` at once (a skill's or the host's landing), triggered
  // by `tag` landing on `agent`: StartReaction, the outcome on each affected
  // agent in turn, then ApplyZoneBecomes.
  void FireReaction(Agent& agent, TagId tag, int rule, ObjectId source,
                    const std::string& cause, TagSource kind);
  // A firing without its outcome: whom it affects (`affected`, replaced: the
  // affectable agents of the region of the zone under `agent`, in
  // agent-index order, when the rule spreads and that zone provides a or b;
  // else the agent alone), its report (outcomes empty; `cells` = the region
  // when it has a zone_becomes). Returns the report's index, or -1 when
  // nothing fires: a trigger no longer affectable (a weakness defeated it)
  // still fires a spread one (it reaches the others); alone, nobody is left.
  int StartReaction(Agent& agent, TagId tag, int rule, ObjectId source, const std::string& cause,
                    TagSource kind, std::vector<Agent*>& affected);
  // The zone_becomes of fired reaction `index` over its report's cells:
  // recorded in pending_zones_ during a step (the map changes at its end,
  // CommitPendingZones), written at once between two steps (a host landing).
  void ApplyZoneBecomes(size_t index);
  // Sub-phase c's outcomes, gathered in reaction_hits_ (every firing's
  // affected agents, computed before any applies), applied agent by agent:
  // every firing's removals, then every result (immunity, tag, tag status;
  // firing order), then ONE weakness check over the results that landed,
  // then each firing's damage (one TakeDamage each, firing order) while still
  // affectable. All agent-local, so the final state of the agents does not
  // depend on the order (reports: see SetReactions).
  void ApplyReactionHits();
  // The connected region (4 neighbours) of the zone on `start`: the cells
  // carrying that zone's tag, reachable from `start` through such cells, as a
  // row-major mask of rows_ * cols_ (empty when `start` has no zone)
  std::vector<char> ZoneRegion(Position start) const;
  // Skill motions: a living agent moved onto a zone cell gets its zone (the
  // zone as the step began: ApplyZoneTag). Only the landing cell applies its
  // zone: cells a dash crosses do not (a decision).
  void MoveActor(Actor& actor, Position to);
  // The zone of the cell `agent` stands on, if any: ResolveZoneLandings with
  // that one landing (a skill motion's)
  void ApplyZoneTag(Agent& agent);
  // The zone phase (after movement, before skills): every affectable agent
  // on a zone, together (CollectZoneLanding, then ResolveZoneLandings)
  void ApplyZoneTags();
  // Sub-phase a of a zone landing: immunity, the zone's tag and its status
  // (PutTag), recorded in zone_landings_ (nothing for an unaffectable agent,
  // an agent on no zone, an immune agent)
  void CollectZoneLanding(Agent& agent);
  // Sub-phases b-d over every landing of zone_landings_, each phase over all
  // of them (agent-index order only orders the reports): b. the weaknesses
  // (defeats); c. the reactions, gathered then applied: c1 the triggers are
  // ALL found (the state after a and b, FindReaction); c2 every firing is
  // started (StartReaction: its affected agents and report, nothing applied);
  // c3 the outcomes apply per agent (ApplyReactionHits), then the
  // zone_becomes in firing order (pending); so no firing's outcome cancels
  // or changes another's; d. the zone damage of each landing still
  // affectable. The map they read is the map as the step began (a firing's
  // zone_becomes waits in pending_zones_), so the order of the agents does
  // not change an outcome, but for the zones (see SetReactions: two firings
  // writing the same cells, the later one wins).
  void ResolveZoneLandings();
  // End of Step, before the agents' timers: the zone changes the step's
  // reactions recorded (pending_zones_), in the order they were recorded (a
  // later write to the same cell wins), their timers as set during the step
  // (n + 1, so after TickZones they cover their n next steps).
  void CommitPendingZones();
  // End of Step, after the agents' timers: every timed zone loses a step, and
  // an expired one becomes its successor (or nothing). Ends the step (in_step_).
  void TickZones();
  // A Step that threw: between two steps again (in_step_ and the agents'
  // Agent::AbortStep), its timers not ticked, so a later SaveSnapshot or
  // timer set is not taken for one inside a step. It keeps what the step did,
  // its pending zone changes committed (dropped, without allocating, when the
  // map was cleared during the step: it runs while the throw unwinds).
  void AbortStep();
  // `def` for `tag` on a cell: its steps as a step timer (n + 1 in a step),
  // its successor interned. Validated by the caller.
  CellTag ResolveZone(TagId tag, const ZoneDef& def);
  // PushOut (each ring MotionThingAt, away from the centre) / PullIn
  // (PullFrom's thing into the centre), read from the world as it is then;
  // returns the ids of the actors it really moved.
  std::vector<ObjectId> AreaMotion(const SkillConfig& skill, Position centre,
                                   const Agent& caster);

  int rows_;
  int cols_;
  std::unique_ptr<Grid> grid_;
  std::unique_ptr<ObjectManager> object_manager_;
  std::unique_ptr<EffectSystem> effect_system_;
  int tick_ = 0;
  int horizon_ = kDefaultHorizon;
  int d4_transform_ = 0;  // D4 symmetry transformation (0-7)
  std::unique_ptr<TaskLens> task_lens_;
  AnnotationStore annotations_;
  // Latched once the active lens reports IsSuccess. Reset via ResetSuccess.
  bool success_ = false;
  // Latched once the active lens reports IsFailed. Reset via ResetOutcome.
  bool failed_ = false;
  // Latched when done first becomes true (LatchEndReason). Reset via
  // ResetOutcome.
  EndReason end_reason_ = EndReason::None;
  TagTable tags_;
  SkillBook skills_;
  std::vector<SkillUse> last_skill_uses_;
  std::vector<TagApplication> last_tags_applied_;
  std::vector<ObjectId> last_downs_;
  std::vector<Revival> last_revives_;
  // Row-major rows_ * cols_ once a zone is set; empty = no zones.
  std::vector<CellTag> cell_tags_;
  // The level's zone table (DefineZone), by tag name
  std::map<std::string, ZoneDef> zone_defs_;
  // From the start of a Step to the world's timer tick (TickZones): a timer
  // the world sets then (a zone's) is kept as n + 1, like Agent::BeginStep's.
  bool in_step_ = false;
  // A step reads one zone map: the zones a reaction's zone_becomes sets
  // during a step (cell index into cell_tags_, the zone as ResolveZone made
  // it) wait here until its end (CommitPendingZones), in the order they were
  // recorded. Empty between two steps. Reused: no allocation once grown.
  struct PendingZone {
    size_t cell = 0;
    CellTag zone;
  };
  std::vector<PendingZone> pending_zones_;
  // Pre-reserved reward buffer, reused each Step to avoid allocation on the
  // hot path. Audit F11.
  mutable std::vector<double> reward_buffer_;

 private:
  // CanUseSkill, also giving the rule that gives the slot its effective skill
  // (nullptr: the equipped one): the context is evaluated once per use.
  bool CanUseSkill(const Companion& comp, int slot, const ContextSkillRule*& rule) const;

  // The skill each agent's use settled on when the step read the intentions
  // (GatherIntentions), for ResolveSkills: indexed like GetAllAgents() (the
  // action vector), rebuilt every step.
  struct IntendedSkill {
    ObjectId caster = kInvalidObjectId;
    int slot = -1;  // -1: no skill use
    int rule = -1;  // Index in context_skills_ of the rule that gave it; -1: equipped
  };
  std::vector<IntendedSkill> intended_skills_;

  int max_downs_ = kDefaultMaxDowns;  // Level data (SetMaxDowns)
  // Level data (SetContextSkills)
  std::vector<ContextSkillRule> context_skills_ = DefaultContextSkills();

  // Level data (SetReactions / SetTagStatuses), each with its tags resolved
  // (interned when set: ids stay valid, the TagTable only grows)
  struct ResolvedReaction {
    TagId a = kInvalidTag;
    TagId b = kInvalidTag;
    TagId result = kInvalidTag;
    bool keep_a = false;
    bool keep_b = false;
    TagId becomes = kInvalidTag;  // zone_becomes, or kInvalidTag (its fields: the table's, when set)
  };
  std::vector<ReactionRule> reactions_;
  std::vector<ResolvedReaction> resolved_reactions_;  // Parallel to reactions_
  std::vector<TagStatusRule> tag_statuses_;
  std::vector<TagId> tag_status_ids_;  // Parallel to tag_statuses_
  std::vector<ReactionReport> last_reactions_;
  std::vector<DefeatReport> last_defeats_;

  // Scratch of one zone phase (ResolveZoneLandings): each zone landing, with
  // the zone that landed (a copy), its report entry and the reaction it
  // triggers. Never copied (it points at this env's agents); reused.
  struct ZoneLanding {
    Agent* agent = nullptr;
    CellTag zone;
    size_t report = 0;  // Its entry in last_tags_applied_
    int rule = -1;      // The reaction it triggers (FindReaction), or -1
  };
  std::vector<ZoneLanding> zone_landings_;
  // Scratch of sub-phase c (never copied, reused): one entry per (firing,
  // affected agent), in firing order then the firing's affected order
  struct ReactionHit {
    Agent* agent = nullptr;
    size_t firing = 0;     // Its index in last_reactions_
    bool carried = false;  // The agent carried the result before any removal
    size_t outcome = 0;    // Its entry in the firing's affected
  };
  std::vector<ReactionHit> reaction_hits_;
  std::vector<Agent*> reaction_affected_;  // StartReaction's output, per firing
  std::vector<Agent*> hit_agents_;         // The distinct agents of reaction_hits_
};

}  // namespace companions

#endif  // COMPANIONS_ENV_BASE_ENV_H_
