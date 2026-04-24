import sys

collect_ignore_glob: list[str] = []

if sys.platform == "win32":
    # D4-equivariant tests need `escnn` (requires MSVC to build on Windows) and
    # the compiled `pufferlib.ocean.companions.binding` C-extension (setup.py
    # rejects Windows). Skip collection instead of failing on import.
    collect_ignore_glob += [
        "test_d4_networks.py",
        "test_d4_symmetry.py",
        "test_escnn_mps.py",
    ]
