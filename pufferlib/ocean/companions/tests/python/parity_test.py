#!/usr/bin/env python3
"""Parity test for Companions Python wrapper.

Compares Python wrapper outputs against C++ reference trace files
to verify the binding layer produces identical results.
"""

import argparse
import struct
import sys
from pathlib import Path

import numpy as np

# Observation tensor channel count - must match synchro.py NUM_CHANNELS and
# BaseEnv::kNumObservationPlanes (universal 7-plane layout).
NUM_CHANNELS = 7

# Binary file format constants
PARITY_MAGIC = 0x50415249  # "PARI"
PARITY_VERSION_V1 = 1  # tensor only
PARITY_VERSION_V2 = 2  # tensor + vector
HEADER_FORMAT_V1 = '<IIIIIIIIIIII'  # 12 uint32s, little-endian
HEADER_FORMAT_V2 = '<IIIIIIIIIIIII'  # 13 uint32s, little-endian (adds vector_obs_size)
HEADER_SIZE_V1 = 48
HEADER_SIZE_V2 = 52


class ParityHeader:
    """Binary file header."""

    def __init__(self, data: bytes):
        # Peek at version to determine format
        magic, version = struct.unpack('<II', data[:8])

        if magic != PARITY_MAGIC:
            raise ValueError(f"Invalid magic: {hex(magic)}, expected {hex(PARITY_MAGIC)}")

        if version == PARITY_VERSION_V1:
            fields = struct.unpack(HEADER_FORMAT_V1, data[:HEADER_SIZE_V1])
            self.vector_obs_size = 0  # v1 had tensor only
            self.header_size = HEADER_SIZE_V1
        elif version == PARITY_VERSION_V2:
            fields = struct.unpack(HEADER_FORMAT_V2, data[:HEADER_SIZE_V2])
            self.vector_obs_size = fields[12]
            self.header_size = HEADER_SIZE_V2
        else:
            raise ValueError(f"Unsupported version: {version}")

        self.magic = fields[0]
        self.version = fields[1]
        self.env_seed = fields[2]
        self.action_seed = fields[3]
        self.num_steps = fields[4]
        self.num_agents = fields[5]
        self.rows = fields[6]
        self.cols = fields[7]
        self.num_synchro = fields[8]
        self.map_complexity = fields[9]
        self.horizon = fields[10]
        self.num_episodes = fields[11]

        # Compute observation sizes
        self.tensor_size = NUM_CHANNELS * self.rows * self.cols
        self.obs_size = self.tensor_size + self.vector_obs_size

    def __repr__(self):
        return (
            f"ParityHeader(v{self.version}, env_seed={self.env_seed}, action_seed={self.action_seed}, "
            f"steps={self.num_steps}, agents={self.num_agents}, grid={self.rows}x{self.cols}, "
            f"synchro={self.num_synchro}, complexity={self.map_complexity}, "
            f"horizon={self.horizon}, episodes={self.num_episodes}, "
            f"vector_obs={self.vector_obs_size})"
        )


def load_reference(path: Path):
    """Load binary reference file.

    Returns:
        header: ParityHeader
        records: list of dicts with keys: actions, observations, rewards, terminals, reset_seed
    """
    with open(path, 'rb') as f:
        # Read enough bytes for max header size
        header_data = f.read(HEADER_SIZE_V2)
        header = ParityHeader(header_data)

        # Seek to correct position after header
        f.seek(header.header_size)

        obs_size = header.num_agents * header.obs_size  # tensor + vector per agent
        action_size = header.num_agents * 2

        records = []
        # +1 for initial observation record
        for _ in range(header.num_steps + 1):
            actions = np.frombuffer(f.read(action_size * 4), dtype=np.int32).copy()
            observations = np.frombuffer(f.read(obs_size * 4), dtype=np.float32).copy()
            rewards = np.frombuffer(f.read(header.num_agents * 4), dtype=np.float32).copy()
            terminals = np.frombuffer(f.read(header.num_agents), dtype=np.uint8).copy()
            reset_seed = struct.unpack('<I', f.read(4))[0]

            # Reshape observations: [num_agents, obs_size] flattened
            records.append({
                'actions': actions.reshape(header.num_agents, 2),
                'observations': observations.reshape(header.num_agents, header.obs_size),
                'rewards': rewards,
                'terminals': terminals,
                'reset_seed': reset_seed,
            })

        return header, records


def compare_observations(ref: np.ndarray, actual: np.ndarray,
                         atol: float = 1e-6, rtol: float = 1e-5) -> tuple:
    """Compare observations with floating-point tolerance.

    Returns:
        (match: bool, error_msg: str)
    """
    if ref.shape != actual.shape:
        return False, f"Shape mismatch: ref={ref.shape}, actual={actual.shape}"

    if not np.allclose(ref, actual, atol=atol, rtol=rtol):
        diff = np.abs(ref - actual)
        max_diff = np.max(diff)
        max_idx = np.unravel_index(np.argmax(diff), diff.shape)
        return False, f"Max diff={max_diff:.2e} at {max_idx}"

    return True, ""


def test_parity(reference_path: Path, verbose: bool = False):
    """Run parity test comparing Python wrapper to C++ reference.

    Args:
        reference_path: Path to binary reference file
        verbose: Print progress every 100 steps

    Returns:
        True if parity test passes

    Raises:
        AssertionError if mismatches found
    """
    # Import here to allow running from different directories
    from pufferlib.ocean.companions import binding

    header, ref_records = load_reference(reference_path)
    print(f"Loaded reference: {header}")

    # Create environment using low-level binding
    # Allocate numpy arrays for observations, actions, rewards, terminals
    # v2 format: flattened tensor + vector per agent
    obs_shape = (header.num_agents, header.obs_size)
    observations = np.zeros(obs_shape, dtype=np.float32)
    actions = np.zeros((header.num_agents, 2), dtype=np.int32)
    rewards = np.zeros(header.num_agents, dtype=np.float32)
    terminals = np.zeros(header.num_agents, dtype=np.uint8)
    truncations = np.zeros(header.num_agents, dtype=np.uint8)

    # Initialize environment
    env_handle = binding.env_init(
        observations,
        actions,
        rewards,
        terminals,
        truncations,
        header.env_seed,
        rows=header.rows,
        cols=header.cols,
        num_agents=header.num_agents,
        num_synchro=header.num_synchro,
        map_complexity=header.map_complexity,
        horizon=header.horizon,
        d4_transform=0,
        overfit=0,
        # Obs-size handshake: declare the per-agent buffer size we allocated;
        # C++ init fails hard on mismatch (see synchro_wrapper.cc).
        expected_obs_size=header.obs_size,
    )

    # Initial reset with seed
    binding.env_reset_seed(env_handle, header.env_seed)

    # Check initial observation (step 0)
    ref_initial = ref_records[0]
    match, msg = compare_observations(ref_initial['observations'], observations)
    if not match:
        raise AssertionError(f"Step 0 (initial): Observation mismatch - {msg}")

    if verbose:
        print("Step 0 (initial): OK")

    mismatches = []

    for step in range(1, header.num_steps + 1):
        ref = ref_records[step]

        # Copy reference actions to environment
        actions[:] = ref['actions']

        # Step environment
        binding.env_step(env_handle)

        # Compare observations
        match, msg = compare_observations(ref['observations'], observations)
        if not match:
            mismatches.append(f"Step {step}: Observation - {msg}")

        # Compare rewards
        if not np.allclose(ref['rewards'], rewards, atol=1e-6):
            diff = np.abs(ref['rewards'] - rewards)
            mismatches.append(f"Step {step}: Rewards - max_diff={np.max(diff):.2e}")

        # Compare terminals
        if not np.array_equal(ref['terminals'], terminals):
            mismatches.append(f"Step {step}: Terminals - ref={ref['terminals']}, actual={terminals}")

        # Note: The wrapper auto-resets when done, no manual reset needed
        # The reset_seed field is just a flag indicating a reset occurred

        if verbose and step % 100 == 0:
            print(f"  Step {step}/{header.num_steps} OK")

    # Close environment
    binding.env_close(env_handle)

    if mismatches:
        print(f"\nParity test FAILED with {len(mismatches)} mismatches:")
        for m in mismatches[:20]:  # Show first 20
            print(f"  {m}")
        if len(mismatches) > 20:
            print(f"  ... and {len(mismatches) - 20} more")
        raise AssertionError(f"Parity test failed with {len(mismatches)} mismatches")

    return True


def main():
    parser = argparse.ArgumentParser(
        description="Parity test for Companions Python wrapper"
    )
    parser.add_argument(
        "reference_file",
        type=Path,
        help="Path to binary reference file generated by parity_generator"
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Print progress every 100 steps"
    )
    args = parser.parse_args()

    if not args.reference_file.exists():
        print(f"Error: Reference file not found: {args.reference_file}")
        sys.exit(1)

    try:
        test_parity(args.reference_file, args.verbose)
        print("\nParity test PASSED")
    except AssertionError as e:
        print(f"\n{e}")
        sys.exit(1)
    except Exception as e:
        print(f"\nError: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
