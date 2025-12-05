// Parity reference generator for Python wrapper validation
// Generates binary trace files for comparison with Python wrapper outputs

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../src/core/types.h"
#include "../src/env/synchro_env.h"

using namespace companions;

// Binary file format constants
constexpr uint32_t PARITY_MAGIC = 0x50415249;  // "PARI"
constexpr uint32_t PARITY_VERSION = 1;

// File header (48 bytes)
struct ParityHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t env_seed;
    uint32_t action_seed;
    uint32_t num_steps;
    uint32_t num_agents;
    uint32_t rows;
    uint32_t cols;
    uint32_t num_synchro;
    uint32_t map_complexity;
    uint32_t horizon;
    uint32_t num_episodes;
};

// Write a step record to binary file
void write_step_record(
    std::ofstream& out,
    const std::vector<int>& actions,       // [num_agents * 2] movement, interact
    const std::vector<float>& observations, // [num_agents * 5 * rows * cols]
    const std::vector<float>& rewards,      // [num_agents]
    const std::vector<uint8_t>& terminals,  // [num_agents]
    uint32_t reset_seed                     // seed used for reset, 0 if no reset
) {
    out.write(reinterpret_cast<const char*>(actions.data()),
              actions.size() * sizeof(int));
    out.write(reinterpret_cast<const char*>(observations.data()),
              observations.size() * sizeof(float));
    out.write(reinterpret_cast<const char*>(rewards.data()),
              rewards.size() * sizeof(float));
    out.write(reinterpret_cast<const char*>(terminals.data()),
              terminals.size() * sizeof(uint8_t));
    out.write(reinterpret_cast<const char*>(&reset_seed), sizeof(uint32_t));
}

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --env-seed N      Environment seed (default: 42)\n"
              << "  --action-seed N   Action RNG seed (default: 123)\n"
              << "  --steps N         Number of steps (default: 1000)\n"
              << "  --rows N          Grid rows (default: 12)\n"
              << "  --cols N          Grid cols (default: 12)\n"
              << "  --agents N        Number of agents (default: 3)\n"
              << "  --synchro N       Number of synchro cells (default: 3)\n"
              << "  --complexity N    Map complexity 0-5 (default: 0)\n"
              << "  --horizon N       Episode horizon (default: 100)\n"
              << "  --output PATH     Output file path (required)\n";
}

int main(int argc, char** argv) {
    // Default parameters
    uint32_t env_seed = 42;
    uint32_t action_seed = 123;
    uint32_t num_steps = 1000;
    uint32_t rows = 12;
    uint32_t cols = 12;
    uint32_t num_agents = 3;
    uint32_t num_synchro = 3;
    uint32_t map_complexity = 0;
    uint32_t horizon = 100;
    std::string output_path;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--env-seed" && i + 1 < argc) {
            env_seed = std::stoul(argv[++i]);
        } else if (arg == "--action-seed" && i + 1 < argc) {
            action_seed = std::stoul(argv[++i]);
        } else if (arg == "--steps" && i + 1 < argc) {
            num_steps = std::stoul(argv[++i]);
        } else if (arg == "--rows" && i + 1 < argc) {
            rows = std::stoul(argv[++i]);
        } else if (arg == "--cols" && i + 1 < argc) {
            cols = std::stoul(argv[++i]);
        } else if (arg == "--agents" && i + 1 < argc) {
            num_agents = std::stoul(argv[++i]);
        } else if (arg == "--synchro" && i + 1 < argc) {
            num_synchro = std::stoul(argv[++i]);
        } else if (arg == "--complexity" && i + 1 < argc) {
            map_complexity = std::stoul(argv[++i]);
        } else if (arg == "--horizon" && i + 1 < argc) {
            horizon = std::stoul(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (output_path.empty()) {
        std::cerr << "Error: --output is required\n";
        print_usage(argv[0]);
        return 1;
    }

    // Create environment
    SynchroEnv env(rows, cols, num_agents, num_synchro,
                   map_complexity, env_seed, 0, horizon);

    // Create action RNG
    std::mt19937 action_rng(action_seed);
    std::uniform_int_distribution<int> move_dist(0, 4);
    std::uniform_int_distribution<int> interact_dist(0, 1);

    // Allocate buffers
    int obs_size = 5 * rows * cols;
    std::vector<int> actions(num_agents * 2);
    std::vector<float> observations(num_agents * obs_size);
    std::vector<float> rewards(num_agents);
    std::vector<uint8_t> terminals(num_agents);

    // Open output file
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Error: cannot open output file: " << output_path << "\n";
        return 1;
    }

    // Write header (will update num_episodes at end)
    ParityHeader header = {
        PARITY_MAGIC,
        PARITY_VERSION,
        env_seed,
        action_seed,
        num_steps,
        num_agents,
        rows,
        cols,
        num_synchro,
        map_complexity,
        horizon,
        0  // num_episodes - updated at end
    };
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Initial reset
    env.Reset(env_seed);

    // Collect initial observations (step 0)
    for (uint32_t agent = 0; agent < num_agents; agent++) {
        std::vector<float> obs;
        env.ObservationTensor(obs, agent);
        std::memcpy(observations.data() + agent * obs_size,
                    obs.data(), obs_size * sizeof(float));
    }
    std::fill(actions.begin(), actions.end(), 0);
    std::fill(rewards.begin(), rewards.end(), 0.0f);
    std::fill(terminals.begin(), terminals.end(), 0);

    write_step_record(out, actions, observations, rewards, terminals, env_seed);

    // Episode tracking
    uint32_t num_episodes = 0;

    std::cout << "Generating " << num_steps << " steps...\n";

    for (uint32_t step = 0; step < num_steps; step++) {
        // Generate reproducible actions
        std::vector<Action> cpp_actions(num_agents);
        for (uint32_t a = 0; a < num_agents; a++) {
            int movement = move_dist(action_rng);
            int interact = interact_dist(action_rng);
            cpp_actions[a] = EncodeAction(
                static_cast<MovementAction>(movement),
                static_cast<InteractAction>(interact)
            );
            actions[a * 2] = movement;
            actions[a * 2 + 1] = interact;
        }

        // Step environment
        StepResult result = env.Step(cpp_actions);

        // Collect observations
        for (uint32_t agent = 0; agent < num_agents; agent++) {
            std::vector<float> obs;
            env.ObservationTensor(obs, agent);
            std::memcpy(observations.data() + agent * obs_size,
                        obs.data(), obs_size * sizeof(float));
            rewards[agent] = static_cast<float>(result.rewards[agent]);
            terminals[agent] = result.done ? 1 : 0;
        }

        // Handle episode reset (matches wrapper's auto-reset behavior)
        uint32_t reset_seed = 0;
        if (result.done) {
            num_episodes++;
            reset_seed = 1;  // Flag that reset occurred
            env.Reset();     // Unseeded reset, matches synchro_wrapper.cc::c_step

            // Overwrite observations with post-reset state
            for (uint32_t agent = 0; agent < num_agents; agent++) {
                std::vector<float> obs;
                env.ObservationTensor(obs, agent);
                std::memcpy(observations.data() + agent * obs_size,
                            obs.data(), obs_size * sizeof(float));
            }
        }

        write_step_record(out, actions, observations, rewards, terminals, reset_seed);

        if ((step + 1) % 100 == 0) {
            std::cout << "  Step " << (step + 1) << "/" << num_steps
                      << " (episodes: " << num_episodes << ")\n";
        }
    }

    // Update header with episode count
    header.num_episodes = num_episodes;
    out.seekp(0);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.close();

    std::cout << "Done! Wrote " << output_path << "\n";
    std::cout << "  Steps: " << num_steps << "\n";
    std::cout << "  Episodes: " << num_episodes << "\n";

    return 0;
}
