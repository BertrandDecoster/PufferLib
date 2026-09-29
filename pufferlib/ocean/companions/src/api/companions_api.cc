// Copyright 2024
// C API implementation for external integration

#include "companions_api.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "../core/annotations.h"
#include "../core/cell.h"
#include "../core/fsm/fsm_state.h"
#include "../core/fsm/fsm_states.h"
#include "../core/grid.h"
#include "../core/level_config.h"
#include "../core/level_generator.h"
#include "../core/fsm/enemies.h"
#include "../core/object.h"
#include "../core/object_manager.h"
#include "../core/skill_config.h"
#include "../core/snapshot.h"
#include "../core/snapshot_json.h"
#include "../core/types.h"
#include "../env/effect_system.h"
#include "../env/aggro_env.h"
#include "../env/synchro_env.h"
#include "../env/task_lens.h"
#include "../env/synchro_lens.h"
#include "../env/aggro_lens.h"
#include "../env/dodge_lens.h"
#include "../viz/renderer.h"

// =============================================================================
// Version
// =============================================================================
// 1.1.0: skills, tags, zones; Companions_AgentState, Companions_Event and
// Companions_StepResult layouts changed (consumers must rebuild).
// 1.2.0: removed the legacy generic companion cast (its on/off setter and
// getter, its EffectSpawned events): every skill slot holds a skill, "attack"
// by default.
// 1.2.1: companions_get_end_reason (additive), EpisodeEnd's effect_id.
// 1.2.2: timers tick at the end of a step (cooldown n = n blocked steps).
// 1.3.0: downs (Companions_AgentState.downed, Companions_GameState.downs /
// max_downs / team_down, Companions_End_TeamDown, Companions_Event_AgentDowned); struct
// layouts changed (consumers must rebuild).
// 1.4.0: revives and context skills (Companions_AgentState.skills = the
// effective skills, equipped_skills = the equipped ones,
// Companions_Event_AgentRevived); struct layouts changed (consumers must
// rebuild). Amended before release (additive): the skill book query
// (companions_get_skill_count / _get_skill / _find_skill, Companions_SkillInfo),
// the skill use preview (companions_preview_skill, Companions_SkillPreview)
// and the last step's skill uses with whom they affected
// (companions_get_last_skill_use_count / _get_last_skill_use,
// Companions_SkillUseInfo).
// 1.5.0: the rules' reports and the level data, as data (additive, but
// Companions_Event's layout changed: consumers must rebuild). Report queries
// with a source (the last step's, or the outcome preview's): skill uses, tag
// landings, reactions (with the cells a zone_becomes changed), defeats,
// downs; companions_preview_skill_outcome; the level data (zone table, cell
// zones, reaction rules, tag statuses, weaknesses, immunities);
// Companions_Event_ReactionFired / _AgentDefeated, Companions_Event.tag_kind /
// tag_reaction / report_index, TagApplied's health_amount (zone damage),
// revives as a report too; companions_step and companions_reset return bool.
// 1.6.0: only a team down or the horizon fails a task (behaviour; struct
// layouts unchanged). Done is uniform: success, team down, horizon. Aggro no
// longer fails when no enemy lives, Dodge no longer fails on a down;
// Companions_End_TaskFailed is no longer produced.
#define COMPANIONS_VERSION "1.6.0"

// =============================================================================
// Thread-local error message
// =============================================================================
static thread_local char g_error_buffer[256] = {0};

static void SetError(const char* msg) {
  std::strncpy(g_error_buffer, msg, sizeof(g_error_buffer) - 1);
  g_error_buffer[sizeof(g_error_buffer) - 1] = '\0';
}

// =============================================================================
// Thread-local generated level cache
// =============================================================================
static thread_local std::vector<uint8_t> g_generated_level;

// =============================================================================
// Internal Environment Wrapper
// =============================================================================
struct Companions_Env {
  std::unique_ptr<companions::BaseEnv> env;
  Companions_EnvConfig config;
  bool done = false;
  bool success = false;
  // The done of the last step: EpisodeEnd is reported only on the step where
  // done becomes true, not on the steps a host keeps playing afterwards.
  // Cleared by reset, snapshot loads and lens changes (a new episode).
  bool last_step_done = false;
  // Why the episode ended: set when done becomes true (a step or a lens
  // change), kept while a host plays on; reset and snapshot loads take the
  // env's (a snapshot loaded at the horizon is done, as Horizon).
  Companions_EndReason end_reason = Companions_End_None;
  std::vector<double> last_rewards;

  // Previous positions for tracking movement (for animation)
  std::vector<companions::Position> prev_positions;

  // Event buffer for current step
  std::vector<Companions_Event> events;

  // Cached snapshot for the two-call save pattern. `mutable` because
  // companions_get_snapshot_size is declared `const Companions_Env*` on the
  // C API boundary, yet must populate the cache to return its size.
  // Audit F13.
  mutable std::vector<uint8_t> cached_snapshot;

  // Copies of the TagTable's names (index = tag id), handed out by
  // companions_get_tag_name. A deque never moves its elements, and the table
  // only grows and never renames, so a returned pointer stays valid for the
  // env's lifetime (TagTable's own strings move when it grows).
  mutable std::deque<std::string> tag_names;

  // The last companions_preview_skill_outcome's world (the clone its use
  // resolved on: its reports are Companions_Report_Preview's), or null.
  // `mutable` like cached_snapshot: the preview is a const query of the env.
  // Dropped by a step, a reset and a snapshot load.
  mutable std::unique_ptr<companions::BaseEnv> preview;
};

// =============================================================================
// Conversion Helpers
// =============================================================================

static Companions_Position ToAPIPosition(companions::Position pos) {
  return {pos.row, pos.col};
}

static Companions_Direction ToAPIDirection(companions::Direction dir) {
  switch (dir) {
    case companions::Direction::Up:
      return Companions_Direction_Up;
    case companions::Direction::Down:
      return Companions_Direction_Down;
    case companions::Direction::Left:
      return Companions_Direction_Left;
    case companions::Direction::Right:
      return Companions_Direction_Right;
  }
  return Companions_Direction_Down;
}

static Companions_Faction ToAPIFaction(companions::Faction faction) {
  switch (faction) {
    case companions::Faction::COMPANION:
      return Companions_Faction_Companion;
    case companions::Faction::ENEMY:
      return Companions_Faction_Enemy;
    case companions::Faction::NEUTRAL:
      return Companions_Faction_Neutral;
  }
  return Companions_Faction_Neutral;
}

static Companions_ObjectType ToAPIObjectType(companions::ObjectType type) {
  switch (type) {
    case companions::ObjectType::Object:
      return Companions_ObjectType_Object;
    case companions::ObjectType::Actor:
      return Companions_ObjectType_Actor;
    case companions::ObjectType::Agent:
      return Companions_ObjectType_Agent;
    case companions::ObjectType::AgentFSM:
      return Companions_ObjectType_AgentFSM;
    case companions::ObjectType::Companion:
      return Companions_ObjectType_Companion;
    case companions::ObjectType::Player:
      return Companions_ObjectType_Player;
    case companions::ObjectType::NPCCompanion:
      return Companions_ObjectType_NPCCompanion;
  }
  return Companions_ObjectType_Object;
}

static Companions_CellKind ToAPICellKind(companions::CellKind kind) {
  switch (kind) {
    case companions::CellKind::Floor:
      return Companions_CellKind_Floor;
    case companions::CellKind::Wall:
      return Companions_CellKind_Wall;
    case companions::CellKind::Hazard:
      return Companions_CellKind_Hazard;
    case companions::CellKind::HealArea:
      return Companions_CellKind_HealArea;
  }
  return Companions_CellKind_Floor;
}

static Companions_StatusType ToAPIStatusType(companions::StatusType type) {
  switch (type) {
    case companions::StatusType::None:
      return Companions_Status_None;
    case companions::StatusType::Stunned:
      return Companions_Status_Stunned;
    case companions::StatusType::Marked:
      return Companions_Status_Marked;
    case companions::StatusType::Rooted:
      return Companions_Status_Rooted;
  }
  return Companions_Status_None;
}

static_assert(static_cast<int>(companions::StatusType::None) == Companions_Status_None,
              "StatusType::None out of sync with C API");
static_assert(static_cast<int>(companions::StatusType::Stunned) == Companions_Status_Stunned,
              "StatusType::Stunned out of sync with C API");
static_assert(static_cast<int>(companions::StatusType::Marked) == Companions_Status_Marked,
              "StatusType::Marked out of sync with C API");
static_assert(static_cast<int>(companions::StatusType::Rooted) == Companions_Status_Rooted,
              "StatusType::Rooted out of sync with C API");

// Skills and interact actions
static_assert(companions::kMaxSkillSlots == Companions_MAX_SKILL_SLOTS,
              "kMaxSkillSlots out of sync with C API");
static_assert(static_cast<int>(companions::InteractAction::None) == Companions_Interact_None &&
                  static_cast<int>(companions::InteractAction::Skill1) ==
                      Companions_Interact_Skill1 &&
                  static_cast<int>(companions::InteractAction::Skill2) ==
                      Companions_Interact_Skill2,
              "InteractAction out of sync with C API");
static_assert(companions::kInvalidTag == -1, "C API documents -1 as the invalid tag id");
static_assert(companions::kPermanentTag == -1, "C API documents -1 as a permanent tag");
// The env refuses longer skill / tag names, so they always fit the buffers.
static_assert(companions::kMaxNameLength == Companions_SKILL_NAME_LEN - 1,
              "kMaxNameLength out of sync with Companions_SKILL_NAME_LEN");
static_assert(companions::kMaxNameLength <= Companions_EFFECT_NAME_LEN - 1,
              "Event names (effect_name) must hold kMaxNameLength bytes");

// The skill book's enums are numerically 1:1 with the C API's.
static_assert(static_cast<int>(companions::SkillTargeting::Self) == Companions_SkillTargeting_Self &&
                  static_cast<int>(companions::SkillTargeting::Ground) ==
                      Companions_SkillTargeting_Ground &&
                  static_cast<int>(companions::SkillTargeting::Projectile) ==
                      Companions_SkillTargeting_Projectile,
              "SkillTargeting out of sync with C API");
static_assert(static_cast<int>(companions::SkillArea::Single) == Companions_SkillArea_Single &&
                  static_cast<int>(companions::SkillArea::Cross) == Companions_SkillArea_Cross,
              "SkillArea out of sync with C API");
static_assert(static_cast<int>(companions::SkillMotion::None) == Companions_SkillMotion_None &&
                  static_cast<int>(companions::SkillMotion::Dash) == Companions_SkillMotion_Dash &&
                  static_cast<int>(companions::SkillMotion::Teleport) ==
                      Companions_SkillMotion_Teleport &&
                  static_cast<int>(companions::SkillMotion::PushOut) ==
                      Companions_SkillMotion_PushOut &&
                  static_cast<int>(companions::SkillMotion::PullIn) ==
                      Companions_SkillMotion_PullIn,
              "SkillMotion out of sync with C API");
static_assert(static_cast<int>(companions::TargetFilter::All) == Companions_TargetFilter_All &&
                  static_cast<int>(companions::TargetFilter::Companion) ==
                      Companions_TargetFilter_Companion &&
                  static_cast<int>(companions::TargetFilter::Enemy) ==
                      Companions_TargetFilter_Enemy &&
                  static_cast<int>(companions::TargetFilter::Neutral) ==
                      Companions_TargetFilter_Neutral,
              "TargetFilter out of sync with C API");
// The env refuses a skill with more tags, so Companions_SkillInfo never truncates.
static_assert(companions::kMaxSkillTags == Companions_MAX_SKILL_TAGS,
              "kMaxSkillTags out of sync with Companions_MAX_SKILL_TAGS");

// Copy `src` into a fixed-size C string, always terminated. Skill and tag
// names never exceed kMaxNameLength (see the static_asserts above), so the
// truncation is only a safety net.
template <size_t N>
static void CopyName(char (&dst)[N], const std::string& src) {
  std::strncpy(dst, src.c_str(), N - 1);
  dst[N - 1] = '\0';
}

// companions::AgentKind is numerically 1:1 with Companions_AgentKind by
// construction — these asserts fire at compile time if someone adds a value
// to one enum and forgets the other.
static_assert(static_cast<int32_t>(companions::AgentKind::Unknown) ==
                  Companions_AgentKind_Unknown,
              "AgentKind::Unknown out of sync with C API");
static_assert(static_cast<int32_t>(companions::AgentKind::Companion) ==
                  Companions_AgentKind_Companion,
              "AgentKind::Companion out of sync with C API");
static_assert(static_cast<int32_t>(companions::AgentKind::NeutralNpc) ==
                  Companions_AgentKind_NeutralNpc,
              "AgentKind::NeutralNpc out of sync with C API");
static_assert(static_cast<int32_t>(companions::AgentKind::EnemyZombie) ==
                  Companions_AgentKind_EnemyZombie,
              "AgentKind::EnemyZombie out of sync with C API");
static_assert(static_cast<int32_t>(companions::AgentKind::EnemyGoblin) ==
                  Companions_AgentKind_EnemyGoblin,
              "AgentKind::EnemyGoblin out of sync with C API");
static_assert(static_cast<int32_t>(companions::AgentKind::EnemyDragon) ==
                  Companions_AgentKind_EnemyDragon,
              "AgentKind::EnemyDragon out of sync with C API");

static Companions_AgentKind ToAPIAgentKind(const companions::Agent* agent) {
  return static_cast<Companions_AgentKind>(agent->GetAgentKind());
}

static Companions_FSMStateType ToAPIFSMState(const companions::FSMState* state) {
  if (!state) return Companions_FSMState_None;

  std::string name = state->GetName();
  if (name == "Patrol") return Companions_FSMState_Patrol;
  if (name == "Aggro") return Companions_FSMState_Aggro;
  if (name == "ReturnToPatrol") return Companions_FSMState_ReturnToPatrol;
  if (name == "Telegraph") return Companions_FSMState_Telegraph;
  if (name == "Attack") return Companions_FSMState_Attack;
  if (name == "Recovery") return Companions_FSMState_Recovery;
  return Companions_FSMState_None;
}

// Extract agent state from C++ Agent (`env` computes the effective skills)
static void ExtractAgentState(const companions::BaseEnv& env,
                              const companions::Agent* agent,
                              Companions_AgentState* out,
                              companions::Position prev_pos) {
  out->id = agent->GetId();
  out->type = ToAPIObjectType(agent->GetType());
  out->kind = ToAPIAgentKind(agent);
  out->position = ToAPIPosition(agent->GetPosition());
  out->prev_position = ToAPIPosition(prev_pos);
  out->faction = ToAPIFaction(agent->GetFaction());
  out->health = agent->GetHealth();
  out->max_health = agent->GetMaxHealth();
  out->alive = agent->IsAlive();
  out->downed = agent->IsDowned();
  out->agent_index = agent->GetAgentIndex();

  // Default values for non-companion types
  out->facing = Companions_Direction_Down;

  // Check if it's a Companion (has direction)
  if (auto* companion =
          dynamic_cast<const companions::Companion*>(agent)) {
    out->facing = ToAPIDirection(companion->GetDirection());
  }

  // FSM state (for AgentFSM types)
  out->fsm_state = Companions_FSMState_None;
  out->fsm_target_id = Companions_INVALID_ID;
  if (auto* fsm_agent =
          dynamic_cast<const companions::AgentFSM*>(agent)) {
    out->fsm_state = ToAPIFSMState(fsm_agent->GetCurrentState());
    if (fsm_agent->HasFSM()) {
      out->fsm_target_id = fsm_agent->GetFSMContext().target_id;
    }
  }

  // Status effects
  const auto& statuses = agent->GetStatuses();
  out->status_count = std::min(static_cast<int>(statuses.size()),
                               Companions_MAX_STATUSES);
  for (int i = 0; i < out->status_count; i++) {
    out->statuses[i].type = ToAPIStatusType(statuses[i].type);
    out->statuses[i].duration = statuses[i].duration;
  }

  // Tags (the first Companions_MAX_TAGS)
  const auto& tags = agent->GetTags();
  out->tag_count = std::min(static_cast<int>(tags.size()), Companions_MAX_TAGS);
  for (int i = 0; i < out->tag_count; i++) {
    out->tags[i].tag_id = tags[i].id;
    out->tags[i].duration = tags[i].duration;
  }

  // Skill slots: companions only (never "": "attack" by default), "" / 0 for
  // everyone else. skills = effective (a context rule's, else the equipped
  // one), equipped_skills = the slot's own; the cooldowns are the equipped
  // skills'.
  const auto* slots = dynamic_cast<const companions::Companion*>(agent);
  for (int slot = 0; slot < Companions_MAX_SKILL_SLOTS; slot++) {
    CopyName(out->skills[slot], slots ? env.EffectiveSkill(*slots, slot) : std::string());
    CopyName(out->equipped_skills[slot], slots ? slots->GetSkill(slot) : std::string());
    out->skill_cooldowns[slot] = slots ? slots->GetCooldown(slot) : 0;
  }

  // Actions - intent (before collision resolution) vs actual (after)
  auto intent = agent->GetOriginalIntention();
  out->action_intent.movement =
      static_cast<Companions_MovementAction>(intent.movement);
  out->action_intent.interact =
      static_cast<Companions_InteractAction>(intent.interact);

  auto actual = agent->GetExecutedAction();
  out->action_actual.movement =
      static_cast<Companions_MovementAction>(actual.movement);
  out->action_actual.interact =
      static_cast<Companions_InteractAction>(actual.interact);

  // Action succeeded if intent matches actual (movement wasn't blocked)
  out->action_succeeded = (intent.movement == actual.movement);
}

// Extract full game state
static void ExtractGameState(const Companions_Env* wrapper,
                             Companions_GameState* out) {
  auto* env = wrapper->env.get();

  out->rows = env->GetRows();
  out->cols = env->GetCols();
  out->tick = env->GetTick();

  // Extract agents
  const auto& obj_mgr = env->GetObjectManager();
  auto agents = obj_mgr.GetAllAgents();
  out->agent_count = std::min(static_cast<int>(agents.size()),
                              static_cast<int>(Companions_MAX_AGENTS));

  for (int i = 0; i < out->agent_count; i++) {
    companions::Position prev_pos = agents[i]->GetPosition();
    if (i < static_cast<int>(wrapper->prev_positions.size())) {
      prev_pos = wrapper->prev_positions[i];
    }
    ExtractAgentState(*env, agents[i], &out->agents[i], prev_pos);
  }

  // Extract special cells (non-Floor)
  const auto& grid = env->GetGrid();
  out->special_cell_count = 0;
  for (int r = 0; r < out->rows && out->special_cell_count < Companions_MAX_SPECIAL_CELLS; r++) {
    for (int c = 0; c < out->cols && out->special_cell_count < Companions_MAX_SPECIAL_CELLS; c++) {
      const auto& cell = grid.GetCell(r, c);
      if (cell.GetKind() != companions::CellKind::Floor) {
        auto& sc = out->special_cells[out->special_cell_count++];
        sc.position = {r, c};
        sc.kind = ToAPICellKind(cell.GetKind());
        // Check for occupant
        auto* actor = obj_mgr.GetActorAt({r, c});
        sc.occupant_id = actor ? actor->GetId() : Companions_INVALID_ID;
      }
    }
  }

  // Extract active effects
  const auto& effects = env->GetActiveEffects();
  out->effect_count = std::min(static_cast<int>(effects.size()),
                               static_cast<int>(Companions_MAX_EFFECTS));
  for (int i = 0; i < out->effect_count; i++) {
    auto& ue_effect = out->effects[i];
    const auto& effect = effects[i];

    // Generate a simple ID from index
    ue_effect.effect_id = i;
    // Get name from config
    if (effect.config) {
      std::strncpy(ue_effect.effect_name, effect.config->name.c_str(),
                   Companions_EFFECT_NAME_LEN - 1);
    } else {
      ue_effect.effect_name[0] = '\0';
    }
    ue_effect.effect_name[Companions_EFFECT_NAME_LEN - 1] = '\0';
    ue_effect.center = ToAPIPosition(effect.GetCenter(obj_mgr));
    ue_effect.direction = ToAPIDirection(effect.direction);
    ue_effect.ticks_remaining = effect.ticks_remaining;
    ue_effect.in_telegraph = effect.in_telegraph;
    ue_effect.source_id = effect.source_id;

    // For now, leave affected_count as 0 (would need to compute from config)
    ue_effect.affected_count = 0;
  }

  // Episode status
  out->done = wrapper->done;
  out->success = wrapper->success;
  out->downs = env->GetDowns();
  out->max_downs = env->GetMaxDowns();
  out->team_down = env->IsTeamDown();  // Live, not the latched end reason
  for (int i = 0; i < out->agent_count; i++) {
    out->rewards[i] = static_cast<float>(
        i < static_cast<int>(wrapper->last_rewards.size())
            ? wrapper->last_rewards[i]
            : 0.0);
  }
}

// =============================================================================
// Event Generation
// =============================================================================

// An event with every field zeroed but those whose "none" is -1 (1.5): no
// reaction (tag_reaction), no report entry (report_index)
static Companions_Event NewEvent() {
  Companions_Event evt = {};
  evt.tag_reaction = -1;
  evt.report_index = -1;
  return evt;
}

static void AddMovementEvents(Companions_Env* wrapper) {
  auto* env = wrapper->env.get();
  auto agents = env->GetObjectManager().GetAllAgents();

  for (size_t i = 0; i < agents.size() && i < wrapper->prev_positions.size(); i++) {
    auto prev = wrapper->prev_positions[i];
    auto curr = agents[i]->GetPosition();

    if (prev != curr) {
      // Agent moved
      Companions_Event evt = NewEvent();
      evt.type = Companions_Event_AgentMoved;
      evt.tick = env->GetTick();
      evt.subject_id = agents[i]->GetId();
      evt.position = ToAPIPosition(curr);
      evt.from_pos = ToAPIPosition(prev);
      evt.to_pos = ToAPIPosition(curr);
      evt.move_action = static_cast<Companions_MovementAction>(
          agents[i]->GetExecutedAction().movement);
      wrapper->events.push_back(evt);
    } else if (agents[i]->GetOriginalIntention().movement !=
               companions::MovementAction::Stay) {
      // Tried to move but was blocked. Use original_intention_ (pre-collision)
      // because collision resolution overwrites intention_ with Stay, so
      // GetExecutedAction() would report Stay for every blocked agent.
      Companions_Event evt = NewEvent();
      evt.type = Companions_Event_AgentBlocked;
      evt.tick = env->GetTick();
      evt.subject_id = agents[i]->GetId();
      evt.position = ToAPIPosition(curr);
      evt.from_pos = ToAPIPosition(prev);
      auto intended = companions::ApplyMovement(
          prev, agents[i]->GetOriginalIntention().movement);
      evt.to_pos = ToAPIPosition(intended);
      evt.move_action = static_cast<Companions_MovementAction>(
          agents[i]->GetOriginalIntention().movement);
      wrapper->events.push_back(evt);
    }
  }
}

// Where `id` stands now, {-1, -1} for no actor
static Companions_Position ActorCell(const companions::BaseEnv& env, companions::ObjectId id) {
  const companions::Actor* actor = env.GetObjectManager().GetActor(id);
  return actor ? ToAPIPosition(actor->GetPosition()) : Companions_Position{-1, -1};
}

// One SkillUsed event per skill use of this step (subject = caster, position
// = the skill's centre, effect_id = the slot), then one TagApplied per tag
// landing (subject = the agent, at its cell after the step; effect_id = the
// tag id; health_source_id = caster or -1 for a zone or the host; tag_kind,
// tag_reaction; health_amount = the zone damage it dealt), then one
// ReactionFired per reaction (subject = the trigger, at its cell; effect_id =
// the rule, effect_name = its result; health_source_id / tag_kind = the
// triggering landing's). report_index = the entry in the report query.
static void AddSkillAndTagEvents(Companions_Env* wrapper) {
  auto* env = wrapper->env.get();
  const auto& uses = env->GetLastSkillUses();
  for (size_t i = 0; i < uses.size(); ++i) {
    const auto& use = uses[i];
    Companions_Event evt = NewEvent();
    evt.type = Companions_Event_SkillUsed;
    evt.tick = env->GetTick();
    evt.subject_id = use.caster;
    evt.position = ToAPIPosition(use.target);
    evt.effect_id = use.slot;
    CopyName(evt.effect_name, use.skill);
    evt.report_index = static_cast<int32_t>(i);
    wrapper->events.push_back(evt);
  }
  const auto& landings = env->GetLastTagsApplied();
  for (size_t i = 0; i < landings.size(); ++i) {
    const auto& landed = landings[i];
    Companions_Event evt = NewEvent();
    evt.type = Companions_Event_TagApplied;
    evt.tick = env->GetTick();
    evt.subject_id = landed.agent;
    evt.position = ActorCell(*env, landed.agent);
    evt.effect_id = landed.tag;
    CopyName(evt.effect_name, env->GetTagTable().Name(landed.tag));
    evt.status_duration = landed.duration;
    evt.health_source_id = landed.source;
    evt.health_amount = landed.damage;
    evt.tag_fresh = landed.fresh;
    evt.tag_kind = static_cast<Companions_TagSource>(landed.kind);
    evt.tag_reaction = landed.reaction;
    evt.report_index = static_cast<int32_t>(i);
    wrapper->events.push_back(evt);
  }
  const auto& reactions = env->GetLastReactions();
  for (size_t i = 0; i < reactions.size(); ++i) {
    const auto& fired = reactions[i];
    Companions_Event evt = NewEvent();
    evt.type = Companions_Event_ReactionFired;
    evt.tick = env->GetTick();
    evt.subject_id = fired.trigger;
    evt.position = ActorCell(*env, fired.trigger);
    evt.effect_id = fired.rule;
    const auto& rules = env->GetReactions();
    if (fired.rule >= 0 && static_cast<size_t>(fired.rule) < rules.size()) {
      CopyName(evt.effect_name, rules[static_cast<size_t>(fired.rule)].result);
    }
    evt.health_source_id = fired.source;
    evt.tag_kind = static_cast<Companions_TagSource>(fired.kind);
    evt.report_index = static_cast<int32_t>(i);
    wrapper->events.push_back(evt);
  }
}

// One AgentDefeated per defeat (subject = the agent, at its cell; effect_id /
// effect_name = the tag S; health_source_id / tag_kind / tag_reaction = its
// landing's; report_index), among the state changes: for an agent that is
// not a companion it is the only event of its death, so the cap must not
// drop it before the skills and tags.
static void AddDefeatEvents(Companions_Env* wrapper) {
  auto* env = wrapper->env.get();
  const auto& defeats = env->GetLastDefeats();
  for (size_t i = 0; i < defeats.size(); ++i) {
    const auto& defeat = defeats[i];
    Companions_Event evt = NewEvent();
    evt.type = Companions_Event_AgentDefeated;
    evt.tick = env->GetTick();
    evt.subject_id = defeat.agent;
    evt.position = ActorCell(*env, defeat.agent);
    evt.effect_id = defeat.tag;
    CopyName(evt.effect_name, env->GetTagTable().Name(defeat.tag));
    evt.health_source_id = defeat.source;
    evt.tag_kind = static_cast<Companions_TagSource>(defeat.kind);
    evt.tag_reaction = defeat.reaction;
    evt.report_index = static_cast<int32_t>(i);
    wrapper->events.push_back(evt);
  }
}

// One AgentDowned event per down since the last report (subject = the
// companion, at its cell): this step's, and any between steps (a host effect).
static void AddDownEvents(Companions_Env* wrapper) {
  const companions::BaseEnv* env = wrapper->env.get();
  for (companions::ObjectId id : env->GetLastDowns()) {
    Companions_Event evt = NewEvent();
    evt.type = Companions_Event_AgentDowned;
    evt.tick = env->GetTick();
    evt.subject_id = id;
    const companions::Actor* actor = env->GetObjectManager().GetActor(id);
    evt.position = actor ? ToAPIPosition(actor->GetPosition()) : Companions_Position{-1, -1};
    wrapper->events.push_back(evt);
  }
}

// One AgentRevived event per revive of this step, in resolution order
// (subject = the revived companion, at its cell; health_source_id = the
// reviver; health_new = health_amount = the HP it got up with).
static void AddReviveEvents(Companions_Env* wrapper) {
  const companions::BaseEnv* env = wrapper->env.get();
  for (const companions::BaseEnv::Revival& revival : env->GetLastRevives()) {
    Companions_Event evt = NewEvent();
    evt.type = Companions_Event_AgentRevived;
    evt.tick = env->GetTick();
    evt.subject_id = revival.revived;
    const companions::Actor* actor = env->GetObjectManager().GetActor(revival.revived);
    evt.position = actor ? ToAPIPosition(actor->GetPosition()) : Companions_Position{-1, -1};
    evt.health_source_id = revival.reviver;
    evt.health_new = revival.health;
    evt.health_amount = revival.health;  // Gained from 0
    wrapper->events.push_back(evt);
  }
}

// A step that threw: the state as the env holds it now (what the aborted step
// did before it threw included), and no event
static void FillFailedStep(Companions_Env* env, Companions_StepResult* out) {
  env->events.clear();
  ExtractGameState(env, &out->state);
  out->event_count = 0;
  out->events_dropped = 0;
}

// The companion `agent` of `env` (errors "Agent not found" / "Not a
// companion"), or null
static const companions::Companion* PreviewedCompanion(const Companions_Env* env,
                                                       Companions_ObjectId agent) {
  const companions::Actor* actor = env->env->GetObjectManager().GetActor(agent);
  if (!dynamic_cast<const companions::Agent*>(actor)) {
    SetError("Agent not found");
    return nullptr;
  }
  const auto* comp = dynamic_cast<const companions::Companion*>(actor);
  if (!comp) SetError("Not a companion");
  return comp;
}

// `aim` as a companions::Direction; false ("Invalid direction") outside Up..Right
static bool ToDirection(Companions_Direction aim, companions::Direction* out) {
  switch (aim) {
    case Companions_Direction_Up: *out = companions::Direction::Up; return true;
    case Companions_Direction_Down: *out = companions::Direction::Down; return true;
    case Companions_Direction_Left: *out = companions::Direction::Left; return true;
    case Companions_Direction_Right: *out = companions::Direction::Right; return true;
  }
  SetError("Invalid direction");
  return false;
}

// Name of `tag_id` from the wrapper's stable copies (see tag_names), or null.
static const char* StableTagName(const Companions_Env* wrapper, int32_t tag_id) {
  const auto& table = wrapper->env->GetTagTable();
  if (tag_id < 0 || tag_id >= table.Size()) return nullptr;
  while (static_cast<int>(wrapper->tag_names.size()) < table.Size()) {
    wrapper->tag_names.push_back(table.Name(static_cast<int>(wrapper->tag_names.size())));
  }
  const std::string& name = wrapper->tag_names[static_cast<size_t>(tag_id)];
  assert(name == table.Name(tag_id) && "TagTable must only grow, never rename");
  return name.c_str();
}

// =============================================================================
// Public API Implementation
// =============================================================================

extern "C" {

COMPANIONS_API Companions_Env* companions_create(
    const Companions_EnvConfig* config) {
  if (!config) {
    SetError("Config is null");
    return nullptr;
  }

  auto* wrapper = new Companions_Env();
  wrapper->config = *config;

  try {
    wrapper->env = std::make_unique<companions::SynchroEnv>(
        config->rows,
        config->cols,
        config->num_companions,
        config->num_synchro,
        config->map_complexity,
        config->seed,
        config->d4_transform,
        config->horizon);

    wrapper->last_rewards.resize(config->num_companions, 0.0);
    wrapper->prev_positions.resize(config->num_companions);

  } catch (const std::exception& e) {
    SetError(e.what());
    delete wrapper;
    return nullptr;
  } catch (...) {
    SetError("Unknown error");
    delete wrapper;
    return nullptr;
  }

  return wrapper;
}

COMPANIONS_API void companions_destroy(Companions_Env* env) {
  delete env;
}

COMPANIONS_API Companions_Env* companions_create_aggro(
    const Companions_AggroEnvConfig* config) {
  if (!config) {
    SetError("Config is null");
    return nullptr;
  }

  auto* wrapper = new Companions_Env();

  try {
    // Convert API enemy type to internal enum
    companions::EnemyType enemy_type;
    switch (config->enemy_type) {
      case Companions_Enemy_Zombie:
        enemy_type = companions::EnemyType::Zombie;
        break;
      case Companions_Enemy_Archer:
      case Companions_Enemy_Mage:
      default:
        enemy_type = companions::EnemyType::Zombie;  // Default for now
        break;
    }

    // AggroEnv uses grid_size (square grid)
    int grid_size = std::max(config->rows, config->cols);

    wrapper->env = std::make_unique<companions::AggroEnv>(
        grid_size,
        config->num_companions,
        enemy_type,
        config->seed,
        config->d4_transform,
        config->horizon);

    // AggroEnv has companions + 1 enemy
    int total_agents = config->num_companions + 1;
    wrapper->last_rewards.resize(total_agents, 0.0);
    wrapper->prev_positions.resize(total_agents);

  } catch (const std::exception& e) {
    SetError(e.what());
    delete wrapper;
    return nullptr;
  } catch (...) {
    SetError("Unknown error");
    delete wrapper;
    return nullptr;
  }

  return wrapper;
}

// The env's end reason while the cached done is true, else None (the values
// of companions::EndReason are those of Companions_EndReason).
static_assert(static_cast<int>(companions::EndReason::None) == Companions_End_None &&
                  static_cast<int>(companions::EndReason::Success) == Companions_End_Success &&
                  static_cast<int>(companions::EndReason::Horizon) == Companions_End_Horizon &&
                  static_cast<int>(companions::EndReason::TaskFailed) == Companions_End_TaskFailed &&
                  static_cast<int>(companions::EndReason::TeamDown) == Companions_End_TeamDown,
              "EndReason and Companions_EndReason must match");
static Companions_EndReason CurrentEndReason(const Companions_Env& env) {
  if (!env.done) return Companions_End_None;
  return static_cast<Companions_EndReason>(env.env->GetEndReason());
}

// A new episode (reset, snapshot load, lens change): done / success / end reason as the
// env latched them (a state loaded at the horizon is done, as Horizon). The
// EpisodeEnd event is still to report: the next step does.
static void StartEpisode(Companions_Env& env) {
  env.done = env.env->IsDone();
  env.success = env.env->IsSuccess();
  env.last_step_done = false;
  env.end_reason = CurrentEndReason(env);
}

// =============================================================================
// Task Lens API
// =============================================================================

COMPANIONS_API bool companions_set_task_lens(Companions_Env* env, Companions_LensType lens) {
  if (!env || !env->env) {
    SetError("companions_set_task_lens: null env");
    return false;
  }
  try {
    std::unique_ptr<companions::TaskLens> new_lens;
    switch (lens) {
      case Companions_Lens_Synchro:
        new_lens = std::make_unique<companions::SynchroLens>();
        break;
      case Companions_Lens_Aggro:
        new_lens = std::make_unique<companions::AggroLens>();
        break;
      case Companions_Lens_Dodge:
        new_lens = std::make_unique<companions::DodgeLens>();
        break;
      default:
        SetError("companions_set_task_lens: invalid lens type");
        return false;
    }
    if (!env->env->SetTaskLens(std::move(new_lens))) {
      SetError("Lens incompatible with current environment state");
      return false;
    }
    // A new task is a new episode: its done / success (the player may
    // already be on the new lens's goal cell) and its EpisodeEnd to report
    StartEpisode(*env);
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

COMPANIONS_API bool companions_set_task_lens_with_params(
    Companions_Env* env,
    Companions_LensType lens,
    const Companions_Position* positions,
    int num_positions) {
  if (!env || !env->env) {
    SetError("companions_set_task_lens_with_params: null env");
    return false;
  }
  try {
    std::unique_ptr<companions::TaskLens> new_lens;
    switch (lens) {
      case Companions_Lens_Synchro:
        new_lens = std::make_unique<companions::SynchroLens>();
        break;
      case Companions_Lens_Aggro:
        new_lens = std::make_unique<companions::AggroLens>();
        break;
      case Companions_Lens_Dodge:
        new_lens = std::make_unique<companions::DodgeLens>();
        break;
      case Companions_Lens_TagApply:
        // TagApplyLens not yet implemented — fall back to SynchroLens so the
        // plumbing path exercises. Remove this fallback once the lens lands.
        new_lens = std::make_unique<companions::SynchroLens>();
        break;
      default:
        SetError("companions_set_task_lens_with_params: invalid lens type");
        return false;
    }

    companions::LensParams params;
    if (positions && num_positions > 0) {
      params.positions.reserve(num_positions);
      for (int i = 0; i < num_positions; ++i) {
        params.positions.push_back(companions::Position{
            positions[i].row, positions[i].col});
      }
    }

    if (!env->env->SetTaskLensWithParams(std::move(new_lens), params)) {
      SetError("Lens incompatible with current environment state");
      return false;
    }
    StartEpisode(*env);  // A new episode, as companions_set_task_lens
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

COMPANIONS_API Companions_LensType companions_get_task_lens(Companions_Env* env) {
  if (!env || !env->env) {
    return Companions_Lens_Synchro;  // Default
  }
  auto* lens = env->env->GetTaskLens();
  if (!lens) return Companions_Lens_Synchro;

  // Single virtual dispatch instead of a dynamic_cast chain; unknown kinds
  // map to Companions_Lens_Unknown rather than silently returning Synchro.
  // Audit F9.
  switch (lens->GetKind()) {
    case companions::TaskLens::kSynchro:  return Companions_Lens_Synchro;
    case companions::TaskLens::kAggro:    return Companions_Lens_Aggro;
    case companions::TaskLens::kDodge:    return Companions_Lens_Dodge;
    case companions::TaskLens::kTagApply: return Companions_Lens_TagApply;
    case companions::TaskLens::kUnknown:  return Companions_Lens_Unknown;
  }
  return Companions_Lens_Unknown;
}

COMPANIONS_API bool companions_reset(Companions_Env* env,
                                           uint32_t seed) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return false;
  }

  env->preview.reset();
  env->events.clear();
  try {
    env->env->Reset(seed);
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
  StartEpisode(*env);
  std::fill(env->last_rewards.begin(), env->last_rewards.end(), 0.0);
  env->events.clear();

  // Store initial positions
  auto agents = env->env->GetObjectManager().GetAllAgents();
  env->prev_positions.resize(agents.size());
  for (size_t i = 0; i < agents.size(); i++) {
    env->prev_positions[i] = agents[i]->GetPosition();
  }
  return true;
}

// The actions as the env's flat actions; false with the error set for a
// value outside the enums
static bool ConvertActions(const Companions_Action* actions, int32_t action_count,
                           std::vector<companions::Action>* out) {
  out->resize(static_cast<size_t>(action_count));
  for (int i = 0; i < action_count; i++) {
    if (actions[i].movement < Companions_Movement_Stay ||
        actions[i].movement > Companions_Movement_Right) {
      SetError("Invalid movement action");
      return false;
    }
    // An interact outside None..Skill2 is invalid. A skill slot the action
    // space does not enable yet (today Skill2) is None: EncodeAction would
    // otherwise carry it into the movement digit of the flat action.
    int32_t interact = static_cast<int32_t>(actions[i].interact);
    if (interact < Companions_Interact_None || interact > Companions_Interact_Skill2) {
      SetError("Invalid interact action");
      return false;
    }
    if (interact >= companions::kNumInteractActions) interact = Companions_Interact_None;
    (*out)[static_cast<size_t>(i)] = companions::EncodeAction(
        static_cast<companions::MovementAction>(actions[i].movement),
        static_cast<companions::InteractAction>(interact));
  }
  return true;
}

// Steps, then fills `out_result` (see companions_step); throws what the env
// throws
static bool StepAndReport(Companions_Env* env, const std::vector<companions::Action>& cpp_actions,
                          Companions_StepResult* out_result) {
  // Store previous positions for event generation
  auto agents = env->env->GetObjectManager().GetAllAgents();
  env->prev_positions.resize(agents.size());
  for (size_t i = 0; i < agents.size(); i++) {
    env->prev_positions[i] = agents[i]->GetPosition();
  }

  // Clear events from previous step, and the outcome preview (of the world
  // before it)
  env->events.clear();
  env->preview.reset();

  companions::StepResult result = env->env->Step(cpp_actions);

  // Update wrapper state
  const bool episode_ended = result.done && !env->last_step_done;
  env->last_step_done = result.done;
  env->done = result.done;
  env->success = env->env->IsSuccess();
  // BaseEnv::Step latches the reason when done becomes true, so playing on
  // keeps it
  env->end_reason = CurrentEndReason(*env);
  env->last_rewards = result.rewards;

  // Generate the events. The state changes (movements, downs, revives,
  // defeats) come before the skills, tags and reactions, which can overflow
  // Companions_MAX_EVENTS, so the cap cuts those first (see "Event System"
  // in companions_api.h for when the state changes always fit).
  AddMovementEvents(env);
  AddDownEvents(env);
  AddReviveEvents(env);
  AddDefeatEvents(env);
  AddSkillAndTagEvents(env);

  // Add the episode end event on the step that ends the episode
  if (episode_ended) {
    Companions_Event evt = NewEvent();
    evt.type = Companions_Event_EpisodeEnd;
    evt.tick = env->env->GetTick();
    evt.episode_success = env->success;
    evt.episode_reward = 0.0f;
    for (double r : env->last_rewards) {
      evt.episode_reward += static_cast<float>(r);
    }
    evt.episode_steps = env->env->GetTick();
    evt.effect_id = static_cast<int32_t>(env->end_reason);  // Companions_EndReason
    env->events.push_back(evt);
  }

  // Extract state
  ExtractGameState(env, &out_result->state);

  // Copy events: the first Companions_MAX_EVENTS, except that an EpisodeEnd
  // (always the last event) takes the last place when they do not all fit.
  const int total = static_cast<int>(env->events.size());
  const int count = std::min(total, static_cast<int>(Companions_MAX_EVENTS));
  std::copy_n(env->events.begin(), count, out_result->events);
  if (episode_ended && total > count) out_result->events[count - 1] = env->events.back();
  out_result->event_count = count;
  out_result->events_dropped = total - count;
  return true;
}

COMPANIONS_API bool companions_step(Companions_Env* env,
                                          const Companions_Action* actions,
                                          int32_t action_count,
                                          Companions_StepResult* out_result) {
  g_error_buffer[0] = '\0';  // A step's error is its own
  if (!env || !env->env || !actions || !out_result) {
    SetError("Invalid arguments");
    return false;
  }
  if (action_count != env->env->NumAgents()) {
    SetError("Invalid action count");
    return false;
  }
  std::vector<companions::Action> cpp_actions;
  try {
    if (!ConvertActions(actions, action_count, &cpp_actions)) return false;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
  // A step that throws reports the error (nothing crosses the C boundary),
  // and out_result the state as the env holds it (BaseEnv::Step aborted the
  // step: between two steps again), with no event.
  try {
    return StepAndReport(env, cpp_actions, out_result);
  } catch (const std::exception& e) {
    SetError(e.what());
  } catch (...) {
    SetError("Unknown error");
  }
  try {
    FillFailedStep(env, out_result);
  } catch (...) {
    out_result->event_count = 0;
    out_result->events_dropped = 0;
  }
  return false;
}

COMPANIONS_API void companions_get_state(const Companions_Env* env,
                                               Companions_GameState* out_state) {
  try {
    if (!env || !env->env || !out_state) {
      SetError("Invalid arguments");
      return;
    }

    ExtractGameState(env, out_state);
  } catch (const std::exception& e) {
    SetError(e.what());
    return;
  } catch (...) {
    SetError("Unknown error");
    return;
  }
}

COMPANIONS_API bool companions_get_agent(const Companions_Env* env,
                                               Companions_ObjectId id,
                                               Companions_AgentState* out_agent) {
  try {
    if (!env || !env->env || !out_agent) {
      SetError("Invalid arguments");
      return false;
    }

    auto agents = env->env->GetObjectManager().GetAllAgents();
    for (size_t i = 0; i < agents.size(); i++) {
      if (agents[i]->GetId() == id) {
        companions::Position prev = agents[i]->GetPosition();
        if (i < env->prev_positions.size()) {
          prev = env->prev_positions[i];
        }
        ExtractAgentState(*env->env, agents[i], out_agent, prev);
        return true;
      }
    }

    SetError("Agent not found");
    return false;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

COMPANIONS_API bool companions_get_agent_by_index(
    const Companions_Env* env,
    int32_t index,
    Companions_AgentState* out_agent) {
  try {
    if (!env || !env->env || !out_agent) {
      SetError("Invalid arguments");
      return false;
    }

    auto agents = env->env->GetObjectManager().GetAllAgents();
    if (index < 0 || index >= static_cast<int>(agents.size())) {
      SetError("Index out of range");
      return false;
    }

    companions::Position prev = agents[index]->GetPosition();
    if (static_cast<size_t>(index) < env->prev_positions.size()) {
      prev = env->prev_positions[index];
    }
    ExtractAgentState(*env->env, agents[index], out_agent, prev);
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

COMPANIONS_API Companions_CellKind companions_get_cell(
    const Companions_Env* env,
    int32_t row,
    int32_t col) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return Companions_CellKind_Floor;
  }

  if (row < 0 || row >= env->env->GetRows() ||
      col < 0 || col >= env->env->GetCols()) {
    SetError("Position out of bounds");
    return Companions_CellKind_Wall;
  }

  return ToAPICellKind(env->env->GetGrid().GetCell(row, col).GetKind());
}

COMPANIONS_API void companions_get_grid(const Companions_Env* env,
                                              Companions_CellKind* out_grid) {
  if (!env || !env->env || !out_grid) {
    SetError("Invalid arguments");
    return;
  }

  const auto& grid = env->env->GetGrid();
  int rows = env->env->GetRows();
  int cols = env->env->GetCols();

  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      out_grid[r * cols + c] = ToAPICellKind(grid.GetCell(r, c).GetKind());
    }
  }
}

COMPANIONS_API bool companions_set_cell(Companions_Env* env, int32_t row,
                                        int32_t col, Companions_CellKind kind) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return false;
  }
  if (row < 0 || row >= env->env->GetRows() ||
      col < 0 || col >= env->env->GetCols()) {
    SetError("Position out of bounds");
    return false;
  }
  companions::CellKind cpp_kind;
  switch (kind) {
    case Companions_CellKind_Floor: cpp_kind = companions::CellKind::Floor; break;
    case Companions_CellKind_Wall: cpp_kind = companions::CellKind::Wall; break;
    case Companions_CellKind_Hazard: cpp_kind = companions::CellKind::Hazard; break;
    case Companions_CellKind_HealArea: cpp_kind = companions::CellKind::HealArea; break;
    default:
      SetError("Invalid cell kind");
      return false;
  }
  env->env->GetMutableGrid().SetCell(row, col, cpp_kind);
  return true;
}

COMPANIONS_API bool companions_spawn_effect(Companions_Env* env,
                                            const char* effect_name,
                                            int32_t row, int32_t col,
                                            Companions_Direction direction,
                                            Companions_ObjectId source_id) {
  try {
    if (!env || !env->env || !effect_name) {
      SetError("Invalid arguments");
      return false;
    }
    if (row < 0 || row >= env->env->GetRows() ||
        col < 0 || col >= env->env->GetCols()) {
      SetError("Position out of bounds");
      return false;
    }
    if (!companions::EffectConfigRegistry::Instance().GetConfig(effect_name)) {
      SetError((std::string("Unknown effect: ") + effect_name).c_str());
      return false;
    }
    companions::Direction dir = companions::Direction::Up;
    switch (direction) {
      case Companions_Direction_Up: dir = companions::Direction::Up; break;
      case Companions_Direction_Down: dir = companions::Direction::Down; break;
      case Companions_Direction_Left: dir = companions::Direction::Left; break;
      case Companions_Direction_Right: dir = companions::Direction::Right; break;
    }
    env->env->SpawnEffect(effect_name,
                          companions::EffectTarget::AtCell(companions::Position{row, col}), dir,
                          source_id);
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

// =============================================================================
// Skills, tags and zones
// =============================================================================

COMPANIONS_API const char* companions_get_tag_name(const Companions_Env* env, int32_t tag_id) {
  try {
    if (!env || !env->env) {
      SetError("Invalid environment");
      return nullptr;
    }
    const char* name = StableTagName(env, tag_id);
    if (!name) SetError("Unknown tag id");
    return name;
  } catch (const std::exception& e) {
    SetError(e.what());
    return nullptr;
  } catch (...) {
    SetError("Unknown error");
    return nullptr;
  }
}

COMPANIONS_API int32_t companions_find_tag(const Companions_Env* env, const char* name) {
  try {
    if (!env || !env->env || !name) {
      SetError("Invalid arguments");
      return -1;
    }
    return env->env->GetTagTable().Find(name);
  } catch (const std::exception& e) {
    SetError(e.what());
    return -1;
  } catch (...) {
    SetError("Unknown error");
    return -1;
  }
}

COMPANIONS_API bool companions_apply_tag(Companions_Env* env, Companions_ObjectId agent,
                                         const char* tag, int32_t duration) {
  try {
    if (!env || !env->env || !tag) {
      SetError("Invalid arguments");
      return false;
    }
    if (!env->env->ApplyTagTo(agent, tag, duration)) {
      SetError("companions_apply_tag: unknown, downed or dead agent, an agent immune to the tag, "
               "empty or overlong tag, or duration 0, below -1 or above 1000000");
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

COMPANIONS_API bool companions_remove_tag(Companions_Env* env, Companions_ObjectId agent,
                                          const char* tag) {
  try {
    if (!env || !env->env || !tag) {
      SetError("Invalid arguments");
      return false;
    }
    if (!env->env->RemoveTagFrom(agent, tag)) {
      SetError("companions_remove_tag: unknown agent");
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

COMPANIONS_API bool companions_set_cell_tag(Companions_Env* env, int32_t row, int32_t col,
                                            const char* tag, int32_t duration) {
  try {
    if (!env || !env->env) {
      SetError("Invalid environment");
      return false;
    }
    if (!env->env->SetCellTag(companions::Position{row, col}, tag ? tag : "", duration)) {
      SetError("companions_set_cell_tag: out of bounds, overlong tag, or duration 0, below -1 or "
               "above 1000000");
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

COMPANIONS_API int32_t companions_get_cell_tag(const Companions_Env* env, int32_t row,
                                               int32_t col) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return -1;
  }
  if (row < 0 || row >= env->env->GetRows() || col < 0 || col >= env->env->GetCols()) {
    SetError("Position out of bounds");
    return -1;
  }
  return env->env->GetCellTag(companions::Position{row, col}).tag;
}

COMPANIONS_API bool companions_set_agent_skill(Companions_Env* env, Companions_ObjectId agent,
                                               int32_t slot, const char* skill) {
  try {
    if (!env || !env->env) {
      SetError("Invalid arguments");
      return false;
    }
    // NULL, like "", puts the default skill back.
    if (!env->env->SetCompanionSkill(agent, slot, skill ? skill : "")) {
      SetError("companions_set_agent_skill: unknown skill, slot or companion");
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

// Companions_SkillInfo of a book entry. Names and tags fit (the env refuses
// longer names and more tags than the buffers hold).
static void ToAPISkillInfo(const companions::SkillConfig& s, Companions_SkillInfo* out) {
  // Zeroed first, padding included: two copies of a skill compare equal.
  std::memset(out, 0, sizeof(*out));
  Companions_SkillInfo& info = *out;
  CopyName(info.name, s.name);
  info.targeting = static_cast<Companions_SkillTargeting>(s.targeting);
  info.range = s.range;
  info.filter = static_cast<Companions_TargetFilter>(s.filter);
  info.area = static_cast<Companions_SkillArea>(s.area);
  info.motion = static_cast<Companions_SkillMotion>(s.motion);
  info.motion_distance = s.motion_distance;
  info.tag_path = s.tag_path;
  const size_t tag_count =
      std::min(s.tags.size(), static_cast<size_t>(Companions_MAX_SKILL_TAGS));
  for (size_t i = 0; i < tag_count; ++i) {
    CopyName(info.tags[i].tag, s.tags[i].tag);
    info.tags[i].duration = s.tags[i].duration;
  }
  info.tag_count = static_cast<int32_t>(tag_count);
  info.damage = s.damage;
  info.root_steps = s.root_steps;
  info.cooldown = s.cooldown;
  info.friendly_fire = s.friendly_fire;
  info.self_tags = s.self_tags;
  info.self_motion = s.self_motion;
  info.self_root = s.self_root;
  info.self_damage = s.self_damage;
  info.affects_downed = s.affects_downed;
  info.revive_percent = s.revive_percent;
}

COMPANIONS_API int32_t companions_get_skill_count(const Companions_Env* env) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return 0;
  }
  return static_cast<int32_t>(env->env->GetSkillBook().All().size());
}

COMPANIONS_API bool companions_get_skill(const Companions_Env* env, int32_t index,
                                         Companions_SkillInfo* out) {
  if (!env || !env->env || !out) {
    SetError("Invalid arguments");
    return false;
  }
  const std::vector<companions::SkillConfig>& all = env->env->GetSkillBook().All();
  if (index < 0 || static_cast<size_t>(index) >= all.size()) {
    SetError("Skill index out of range");
    return false;
  }
  ToAPISkillInfo(all[static_cast<size_t>(index)], out);
  return true;
}

COMPANIONS_API bool companions_find_skill(const Companions_Env* env, const char* name,
                                          Companions_SkillInfo* out) {
  if (!env || !env->env || !name || !out) {
    SetError("Invalid arguments");
    return false;
  }
  const companions::SkillConfig* skill = env->env->GetSkillBook().Find(name);
  if (!skill) {
    SetError((std::string("Unknown skill: ") + name).c_str());
    return false;
  }
  ToAPISkillInfo(*skill, out);
  return true;
}

static_assert(companions::BaseEnv::kSkillEffectTags == Companions_SkillEffect_Tags &&
                  companions::BaseEnv::kSkillEffectDamage == Companions_SkillEffect_Damage &&
                  companions::BaseEnv::kSkillEffectRoot == Companions_SkillEffect_Root &&
                  companions::BaseEnv::kSkillEffectMotion == Companions_SkillEffect_Motion &&
                  companions::BaseEnv::kSkillEffectRevive == Companions_SkillEffect_Revive,
              "SkillEffect out of sync with C API");

// Up to Companions_MAX_AGENTS affected agents into fixed arrays (ids,
// effects) of a Companions_SkillPreview or Companions_SkillUseInfo, with the
// count written and the total.
static void CopyAffected(const std::vector<companions::BaseEnv::AffectedAgent>& affected,
                         Companions_ObjectId* ids, uint32_t* effects, int32_t* count,
                         int32_t* total) {
  const size_t n = std::min(affected.size(), static_cast<size_t>(Companions_MAX_AGENTS));
  for (size_t i = 0; i < n; ++i) {
    ids[i] = affected[i].id;
    effects[i] = affected[i].effects;
  }
  *count = static_cast<int32_t>(n);
  *total = static_cast<int32_t>(affected.size());
}

COMPANIONS_API bool companions_preview_skill(const Companions_Env* env, Companions_ObjectId agent,
                                             int32_t slot, Companions_Direction aim,
                                             Companions_SkillPreview* out) {
  try {
    if (!env || !env->env || !out) {
      SetError("Invalid arguments");
      return false;
    }
    const companions::Companion* comp = PreviewedCompanion(env, agent);
    if (!comp) return false;
    if (slot < 0 || slot >= Companions_MAX_SKILL_SLOTS) {
      SetError("Skill slot out of range");
      return false;
    }
    companions::Direction dir = companions::Direction::Up;
    if (!ToDirection(aim, &dir)) return false;
    const companions::BaseEnv::SkillPreview p = env->env->PreviewSkill(*comp, slot, dir);
    // Zeroed first, padding included, like Companions_SkillInfo
    std::memset(out, 0, sizeof(*out));
    out->usable = p.usable;
    CopyName(out->skill, p.skill);
    out->centre = ToAPIPosition(p.centre);
    out->caster_landing = ToAPIPosition(p.caster_landing);
    CopyAffected(p.affected, out->affected, out->affected_effects, &out->affected_count,
                 &out->affected_total);
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

COMPANIONS_API int32_t companions_get_last_skill_use_count(const Companions_Env* env) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return 0;
  }
  return static_cast<int32_t>(env->env->GetLastSkillUses().size());
}

COMPANIONS_API bool companions_get_last_skill_use(const Companions_Env* env, int32_t index,
                                                  Companions_SkillUseInfo* out) {
  if (!env || !env->env || !out) {
    SetError("Invalid arguments");
    return false;
  }
  const auto& uses = env->env->GetLastSkillUses();
  if (index < 0 || static_cast<size_t>(index) >= uses.size()) {
    SetError("Skill use index out of range");
    return false;
  }
  const companions::BaseEnv::SkillUse& use = uses[static_cast<size_t>(index)];
  std::memset(out, 0, sizeof(*out));
  out->caster = use.caster;
  CopyName(out->skill, use.skill);
  out->slot = use.slot;
  out->centre = ToAPIPosition(use.target);
  CopyAffected(use.affected, out->affected, out->affected_effects, &out->affected_count,
               &out->affected_total);
  return true;
}

// =============================================================================
// Reports (since 1.5)
// =============================================================================

}  // extern "C": the report helpers below are C++

static_assert(static_cast<int>(companions::BaseEnv::TagSource::Skill) == Companions_TagSource_Skill &&
                  static_cast<int>(companions::BaseEnv::TagSource::Zone) ==
                      Companions_TagSource_Zone &&
                  static_cast<int>(companions::BaseEnv::TagSource::Reaction) ==
                      Companions_TagSource_Reaction &&
                  static_cast<int>(companions::BaseEnv::TagSource::Host) ==
                      Companions_TagSource_Host,
              "TagSource out of sync with C API");

// The env holding `source`'s reports: the env itself (LastStep), the last
// outcome preview's world (Preview; null before any: empty reports). False,
// error set, for a null env or a source out of range.
static bool ReportsOf(const Companions_Env* env, Companions_ReportSource source,
                      const companions::BaseEnv** out) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return false;
  }
  switch (source) {
    case Companions_Report_LastStep:
      *out = env->env.get();
      return true;
    case Companions_Report_Preview:
      *out = env->preview.get();
      return true;
  }
  SetError("Invalid report source");
  return false;
}

// The entry `index` of `source`'s report `get` (a BaseEnv getter), or null
// with the error set ("Invalid arguments", "Invalid report source", `range`).
template <typename Getter>
static auto ReportEntry(const Companions_Env* env, Companions_ReportSource source, int32_t index,
                        const void* out, Getter get, const char* range,
                        const companions::BaseEnv** reports)
    -> decltype(&get(*env->env)[0]) {
  if (!env || !env->env || !out) {
    SetError("Invalid arguments");
    return nullptr;
  }
  if (!ReportsOf(env, source, reports)) return nullptr;
  if (!*reports || index < 0 || static_cast<size_t>(index) >= get(**reports).size()) {
    SetError(range);
    return nullptr;
  }
  return &get(**reports)[static_cast<size_t>(index)];
}

// The size of `source`'s report `get`, 0 with the error set for a bad env or source
template <typename Getter>
static int32_t ReportCount(const Companions_Env* env, Companions_ReportSource source, Getter get) {
  const companions::BaseEnv* reports = nullptr;
  if (!ReportsOf(env, source, &reports)) return 0;
  return reports ? static_cast<int32_t>(get(*reports).size()) : 0;
}

static const std::vector<companions::BaseEnv::SkillUse>& SkillUsesOf(const companions::BaseEnv& e) {
  return e.GetLastSkillUses();
}
static const std::vector<companions::BaseEnv::TagApplication>& LandingsOf(
    const companions::BaseEnv& e) {
  return e.GetLastTagsApplied();
}
static const std::vector<companions::BaseEnv::ReactionReport>& ReactionsOf(
    const companions::BaseEnv& e) {
  return e.GetLastReactions();
}
static const std::vector<companions::BaseEnv::DefeatReport>& DefeatsOf(
    const companions::BaseEnv& e) {
  return e.GetLastDefeats();
}
static const std::vector<companions::ObjectId>& DownsOf(const companions::BaseEnv& e) {
  return e.GetLastDowns();
}
static const std::vector<companions::BaseEnv::Revival>& RevivesOf(const companions::BaseEnv& e) {
  return e.GetLastRevives();
}

// A tag id of `env`'s table by name ("" for none)
static std::string TagNameIn(const companions::BaseEnv& env, companions::TagId tag) {
  const companions::TagTable& table = env.GetTagTable();
  return tag >= 0 && tag < table.Size() ? table.Name(tag) : std::string();
}

static void ToAPISkillUse(const companions::BaseEnv::SkillUse& use, Companions_SkillUseInfo* out) {
  std::memset(out, 0, sizeof(*out));
  out->caster = use.caster;
  CopyName(out->skill, use.skill);
  out->slot = use.slot;
  out->centre = ToAPIPosition(use.target);
  CopyAffected(use.affected, out->affected, out->affected_effects, &out->affected_count,
               &out->affected_total);
}

extern "C" {

COMPANIONS_API int32_t companions_get_skill_use_count(const Companions_Env* env,
                                                      Companions_ReportSource source) {
  return ReportCount(env, source, SkillUsesOf);
}

COMPANIONS_API bool companions_get_skill_use(const Companions_Env* env,
                                             Companions_ReportSource source, int32_t index,
                                             Companions_SkillUseInfo* out) {
  const companions::BaseEnv* reports = nullptr;
  const auto* use =
      ReportEntry(env, source, index, out, SkillUsesOf, "Skill use index out of range", &reports);
  if (!use) return false;
  ToAPISkillUse(*use, out);
  return true;
}

COMPANIONS_API int32_t companions_get_tag_landing_count(const Companions_Env* env,
                                                        Companions_ReportSource source) {
  return ReportCount(env, source, LandingsOf);
}

COMPANIONS_API bool companions_get_tag_landing(const Companions_Env* env,
                                               Companions_ReportSource source, int32_t index,
                                               Companions_TagLanding* out) {
  const companions::BaseEnv* reports = nullptr;
  const auto* landed =
      ReportEntry(env, source, index, out, LandingsOf, "Tag landing index out of range", &reports);
  if (!landed) return false;
  std::memset(out, 0, sizeof(*out));
  out->agent = landed->agent;
  CopyName(out->tag, TagNameIn(*reports, landed->tag));
  out->duration = landed->duration;
  out->source = landed->source;
  CopyName(out->cause, landed->cause);
  out->kind = static_cast<Companions_TagSource>(landed->kind);
  out->reaction = landed->reaction;
  out->fresh = landed->fresh;
  out->damage = landed->damage;
  return true;
}

COMPANIONS_API int32_t companions_get_reaction_count(const Companions_Env* env,
                                                     Companions_ReportSource source) {
  return ReportCount(env, source, ReactionsOf);
}

COMPANIONS_API bool companions_get_reaction(const Companions_Env* env,
                                            Companions_ReportSource source, int32_t index,
                                            Companions_ReactionInfo* out) {
  const companions::BaseEnv* reports = nullptr;
  const auto* fired =
      ReportEntry(env, source, index, out, ReactionsOf, "Reaction index out of range", &reports);
  if (!fired) return false;
  std::memset(out, 0, sizeof(*out));
  out->rule = fired->rule;
  // The rule's tags (the reports' env holds the rules they index)
  const auto& rules = reports->GetReactions();
  if (fired->rule >= 0 && static_cast<size_t>(fired->rule) < rules.size()) {
    const companions::ReactionRule& rule = rules[static_cast<size_t>(fired->rule)];
    CopyName(out->a, rule.a);
    CopyName(out->b, rule.b);
    CopyName(out->result, rule.result);
    if (!fired->cells.empty()) CopyName(out->zone_becomes, rule.zone_becomes);
  }
  out->trigger = fired->trigger;
  CopyName(out->tag, TagNameIn(*reports, fired->tag));
  out->source = fired->source;
  CopyName(out->cause, fired->cause);
  out->kind = static_cast<Companions_TagSource>(fired->kind);
  out->spread = fired->spread;
  const size_t n = std::min(fired->affected.size(), static_cast<size_t>(Companions_MAX_AGENTS));
  for (size_t i = 0; i < n; ++i) {
    const companions::BaseEnv::ReactionOutcome& o = fired->affected[i];
    out->affected[i] = o.agent;
    out->affected_result_landed[i] = o.result_landed;
    out->affected_defeated[i] = o.defeated;
    out->affected_damage[i] = o.damage;
  }
  out->affected_count = static_cast<int32_t>(n);
  out->affected_total = static_cast<int32_t>(fired->affected.size());
  out->cell_count = static_cast<int32_t>(fired->cells.size());
  return true;
}

COMPANIONS_API bool companions_get_reaction_cell(const Companions_Env* env,
                                                 Companions_ReportSource source,
                                                 int32_t reaction_index, int32_t cell_index,
                                                 Companions_Position* out) {
  const companions::BaseEnv* reports = nullptr;
  const auto* fired = ReportEntry(env, source, reaction_index, out, ReactionsOf,
                                  "Reaction index out of range", &reports);
  if (!fired) return false;
  if (cell_index < 0 || static_cast<size_t>(cell_index) >= fired->cells.size()) {
    SetError("Reaction cell index out of range");
    return false;
  }
  *out = ToAPIPosition(fired->cells[static_cast<size_t>(cell_index)]);
  return true;
}

COMPANIONS_API int32_t companions_get_defeat_count(const Companions_Env* env,
                                                   Companions_ReportSource source) {
  return ReportCount(env, source, DefeatsOf);
}

COMPANIONS_API bool companions_get_defeat(const Companions_Env* env,
                                          Companions_ReportSource source, int32_t index,
                                          Companions_DefeatInfo* out) {
  const companions::BaseEnv* reports = nullptr;
  const auto* defeat =
      ReportEntry(env, source, index, out, DefeatsOf, "Defeat index out of range", &reports);
  if (!defeat) return false;
  std::memset(out, 0, sizeof(*out));
  out->agent = defeat->agent;
  CopyName(out->zone, TagNameIn(*reports, defeat->zone));
  CopyName(out->tag, TagNameIn(*reports, defeat->tag));
  out->source = defeat->source;
  CopyName(out->cause, defeat->cause);
  out->kind = static_cast<Companions_TagSource>(defeat->kind);
  out->reaction = defeat->reaction;
  return true;
}

COMPANIONS_API int32_t companions_get_down_count(const Companions_Env* env,
                                                 Companions_ReportSource source) {
  return ReportCount(env, source, DownsOf);
}

COMPANIONS_API bool companions_get_down(const Companions_Env* env,
                                        Companions_ReportSource source, int32_t index,
                                        Companions_ObjectId* out) {
  const companions::BaseEnv* reports = nullptr;
  const auto* down = ReportEntry(env, source, index, out, DownsOf, "Down index out of range",
                                 &reports);
  if (!down) return false;
  *out = *down;
  return true;
}

COMPANIONS_API int32_t companions_get_revive_count(const Companions_Env* env,
                                                   Companions_ReportSource source) {
  return ReportCount(env, source, RevivesOf);
}

COMPANIONS_API bool companions_get_revive(const Companions_Env* env,
                                          Companions_ReportSource source, int32_t index,
                                          Companions_ReviveInfo* out) {
  const companions::BaseEnv* reports = nullptr;
  const auto* revival =
      ReportEntry(env, source, index, out, RevivesOf, "Revive index out of range", &reports);
  if (!revival) return false;
  std::memset(out, 0, sizeof(*out));
  out->reviver = revival->reviver;
  out->revived = revival->revived;
  out->health = revival->health;
  return true;
}

COMPANIONS_API bool companions_preview_skill_outcome(const Companions_Env* env,
                                                     Companions_ObjectId agent, int32_t slot,
                                                     Companions_Direction aim,
                                                     Companions_SkillOutcome* out) {
  if (!env || !env->env || !out) {
    SetError("Invalid arguments");
    return false;
  }
  if (!PreviewedCompanion(env, agent)) return false;
  if (slot < 0 || slot >= Companions_MAX_SKILL_SLOTS) {
    SetError("Skill slot out of range");
    return false;
  }
  companions::Direction dir = companions::Direction::Up;
  if (!ToDirection(aim, &dir)) return false;
  try {
    companions::BaseEnv::SkillOutcome outcome = env->env->PreviewSkillOutcome(agent, slot, dir);
    if (!outcome.world) {
      SetError("Not a companion");
      return false;
    }
    env->preview = std::move(outcome.world);
    const companions::BaseEnv& world = *env->preview;
    std::memset(out, 0, sizeof(*out));
    out->usable = outcome.usable;
    out->skill_use_count = static_cast<int32_t>(world.GetLastSkillUses().size());
    out->tag_landing_count = static_cast<int32_t>(world.GetLastTagsApplied().size());
    out->reaction_count = static_cast<int32_t>(world.GetLastReactions().size());
    out->defeat_count = static_cast<int32_t>(world.GetLastDefeats().size());
    out->down_count = static_cast<int32_t>(world.GetLastDowns().size());
    out->revive_count = static_cast<int32_t>(world.GetLastRevives().size());
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

// =============================================================================
// Level data (since 1.5)
// =============================================================================

static void ToAPIZoneDef(const std::string& tag, const companions::ZoneDef& def,
                         Companions_ZoneDefInfo* out) {
  std::memset(out, 0, sizeof(*out));
  CopyName(out->tag, tag);
  out->duration = def.duration;
  out->steps = def.steps;
  CopyName(out->then, def.then);
  out->damage = def.damage;
}

COMPANIONS_API int32_t companions_get_zone_def_count(const Companions_Env* env) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return 0;
  }
  return static_cast<int32_t>(env->env->GetZoneDefs().size());
}

COMPANIONS_API bool companions_get_zone_def(const Companions_Env* env, int32_t index,
                                            Companions_ZoneDefInfo* out) {
  if (!env || !env->env || !out) {
    SetError("Invalid arguments");
    return false;
  }
  const auto& table = env->env->GetZoneDefs();
  if (index < 0 || static_cast<size_t>(index) >= table.size()) {
    SetError("Zone index out of range");
    return false;
  }
  auto it = table.begin();
  std::advance(it, index);
  ToAPIZoneDef(it->first, it->second, out);
  return true;
}

COMPANIONS_API bool companions_find_zone_def(const Companions_Env* env, const char* tag,
                                             Companions_ZoneDefInfo* out) {
  if (!env || !env->env || !tag || !out) {
    SetError("Invalid arguments");
    return false;
  }
  const auto& table = env->env->GetZoneDefs();
  auto it = table.find(tag);
  if (it == table.end()) {
    SetError((std::string("Unknown zone: ") + tag).c_str());
    return false;
  }
  ToAPIZoneDef(it->first, it->second, out);
  return true;
}

COMPANIONS_API bool companions_get_cell_zone(const Companions_Env* env, int32_t row, int32_t col,
                                             Companions_CellZone* out) {
  if (!env || !env->env || !out) {
    SetError("Invalid arguments");
    return false;
  }
  if (row < 0 || row >= env->env->GetRows() || col < 0 || col >= env->env->GetCols()) {
    SetError("Position out of bounds");
    return false;
  }
  const companions::BaseEnv::CellTag zone = env->env->GetCellTag(companions::Position{row, col});
  std::memset(out, 0, sizeof(*out));
  if (zone.tag == companions::kInvalidTag) return true;
  out->has_zone = true;
  CopyName(out->tag, TagNameIn(*env->env, zone.tag));
  out->duration = zone.duration;
  out->steps = zone.steps;
  CopyName(out->then, TagNameIn(*env->env, zone.then));
  out->damage = zone.damage;
  return true;
}

COMPANIONS_API int32_t companions_get_reaction_rule_count(const Companions_Env* env) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return 0;
  }
  return static_cast<int32_t>(env->env->GetReactions().size());
}

COMPANIONS_API bool companions_get_reaction_rule(const Companions_Env* env, int32_t index,
                                                 Companions_ReactionRuleInfo* out) {
  if (!env || !env->env || !out) {
    SetError("Invalid arguments");
    return false;
  }
  const auto& rules = env->env->GetReactions();
  if (index < 0 || static_cast<size_t>(index) >= rules.size()) {
    SetError("Reaction rule index out of range");
    return false;
  }
  const companions::ReactionRule& rule = rules[static_cast<size_t>(index)];
  std::memset(out, 0, sizeof(*out));
  CopyName(out->a, rule.a);
  CopyName(out->b, rule.b);
  CopyName(out->result, rule.result);
  for (const std::string& k : rule.keep) {
    if (k == rule.a) out->keep_a = true;
    if (k == rule.b) out->keep_b = true;
  }
  out->damage = rule.damage;
  out->spread = rule.spread;
  CopyName(out->zone_becomes, rule.zone_becomes);
  return true;
}

COMPANIONS_API int32_t companions_get_tag_status_count(const Companions_Env* env) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return 0;
  }
  return static_cast<int32_t>(env->env->GetTagStatuses().size());
}

COMPANIONS_API bool companions_get_tag_status(const Companions_Env* env, int32_t index,
                                              Companions_TagStatusInfo* out) {
  if (!env || !env->env || !out) {
    SetError("Invalid arguments");
    return false;
  }
  const auto& rules = env->env->GetTagStatuses();
  if (index < 0 || static_cast<size_t>(index) >= rules.size()) {
    SetError("Tag status index out of range");
    return false;
  }
  const companions::TagStatusRule& rule = rules[static_cast<size_t>(index)];
  std::memset(out, 0, sizeof(*out));
  CopyName(out->tag, rule.tag);
  out->status = ToAPIStatusType(rule.status);
  out->steps = rule.steps;
  return true;
}

// The agent `id` of `env`, or null ("Agent not found")
static const companions::Agent* LevelAgent(const Companions_Env* env, Companions_ObjectId id) {
  const auto* agent =
      dynamic_cast<const companions::Agent*>(env->env->GetObjectManager().GetActor(id));
  if (!agent) SetError("Agent not found");
  return agent;
}

COMPANIONS_API int32_t companions_get_agent_weakness_count(const Companions_Env* env,
                                                           Companions_ObjectId agent) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return 0;
  }
  const companions::Agent* a = LevelAgent(env, agent);
  return a ? static_cast<int32_t>(a->GetWeakTo().size()) : 0;
}

COMPANIONS_API bool companions_get_agent_weakness(const Companions_Env* env,
                                                  Companions_ObjectId agent, int32_t index,
                                                  Companions_Weakness* out) {
  if (!env || !env->env || !out) {
    SetError("Invalid arguments");
    return false;
  }
  const companions::Agent* a = LevelAgent(env, agent);
  if (!a) return false;
  const auto& weak_to = a->GetWeakTo();
  if (index < 0 || static_cast<size_t>(index) >= weak_to.size()) {
    SetError("Weakness index out of range");
    return false;
  }
  const companions::Agent::WeakTo& w = weak_to[static_cast<size_t>(index)];
  std::memset(out, 0, sizeof(*out));
  CopyName(out->zone, TagNameIn(*env->env, w.zone));
  CopyName(out->tag, TagNameIn(*env->env, w.tag));
  return true;
}

COMPANIONS_API int32_t companions_get_agent_immunity_count(const Companions_Env* env,
                                                           Companions_ObjectId agent) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return 0;
  }
  const companions::Agent* a = LevelAgent(env, agent);
  return a ? static_cast<int32_t>(a->GetImmune().size()) : 0;
}

COMPANIONS_API bool companions_get_agent_immunity(const Companions_Env* env,
                                                  Companions_ObjectId agent, int32_t index,
                                                  char* out_tag) {
  if (!env || !env->env || !out_tag) {
    SetError("Invalid arguments");
    return false;
  }
  const companions::Agent* a = LevelAgent(env, agent);
  if (!a) return false;
  const auto& immune = a->GetImmune();
  if (index < 0 || static_cast<size_t>(index) >= immune.size()) {
    SetError("Immunity index out of range");
    return false;
  }
  const std::string name = TagNameIn(*env->env, immune[static_cast<size_t>(index)]);
  std::strncpy(out_tag, name.c_str(), Companions_SKILL_NAME_LEN - 1);
  out_tag[Companions_SKILL_NAME_LEN - 1] = '\0';
  return true;
}

COMPANIONS_API int32_t companions_get_rows(const Companions_Env* env) {
  return env && env->env ? env->env->GetRows() : 0;
}

COMPANIONS_API int32_t companions_get_cols(const Companions_Env* env) {
  return env && env->env ? env->env->GetCols() : 0;
}

COMPANIONS_API int32_t companions_get_agent_count(
    const Companions_Env* env) {
  return env && env->env ? env->env->NumAgents() : 0;
}

COMPANIONS_API int32_t companions_get_tick(const Companions_Env* env) {
  return env && env->env ? env->env->GetTick() : 0;
}

COMPANIONS_API bool companions_is_done(const Companions_Env* env) {
  return env ? env->done : true;
}

COMPANIONS_API Companions_EndReason companions_get_end_reason(const Companions_Env* env) {
  return env ? env->end_reason : Companions_End_None;
}

COMPANIONS_API bool companions_is_success(const Companions_Env* env) {
  return env ? env->success : false;
}

COMPANIONS_API int32_t companions_render_ascii(
    const Companions_Env* env,
    char* out_buffer,
    int32_t buffer_size) {
  try {
    if (!env || !env->env) {
      SetError("Invalid environment");
      return 0;
    }

    // Use the renderer to generate ASCII output
    companions::Renderer renderer;
    std::string ascii = renderer.RenderAscii(*env->env);

    int32_t required_size = static_cast<int32_t>(ascii.size() + 1);  // +1 for null terminator

    // If out_buffer is NULL, just return required size
    if (!out_buffer) {
      return required_size;
    }

    // Check buffer size
    if (buffer_size < required_size) {
      SetError("Buffer too small");
      return required_size;
    }

    // Copy to output buffer
    std::memcpy(out_buffer, ascii.c_str(), ascii.size());
    out_buffer[ascii.size()] = '\0';

    return required_size;
  } catch (const std::exception& e) {
    SetError(e.what());
    return 0;
  } catch (...) {
    SetError("Unknown error");
    return 0;
  }
}

// =============================================================================
// Semantic Annotations (task-role tags on cells / agents)
// =============================================================================

namespace {

// Render an annotation's params map as compact JSON into `out`. Writes a
// "{...}" string fitting in `out_size` bytes. If any key/value can't fit,
// output is truncated and null-terminated.
void ParamsToCompactJson(const std::unordered_map<std::string, std::string>& params,
                         char* out, std::size_t out_size) {
  if (out_size == 0) return;
  std::string buf;
  buf.reserve(out_size);
  buf.push_back('{');
  bool first = true;
  for (const auto& kv : params) {
    if (!first) buf.push_back(',');
    first = false;
    buf.push_back('"');
    buf.append(kv.first);
    buf.append("\":\"");
    buf.append(kv.second);
    buf.push_back('"');
    if (buf.size() + 1 >= out_size) break;  // leave room for closing brace
  }
  buf.push_back('}');
  std::size_t copy_len = std::min(buf.size(), out_size - 1);
  std::memcpy(out, buf.data(), copy_len);
  out[copy_len] = '\0';
}

}  // namespace

COMPANIONS_API int32_t
companions_get_annotation_count(const Companions_Env* env) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return 0;
  }
  return static_cast<int32_t>(env->env->GetAnnotations().Size());
}

COMPANIONS_API int32_t companions_get_annotations(
    const Companions_Env* env, Companions_Annotation* out, int32_t count) {
  try {
    if (!env || !env->env || !out) {
      SetError("Invalid environment or output buffer");
      return 0;
    }
    const auto& store = env->env->GetAnnotations();
    auto serialized = store.Serialize();
    int32_t n = std::min(count, static_cast<int32_t>(serialized.size()));
    for (int32_t i = 0; i < n; ++i) {
      const auto& a = serialized[i];
      out[i].target_kind = a.target_type;
      out[i].pos.row = a.pos.row;
      out[i].pos.col = a.pos.col;
      out[i].agent_id = a.agent_id;
      out[i].tag = static_cast<int32_t>(a.tag);
      out[i].owner_lens_id = a.owner_lens_id;
      std::unordered_map<std::string, std::string> params_map;
      for (const auto& kv : a.params) params_map.emplace(kv.first, kv.second);
      ParamsToCompactJson(params_map, out[i].params_json,
                          COMPANIONS_MAX_ANNOTATION_PARAMS);
    }
    return n;
  } catch (const std::exception& e) {
    SetError(e.what());
    return 0;
  } catch (...) {
    SetError("Unknown error");
    return 0;
  }
}

COMPANIONS_API bool companions_has_tag_at(
    const Companions_Env* env, int32_t row, int32_t col, int32_t tag) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return false;
  }
  return env->env->GetAnnotations().HasTag(
      companions::AnnotationKey{companions::AnnotationTarget::Cell,
                                companions::Position{row, col},
                                companions::kInvalidObjectId},
      static_cast<companions::SemanticTag>(tag));
}

COMPANIONS_API bool companions_agent_has_tag(
    const Companions_Env* env, Companions_ObjectId agent_id, int32_t tag) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return false;
  }
  return env->env->GetAnnotations().HasTag(
      companions::AnnotationKey{companions::AnnotationTarget::Agent,
                                companions::Position{-1, -1}, agent_id},
      static_cast<companions::SemanticTag>(tag));
}

COMPANIONS_API const char* companions_version(void) {
  return COMPANIONS_VERSION;
}

COMPANIONS_API const char* companions_get_error(void) {
  return g_error_buffer;
}

// =============================================================================
// Snapshot Save/Load
// =============================================================================

COMPANIONS_API int32_t
companions_get_snapshot_size(const Companions_Env* env) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return 0;
  }

  try {
    // Save and cache snapshot
    companions::Snapshot snap = env->env->SaveSnapshot();
    env->cached_snapshot = snap.Serialize();
    return static_cast<int32_t>(env->cached_snapshot.size());
  } catch (const std::exception& e) {
    SetError(e.what());
    return 0;
  } catch (...) {
    SetError("Unknown error");
    return 0;
  }
}

COMPANIONS_API bool companions_save_snapshot(const Companions_Env* env,
                                                   uint8_t* out_buffer,
                                                   int32_t buffer_size) {
  if (!env || !out_buffer) {
    SetError("Invalid arguments");
    return false;
  }

  if (env->cached_snapshot.empty()) {
    SetError("No cached snapshot - call get_snapshot_size first");
    return false;
  }

  if (buffer_size < static_cast<int32_t>(env->cached_snapshot.size())) {
    SetError("Buffer too small");
    return false;
  }

  std::memcpy(out_buffer, env->cached_snapshot.data(),
              env->cached_snapshot.size());
  return true;
}

COMPANIONS_API bool companions_load_snapshot(Companions_Env* env,
                                                   const uint8_t* data,
                                                   int32_t data_size) {
  if (!env || !env->env || !data || data_size <= 0) {
    SetError("Invalid arguments");
    return false;
  }

  // Minimum buffer size validation to avoid exceptions crossing DLL boundary
  constexpr int32_t kMinSnapshotSize = 60;
  if (data_size < kMinSnapshotSize) {
    SetError("Snapshot buffer too small");
    return false;
  }

  // Validate magic number before attempting full deserialization
  uint32_t magic;
  std::memcpy(&magic, data, sizeof(magic));
  if (magic != 0x534E4150) {  // "SNAP"
    SetError("Invalid snapshot magic number");
    return false;
  }

  try {
    std::vector<uint8_t> buffer(data, data + data_size);
    companions::Snapshot snap = companions::Snapshot::Deserialize(buffer);
    env->env->LoadSnapshot(snap);
    env->preview.reset();

    // Update wrapper state
    StartEpisode(*env);

    // Reset prev_positions for event tracking
    auto agents = env->env->GetObjectManager().GetAllAgents();
    env->prev_positions.clear();
    for (const auto* agent : agents) {
      env->prev_positions.push_back(agent->GetPosition());
    }

    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

// =============================================================================
// Level Generation
// =============================================================================

COMPANIONS_API int32_t companions_generate_level(
    const Companions_LevelConfig* config) {
  if (!config) {
    SetError("Config is null");
    return 0;
  }

  if (config->rows <= 0 || config->cols <= 0) {
    SetError("Invalid grid dimensions");
    return 0;
  }

  try {
    // Convert Companions_LevelConfig to companions::LevelConfig
    companions::LevelConfig level_config;

    // Base map
    level_config.map = companions::MapGenerator::DefaultConfig(
        config->rows, config->cols, config->map_complexity, config->seed);

    // Level features
    level_config.synchro_cell_count = config->synchro_cell_count;
    level_config.patrol_square_size = config->patrol_square_size;
    level_config.has_target_cell = config->has_target_cell;

    // Agents
    level_config.num_companions = config->num_companions;
    level_config.num_enemies = config->num_enemies;

    // Episode
    level_config.horizon = config->horizon > 0 ? config->horizon : 100;
    level_config.d4_transform = config->d4_transform;

    // Generate level
    companions::Snapshot snapshot = companions::LevelGenerator::Generate(level_config);

    // Serialize and cache
    g_generated_level = snapshot.Serialize();

    return static_cast<int32_t>(g_generated_level.size());
  } catch (const std::exception& e) {
    SetError(e.what());
    return 0;
  } catch (...) {
    SetError("Unknown error");
    return 0;
  }
}

COMPANIONS_API bool companions_get_generated_level(uint8_t* out_buffer,
                                                          int32_t buffer_size) {
  if (!out_buffer) {
    SetError("Output buffer is null");
    return false;
  }

  if (g_generated_level.empty()) {
    SetError("No generated level - call generate_level first");
    return false;
  }

  if (buffer_size < static_cast<int32_t>(g_generated_level.size())) {
    SetError("Buffer too small");
    return false;
  }

  std::memcpy(out_buffer, g_generated_level.data(), g_generated_level.size());
  return true;
}

// =============================================================================
// JSON Snapshot Save/Load
// =============================================================================

COMPANIONS_API const char* companions_snapshot_to_json(
    const Companions_Env* env) {
  if (!env || !env->env) {
    SetError("Invalid environment");
    return nullptr;
  }

  try {
    companions::Snapshot snap = env->env->SaveSnapshot();
    std::string json = companions::SnapshotToJson(snap);

    // Allocate new string (caller must free with companions_free_string)
    char* result = new char[json.size() + 1];
    std::memcpy(result, json.c_str(), json.size() + 1);
    return result;
  } catch (const std::exception& e) {
    SetError(e.what());
    return nullptr;
  } catch (...) {
    SetError("Unknown error");
    return nullptr;
  }
}

COMPANIONS_API void companions_free_string(const char* str) {
  delete[] str;
}

COMPANIONS_API bool companions_load_snapshot_json(
    Companions_Env* env,
    const char* json_str) {
  if (!env || !env->env || !json_str) {
    SetError("Invalid arguments");
    return false;
  }

  try {
    companions::Snapshot snap = companions::SnapshotFromJson(json_str);
    env->env->LoadSnapshot(snap);
    env->preview.reset();

    // Update wrapper state
    StartEpisode(*env);

    // Reset prev_positions for event tracking
    auto agents = env->env->GetObjectManager().GetAllAgents();
    env->prev_positions.clear();
    for (const auto* agent : agents) {
      env->prev_positions.push_back(agent->GetPosition());
    }

    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

COMPANIONS_API bool companions_save_snapshot_json(
    const Companions_Env* env,
    const char* filepath) {
  if (!env || !env->env || !filepath) {
    SetError("Invalid arguments");
    return false;
  }

  try {
    companions::Snapshot snap = env->env->SaveSnapshot();
    return companions::SaveSnapshotToJsonFile(snap, filepath);
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

COMPANIONS_API bool companions_load_snapshot_json_file(
    Companions_Env* env,
    const char* filepath) {
  if (!env || !env->env || !filepath) {
    SetError("Invalid arguments");
    return false;
  }

  try {
    companions::Snapshot snap = companions::LoadSnapshotFromJsonFile(filepath);
    env->env->LoadSnapshot(snap);
    env->preview.reset();

    // Update wrapper state
    StartEpisode(*env);

    // Reset prev_positions for event tracking
    auto agents = env->env->GetObjectManager().GetAllAgents();
    env->prev_positions.clear();
    for (const auto* agent : agents) {
      env->prev_positions.push_back(agent->GetPosition());
    }

    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    return false;
  } catch (...) {
    SetError("Unknown error");
    return false;
  }
}

}  // extern "C"
