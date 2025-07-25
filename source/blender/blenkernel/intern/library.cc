/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 *
 * Contains code specific to the `Library` ID type.
 */

#include <optional>

#include "CLG_log.h"

/* all types are needed here, in order to do memory operations */
#include "DNA_ID.h"

#include "BLI_utildefines.h"

#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_path_utils.hh"
#include "BLI_set.hh"
#include "BLI_string.h"

#include "BLT_translation.hh"

#include "BLO_read_write.hh"

#include "BKE_bpath.hh"
#include "BKE_idtype.hh"
#include "BKE_lib_id.hh"
#include "BKE_lib_query.hh"
#include "BKE_library.hh"
#include "BKE_main.hh"
#include "BKE_main_namemap.hh"
#include "BKE_packedFile.hh"

struct BlendDataReader;

static CLG_LogRef LOG = {"lib.library"};

using namespace blender::bke::library;

static void library_runtime_reset(Library *lib)
{
  BKE_main_namemap_destroy(&lib->runtime->name_map);
}

static void library_init_data(ID *id)
{
  Library *library = reinterpret_cast<Library *>(id);
  library->runtime = MEM_new<LibraryRuntime>(__func__);
}

static void library_free_data(ID *id)
{
  Library *library = (Library *)id;
  library_runtime_reset(library);
  MEM_delete(library->runtime);
  if (library->packedfile) {
    BKE_packedfile_free(library->packedfile);
  }
}

static void library_copy_data(Main *bmain,
                              std::optional<Library *> owner_library,
                              ID *id_dst,
                              const ID *id_src,
                              int /*flag*/)
{
  /* Libraries are always local IDs. */
  BLI_assert(!owner_library || *owner_library == nullptr);
  UNUSED_VARS_NDEBUG(bmain, owner_library);

  const Library *library_src = reinterpret_cast<const Library *>(id_src);

  /* Libraries are copyable now, but there should still be only one library ID for each linked
   * blendfile (based on absolute filepath). */
  BLI_assert(!bmain ||
             !search_filepath_abs(&bmain->libraries, library_src->runtime->filepath_abs));

  Library *library_dst = reinterpret_cast<Library *>(id_dst);
  if (library_src->packedfile) {
    library_dst->packedfile = BKE_packedfile_duplicate(library_src->packedfile);
  }
  library_dst->runtime = MEM_new<LibraryRuntime>(__func__, *library_src->runtime);
  library_dst->runtime->filedata = nullptr;
  library_dst->runtime->is_filedata_owner = false;
  library_dst->runtime->name_map = nullptr;
}

static void library_foreach_id(ID *id, LibraryForeachIDData *data)
{
  Library *lib = (Library *)id;
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, lib->runtime->parent, IDWALK_CB_NEVER_SELF);
}

static void library_foreach_path(ID *id, BPathForeachPathData *bpath_data)
{
  Library *lib = (Library *)id;

  /* FIXME: Find if we should respect #BKE_BPATH_FOREACH_PATH_SKIP_PACKED here, and if not, explain
   * why. */
  if (lib->packedfile !=
      nullptr /*&& (bpath_data->flag & BKE_BPATH_FOREACH_PATH_SKIP_PACKED) != 0 */)
  {
    return;
  }

  if (BKE_bpath_foreach_path_fixed_process(bpath_data, lib->filepath, sizeof(lib->filepath))) {
    BKE_library_filepath_set(bpath_data->bmain, lib, lib->filepath);
  }
}

static void library_blend_write_data(BlendWriter *writer, ID *id, const void *id_address)
{
  Library *library = reinterpret_cast<Library *>(id);
  const bool is_undo = BLO_write_is_undo(writer);

  /* Clear runtime data. */
  library->runtime = nullptr;

  BLO_write_id_struct(writer, Library, id_address, id);
  BKE_id_blend_write(writer, id);

  /* Write packed file if necessary. */
  if (library->packedfile) {
    BKE_packedfile_blend_write(writer, library->packedfile);
    if (!is_undo) {
      CLOG_DEBUG(&LOG, "Write packed .blend: %s", library->filepath);
    }
  }
}

static void library_blend_read_data(BlendDataReader * /*reader*/, ID *id)
{
  Library *lib = reinterpret_cast<Library *>(id);
  lib->runtime = MEM_new<LibraryRuntime>(__func__);
}

IDTypeInfo IDType_ID_LI = {
    /*id_code*/ Library::id_type,
    /*id_filter*/ FILTER_ID_LI,
    /*dependencies_id_types*/ FILTER_ID_LI,
    /*main_listbase_index*/ INDEX_ID_LI,
    /*struct_size*/ sizeof(Library),
    /*name*/ "Library",
    /*name_plural*/ N_("libraries"),
    /*translation_context*/ BLT_I18NCONTEXT_ID_LIBRARY,
    /*flags*/ IDTYPE_FLAGS_NO_LIBLINKING | IDTYPE_FLAGS_NO_ANIMDATA | IDTYPE_FLAGS_NEVER_UNUSED,
    /*asset_type_info*/ nullptr,

    /*init_data*/ library_init_data,
    /*copy_data*/ library_copy_data,
    /*free_data*/ library_free_data,
    /*make_local*/ nullptr,
    /*foreach_id*/ library_foreach_id,
    /*foreach_cache*/ nullptr,
    /*foreach_path*/ library_foreach_path,
    /*owner_pointer_get*/ nullptr,

    /*blend_write*/ library_blend_write_data,
    /*blend_read_data*/ library_blend_read_data,
    /*blend_read_after_liblink*/ nullptr,

    /*blend_read_undo_preserve*/ nullptr,

    /*lib_override_apply_post*/ nullptr,
};

void BKE_library_filepath_set(Main *bmain, Library *lib, const char *filepath)
{
  /* in some cases this is used to update the absolute path from the
   * relative */
  if (lib->filepath != filepath) {
    STRNCPY(lib->filepath, filepath);
  }

  STRNCPY(lib->runtime->filepath_abs, filepath);

  /* Not essential but set `filepath_abs` is an absolute copy of value which
   * is more useful if its kept in sync. */
  if (BLI_path_is_rel(lib->runtime->filepath_abs)) {
    /* NOTE(@ideasman42): the file may be unsaved, in this case, setting the
     * `filepath_abs` on an indirectly linked path is not allowed from the
     * outliner, and its not really supported but allow from here for now
     * since making local could cause this to be directly linked.
     */
    /* Never make paths relative to parent lib - reading code (blenloader) always set *all*
     * `lib->filepath` relative to current main, not to their parent for indirectly linked ones. */
    const char *blendfile_path = BKE_main_blendfile_path(bmain);
    BLI_path_abs(lib->runtime->filepath_abs, blendfile_path);
  }
}

static void rebuild_hierarchy_best_parent_find(Main *bmain,
                                               blender::Set<Library *> &directly_used_libs,
                                               Library *lib)
{
  BLI_assert(!directly_used_libs.contains(lib));

  Library *best_parent_lib = nullptr;
  bool do_break = false;
  ListBase *lb;
  ID *id_iter;
  FOREACH_MAIN_LISTBASE_BEGIN (bmain, lb) {
    FOREACH_MAIN_LISTBASE_ID_BEGIN (lb, id_iter) {
      if (!ID_IS_LINKED(id_iter) || id_iter->lib != lib) {
        continue;
      }
      MainIDRelationsEntry *entry = static_cast<MainIDRelationsEntry *>(
          BLI_ghash_lookup(bmain->relations->relations_from_pointers, id_iter));
      for (MainIDRelationsEntryItem *item = entry->from_ids; item; item = item->next) {
        ID *from_id = item->id_pointer.from;
        if (!ID_IS_LINKED(from_id)) {
          BLI_assert_unreachable();
          continue;
        }
        Library *from_id_lib = from_id->lib;
        if (from_id_lib == lib) {
          continue;
        }
        if (directly_used_libs.contains(from_id_lib)) {
          /* Found the first best possible candidate, no need to search further. */
          BLI_assert(best_parent_lib == nullptr || best_parent_lib->runtime->temp_index > 0);
          best_parent_lib = from_id_lib;
          do_break = true;
          break;
        }
        if (!from_id_lib->runtime->parent) {
          rebuild_hierarchy_best_parent_find(bmain, directly_used_libs, from_id_lib);
        }
        if (!best_parent_lib ||
            best_parent_lib->runtime->temp_index > from_id_lib->runtime->temp_index)
        {
          best_parent_lib = from_id_lib;
          if (best_parent_lib->runtime->temp_index == 0) {
            /* Found the first best possible candidate, no need to search further. */
            BLI_assert(directly_used_libs.contains(best_parent_lib));
            do_break = true;
            break;
          }
        }
      }
      if (do_break) {
        break;
      }
    }
    FOREACH_MAIN_LISTBASE_ID_END;
    if (do_break) {
      break;
    }
  }
  FOREACH_MAIN_LISTBASE_END;

  /* NOTE: It may happen that no parent library is found, e.g. if after deleting a directly used
   * library, its indirect dependency is still around, but none of its linked IDs are used by local
   * data. */
  if (best_parent_lib) {
    lib->runtime->parent = best_parent_lib;
    lib->runtime->temp_index = best_parent_lib->runtime->temp_index + 1;
  }
  else {
    lib->runtime->parent = nullptr;
    lib->runtime->temp_index = 0;
    directly_used_libs.add(lib);
  }
}

void BKE_library_main_rebuild_hierarchy(Main *bmain)
{
  BKE_main_relations_create(bmain, 0);

  /* Reset all values, they may have been set to irrelevant values by other processes (like the
   * liboverride handling e.g., see #lib_override_libraries_index_define). */
  LISTBASE_FOREACH (Library *, lib_iter, &bmain->libraries) {
    lib_iter->runtime->temp_index = 0;
  }

  /* Find all libraries with directly linked IDs (i.e. IDs used by local data). */
  blender::Set<Library *> directly_used_libs;
  ID *id_iter;
  FOREACH_MAIN_ID_BEGIN (bmain, id_iter) {
    if (!ID_IS_LINKED(id_iter)) {
      continue;
    }
    id_iter->lib->runtime->temp_index = 0;
    if (directly_used_libs.contains(id_iter->lib)) {
      continue;
    }
    MainIDRelationsEntry *entry = static_cast<MainIDRelationsEntry *>(
        BLI_ghash_lookup(bmain->relations->relations_from_pointers, id_iter));
    for (MainIDRelationsEntryItem *item = entry->from_ids; item; item = item->next) {
      if (!ID_IS_LINKED(item->id_pointer.from)) {
        directly_used_libs.add(id_iter->lib);
        id_iter->lib->runtime->parent = nullptr;
        break;
      }
    }
  }
  FOREACH_MAIN_ID_END;

  LISTBASE_FOREACH (Library *, lib_iter, &bmain->libraries) {
    /* A directly used library. */
    if (directly_used_libs.contains(lib_iter)) {
      BLI_assert(lib_iter->runtime->temp_index == 0);
      continue;
    }

    /* Assume existing parent is still valid, since it was not cleared in previous loop above.
     * Just compute 'hierarchy value' in temp index, if needed. */
    if (lib_iter->runtime->parent) {
      if (lib_iter->runtime->temp_index > 0) {
        continue;
      }
      blender::Vector<Library *> parent_libraries;
      for (Library *parent_lib_iter = lib_iter;
           parent_lib_iter && parent_lib_iter->runtime->temp_index == 0;
           parent_lib_iter = parent_lib_iter->runtime->parent)
      {
        parent_libraries.append(parent_lib_iter);
      }
      int parent_temp_index = parent_libraries.last()->runtime->temp_index +
                              int(parent_libraries.size()) - 1;
      for (Library *parent_lib_iter : parent_libraries) {
        BLI_assert(parent_lib_iter != parent_libraries.last() ||
                   parent_lib_iter->runtime->temp_index == parent_temp_index);
        parent_lib_iter->runtime->temp_index = parent_temp_index--;
      }
      continue;
    }

    /* Otherwise, it's an indirectly used library with no known parent, another loop is needed to
     * ensure all known hierarchy has valid indices when trying to find the best valid parent
     * library. */
  }

  /* For all libraries known to be indirect, but without a known parent, find a best valid parent
   * (i.e. a 'most directly used' library). */
  LISTBASE_FOREACH (Library *, lib_iter, &bmain->libraries) {
    /* A directly used library. */
    if (directly_used_libs.contains(lib_iter)) {
      BLI_assert(lib_iter->runtime->temp_index == 0);
      continue;
    }

    if (lib_iter->runtime->parent) {
      BLI_assert(lib_iter->runtime->temp_index > 0);
    }
    else {
      BLI_assert(lib_iter->runtime->temp_index == 0);
      rebuild_hierarchy_best_parent_find(bmain, directly_used_libs, lib_iter);
    }
  }

  BKE_main_relations_free(bmain);
}

Library *blender::bke::library::search_filepath_abs(ListBase *libraries,
                                                    blender::StringRef filepath_abs)
{
  LISTBASE_FOREACH (Library *, lib_iter, libraries) {
    if (filepath_abs == lib_iter->runtime->filepath_abs) {
      return lib_iter;
    }
  }
  return nullptr;
}
