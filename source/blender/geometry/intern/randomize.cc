/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>
#include <random>

#include "GEO_randomize.hh"

#include "DNA_curves_types.h"
#include "DNA_mesh_types.h"
#include "DNA_pointcloud_types.h"

#include "BKE_attribute_storage.hh"
#include "BKE_curves.hh"
#include "BKE_customdata.hh"
#include "BKE_geometry_set.hh"
#include "BKE_global.hh"
#include "BKE_instances.hh"

#include "BLI_array.hh"

namespace blender::geometry {

static Array<int> get_permutation(const int length, const int seed)
{
  Array<int> data(length);
  for (const int i : IndexRange(length)) {
    data[i] = i;
  }
  std::shuffle(data.begin(), data.end(), std::default_random_engine(seed));
  return data;
}

static Array<int> invert_permutation(const Span<int> permutation)
{
  Array<int> data(permutation.size());
  for (const int i : permutation.index_range()) {
    data[permutation[i]] = i;
  }
  return data;
}

/**
 * We can't use a fully random seed, because then the randomization wouldn't be deterministic,
 * which is important to avoid causing issues when determinism is expected. Using a single constant
 * seed is not ideal either, because then two geometries might be randomized equally or very
 * similar. Ideally, the seed would be a hash of everything that feeds into the geometry processing
 * algorithm before the randomization, but that's too expensive. Just use something simple but
 * correct for now.
 */
static int seed_from_mesh(const Mesh &mesh)
{
  return mesh.verts_num;
}

static int seed_from_pointcloud(const PointCloud &pointcloud)
{
  return pointcloud.totpoint;
}

static int seed_from_curves(const bke::CurvesGeometry &curves)
{
  return curves.point_num;
}

static int seed_from_instances(const bke::Instances &instances)
{
  return instances.instances_num();
}

static void reorder_customdata(CustomData &data, const Span<int> new_by_old_map)
{
  CustomData new_data;
  CustomData_init_layout_from(&data, &new_data, CD_MASK_ALL, CD_CONSTRUCT, new_by_old_map.size());

  for (const int old_i : new_by_old_map.index_range()) {
    const int new_i = new_by_old_map[old_i];
    CustomData_copy_data(&data, &new_data, old_i, new_i, 1);
  }
  CustomData_free(&data);
  data = new_data;
}

static void reorder_attribute_domain(bke::AttributeStorage &data,
                                     const bke::AttrDomain domain,
                                     const Span<int> new_by_old_map)
{
  data.foreach([&](bke::Attribute &attr) {
    if (attr.domain() != domain) {
      return;
    }
    const CPPType &type = bke::attribute_type_to_cpp_type(attr.data_type());
    switch (attr.storage_type()) {
      case bke::AttrStorageType::Array: {
        const auto &data = std::get<bke::Attribute::ArrayData>(attr.data());
        auto new_data = bke::Attribute::ArrayData::from_constructed(type, new_by_old_map.size());
        bke::attribute_math::gather(GSpan(type, data.data, data.size),
                                    new_by_old_map,
                                    GMutableSpan(type, new_data.data, new_data.size));
        attr.assign_data(std::move(new_data));
      }
      case bke::AttrStorageType::Single: {
        return;
      }
    }
  });
}

void debug_randomize_vert_order(Mesh *mesh)
{
  if (mesh == nullptr || !use_debug_randomization()) {
    return;
  }

  const int seed = seed_from_mesh(*mesh);
  const Array<int> new_by_old_map = get_permutation(mesh->verts_num, seed);

  reorder_customdata(mesh->vert_data, new_by_old_map);

  for (int &v : mesh->edges_for_write().cast<int>()) {
    v = new_by_old_map[v];
  }
  for (int &v : mesh->corner_verts_for_write()) {
    v = new_by_old_map[v];
  }

  mesh->tag_topology_changed();
}

void debug_randomize_edge_order(Mesh *mesh)
{
  if (mesh == nullptr || !use_debug_randomization()) {
    return;
  }

  const int seed = seed_from_mesh(*mesh);
  const Array<int> new_by_old_map = get_permutation(mesh->edges_num, seed);

  reorder_customdata(mesh->edge_data, new_by_old_map);

  for (int &e : mesh->corner_edges_for_write()) {
    e = new_by_old_map[e];
  }

  mesh->tag_topology_changed();
}

static Array<int> make_new_offset_indices(const OffsetIndices<int> old_offsets,
                                          const Span<int> old_by_new_map)
{
  Array<int> new_offsets(old_offsets.data().size());
  new_offsets[0] = 0;
  for (const int new_i : old_offsets.index_range()) {
    const int old_i = old_by_new_map[new_i];
    new_offsets[new_i + 1] = new_offsets[new_i] + old_offsets[old_i].size();
  }
  return new_offsets;
}

static void reorder_customdata_groups(CustomData &data,
                                      const OffsetIndices<int> old_offsets,
                                      const OffsetIndices<int> new_offsets,
                                      const Span<int> new_by_old_map)
{
  const int elements_num = new_offsets.total_size();
  const int groups_num = new_by_old_map.size();
  CustomData new_data;
  CustomData_init_layout_from(&data, &new_data, CD_MASK_ALL, CD_CONSTRUCT, elements_num);
  for (const int old_i : IndexRange(groups_num)) {
    const int new_i = new_by_old_map[old_i];
    const IndexRange old_range = old_offsets[old_i];
    const IndexRange new_range = new_offsets[new_i];
    BLI_assert(old_range.size() == new_range.size());
    CustomData_copy_data(&data, &new_data, old_range.start(), new_range.start(), old_range.size());
  }
  CustomData_free(&data);
  data = new_data;
}

static void reorder_attribute_groups(bke::AttributeStorage &storage,
                                     const bke::AttrDomain domain,
                                     const OffsetIndices<int> old_offsets,
                                     const OffsetIndices<int> new_offsets,
                                     const Span<int> new_by_old_map)
{
  const int groups_num = new_by_old_map.size();
  storage.foreach([&](bke::Attribute &attr) {
    if (attr.domain() != domain) {
      return;
    }
    const CPPType &type = bke::attribute_type_to_cpp_type(attr.data_type());
    switch (attr.storage_type()) {
      case bke::AttrStorageType::Array: {
        const auto &data = std::get<bke::Attribute::ArrayData>(attr.data());

        auto new_data = bke::Attribute::ArrayData::from_uninitialized(type, new_by_old_map.size());
        threading::parallel_for(IndexRange(groups_num), 1024, [&](const IndexRange range) {
          for (const int old_i : range) {
            const int new_i = new_by_old_map[old_i];
            const IndexRange old_range = old_offsets[old_i];
            const IndexRange new_range = new_offsets[new_i];
            BLI_assert(old_range.size() == new_range.size());
            type.copy_construct_n(POINTER_OFFSET(data.data, old_range.start() * type.size),
                                  POINTER_OFFSET(new_data.data, new_range.start() * type.size),
                                  old_range.size());
          }
        });

        attr.assign_data(std::move(new_data));
      }
      case bke::AttrStorageType::Single: {
        return;
      }
    }
  });
}

void debug_randomize_face_order(Mesh *mesh)
{
  if (mesh == nullptr || mesh->faces_num == 0 || !use_debug_randomization()) {
    return;
  }

  const int seed = seed_from_mesh(*mesh);
  const Array<int> new_by_old_map = get_permutation(mesh->faces_num, seed);
  const Array<int> old_by_new_map = invert_permutation(new_by_old_map);

  reorder_customdata(mesh->face_data, new_by_old_map);

  const OffsetIndices old_faces = mesh->faces();
  Array<int> new_face_offsets = make_new_offset_indices(old_faces, old_by_new_map);
  const OffsetIndices<int> new_faces = new_face_offsets.as_span();

  reorder_customdata_groups(mesh->corner_data, old_faces, new_faces, new_by_old_map);

  mesh->face_offsets_for_write().copy_from(new_face_offsets);

  mesh->tag_topology_changed();
}

void debug_randomize_point_order(PointCloud *pointcloud)
{
  if (pointcloud == nullptr || !use_debug_randomization()) {
    return;
  }

  const int seed = seed_from_pointcloud(*pointcloud);
  const Array<int> new_by_old_map = get_permutation(pointcloud->totpoint, seed);
  reorder_attribute_domain(
      pointcloud->attribute_storage.wrap(), bke::AttrDomain::Point, new_by_old_map);

  pointcloud->tag_positions_changed();
  pointcloud->tag_radii_changed();
}

void debug_randomize_curve_order(bke::CurvesGeometry *curves)
{
  if (curves == nullptr || !use_debug_randomization()) {
    return;
  }

  const int seed = seed_from_curves(*curves);
  const Array<int> new_by_old_map = get_permutation(curves->curve_num, seed);
  const Array<int> old_by_new_map = invert_permutation(new_by_old_map);

  bke::AttributeStorage &attributes = curves->attribute_storage.wrap();

  reorder_attribute_domain(attributes, bke::AttrDomain::Curve, new_by_old_map);

  const OffsetIndices old_points_by_curve = curves->points_by_curve();
  Array<int> new_curve_offsets = make_new_offset_indices(old_points_by_curve, old_by_new_map);
  const OffsetIndices<int> new_points_by_curve = new_curve_offsets.as_span();

  reorder_customdata_groups(
      curves->point_data, old_points_by_curve, new_points_by_curve, new_by_old_map);
  reorder_attribute_groups(attributes,
                           bke::AttrDomain::Point,
                           old_points_by_curve,
                           new_points_by_curve,
                           new_by_old_map);

  curves->offsets_for_write().copy_from(new_curve_offsets);

  curves->tag_topology_changed();
}

void debug_randomize_mesh_order(Mesh *mesh)
{
  if (mesh == nullptr || !use_debug_randomization()) {
    return;
  }

  debug_randomize_vert_order(mesh);
  debug_randomize_edge_order(mesh);
  debug_randomize_face_order(mesh);
}

void debug_randomize_instance_order(bke::Instances *instances)
{
  if (instances == nullptr || !use_debug_randomization()) {
    return;
  }
  const int instances_num = instances->instances_num();
  const int seed = seed_from_instances(*instances);
  const Array<int> new_by_old_map = get_permutation(instances_num, seed);
  reorder_attribute_domain(
      instances->attribute_storage(), bke::AttrDomain::Instance, new_by_old_map);
}

bool use_debug_randomization()
{
  return G.randomize_geometry_element_order;
}

}  // namespace blender::geometry
