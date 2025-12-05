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
