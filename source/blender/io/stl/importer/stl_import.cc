/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup stl
 */

#include <cstdio>

#include "BKE_context.hh"
#include "BKE_layer.hh"
#include "BKE_mesh.hh"
#include "BKE_object.hh"
#include "BKE_report.hh"

#include "DNA_collection_types.h"
#include "DNA_layer_types.h"
#include "DNA_scene_types.h"

#include "BLI_fileops.hh"
#include "BLI_math_matrix.h"
#include "BLI_math_rotation.h"
#include "BLI_memory_utils.hh"
#include "BLI_string.h"

#include "DNA_object_types.h"

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"

#include "stl_data.hh"
#include "stl_import.hh"
#include "stl_import_ascii_reader.hh"
#include "stl_import_binary_reader.hh"

#include "CLG_log.h"
static CLG_LogRef LOG = {"io.stl"};

namespace blender::io::stl {

void stl_import_report_error(FILE *file)
{
  CLOG_ERROR(&LOG, "STL Importer: failed to read file");
  if (feof(file)) {
    CLOG_ERROR(&LOG, "End of file reached");
  }
  else if (ferror(file)) {
    perror("Error");
  }
}

Mesh *read_stl_file(const STLImportParams &import_params)
{
  FILE *file = BLI_fopen(import_params.filepath, "rb");
  if (!file) {
    CLOG_ERROR(&LOG, "Failed to open STL file:'%s'.", import_params.filepath);
    BKE_reportf(import_params.reports,
                RPT_ERROR,
                "STL Import: Cannot open file '%s'",
                import_params.filepath);
    return nullptr;
  }
  BLI_SCOPED_DEFER([&]() { fclose(file); });

  /* Detect STL file type by comparing file size with expected file size,
   * could check if file starts with "solid", but some files do not adhere,
   * this is the same as the old Python importer.
   */
  uint32_t num_tri = 0;
  size_t file_size = BLI_file_size(import_params.filepath);
  fseek(file, BINARY_HEADER_SIZE, SEEK_SET);
  if (fread(&num_tri, sizeof(uint32_t), 1, file) != 1) {
    stl_import_report_error(file);
    BKE_reportf(import_params.reports,
                RPT_ERROR,
                "STL Import: Failed to read file '%s'",
                import_params.filepath);
    return nullptr;
  }
  bool is_ascii_stl = (file_size != (BINARY_HEADER_SIZE + 4 + BINARY_STRIDE * num_tri));

  Mesh *mesh = is_ascii_stl ?
                   read_stl_ascii(import_params.filepath, import_params.use_facet_normal) :
                   read_stl_binary(file, import_params.use_facet_normal);

  if (mesh == nullptr) {
    CLOG_ERROR(&LOG, "STL Importer: Failed to import mesh '%s'", import_params.filepath);
    BKE_reportf(import_params.reports,
                RPT_ERROR,
                "STL Import: Failed to import mesh from file '%s'",
                import_params.filepath);
    return nullptr;
  }

  if (import_params.use_mesh_validate) {
    bool verbose_validate = false;
#ifndef NDEBUG
    verbose_validate = true;
#endif
    BKE_mesh_validate(mesh, verbose_validate, false);
  }

  return mesh;
}

void importer_main(const bContext *C, const STLImportParams &import_params)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  importer_main(bmain, scene, view_layer, import_params);
}

void importer_main(Main *bmain,
                   Scene *scene,
                   ViewLayer *view_layer,
                   const STLImportParams &import_params)
{
  /* Name used for both mesh and object. */
  char ob_name[FILE_MAX];
  STRNCPY(ob_name, BLI_path_basename(import_params.filepath));
  BLI_path_extension_strip(ob_name);

  Mesh *mesh = read_stl_file(import_params);
  if (!mesh) {
    return;
  }

  Mesh *mesh_in_main = BKE_mesh_add(bmain, ob_name);
  BKE_mesh_nomain_to_mesh(mesh, mesh_in_main, nullptr);
  BKE_view_layer_base_deselect_all(scene, view_layer);
  LayerCollection *lc = BKE_layer_collection_get_active(view_layer);
  Object *obj = BKE_object_add_only_object(bmain, OB_MESH, ob_name);
  obj->data = mesh_in_main;
  BKE_collection_object_add(bmain, lc->collection, obj);
  BKE_view_layer_synced_ensure(scene, view_layer);
  Base *base = BKE_view_layer_base_find(view_layer, obj);
  BKE_view_layer_base_select_and_set_active(view_layer, base);

  float global_scale = import_params.global_scale;
  if ((scene->unit.system != USER_UNIT_NONE) && import_params.use_scene_unit) {
    global_scale /= scene->unit.scale_length;
  }
  float scale_vec[3] = {global_scale, global_scale, global_scale};
  float obmat3x3[3][3];
  unit_m3(obmat3x3);
  float obmat4x4[4][4];
  unit_m4(obmat4x4);
  /* +Y-forward and +Z-up are the Blender's default axis settings. */
  mat3_from_axis_conversion(
      IO_AXIS_Y, IO_AXIS_Z, import_params.forward_axis, import_params.up_axis, obmat3x3);
  copy_m4_m3(obmat4x4, obmat3x3);
  rescale_m4(obmat4x4, scale_vec);
  BKE_object_apply_mat4(obj, obmat4x4, true, false);

  DEG_id_tag_update(&lc->collection->id, ID_RECALC_SYNC_TO_EVAL);
  int flags = ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_ANIMATION |
              ID_RECALC_BASE_FLAGS;
  DEG_id_tag_update_ex(bmain, &obj->id, flags);
  DEG_id_tag_update(&scene->id, ID_RECALC_BASE_FLAGS);
  DEG_relations_tag_update(bmain);
}
}  // namespace blender::io::stl
