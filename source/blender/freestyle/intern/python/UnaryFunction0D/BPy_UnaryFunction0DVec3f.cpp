/* SPDX-FileCopyrightText: 2008-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup freestyle
 */

#include "BPy_UnaryFunction0DVec3f.h"

#include "../BPy_Convert.h"
#include "../Iterator/BPy_Interface0DIterator.h"

#include "UnaryFunction0D_Vec3f/BPy_VertexOrientation3DF0D.h"

using namespace Freestyle;

///////////////////////////////////////////////////////////////////////////////////////////

//-------------------MODULE INITIALIZATION--------------------------------

int UnaryFunction0DVec3f_Init(PyObject *module)
{
  if (module == nullptr) {
    return -1;
  }

  if (PyType_Ready(&UnaryFunction0DVec3f_Type) < 0) {
    return -1;
  }
  PyModule_AddObjectRef(module, "UnaryFunction0DVec3f", (PyObject *)&UnaryFunction0DVec3f_Type);

  if (PyType_Ready(&VertexOrientation3DF0D_Type) < 0) {
    return -1;
  }
  PyModule_AddObjectRef(
      module, "VertexOrientation3DF0D", (PyObject *)&VertexOrientation3DF0D_Type);

  return 0;
}

//------------------------INSTANCE METHODS ----------------------------------

PyDoc_STRVAR(
    /* Wrap. */
    UnaryFunction0DVec3f___doc__,
    "Class hierarchy: :class:`UnaryFunction0D` > :class:`UnaryFunction0DVec3f`\n"
    "\n"
    "Base class for unary functions (functors) that work on\n"
    ":class:`Interface0DIterator` and return a 3D vector.\n"
    "\n"
    ".. method:: __init__()\n"
    "\n"
    "   Default constructor.\n");

static int UnaryFunction0DVec3f___init__(BPy_UnaryFunction0DVec3f *self,
                                         PyObject *args,
                                         PyObject *kwds)
{
  static const char *kwlist[] = {nullptr};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "", (char **)kwlist)) {
    return -1;
  }
  self->uf0D_vec3f = new UnaryFunction0D<Vec3f>();
  self->uf0D_vec3f->py_uf0D = (PyObject *)self;
  return 0;
}

static void UnaryFunction0DVec3f___dealloc__(BPy_UnaryFunction0DVec3f *self)
{
  delete self->uf0D_vec3f;
  UnaryFunction0D_Type.tp_dealloc((PyObject *)self);
}

static PyObject *UnaryFunction0DVec3f___repr__(BPy_UnaryFunction0DVec3f *self)
{
  return PyUnicode_FromFormat("type: %s - address: %p", Py_TYPE(self)->tp_name, self->uf0D_vec3f);
}

static PyObject *UnaryFunction0DVec3f___call__(BPy_UnaryFunction0DVec3f *self,
                                               PyObject *args,
                                               PyObject *kwds)
{
  static const char *kwlist[] = {"it", nullptr};
  PyObject *obj;

  if (!PyArg_ParseTupleAndKeywords(
          args, kwds, "O!", (char **)kwlist, &Interface0DIterator_Type, &obj))
  {
    return nullptr;
  }

  if (typeid(*(self->uf0D_vec3f)) == typeid(UnaryFunction0D<Vec3f>)) {
    PyErr_SetString(PyExc_TypeError, "__call__ method not properly overridden");
    return nullptr;
  }
  if (self->uf0D_vec3f->operator()(*(((BPy_Interface0DIterator *)obj)->if0D_it)) < 0) {
    if (!PyErr_Occurred()) {
      string class_name(Py_TYPE(self)->tp_name);
      PyErr_SetString(PyExc_RuntimeError, (class_name + " __call__ method failed").c_str());
    }
    return nullptr;
  }
  return Vector_from_Vec3f(self->uf0D_vec3f->result);
}

/*-----------------------BPy_UnaryFunction0DVec3f type definition ------------------------------*/

PyTypeObject UnaryFunction0DVec3f_Type = {
    /*ob_base*/ PyVarObject_HEAD_INIT(nullptr, 0)
    /*tp_name*/ "UnaryFunction0DVec3f",
    /*tp_basicsize*/ sizeof(BPy_UnaryFunction0DVec3f),
    /*tp_itemsize*/ 0,
    /*tp_dealloc*/ (destructor)UnaryFunction0DVec3f___dealloc__,
    /*tp_vectorcall_offset*/ 0,
    /*tp_getattr*/ nullptr,
    /*tp_setattr*/ nullptr,
    /*tp_as_async*/ nullptr,
    /*tp_repr*/ (reprfunc)UnaryFunction0DVec3f___repr__,
    /*tp_as_number*/ nullptr,
    /*tp_as_sequence*/ nullptr,
    /*tp_as_mapping*/ nullptr,
    /*tp_hash*/ nullptr,
    /*tp_call*/ (ternaryfunc)UnaryFunction0DVec3f___call__,
    /*tp_str*/ nullptr,
    /*tp_getattro*/ nullptr,
    /*tp_setattro*/ nullptr,
    /*tp_as_buffer*/ nullptr,
    /*tp_flags*/ Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    /*tp_doc*/ UnaryFunction0DVec3f___doc__,
    /*tp_traverse*/ nullptr,
    /*tp_clear*/ nullptr,
    /*tp_richcompare*/ nullptr,
    /*tp_weaklistoffset*/ 0,
    /*tp_iter*/ nullptr,
    /*tp_iternext*/ nullptr,
    /*tp_methods*/ nullptr,
    /*tp_members*/ nullptr,
    /*tp_getset*/ nullptr,
    /*tp_base*/ &UnaryFunction0D_Type,
    /*tp_dict*/ nullptr,
    /*tp_descr_get*/ nullptr,
    /*tp_descr_set*/ nullptr,
    /*tp_dictoffset*/ 0,
    /*tp_init*/ (initproc)UnaryFunction0DVec3f___init__,
    /*tp_alloc*/ nullptr,
    /*tp_new*/ nullptr,
};

///////////////////////////////////////////////////////////////////////////////////////////
