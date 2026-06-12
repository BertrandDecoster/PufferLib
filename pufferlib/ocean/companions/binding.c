// PufferLib binding for Companions SynchroEnv

#include <Python.h>
#include "synchro.h"

#define Env Synchro

// Custom method: render environment and return ASCII string
static PyObject* env_render_string(PyObject* self, PyObject* args) {
    if (PyTuple_Size(args) != 1) {
        PyErr_SetString(PyExc_TypeError, "env_render_string requires 1 argument (env_handle)");
        return NULL;
    }

    PyObject* handle_obj = PyTuple_GetItem(args, 0);
    if (!PyObject_TypeCheck(handle_obj, &PyLong_Type)) {
        PyErr_SetString(PyExc_TypeError, "env_handle must be an integer");
        return NULL;
    }
    Env* env = (Env*)PyLong_AsVoidPtr(handle_obj);
    if (!env) {
        PyErr_SetString(PyExc_ValueError, "Invalid env handle");
        return NULL;
    }

    c_render(env);

    if (env->render_buffer) {
        return PyUnicode_FromString(env->render_buffer);
    }
    return PyUnicode_FromString("");
}

// Custom method: seeded reset for deterministic parity testing
static PyObject* env_reset_seed(PyObject* self, PyObject* args) {
    if (PyTuple_Size(args) != 2) {
        PyErr_SetString(PyExc_TypeError, "env_reset_seed requires 2 arguments (env_handle, seed)");
        return NULL;
    }

    PyObject* handle_obj = PyTuple_GetItem(args, 0);
    if (!PyObject_TypeCheck(handle_obj, &PyLong_Type)) {
        PyErr_SetString(PyExc_TypeError, "env_handle must be an integer");
        return NULL;
    }
    Env* env = (Env*)PyLong_AsVoidPtr(handle_obj);
    if (!env) {
        PyErr_SetString(PyExc_ValueError, "Invalid env handle");
        return NULL;
    }

    PyObject* seed_obj = PyTuple_GetItem(args, 1);
    if (!PyObject_TypeCheck(seed_obj, &PyLong_Type)) {
        PyErr_SetString(PyExc_TypeError, "seed must be an integer");
        return NULL;
    }
    unsigned int seed = (unsigned int)PyLong_AsUnsignedLong(seed_obj);

    c_reset_seed(env, seed);
    Py_RETURN_NONE;
}

#define MY_METHODS \
    {"env_render_string", env_render_string, METH_VARARGS, "Render env and return ASCII string"}, \
    {"env_reset_seed", env_reset_seed, METH_VARARGS, "Reset with specific seed for deterministic replay"}

#include "../env_binding.h"

#define UNPACK_INT(dst, key) do { \
    dst = (int)unpack(kwargs, key); \
    if (PyErr_Occurred()) return -1; \
} while(0)


static int my_init(Env* env, PyObject* args, PyObject* kwargs) {
    UNPACK_INT(env->rows, "rows");
    UNPACK_INT(env->cols, "cols");
    UNPACK_INT(env->num_agents, "num_agents");
    UNPACK_INT(env->num_synchro, "num_synchro");
    UNPACK_INT(env->map_complexity, "map_complexity");
    UNPACK_INT(env->horizon, "horizon");
    UNPACK_INT(env->d4_transform, "d4_transform");
    UNPACK_INT(env->overfit, "overfit");
    UNPACK_INT(env->expected_obs_size, "expected_obs_size");
    UNPACK_INT(env->seed, "seed");

    // synchro_init fails hard if the Python-declared expected_obs_size does
    // not match the C++-computed observation size. env_init/shared checks
    // PyErr_Occurred() after my_init, so the exception propagates to Python.
    if (synchro_init(env) != 0) {
        PyErr_Format(PyExc_ValueError,
                     "Observation size contract violation: Python allocated "
                     "%d floats per agent but the C++ env produces a "
                     "different size (see stderr). Update NUM_CHANNELS / "
                     "VECTOR_OBS_SIZE in synchro.py.",
                     env->expected_obs_size);
        return -1;
    }
    return 0;
}

static int my_log(PyObject* dict, Log* log) {
    assign_to_dict(dict, "episode_return", log->episode_return);
    assign_to_dict(dict, "episode_length", log->episode_length);
    assign_to_dict(dict, "success_rate", log->success_rate);
    assign_to_dict(dict, "agents_on_synchro", log->agents_on_synchro);
    return 0;
}
