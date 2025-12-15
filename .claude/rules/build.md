---
paths: setup.py, pufferlib/ocean/companions/CMakeLists.txt, pufferlib/ocean/companions/scripts/**
---

# Build Commands

## Unified Build Script

All builds use a single script with options:

```bash
./pufferlib/ocean/companions/scripts/build.sh [OPTIONS]

Options:
  --release    Build Release config (default: Debug)
  --clean      Force full rebuild (all libs get fresh timestamps)
  --python     Also build and test Python bindings
  --no-test    Skip running tests
```

### Examples

```bash
# Debug build + tests (fast iteration)
./scripts/build.sh

# Release build + tests
./scripts/build.sh --release

# Clean Release build (all libs rebuilt)
./scripts/build.sh --release --clean

# Full build with Python bindings
./scripts/build.sh --release --python
```

## Build Levels

| Level | Command | Time | When to use |
|-------|---------|------|-------------|
| **Fast** | `build.sh` | ~1s | Iterating on C++ game code |
| **Full** | `build.sh --release --python` | ~5s | After C++ changes need Python testing |
| **Clean** | `build.sh --release --clean` | ~10s | When libs need fresh timestamps |
| **Install** | `uv pip install -e .` | ~30s | First setup, after `setup.py` changes |

## VS Code Tasks

Run via Command Palette → "Run Task":

| Task | Description |
|------|-------------|
| `companions-build` | Debug build + tests |
| `companions-build-clean` | Debug clean build + tests |
| `companions-build-release` | Release build + tests |
| `companions-build-release-clean` | Release clean build + tests |
| `companions-build-full` | Release + Python bindings + all tests |
| `companions-build-full-clean` | Clean full build |

### Recommended workflow

1. **First time setup:** Run `uv pip install -e .`
2. **C++ iteration:** `companions-build` (fast Debug)
3. **Testing Python integration:** `companions-build-full`
4. **Copying libs to other project:** `companions-build-release-clean` (ensures all libs fresh)
5. **After git pull:** `uv pip install -e .` (if setup.py changed), else `companions-build-full`
