// C wrapper header for SynchroEnv
// Provides extern "C" interface for PufferLib binding

#ifndef SYNCHRO_H
#define SYNCHRO_H

#ifdef __cplusplus
extern "C" {
#endif

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
    float* observations;           // [num_agents, 8] vector observations
    int* actions;                  // [num_agents, 2] MultiDiscrete actions
    float* rewards;                // [num_agents]
    unsigned char* terminals;      // [num_agents]

    // Environment configuration
    int num_agents;
    int rows;
    int cols;
    int num_synchro;
    int map_complexity;
    int horizon;
    int d4_transform;              // D4 symmetry (0-7), CCW convention

    // Internal state
    void* cpp_env;                 // Opaque pointer to companions::SynchroEnv
    float cumulative_reward;       // Track episode reward
    int episode_steps;             // Track episode length

    // Rendering
    char* render_buffer;           // Buffer for ASCII rendering output
    int render_buffer_size;        // Size of the render buffer
} Synchro;

// Lifecycle functions
void synchro_init(Synchro* env);

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
