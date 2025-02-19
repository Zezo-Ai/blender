/* SPDX-FileCopyrightText: 2025 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#ifdef GPU_SHADER
#  pragma once
#  include "gpu_glsl_cpp_stubs.hh"

#  include "draw_subdiv_shader_shared.hh"
#endif

#include "gpu_shader_create_info.hh"

#include "draw_subdiv_defines.hh"

GPU_SHADER_CREATE_INFO(subdiv_base)
LOCAL_GROUP_SIZE(SUBDIV_GROUP_SIZE)
TYPEDEF_SOURCE("draw_subdiv_shader_shared.hh")
UNIFORM_BUF(SHADER_DATA_BUF_SLOT, DRWSubdivUboStorage, shader_data)
GPU_SHADER_CREATE_END()

/* -------------------------------------------------------------------- */
/** \name Normals Finalize
 * \{ */

GPU_SHADER_CREATE_INFO(subdiv_normals_finalize)
DO_STATIC_COMPILATION()
STORAGE_BUF(NORMALS_FINALIZE_VERTEX_NORMALS_BUF_SLOT, READ, vec3, vertex_normals[])
STORAGE_BUF(NORMALS_FINALIZE_VERTEX_LOOP_MAP_BUF_SLOT, READ, uint, vert_loop_map[])
STORAGE_BUF(NORMALS_FINALIZE_POS_NOR_BUF_SLOT, READ_WRITE, PosNorLoop, pos_nor[])
COMPUTE_SOURCE("subdiv_normals_finalize_comp.glsl")
ADDITIONAL_INFO(subdiv_base)
GPU_SHADER_CREATE_END()

/* Meshes can have (custom) split normals as loop attribute. */
GPU_SHADER_CREATE_INFO(subdiv_custom_normals_finalize)
DO_STATIC_COMPILATION()
DEFINE("CUSTOM_NORMALS")
STORAGE_BUF(NORMALS_FINALIZE_CUSTOM_NORMALS_BUF_SLOT, READ, CustomNormal, custom_normals[])
STORAGE_BUF(NORMALS_FINALIZE_POS_NOR_BUF_SLOT, READ_WRITE, PosNorLoop, pos_nor[])
COMPUTE_SOURCE("subdiv_normals_finalize_comp.glsl")
ADDITIONAL_INFO(subdiv_base)
GPU_SHADER_CREATE_END()

/** \} */
