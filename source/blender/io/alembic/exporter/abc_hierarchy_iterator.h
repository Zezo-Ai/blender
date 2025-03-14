/* SPDX-FileCopyrightText: 2020 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "ABC_alembic.h"
#include "abc_archive.h"

#include "IO_abstract_hierarchy_iterator.h"

#include <string>

#include <Alembic/Abc/OObject.h>

struct Depsgraph;
struct Main;
struct Object;

namespace blender::io::alembic {

class ABCAbstractWriter;
class ABCHierarchyIterator;

struct ABCWriterConstructorArgs {
  Depsgraph *depsgraph;
  ABCArchive *abc_archive;
  Alembic::Abc::OObject abc_parent;
  std::string abc_name;
  std::string abc_path;
  const ABCHierarchyIterator *hierarchy_iterator;
  const AlembicExportParams *export_params;
};

class ABCHierarchyIterator : public AbstractHierarchyIterator {
 private:
  ABCArchive *abc_archive_;
  const AlembicExportParams &params_;

 public:
  ABCHierarchyIterator(Main *bmain,
                       Depsgraph *depsgraph,
                       ABCArchive *abc_archive,
                       const AlembicExportParams &params);

  void iterate_and_write() override;
  std::string make_valid_name(const std::string &name) const override;

  Alembic::Abc::OObject get_alembic_object(const std::string &export_path) const;

 protected:
  bool mark_as_weak_export(const Object *object) const override;

  ObjectIdentifier determine_graph_index_object(const HierarchyContext *context) override;
  ObjectIdentifier determine_graph_index_dupli(
      const HierarchyContext *context,
      const DupliObject *dupli_object,
      const DupliParentFinder &dupli_parent_finder) override;

  AbstractHierarchyWriter *create_transform_writer(const HierarchyContext *context) override;
  AbstractHierarchyWriter *create_data_writer(const HierarchyContext *context) override;
  AbstractHierarchyWriter *create_hair_writer(const HierarchyContext *context) override;
  AbstractHierarchyWriter *create_particle_writer(const HierarchyContext *context) override;

  void release_writer(AbstractHierarchyWriter *writer) override;

 private:
  Alembic::Abc::OObject get_alembic_parent(const HierarchyContext *context) const;
  ABCWriterConstructorArgs writer_constructor_args(const HierarchyContext *context) const;
  void update_archive_bounding_box();
  void update_bounding_box_recursive(Imath::Box3d &bounds, const HierarchyContext *context);

  ABCAbstractWriter *create_data_writer_for_object_type(
      const HierarchyContext *context, const ABCWriterConstructorArgs &writer_args);
};

}  // namespace blender::io::alembic
