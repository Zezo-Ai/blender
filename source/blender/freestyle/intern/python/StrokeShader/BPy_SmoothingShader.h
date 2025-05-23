/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup freestyle
 */

#pragma once

#include "../BPy_StrokeShader.h"

///////////////////////////////////////////////////////////////////////////////////////////

extern PyTypeObject SmoothingShader_Type;

#define BPy_SmoothingShader_Check(v) \
  (PyObject_IsInstance((PyObject *)v, (PyObject *)&SmoothingShader_Type))

/*---------------------------Python BPy_SmoothingShader structure definition----------*/
typedef struct {
  BPy_StrokeShader py_ss;
} BPy_SmoothingShader;

///////////////////////////////////////////////////////////////////////////////////////////
