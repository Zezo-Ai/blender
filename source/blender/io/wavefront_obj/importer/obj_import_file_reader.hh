/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup obj
 */

#pragma once

#include "IO_wavefront_obj.hh"

#include "BLI_map.hh"
#include "BLI_vector.hh"

#include "obj_import_objects.hh"

namespace blender::io::obj {

struct MTLMaterial;

/* NOTE: the OBJ parser implementation is planned to get fairly large changes "soon",
 * so don't read too much into current implementation... */
class OBJParser {
 private:
  const OBJImportParams &import_params_;
  FILE *obj_file_;
  Vector<std::string> mtl_libraries_;
  size_t read_buffer_size_;

 public:
  /**
   * Open OBJ file at the path given in import parameters.
   */
  OBJParser(const OBJImportParams &import_params, size_t read_buffer_size);
  ~OBJParser();

  /**
   * Read the OBJ file line by line and create OBJ Geometry instances. Also store all the vertex
   * and UV vertex coordinates in a struct accessible by all objects.
   */
  void parse(Vector<std::unique_ptr<Geometry>> &r_all_geometries,
             GlobalVertices &r_global_vertices);
  /**
   * Return a list of all material library filepaths referenced by the OBJ file.
   */
  Span<std::string> mtl_libraries() const;

 private:
  void add_mtl_library(StringRef path);
  void add_default_mtl_library();
  size_t parse_string_buffer(StringRef &buffer_str,
                             Vector<std::unique_ptr<Geometry>> &r_all_geometries,
                             GlobalVertices &r_global_vertices,
                             Geometry *&curr_geom,
                             bool &state_shaded_smooth,
                             std::string &state_group_name,
                             int &state_group_index,
                             std::string &state_material_name,
                             int &state_material_index);
};

class MTLParser {
 private:
  char mtl_file_path_[FILE_MAX];
  /**
   * Directory in which the MTL file is found.
   */
  char mtl_dir_path_[FILE_MAX];

 public:
  /**
   * Open material library file.
   */
  MTLParser(StringRefNull mtl_library_, StringRefNull obj_filepath);

  /**
   * Read MTL file(s) and add MTLMaterial instances to the given Map reference.
   */
  void parse_and_store(Map<std::string, std::unique_ptr<MTLMaterial>> &r_materials);
};
}  // namespace blender::io::obj
