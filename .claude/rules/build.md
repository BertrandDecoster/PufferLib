---
paths: setup.py, pufferlib/ocean/companions/CMakeLists.txt, pufferlib/ocean/companions/scripts/**
---

# Build Commands

Three build levels for different workflows:

| Level | What it builds | Time | When to use |
|-------|---------------|------|-------------|
| **Fast** | C++ static libs | ~1s | Iterating on C++ game code |
| **Full** | C++ + Python bindings + tests | ~5s | After C++ changes need Python testing |
| **Install** | Everything + all Ocean envs | ~30s | First setup, after `setup.py` changes, after git pull |

## Prerequisites

CMake must be configured once before any builds:
```bash
mkdir -p pufferlib/ocean/companions/build
cd pufferlib/ocean/companions/build
cmake .. -DCMAKE_BUILD_TYPE=Release
```

## Fast Build (C++ only)

Builds static libraries. Use when iterating on game logic.

```bash
cmake --build pufferlib/ocean/companions/build -j4
```

## Full Build (C++ + Python bindings)

The recommended script for companions development:

```bash
./pufferlib/ocean/companions/scripts/build_and_test.sh
```

This does:
1. CMake build (C++ static libs)
2. C++ tests (ctest)
3. Python bindings (`setup.py build_companions`)
4. Python integration test

## Full PufferLib Install

Only run when needed (slow ~30s):

```bash
uv pip install -e .
```

**When needed:**
- First time setup
- After `setup.py` or `pyproject.toml` changes
- After git pull with dependency changes
- After changes to non-companions Ocean environments

**NOT needed for:**
- Day-to-day companions C++ iteration
- Companions Python code changes

## VS Code Tasks

Run via Command Palette → "Run Task":

| Task | Description |
|------|-------------|
| `companions-build` | Fast build (C++ only, ~1s) |
| `companions-build-full` | Full build + tests (~5s) |
| `companions-test` | Run C++ tests |
| `companions-test-python` | Run Python tests |
| `companions-configure` | CMake configure (Release) |
| `companions-configure-debug` | CMake configure (Debug symbols) |
| `companions-clean` | Delete build directory |
| `pufferlib-install` | Full install (~30s, rarely needed) |

### Recommended workflow

1. **First time setup:** `companions-configure` → `pufferlib-install`
2. **C++ iteration:** `companions-build` (fast)
3. **Testing Python integration:** `companions-build-full`
4. **After git pull:** `pufferlib-install` (if setup.py changed), else `companions-build-full`
