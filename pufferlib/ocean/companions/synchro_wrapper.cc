// C++ implementation of extern "C" wrapper for SynchroEnv
// Bridges PufferLib's C binding to the C++ SynchroEnv class

#include "synchro.h"
#include "src/env/synchro_env.h"
#include "src/core/types.h"
#include "src/viz/renderer.h"

#include <vector>
#include <cstring>
#include <cstdlib>

extern "C" {

void synchro_init(Synchro* env) {
    // Create C++ SynchroEnv with parameters from the C struct
    // Using constructor: SynchroEnv(rows, cols, num_companions, num_synchro,
    //                               map_complexity, seed, d4_transform, horizon)
    auto* cpp_env = new companions::SynchroEnv(
        env->rows,
        env->cols,
        env->num_agents,    // num_companions
        env->num_synchro,
        env->map_complexity,
        0,                  // seed (will be set on reset)
        env->d4_transform,  // d4_transform (0-7), CCW convention
        env->horizon
    );
    env->cpp_env = static_cast<void*>(cpp_env);
    env->cumulative_reward = 0.0f;
    env->episode_steps = 0;
    env->vector_obs_size = cpp_env->VectorObservationSize();

    // Allocate render buffer (4KB should be enough for ASCII grid)
    env->render_buffer_size = 4096;
    env->render_buffer = static_cast<char*>(std::malloc(env->render_buffer_size));
    if (env->render_buffer) {
        env->render_buffer[0] = '\0';
    }
}

void c_reset(Synchro* env) {
    auto* cpp_env = static_cast<companions::SynchroEnv*>(env->cpp_env);
    cpp_env->Reset();

    // Observation size: tensor (5 channels * rows * cols) + vector
    int tensor_size = 5 * env->rows * env->cols;
    int obs_size = tensor_size + env->vector_obs_size;

    // Copy initial observations to buffer (tensor + vector per agent)
    for (int i = 0; i < env->num_agents; i++) {
        // Copy tensor observation
        std::vector<float> tensor_obs;
        cpp_env->ObservationTensor(tensor_obs, i);
        std::memcpy(env->observations + i * obs_size, tensor_obs.data(), tensor_size * sizeof(float));

        // Copy vector observation (appended after tensor)
        std::vector<float> vector_obs;
        cpp_env->VectorObservation(vector_obs, i);
        std::memcpy(env->observations + i * obs_size + tensor_size, vector_obs.data(),
                    env->vector_obs_size * sizeof(float));
    }

    // Clear terminals and rewards
    std::memset(env->terminals, 0, env->num_agents);
    std::memset(env->rewards, 0, env->num_agents * sizeof(float));

    // Reset episode tracking
    env->cumulative_reward = 0.0f;
    env->episode_steps = 0;
}

void c_reset_seed(Synchro* env, unsigned int seed) {
    auto* cpp_env = static_cast<companions::SynchroEnv*>(env->cpp_env);
    cpp_env->Reset(seed);

    // Observation size: tensor (5 channels * rows * cols) + vector
    int tensor_size = 5 * env->rows * env->cols;
    int obs_size = tensor_size + env->vector_obs_size;

    // Copy initial observations to buffer (tensor + vector per agent)
    for (int i = 0; i < env->num_agents; i++) {
        // Copy tensor observation
        std::vector<float> tensor_obs;
        cpp_env->ObservationTensor(tensor_obs, i);
        std::memcpy(env->observations + i * obs_size, tensor_obs.data(), tensor_size * sizeof(float));

        // Copy vector observation (appended after tensor)
        std::vector<float> vector_obs;
        cpp_env->VectorObservation(vector_obs, i);
        std::memcpy(env->observations + i * obs_size + tensor_size, vector_obs.data(),
                    env->vector_obs_size * sizeof(float));
    }

    // Clear terminals and rewards
    std::memset(env->terminals, 0, env->num_agents);
    std::memset(env->rewards, 0, env->num_agents * sizeof(float));

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

    // Observation size: tensor (5 channels * rows * cols) + vector
    int tensor_size = 5 * env->rows * env->cols;
    int obs_size = tensor_size + env->vector_obs_size;

    // Copy observations (tensor + vector per agent)
    for (int i = 0; i < env->num_agents; i++) {
        // Copy tensor observation
        std::vector<float> tensor_obs;
        cpp_env->ObservationTensor(tensor_obs, i);
        std::memcpy(env->observations + i * obs_size, tensor_obs.data(), tensor_size * sizeof(float));

        // Copy vector observation (appended after tensor)
        std::vector<float> vector_obs;
        cpp_env->VectorObservation(vector_obs, i);
        std::memcpy(env->observations + i * obs_size + tensor_size, vector_obs.data(),
                    env->vector_obs_size * sizeof(float));
    }

    // Set terminals (all agents share same done state)
    unsigned char done = result.done ? 1 : 0;
    for (int i = 0; i < env->num_agents; i++) {
        env->terminals[i] = done;
    }

    // Update log on episode completion
    if (result.done) {
        env->log.episode_return += env->cumulative_reward;
        env->log.episode_length += static_cast<float>(env->episode_steps);
        env->log.success_rate += cpp_env->IsSuccess() ? 1.0f : 0.0f;
        env->log.agents_on_synchro += static_cast<float>(cpp_env->NumAgentsOnSynchroCells());
        env->log.n += 1.0f;

        // Auto-reset for next episode
        cpp_env->Reset();
        for (int i = 0; i < env->num_agents; i++) {
            // Copy tensor observation
            std::vector<float> tensor_obs;
            cpp_env->ObservationTensor(tensor_obs, i);
            std::memcpy(env->observations + i * obs_size, tensor_obs.data(), tensor_size * sizeof(float));

            // Copy vector observation (appended after tensor)
            std::vector<float> vector_obs;
            cpp_env->VectorObservation(vector_obs, i);
            std::memcpy(env->observations + i * obs_size + tensor_size, vector_obs.data(),
                        env->vector_obs_size * sizeof(float));
        }
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
