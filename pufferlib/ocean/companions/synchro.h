// C wrapper header for SynchroEnv
// Provides extern "C" interface for PufferLib binding

#ifndef SYNCHRO_H
#define SYNCHRO_H

#ifdef __cplusplus
extern "C" {
#endif

// The RL task (Synchro::task)
#define SYNCHRO_TASK_SYNCHRO 0  // Put every companion on a synchro cell (SynchroLens)
#define SYNCHRO_TASK_REVIVE 1   // Get the downed ally up: each episode starts with one
                                // companion down, under the ReviveLens (num_agents >= 2)

// Log struct - all floats, 'n' must be last (required by env_binding.h)
typedef struct {
    float episode_return;
    float episode_length;
    float success_rate;
    float agents_on_synchro;
    float n;
} Log;

// C-compatible environment struct
typedef struct {
    Log log;                       // Required first field
    float* observations;           // [num_agents, tensor_size + 12] flattened observations
                                   // tensor: 5 * rows * cols, vector: 12 features
    int* actions;                  // [num_agents, 2] MultiDiscrete actions
    float* rewards;                // [num_agents]
    unsigned char* terminals;      // [num_agents] 1: the episode ended (success, horizon, team down)
    unsigned char* truncations;    // [num_agents] 1: the episode was interrupted (a down:
                                   // EndReason::Interrupted); may be NULL (not written)

    // Environment configuration
    int num_agents;
    int rows;
    int cols;
    int num_synchro;
    int map_complexity;
    int horizon;
    int d4_transform;              // D4 symmetry (0-7), CCW convention
    int overfit;                   // If true, always reset to same seed (for equivariance testing)
    int vector_obs_size;           // Size of vector observation (appended to tensor)
    int task;                      // SYNCHRO_TASK_SYNCHRO or SYNCHRO_TASK_REVIVE
    float down_cost;               // Added to every agent's reward once per new down: finite,
                                   // in [-1e6, 0] (BaseEnv::SetDownCost; synchro.py's default -0.5)

    // Internal state
    void* cpp_env;                 // Opaque pointer to companions::SynchroEnv
    float cumulative_reward;       // Track episode reward
    int episode_steps;             // Track episode length
    unsigned int seed;             // Current seed for deterministic episode resets

    // Rendering
    char* render_buffer;           // Buffer for ASCII rendering output
    int render_buffer_size;        // Size of the render buffer

    // Why synchro_init failed ("" when it did not)
    char error[256];
} Synchro;

// Lifecycle functions
// 0 on success. Non-zero on a config it refuses (a bad task, the revive task
// with fewer than 2 companions, a bad down_cost, a config the env rejects):
// `error` says why, and no env is left (cpp_env NULL; c_close stays safe).
int synchro_init(Synchro* env);

// RL interface (required by env_binding.h)
void c_reset(Synchro* env);
void c_reset_seed(Synchro* env, unsigned int seed);  // Deterministic reset
void c_step(Synchro* env);
void c_render(Synchro* env);
void c_close(Synchro* env);

#ifdef __cplusplus
}
#endif

#endif // SYNCHRO_H
