#!/usr/bin/env python3
"""Unit tests for D4 symmetry transforms and D4-equivariant networks.

Tests cover:
- D4 transform mathematical properties (rotation cycle, inverse, action consistency)
- D4EquivariantEncoder (shape, invariance, variable sizes, gradient flow)
- D4Actor (shape, movement equivariance, interaction invariance)
- D4Critic (shape, invariance)
- MPS compatibility (if available)

Usage:
    python -m pytest test_d4_networks.py -v
    python test_d4_networks.py  # Direct run
"""
from __future__ import annotations

import sys
from pathlib import Path

import torch

try:
    import pytest
    PYTEST_AVAILABLE = True
except ImportError:
    PYTEST_AVAILABLE = False

    # Create a mock pytest.mark.skipif when pytest is not available
    class MockMark:
        @staticmethod
        def skipif(condition, reason=""):
            def decorator(func):
                func._skip_condition = condition
                func._skip_reason = reason
                return func
            return decorator

    class MockPytest:
        mark = MockMark()

    pytest = MockPytest()

# Add pufferlib to path
sys.path.insert(0, str(Path(__file__).parents[5]))

from pufferlib.ocean.companions.networks.d4 import (
    apply_d4_transform,
    inverse_d4_transform,
    random_d4_augment,
    ESCNN_AVAILABLE,
)

# Only import escnn-dependent classes if available
if ESCNN_AVAILABLE:
    from pufferlib.ocean.companions.networks.d4 import (
        D4EquivariantEncoder,
        D4Actor,
        D4Critic,
    )


# =============================================================================
# D4 Transform Tests (no escnn dependency)
# =============================================================================


def test_d4_transform_identity():
    """Identity transform (idx=0) leaves observation unchanged."""
    obs = torch.randn(2, 5, 8, 8)
    transformed, _ = apply_d4_transform(obs, None, transform_idx=0)
    assert torch.allclose(obs, transformed), "Identity should not change observation"


def test_d4_transform_rotation_cycle():
    """Four 90-degree rotations return to original."""
    obs = torch.randn(2, 5, 8, 8)

    result = obs
    for _ in range(4):
        result, _ = apply_d4_transform(result, None, transform_idx=1)

    assert torch.allclose(obs, result, atol=1e-5), "4 rotations should return to original"


def test_d4_inverse_correctness():
    """Applying transform then inverse returns to original."""
    obs = torch.randn(2, 5, 8, 8)
    action = torch.tensor([1, 2, 3, 4])  # Various movement actions

    for t_idx in range(8):
        t_obs, t_action = apply_d4_transform(obs.clone(), action.clone(), transform_idx=t_idx)
        inv_idx = inverse_d4_transform(t_idx)
        restored_obs, restored_action = apply_d4_transform(t_obs, t_action, transform_idx=inv_idx)

        assert torch.allclose(obs, restored_obs, atol=1e-5), f"Transform {t_idx}: obs not restored"
        assert torch.equal(action, restored_action), f"Transform {t_idx}: action not restored"


def test_d4_transform_action_consistency():
    """Actions transform consistently with observations."""
    zeros = torch.zeros(1, 5, 8, 8)

    # Up (1) under 90 deg CCW becomes Left (3)
    action = torch.tensor([1])
    _, transformed_action = apply_d4_transform(zeros, action, transform_idx=1)
    assert transformed_action.item() == 3, f"Up rotated 90 CCW should be Left (3), got {transformed_action.item()}"

    # Down (2) under 90 deg CCW becomes Right (4)
    action = torch.tensor([2])
    _, transformed_action = apply_d4_transform(zeros, action, transform_idx=1)
    assert transformed_action.item() == 4, f"Down rotated 90 CCW should be Right (4), got {transformed_action.item()}"

    # Stay should always stay
    for t_idx in range(8):
        _, t_action = apply_d4_transform(zeros, torch.tensor([0]), transform_idx=t_idx)
        assert t_action.item() == 0, f"Stay should remain Stay under transform {t_idx}"


def test_d4_reflections_self_inverse():
    """Reflections (transforms 4-7) are self-inverse."""
    obs = torch.randn(2, 5, 8, 8)

    for t_idx in range(4, 8):
        t_obs, _ = apply_d4_transform(obs.clone(), None, transform_idx=t_idx)
        restored_obs, _ = apply_d4_transform(t_obs, None, transform_idx=t_idx)
        assert torch.allclose(obs, restored_obs, atol=1e-5), f"Reflection {t_idx} should be self-inverse"


def test_d4_rot180_self_inverse():
    """Rot180 (transform 2) is self-inverse."""
    obs = torch.randn(2, 5, 8, 8)

    t_obs, _ = apply_d4_transform(obs.clone(), None, transform_idx=2)
    restored_obs, _ = apply_d4_transform(t_obs, None, transform_idx=2)
    assert torch.allclose(obs, restored_obs, atol=1e-5), "Rot180 should be self-inverse"


def test_random_d4_augment():
    """Random augmentation returns valid transform index."""
    obs = torch.randn(2, 5, 8, 8)
    for _ in range(20):
        _, _, t_idx = random_d4_augment(obs)
        assert 0 <= t_idx < 8, f"Invalid transform index: {t_idx}"


def test_d4_inverse_mapping():
    """Verify the inverse mapping is correct."""
    expected_inverses = {
        0: 0,  # identity
        1: 3,  # rot_1 -> rot_3
        2: 2,  # rot_2 -> rot_2 (180 self-inverse)
        3: 1,  # rot_3 -> rot_1
        4: 4,  # flip -> flip (self-inverse)
        5: 5,  # flip+rot_1 -> self-inverse
        6: 6,  # flip+rot_2 -> self-inverse
        7: 7,  # flip+rot_3 -> self-inverse
    }
    for t_idx, expected_inv in expected_inverses.items():
        assert inverse_d4_transform(t_idx) == expected_inv, f"Inverse of {t_idx} should be {expected_inv}"


# =============================================================================
# D4EquivariantEncoder Tests (requires escnn)
# =============================================================================


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
def test_d4_encoder_output_shape():
    """D4EquivariantEncoder produces correct output shape."""
    encoder = D4EquivariantEncoder(in_channels=5, hidden_dim=64)
    x = torch.randn(2, 5, 10, 10)
    out = encoder(x)
    assert out.shape == (2, 64), f"Expected (2, 64), got {out.shape}"


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
def test_d4_encoder_variable_sizes():
    """D4EquivariantEncoder handles different grid sizes."""
    encoder = D4EquivariantEncoder(in_channels=5, hidden_dim=64)

    for size in [5, 8, 11, 16, 24]:
        x = torch.randn(2, 5, size, size)
        out = encoder(x)
        assert out.shape == (2, 64), f"Size {size}: expected (2, 64), got {out.shape}"


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
def test_d4_encoder_multi_agent_shape():
    """D4EquivariantEncoder handles multi-agent batch dimensions."""
    encoder = D4EquivariantEncoder(in_channels=5, hidden_dim=64)

    # Shape: [batch, n_agents, C, H, W]
    x = torch.randn(4, 3, 5, 10, 10)
    out = encoder(x)
    assert out.shape == (4, 3, 64), f"Expected (4, 3, 64), got {out.shape}"


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
def test_d4_encoder_invariance():
    """D4EquivariantEncoder output is invariant under D4 transformations."""
    encoder = D4EquivariantEncoder(in_channels=5, hidden_dim=64)
    encoder.eval()  # Important for BatchNorm consistency

    x = torch.randn(1, 5, 8, 8)
    with torch.no_grad():
        base_out = encoder(x)

        # Test all 8 D4 transformations
        for t_idx in range(8):
            t_x, _ = apply_d4_transform(x, None, transform_idx=t_idx)
            t_out = encoder(t_x)

            # Outputs should be the same (within numerical tolerance)
            diff = (base_out - t_out).abs().max().item()
            assert diff < 1e-4, f"Transform {t_idx}: invariance violated, diff={diff}"


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
def test_d4_encoder_gradient_flow():
    """Gradients flow through D4EquivariantEncoder."""
    encoder = D4EquivariantEncoder(in_channels=5, hidden_dim=64)
    x = torch.randn(2, 5, 10, 10, requires_grad=True)
    out = encoder(x)
    loss = out.sum()
    loss.backward()
    assert x.grad is not None, "No gradient on input"
    assert x.grad.abs().sum() > 0, "Zero gradients"


# =============================================================================
# D4Actor Tests (requires escnn)
# =============================================================================


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
def test_d4_actor_output_shape():
    """D4Actor produces correct output shapes."""
    actor = D4Actor(n_agents=3, nvec=[5, 2], in_channels=5, hidden_dim=64)
    obs = torch.randn(4, 3, 5, 10, 10)  # [batch, n_agents, C, H, W]
    logits = actor(obs)

    assert len(logits) == 2, f"Expected 2 action heads, got {len(logits)}"
    assert logits[0].shape == (4, 3, 5), f"Movement logits: expected (4, 3, 5), got {logits[0].shape}"
    assert logits[1].shape == (4, 3, 2), f"Interaction logits: expected (4, 3, 2), got {logits[1].shape}"


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
def test_d4_actor_interaction_invariance():
    """D4Actor interaction logits are invariant under D4 transformations."""
    actor = D4Actor(n_agents=1, nvec=[5, 2], in_channels=5, hidden_dim=64)
    actor.eval()

    # Shape for actor: [batch, n_agents, C, H, W]
    x = torch.randn(1, 1, 5, 8, 8)
    with torch.no_grad():
        _, base_interaction = actor(x)

        for t_idx in range(8):
            # Transform the observation
            x_flat = x.reshape(-1, *x.shape[-3:])
            t_x_flat, _ = apply_d4_transform(x_flat, None, transform_idx=t_idx)
            t_x = t_x_flat.reshape(x.shape)
            _, t_interaction = actor(t_x)

            diff = (base_interaction - t_interaction).abs().max().item()
            assert diff < 1e-4, f"Transform {t_idx}: interaction invariance violated, diff={diff}"


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
def test_d4_actor_variable_sizes():
    """D4Actor handles different grid sizes."""
    actor = D4Actor(n_agents=2, nvec=[5, 2], in_channels=5, hidden_dim=64)

    for size in [5, 8, 11, 16]:
        obs = torch.randn(2, 2, 5, size, size)
        logits = actor(obs)
        assert logits[0].shape == (2, 2, 5), f"Size {size}: movement shape wrong"
        assert logits[1].shape == (2, 2, 2), f"Size {size}: interaction shape wrong"


# =============================================================================
# D4Critic Tests (requires escnn)
# =============================================================================


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
def test_d4_critic_output_shape():
    """D4Critic produces correct output shapes."""
    critic = D4Critic(n_agents=3, in_channels=5, hidden_dim=64, centralised=False)
    obs = torch.randn(4, 3, 5, 10, 10)
    values = critic(obs)
    assert values.shape == (4, 3, 1), f"Expected (4, 3, 1), got {values.shape}"

    critic_cent = D4Critic(n_agents=3, in_channels=5, hidden_dim=64, centralised=True)
    values_cent = critic_cent(obs)
    assert values_cent.shape == (4, 3, 1), f"Centralized: expected (4, 3, 1), got {values_cent.shape}"


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
def test_d4_critic_invariance():
    """D4Critic values are invariant under D4 transformations."""
    critic = D4Critic(n_agents=1, in_channels=5, hidden_dim=64, centralised=False)
    critic.eval()

    # Shape for critic: [batch, n_agents, C, H, W]
    x = torch.randn(1, 1, 5, 8, 8)
    with torch.no_grad():
        base_val = critic(x)

        for t_idx in range(8):
            # Transform the observation
            x_flat = x.reshape(-1, *x.shape[-3:])
            t_x_flat, _ = apply_d4_transform(x_flat, None, transform_idx=t_idx)
            t_x = t_x_flat.reshape(x.shape)
            t_val = critic(t_x)

            diff = (base_val - t_val).abs().max().item()
            assert diff < 1e-4, f"Transform {t_idx}: critic invariance violated, diff={diff}"


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
def test_d4_critic_centralised_same_value():
    """D4Critic in centralised mode gives same value to all agents."""
    critic = D4Critic(n_agents=3, in_channels=5, hidden_dim=64, centralised=True)
    obs = torch.randn(4, 3, 5, 8, 8)
    values = critic(obs)

    # All agents should get the same value
    assert torch.allclose(values[:, 0, :], values[:, 1, :]), "Agent 0 and 1 should have same value"
    assert torch.allclose(values[:, 0, :], values[:, 2, :]), "Agent 0 and 2 should have same value"


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
def test_d4_critic_variable_sizes():
    """D4Critic handles different grid sizes."""
    critic = D4Critic(n_agents=2, in_channels=5, hidden_dim=64)

    for size in [5, 8, 11, 16]:
        obs = torch.randn(2, 2, 5, size, size)
        values = critic(obs)
        assert values.shape == (2, 2, 1), f"Size {size}: value shape wrong"


# =============================================================================
# MPS Compatibility Tests
# =============================================================================


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
@pytest.mark.skipif(not torch.backends.mps.is_available(), reason="MPS not available")
def test_d4_encoder_mps():
    """D4EquivariantEncoder works on MPS."""
    device = torch.device("mps")
    encoder = D4EquivariantEncoder(in_channels=5, hidden_dim=64).to(device)
    x = torch.randn(2, 5, 10, 10, device=device)

    out = encoder(x)
    assert out.device.type == "mps", "Output not on MPS"

    # Test backward
    out.sum().backward()


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
@pytest.mark.skipif(not torch.backends.mps.is_available(), reason="MPS not available")
def test_d4_actor_mps():
    """D4Actor works on MPS."""
    device = torch.device("mps")
    actor = D4Actor(n_agents=2, nvec=[5, 2], in_channels=5, hidden_dim=64).to(device)
    obs = torch.randn(2, 2, 5, 10, 10, device=device)

    logits = actor(obs)
    assert logits[0].device.type == "mps", "Movement logits not on MPS"
    assert logits[1].device.type == "mps", "Interaction logits not on MPS"


@pytest.mark.skipif(not ESCNN_AVAILABLE, reason="escnn not installed")
@pytest.mark.skipif(not torch.backends.mps.is_available(), reason="MPS not available")
def test_d4_critic_mps():
    """D4Critic works on MPS."""
    device = torch.device("mps")
    critic = D4Critic(n_agents=2, in_channels=5, hidden_dim=64).to(device)
    obs = torch.randn(2, 2, 5, 10, 10, device=device)

    values = critic(obs)
    assert values.device.type == "mps", "Values not on MPS"


# =============================================================================
# Test Runner
# =============================================================================


def run_all_tests():
    """Run all tests manually (for direct execution)."""
    tests = [
        # D4 Transform tests (no escnn)
        test_d4_transform_identity,
        test_d4_transform_rotation_cycle,
        test_d4_inverse_correctness,
        test_d4_transform_action_consistency,
        test_d4_reflections_self_inverse,
        test_d4_rot180_self_inverse,
        test_random_d4_augment,
        test_d4_inverse_mapping,
        # D4EquivariantEncoder tests (will skip if escnn not available)
        test_d4_encoder_output_shape,
        test_d4_encoder_variable_sizes,
        test_d4_encoder_multi_agent_shape,
        test_d4_encoder_invariance,
        test_d4_encoder_gradient_flow,
        # D4Actor tests
        test_d4_actor_output_shape,
        test_d4_actor_interaction_invariance,
        test_d4_actor_variable_sizes,
        # D4Critic tests
        test_d4_critic_output_shape,
        test_d4_critic_invariance,
        test_d4_critic_centralised_same_value,
        test_d4_critic_variable_sizes,
        # MPS tests
        test_d4_encoder_mps,
        test_d4_actor_mps,
        test_d4_critic_mps,
    ]

    passed = 0
    failed = 0
    skipped = 0

    for test in tests:
        print(f"\n[TEST] {test.__name__}")

        # Check for skip condition
        skip_condition = getattr(test, '_skip_condition', False)
        skip_reason = getattr(test, '_skip_reason', "")

        if skip_condition:
            print(f"  SKIPPED: {skip_reason}")
            skipped += 1
            continue

        try:
            test()
            print(f"  PASSED")
            passed += 1
        except Exception as e:
            print(f"  FAILED: {e}")
            import traceback
            traceback.print_exc()
            failed += 1

    print(f"\n{'='*60}")
    print(f"Results: {passed} passed, {failed} failed, {skipped} skipped")
    print(f"ESCNN available: {ESCNN_AVAILABLE}")
    print(f"MPS available: {torch.backends.mps.is_available()}")
    return failed == 0


if __name__ == "__main__":
    success = run_all_tests()
    sys.exit(0 if success else 1)
