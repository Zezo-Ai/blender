/* SPDX-FileCopyrightText: 2009-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __CUBES_H__
#define __CUBES_H__

#include "MEM_guardedalloc.h"

#include "marching_cubes_table.h"

/* simple wrapper for auto-generated marching cubes data */
class Cubes {
 public:
  /// Get number of triangles
  int getNumTriangle(int mask)
  {
    return marching_cubes_numtri[mask];
  }

  /// Get a triangle
  void getTriangle(int mask, int index, int indices[3])
  {
    for (int i = 0; i < 3; i++) {
      indices[i] = marching_cubes_tris[mask][index][i];
    }
  }

  MEM_CXX_CLASS_ALLOC_FUNCS("DUALCON:Cubes")
};

#endif /* __CUBES_H__ */
