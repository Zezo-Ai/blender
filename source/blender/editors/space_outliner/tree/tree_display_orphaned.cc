/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup spoutliner
 */

#include "DNA_ID.h"
#include "DNA_space_types.h"

#include "BLI_listbase.h"
#include "BLI_listbase_wrapper.hh"

#include "BKE_idtype.hh"
#include "BKE_main.hh"

#include "../outliner_intern.hh"
#include "common.hh"
#include "tree_display.hh"

namespace blender::ed::outliner {

template<typename T> using List = ListBaseWrapper<T>;

TreeDisplayIDOrphans::TreeDisplayIDOrphans(SpaceOutliner &space_outliner)
    : AbstractTreeDisplay(space_outliner)
{
}

ListBase TreeDisplayIDOrphans::build_tree(const TreeSourceData &source_data)
{
  ListBase tree = {nullptr};
  short filter_id_type = (space_outliner_.filter & SO_FILTER_ID_TYPE) ?
                             space_outliner_.filter_id_type :
                             0;

  Vector<ListBase *> lbarray;
  if (filter_id_type) {
    lbarray.append(which_libbase(source_data.bmain, filter_id_type));
  }
  else {
    lbarray.extend(BKE_main_lists_get(*source_data.bmain));
  }

  for (int a = 0; a < lbarray.size(); a++) {
    if (BLI_listbase_is_empty(lbarray[a])) {
      continue;
    }
    if (!datablock_has_orphans(*lbarray[a])) {
      continue;
    }

    /* Header for this type of data-block. */
    TreeElement *te = nullptr;
    if (!filter_id_type) {
      ID *id = (ID *)lbarray[a]->first;
      te = add_element(&tree, nullptr, lbarray[a], nullptr, TSE_ID_BASE, 0);
      te->directdata = lbarray[a];
      te->name = outliner_idcode_to_plural(GS(id->name));
    }

    /* Add the orphaned data-blocks - these will not be added with any subtrees attached. */
    for (ID *id : List<ID>(lbarray[a])) {
      if (ID_REFCOUNTING_USERS(id) <= 0) {
        add_element((te) ? &te->subtree : &tree, id, nullptr, te, TSE_SOME_ID, 0, false);
      }
    }
  }

  return tree;
}

bool TreeDisplayIDOrphans::datablock_has_orphans(ListBase &lb) const
{
  if (BLI_listbase_is_empty(&lb)) {
    return false;
  }
  const IDTypeInfo *id_type = BKE_idtype_get_info_from_id(static_cast<ID *>(lb.first));
  if (id_type->flags & IDTYPE_FLAGS_NEVER_UNUSED) {
    /* These ID types are never unused. */
    return false;
  }

  for (ID *id : List<ID>(lb)) {
    if (ID_REFCOUNTING_USERS(id) <= 0) {
      return true;
    }
  }
  return false;
}

}  // namespace blender::ed::outliner
