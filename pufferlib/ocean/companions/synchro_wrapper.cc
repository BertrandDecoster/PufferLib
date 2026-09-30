// C++ implementation of extern "C" wrapper for SynchroEnv
// Bridges PufferLib's C binding to the C++ SynchroEnv class

#include "synchro.h"
#include "src/env/revive_lens.h"
#include "src/env/synchro_env.h"
#include "src/core/types.h"
#include "src/viz/renderer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// synchro_init's refusal: `error` says why, no env is left
int init_error(Synchro* env, const std::string& message) {
    std::snprintf(env->error, sizeof(env->error), "%s", message.c_str());
    env->cpp_env = nullptr;
    return -1;
}

// The C++ env for the struct's config and task, or an exception saying why not
std::unique_ptr<companions::SynchroEnv> make_env(const Synchro* env) {
    if (env->task != SYNCHRO_TASK_SYNCHRO && env->task != SYNCHRO_TASK_REVIVE) {
        throw std::invalid_argument("task must be " + std::to_string(SYNCHRO_TASK_SYNCHRO) +
                                    " (synchro) or " + std::to_string(SYNCHRO_TASK_REVIVE) +
                                    " (revive), got " + std::to_string(env->task));
    }
    if (env->task == SYNCHRO_TASK_REVIVE && env->num_agents < 2) {
        throw std::invalid_argument("the revive task needs num_agents >= 2, got " +
                                    std::to_string(env->num_agents));
    }
    // Using constructor: SynchroEnv(rows, cols, num_companions, num_synchro,
    //                               map_complexity, seed, d4_transform, horizon)
    // (it Resets and installs the SynchroLens)
    auto cpp_env = std::make_unique<companions::SynchroEnv>(
        env->rows,
        env->cols,
        env->num_agents,    // num_companions
        env->num_synchro,
        env->map_complexity,
        0,                  // seed (will be set on reset)
        env->d4_transform,  // d4_transform (0-7), CCW convention
        env->horizon
    );
    if (!cpp_env->SetDownCost(static_cast<double>(env->down_cost))) {
        char message[128];
        std::snprintf(message, sizeof(message), "down_cost must be finite and in [%g, 0], got %g",
                      companions::BaseEnv::kMinDownCost, static_cast<double>(env->down_cost));
        throw std::invalid_argument(message);
    }
    if (env->task == SYNCHRO_TASK_REVIVE) {
        // In this order: the ReviveLens needs someone down (CanOperateOn).
        // Every later Reset keeps both: it downs one companion again, and the
        // lens's OnNewEpisode makes every downed ally its goal.
        cpp_env->SetStartDowned(1);
        cpp_env->Reset();
        if (!cpp_env->SetTaskLens(std::make_unique<companions::ReviveLens>())) {
            throw std::runtime_error("the revive task: the ReviveLens was refused");
        }
    }
    return cpp_env;
}

}  // namespace

extern "C" {

// Clears the terminals, truncations and rewards (a new episode)
static void clear_step_outputs(Synchro* env) {
    std::memset(env->terminals, 0, env->num_agents);
    if (env->truncations) {
        std::memset(env->truncations, 0, env->num_agents);
    }
    std::memset(env->rewards, 0, env->num_agents * sizeof(float));
}

// Helper function to write observations for all agents (eliminates code duplication)
static void write_observations(Synchro* env) {
    auto* cpp_env = static_cast<companions::SynchroEnv*>(env->cpp_env);
    int tensor_size = 5 * env->rows * env->cols;
    int obs_size = tensor_size + env->vector_obs_size;

    for (int i = 0; i < env->num_agents; i++) {
        cpp_env->WriteObservationTensor(env->observations + i * obs_size, i);
        cpp_env->WriteVectorObservation(env->observations + i * obs_size + tensor_size, i);
    }
}

int synchro_init(Synchro* env) {
    // Initialize log to zero (required since we use env_init, not vec_init)
    std::memset(&env->log, 0, sizeof(Log));
    env->error[0] = '\0';
    env->render_buffer = nullptr;

    // Create the C++ SynchroEnv from the C struct. No exception crosses into C.
    companions::SynchroEnv* cpp_env = nullptr;
    try {
        cpp_env = make_env(env).release();
    } catch (const std::exception& e) {
        return init_error(env, e.what());
    } catch (...) {
        return init_error(env, "the env could not be created");
    }
    env->cpp_env = static_cast<void*>(cpp_env);
    env->cumulative_reward = 0.0f;
    env->episode_steps = 0;
    env->vector_obs_size = cpp_env->VectorObservationSize();

    // Validate vector observation size matches Python's VECTOR_OBS_SIZE (12)
    // If this fails, update VECTOR_OBS_SIZE in synchro.py
    static_assert(companions::BaseEnv::kVectorObsBaseSize == 12,
                  "update VECTOR_OBS_SIZE in synchro.py");
    if (env->vector_obs_size != 12) {
        std::fprintf(stderr, "ERROR: Vector obs size mismatch! C++=%d, expected=12. "
                     "Update VECTOR_OBS_SIZE in synchro.py\n", env->vector_obs_size);
    }

    // Allocate render buffer dynamically based on grid size
    // Estimate: ~3 chars per cell + newlines + ANSI codes + header
    env->render_buffer_size = (env->rows * env->cols * 3) + (env->rows * 10) + 256;
    env->render_buffer = static_cast<char*>(std::malloc(env->render_buffer_size));
    if (env->render_buffer) {
        env->render_buffer[0] = '\0';
    }
    return 0;
}

void c_reset(Synchro* env) {
    auto* cpp_env = static_cast<companions::SynchroEnv*>(env->cpp_env);

    // Use stored seed for deterministic behavior, then advance for next episode
    cpp_env->Reset(env->seed++);

    // Write observations directly to buffer (zero-copy)
    write_observations(env);

    clear_step_outputs(env);

    // Reset episode tracking
    env->cumulative_reward = 0.0f;
    env->episode_steps = 0;
}

void c_reset_seed(Synchro* env, unsigned int seed) {
    auto* cpp_env = static_cast<companions::SynchroEnv*>(env->cpp_env);
    env->seed = seed + 1;  // Set seed for subsequent auto-resets
    cpp_env->Reset(seed);

    // Write observations directly to buffer (zero-copy)
    write_observations(env);

    clear_step_outputs(env);

    // Reset episode tracking
    env->cumulative_reward = 0.0f;
    env->episode_steps = 0;
}

void c_step(Synchro* env) {
    auto* cpp_env = static_cast<companions::SynchroEnv*>(env->cpp_env);

    // Decode MultiDiscrete actions [movement, interact] per agent
    // actions buffer layout: [agent0_movement, agent0_interact, agent1_movement, agent1_interact, ...]
    std::vector<companions::Action> actions(env->num_agents);
    for (int i = 0; i < env->num_agents; i++) {
        int movement = env->actions[i * 2];      // [0-4]: Stay, Up, Down, Left, Right
        int interact = env->actions[i * 2 + 1];  // [0-1]: None, Attack
        actions[i] = companions::EncodeAction(
            static_cast<companions::MovementAction>(movement),
            static_cast<companions::InteractAction>(interact)
        );
    }

    // Step the C++ environment
    companions::StepResult result = cpp_env->Step(actions);

    // Copy rewards (all agents get same reward in SynchroEnv)
    float total_reward = 0.0f;
    for (int i = 0; i < env->num_agents; i++) {
        env->rewards[i] = static_cast<float>(result.rewards[i]);
        total_reward += env->rewards[i];
    }
    env->cumulative_reward += total_reward / env->num_agents;
    env->episode_steps++;

    // Write observations directly to buffer (zero-copy)
    write_observations(env);

    // Set terminals and truncations (all agents share the same done state).
    // An interrupted task (a down: EndReason::Interrupted, provisional) is a
    // truncation; any other end (success, horizon, team down) a terminal.
    const bool interrupted = result.done && cpp_env->IsInterrupted();
    const unsigned char terminal = (result.done && !interrupted) ? 1 : 0;
    const unsigned char truncation = interrupted ? 1 : 0;
    for (int i = 0; i < env->num_agents; i++) {
        env->terminals[i] = terminal;
        if (env->truncations) {
            env->truncations[i] = truncation;
        }
    }

    // Update log on episode completion
    if (result.done) {
        env->log.episode_return += env->cumulative_reward;
        env->log.episode_length += static_cast<float>(env->episode_steps);
        env->log.success_rate += cpp_env->IsSuccess() ? 1.0f : 0.0f;
        env->log.agents_on_synchro += static_cast<float>(cpp_env->NumAgentsOnSynchroCells());
        env->log.n += 1.0f;

        // Auto-reset for next episode (the revive task: it keeps the start
        // down and the ReviveLens, re-targeted to the new episode's body)
        // In overfit mode, always reset to same seed (for D4 equivariance testing)
        if (env->overfit) {
            cpp_env->Reset(env->seed);  // Don't increment - always same game
        } else {
            cpp_env->Reset(env->seed++);  // Normal behavior - different games
        }
        write_observations(env);
        env->cumulative_reward = 0.0f;
        env->episode_steps = 0;
    }
}

void c_render(Synchro* env) {
    if (!env->cpp_env || !env->render_buffer) {
        return;
    }

    auto* cpp_env = static_cast<companions::SynchroEnv*>(env->cpp_env);
    companions::Renderer renderer;
    std::string ascii = renderer.RenderAscii(*cpp_env);

    // Copy to buffer (truncate if too long)
    size_t copy_len = std::min(ascii.size(), static_cast<size_t>(env->render_buffer_size - 1));
    std::memcpy(env->render_buffer, ascii.c_str(), copy_len);
    env->render_buffer[copy_len] = '\0';
}

void c_close(Synchro* env) {
    if (env->cpp_env) {
        auto* cpp_env = static_cast<companions::SynchroEnv*>(env->cpp_env);
        delete cpp_env;
        env->cpp_env = nullptr;
    }
    if (env->render_buffer) {
        std::free(env->render_buffer);
        env->render_buffer = nullptr;
    }
}

}  // extern "C"
