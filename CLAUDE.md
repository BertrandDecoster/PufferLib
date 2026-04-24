# The companions

The companions is a C++ MARL env that is embedded in PufferLib for RL training (this repo) and that is embedded in Unreal Engine to play the game.
PufferLib is a high-performance reinforcement learning library with C-based environments ("Ocean" environments) that achieve 1M+ steps/second. 
The `pufferlib/ocean/companions` directory contains the custom C++ game.

The rest of the documentation is in .claude/rules

To build+run all tests, run the task companions-test-all (Ctrl-Shift-P -> Tasks:Run task -> companions-test-all)