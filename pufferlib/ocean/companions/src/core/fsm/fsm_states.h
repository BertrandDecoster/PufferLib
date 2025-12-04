// Copyright 2024
// FSM States for agent AI: Patrol, Aggro, ReturnToPatrol

#ifndef COMPANIONS_CORE_FSM_FSM_STATES_H_
#define COMPANIONS_CORE_FSM_FSM_STATES_H_

#include "fsm_state.h"

namespace companions {

// =============================================================================
// PatrolState - Agent follows patrol path waypoints
//
// Transitions to AggroState when a companion is detected within detection_range.
// =============================================================================
class PatrolState : public FSMState {
 public:
  static const PatrolState& Instance();

  const FSMState* Update(FSMContext& ctx, AgentFSM& agent,
                         const BaseEnv& env) const override;
  std::string GetName() const override { return "Patrol"; }

 private:
  PatrolState() = default;
};

// =============================================================================
// AggroState - Agent follows target companion
//
// Transitions to:
// - PatrolState if target is lost AND agent is on patrol path
// - ReturnToPatrolState if target is lost AND agent is off patrol path
// =============================================================================
class AggroState : public FSMState {
 public:
  static const AggroState& Instance();

  const FSMState* Update(FSMContext& ctx, AgentFSM& agent,
                         const BaseEnv& env) const override;
  std::string GetName() const override { return "Aggro"; }

 private:
  AggroState() = default;
};

// =============================================================================
// ReturnToPatrolState - Agent returns to nearest patrol waypoint
//
// Transitions to:
// - PatrolState when patrol waypoint is reached
// - AggroState if companion is detected during return
// =============================================================================
class ReturnToPatrolState : public FSMState {
 public:
  static const ReturnToPatrolState& Instance();

  const FSMState* Update(FSMContext& ctx, AgentFSM& agent,
                         const BaseEnv& env) const override;
  std::string GetName() const override { return "ReturnToPatrol"; }

 private:
  ReturnToPatrolState() = default;
};

// =============================================================================
// TelegraphState - Agent is telegraphing an incoming attack
//
// The agent stops moving and prepares to attack. The target area is locked in.
// After telegraph_ticks, transitions to AttackState.
// =============================================================================
class TelegraphState : public FSMState {
 public:
  static const TelegraphState& Instance();

  void OnEnter(FSMContext& ctx, AgentFSM& agent,
               BaseEnv& env) const override;
  const FSMState* Update(FSMContext& ctx, AgentFSM& agent,
                         const BaseEnv& env) const override;
  std::string GetName() const override { return "Telegraph"; }

 private:
  TelegraphState() = default;
};

// =============================================================================
// AttackState - Agent executes the attack
//
// Damage is applied to all valid targets in the attack area.
// After attack_ticks, transitions to RecoveryState.
// =============================================================================
class AttackState : public FSMState {
 public:
  static const AttackState& Instance();

  void OnEnter(FSMContext& ctx, AgentFSM& agent,
               BaseEnv& env) const override;
  const FSMState* Update(FSMContext& ctx, AgentFSM& agent,
                         const BaseEnv& env) const override;
  std::string GetName() const override { return "Attack"; }

 private:
  AttackState() = default;
};

// =============================================================================
// RecoveryState - Agent recovers after attacking
//
// The agent cannot move or attack during recovery.
// After recovery_ticks, transitions back to AggroState (if target in range)
// or ReturnToPatrolState.
// =============================================================================
class RecoveryState : public FSMState {
 public:
  static const RecoveryState& Instance();

  void OnEnter(FSMContext& ctx, AgentFSM& agent,
               BaseEnv& env) const override;
  const FSMState* Update(FSMContext& ctx, AgentFSM& agent,
                         const BaseEnv& env) const override;
  std::string GetName() const override { return "Recovery"; }

 private:
  RecoveryState() = default;
};

}  // namespace companions

#endif  // COMPANIONS_CORE_FSM_FSM_STATES_H_
