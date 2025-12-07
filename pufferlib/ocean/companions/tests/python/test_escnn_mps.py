#!/usr/bin/env python3
"""Test escnn layer compatibility with MPS (Apple Silicon).

This script tests each escnn layer type we use for D4-equivariant
networks on the MPS backend. Run this before implementing new architectures.

Usage:
    python test_escnn_mps.py
"""
from __future__ import annotations

import sys
from pathlib import Path
from typing import Callable

import torch

# Add pufferlib to path
sys.path.insert(0, str(Path(__file__).parents[5]))

try:
    import pytest
    PYTEST_AVAILABLE = True
except ImportError:
    PYTEST_AVAILABLE = False

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


def check_mps_available() -> bool:
    """Check if MPS backend is available."""
    if not torch.backends.mps.is_available():
        print("MPS not available on this system")
        if not torch.backends.mps.is_built():
            print("  PyTorch was not built with MPS support")
        return False
    return True


def check_escnn_available() -> bool:
    """Check if escnn is available."""
    try:
        import escnn  # noqa: F401
        return True
    except ImportError:
        return False


def _check_layer(
    name: str,
    create_fn: Callable[[], torch.nn.Module],
    input_shape: tuple[int, ...],
    device: torch.device,
    input_type=None,
) -> bool:
    """Test a single layer for MPS compatibility.

    Args:
        name: Layer name for reporting
        create_fn: Function that creates the layer
        input_shape: Shape of test input tensor
        device: Device to test on
        input_type: Optional FieldType for wrapping input in GeometricTensor

    Returns:
        True if layer works on device, False otherwise
    """
    from escnn import nn as enn

    try:
        # Create layer and move to device
        layer = create_fn()
        layer = layer.to(device)

        # Create input tensor
        x = torch.randn(*input_shape, device=device, requires_grad=True)

        # Wrap in GeometricTensor if input_type provided
        if input_type is not None:
            x_in = enn.GeometricTensor(x, input_type)
        else:
            x_in = x

        # Forward pass
        y = layer(x_in)

        # Extract tensor if GeometricTensor
        if hasattr(y, 'tensor'):
            y_out = y.tensor
        else:
            y_out = y

        # Backward pass
        loss = y_out.sum()
        loss.backward()

        # Verify gradient exists
        assert x.grad is not None, "No gradient computed"

        print(f"  [PASS] {name}")
        print(f"         Input: {x.shape} -> Output: {y_out.shape}")
        return True

    except Exception as e:
        print(f"  [FAIL] {name}")
        print(f"         Error: {e}")
        import traceback
        traceback.print_exc()
        return False


def _run_escnn_layer_tests(device: torch.device) -> dict[str, bool]:
    """Test all escnn layers we use.

    Args:
        device: Device to test on

    Returns:
        Dict mapping layer name to pass/fail status
    """
    from escnn import gspaces
    from escnn import nn as enn

    # D4 group: 4 rotations x 2 reflections = 8 elements
    gspace = gspaces.flipRot2dOnR2(N=4)

    # Field types we use
    # Input: 5 scalar channels (trivial representation)
    in_type = enn.FieldType(gspace, 5 * [gspace.trivial_repr])

    # Hidden: regular representation (8 channels per field for D4)
    hid_type = enn.FieldType(gspace, 4 * [gspace.regular_repr])  # 4 * 8 = 32 channels

    results = {}

    print(f"\nTesting escnn layers on {device}...")
    print(f"D4 group: {gspace.fibergroup}")
    print(f"Input type: {in_type.size} channels")
    print(f"Hidden type: {hid_type.size} channels")
    print()

    # Test 1: R2Conv (input -> hidden)
    results["R2Conv (in->hid)"] = _check_layer(
        "R2Conv (trivial -> regular)",
        lambda: enn.R2Conv(in_type, hid_type, kernel_size=3, padding=1),
        (2, 5, 10, 10),  # batch=2, channels=5, H=W=10
        device,
        input_type=in_type,
    )

    # Test 2: R2Conv (hidden -> hidden)
    results["R2Conv (hid->hid)"] = _check_layer(
        "R2Conv (regular -> regular)",
        lambda: enn.R2Conv(hid_type, hid_type, kernel_size=3, padding=1),
        (2, 32, 10, 10),  # 4 * 8 = 32 channels
        device,
        input_type=hid_type,
    )

    # Test 3: InnerBatchNorm
    results["InnerBatchNorm"] = _check_layer(
        "InnerBatchNorm",
        lambda: enn.InnerBatchNorm(hid_type),
        (2, 32, 10, 10),
        device,
        input_type=hid_type,
    )

    # Test 4: ReLU (equivariant)
    results["ReLU"] = _check_layer(
        "ReLU (equivariant)",
        lambda: enn.ReLU(hid_type, inplace=False),
        (2, 32, 10, 10),
        device,
        input_type=hid_type,
    )

    # Test 5: GroupPooling (for invariant output)
    results["GroupPooling"] = _check_layer(
        "GroupPooling (regular -> trivial)",
        lambda: enn.GroupPooling(hid_type),
        (2, 32, 10, 10),
        device,
        input_type=hid_type,
    )

    # Test 6: PointwiseAdaptiveAvgPool2D
    results["PointwiseAdaptiveAvgPool2D"] = _check_layer(
        "PointwiseAdaptiveAvgPool2D",
        lambda: enn.PointwiseAdaptiveAvgPool2D(hid_type, output_size=1),
        (2, 32, 10, 10),
        device,
        input_type=hid_type,
    )

    # Test 7: PointwiseAdaptiveMaxPool2D
    results["PointwiseAdaptiveMaxPool2D"] = _check_layer(
        "PointwiseAdaptiveMaxPool2D",
        lambda: enn.PointwiseAdaptiveMaxPool2D(hid_type, output_size=1),
        (2, 32, 10, 10),
        device,
        input_type=hid_type,
    )

    # Test 8: Full mini-network (forward + backward)
    def create_mini_network():
        return enn.SequentialModule(
            enn.R2Conv(in_type, hid_type, kernel_size=3, padding=1),
            enn.InnerBatchNorm(hid_type),
            enn.ReLU(hid_type, inplace=False),
            enn.R2Conv(hid_type, hid_type, kernel_size=3, padding=1),
            enn.InnerBatchNorm(hid_type),
            enn.ReLU(hid_type, inplace=False),
            enn.PointwiseAdaptiveAvgPool2D(hid_type, output_size=1),
            enn.GroupPooling(hid_type),
        )

    results["Full mini-network"] = _check_layer(
        "Full mini-network (5 -> invariant)",
        create_mini_network,
        (2, 5, 10, 10),
        device,
        input_type=in_type,
    )

    # Test 9: Variable input sizes (crucial for curriculum)
    print("\n  Testing variable input sizes...")
    try:
        net = create_mini_network().to(device)
        for size in [5, 8, 11, 16, 24]:
            x = torch.randn(2, 5, size, size, device=device)
            x_geom = enn.GeometricTensor(x, in_type)
            y = net(x_geom)
            print(f"    Grid {size}x{size}: output shape {y.tensor.shape}")
        results["Variable sizes"] = True
        print("  [PASS] Variable input sizes")
    except Exception as e:
        results["Variable sizes"] = False
        print(f"  [FAIL] Variable input sizes: {e}")

    # Test 10: Linear layer in MLP gspace (for actor head)
    print("\n  Testing MLP gspace layers...")
    try:
        from escnn import group
        mlp_gspace = gspaces.no_base_space(group.dihedral_group(4))

        # Regular representation for hidden features
        mlp_hid_type = mlp_gspace.type(*[mlp_gspace.regular_repr for _ in range(4)])

        # Output type with trivial + 2D irrep
        irrep_2d = mlp_gspace.fibergroup.irrep(1, 1)
        out_type = mlp_gspace.type(mlp_gspace.trivial_repr, irrep_2d)

        linear = enn.Linear(mlp_hid_type, out_type).to(device)
        x = torch.randn(2, 32, device=device, requires_grad=True)
        y = linear(enn.GeometricTensor(x, mlp_hid_type)).tensor
        y.sum().backward()
        assert x.grad is not None
        results["Linear (MLP gspace)"] = True
        print(f"  [PASS] Linear (MLP gspace): {x.shape} -> {y.shape}")
    except Exception as e:
        results["Linear (MLP gspace)"] = False
        print(f"  [FAIL] Linear (MLP gspace): {e}")

    return results


# =============================================================================
# Pytest-compatible test functions
# =============================================================================


@pytest.mark.skipif(not check_escnn_available(), reason="escnn not installed")
def test_escnn_mps_compatibility():
    """Test that all escnn layers work on MPS (or CPU fallback)."""
    if torch.backends.mps.is_available():
        device = torch.device("mps")
    else:
        device = torch.device("cpu")

    results = _run_escnn_layer_tests(device)

    # Assert all tests passed
    failed = [name for name, passed in results.items() if not passed]
    assert len(failed) == 0, f"Failed tests: {failed}"


def main():
    print("=" * 60)
    print("ESCNN MPS Compatibility Test")
    print("=" * 60)

    # Check MPS availability
    if not check_mps_available():
        print("\nFalling back to CPU test...")
        device = torch.device("cpu")
    else:
        device = torch.device("mps")
        print(f"\nMPS is available: {device}")

    # Try to import escnn
    if not check_escnn_available():
        print("\nFailed to import escnn")
        print("Install with: pip install escnn")
        return 1

    import escnn
    print(f"escnn version: {escnn.__version__}")

    # Run tests
    results = _run_escnn_layer_tests(device)

    # Summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)

    passed = sum(1 for v in results.values() if v)
    total = len(results)

    for name, status in results.items():
        status_str = "PASS" if status else "FAIL"
        print(f"  [{status_str}] {name}")

    print()
    print(f"Results: {passed}/{total} tests passed")
    print(f"Device: {device}")

    if passed == total:
        print("\nAll tests passed! escnn is compatible with this device.")
        return 0
    else:
        print(f"\n{total - passed} test(s) failed.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
