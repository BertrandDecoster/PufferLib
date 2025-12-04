// PufferLib binding for Companions SynchroEnv

#include "synchro.h"

#define Env Synchro
#include "../env_binding.h"

static int my_init(Env* env, PyObject* args, PyObject* kwargs) {
    env->rows = (int)unpack(kwargs, "rows");
    env->cols = (int)unpack(kwargs, "cols");
    env->num_agents = (int)unpack(kwargs, "num_agents");
    env->num_synchro = (int)unpack(kwargs, "num_synchro");
    env->map_complexity = (int)unpack(kwargs, "map_complexity");
    env->horizon = (int)unpack(kwargs, "horizon");

    synchro_init(env);
    return 0;
}

static int my_log(PyObject* dict, Log* log) {
    assign_to_dict(dict, "episode_return", log->episode_return);
    assign_to_dict(dict, "episode_length", log->episode_length);
    assign_to_dict(dict, "success_rate", log->success_rate);
    assign_to_dict(dict, "agents_on_synchro", log->agents_on_synchro);
    return 0;
}
