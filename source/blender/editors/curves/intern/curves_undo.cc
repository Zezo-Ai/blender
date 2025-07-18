/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup edcurves
 */

#include "BLI_task.hh"

#include "BKE_context.hh"
#include "BKE_curves.hh"
#include "BKE_main.hh"
#include "BKE_object.hh"
#include "BKE_undo_system.hh"

#include "CLG_log.h"

#include "DEG_depsgraph.hh"

#include "ED_curves.hh"
#include "ED_undo.hh"

#include "WM_api.hh"
#include "WM_types.hh"

static CLG_LogRef LOG = {"undo.curves"};

namespace blender::ed::curves {
namespace undo {

/* -------------------------------------------------------------------- */
/** \name Implements ED Undo System
 *
 * \note This is similar for all edit-mode types.
 * \{ */

struct StepObject {
  UndoRefID_Object obedit_ref = {};
  bke::CurvesGeometry geometry = {};
};

struct CurvesUndoStep {
  UndoStep step;
  /** See #ED_undo_object_editmode_validate_scene_from_windows code comment for details. */
  UndoRefID_Scene scene_ref = {};
  Array<StepObject> objects;
};

static bool step_encode(bContext *C, Main *bmain, UndoStep *us_p)
{
  CurvesUndoStep *us = reinterpret_cast<CurvesUndoStep *>(us_p);

  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  Vector<Object *> objects = ED_undo_editmode_objects_from_view_layer(scene, view_layer);

  us->scene_ref.ptr = scene;
  new (&us->objects) Array<StepObject>(objects.size());

  threading::parallel_for(us->objects.index_range(), 8, [&](const IndexRange range) {
    for (const int i : range) {
      Object *ob = objects[i];
      const Curves &curves_id = *static_cast<Curves *>(ob->data);
      StepObject &object = us->objects[i];

      object.obedit_ref.ptr = ob;
      object.geometry = curves_id.geometry.wrap();
    }
  });

  bmain->is_memfile_undo_flush_needed = true;

  return true;
}

static void step_decode(
    bContext *C, Main *bmain, UndoStep *us_p, const eUndoStepDir /*dir*/, bool /*is_final*/)
{
  CurvesUndoStep *us = reinterpret_cast<CurvesUndoStep *>(us_p);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);

  ED_undo_object_editmode_validate_scene_from_windows(
      CTX_wm_manager(C), us->scene_ref.ptr, &scene, &view_layer);
  ED_undo_object_editmode_restore_helper(scene,
                                         view_layer,
                                         &us->objects.first().obedit_ref.ptr,
                                         us->objects.size(),
                                         sizeof(decltype(us->objects)::value_type));

  BLI_assert(BKE_object_is_in_editmode(us->objects.first().obedit_ref.ptr));

  for (const StepObject &object : us->objects) {
    Curves &curves_id = *static_cast<Curves *>(object.obedit_ref.ptr->data);

    /* Overwrite the curves geometry. */
    curves_id.geometry.wrap() = object.geometry;

    DEG_id_tag_update(&curves_id.id, ID_RECALC_GEOMETRY);
  }

  ED_undo_object_set_active_or_warn(
      scene, view_layer, us->objects.first().obedit_ref.ptr, us_p->name, &LOG);

  bmain->is_memfile_undo_flush_needed = true;

  WM_event_add_notifier(C, NC_GEOM | ND_DATA, nullptr);
}

static void step_free(UndoStep *us_p)
{
  CurvesUndoStep *us = reinterpret_cast<CurvesUndoStep *>(us_p);
  us->objects.~Array();
}

static void foreach_ID_ref(UndoStep *us_p,
                           UndoTypeForEachIDRefFn foreach_ID_ref_fn,
                           void *user_data)
{
  CurvesUndoStep *us = reinterpret_cast<CurvesUndoStep *>(us_p);

  foreach_ID_ref_fn(user_data, ((UndoRefID *)&us->scene_ref));
  for (const StepObject &object : us->objects) {
    foreach_ID_ref_fn(user_data, ((UndoRefID *)&object.obedit_ref));
  }
}

/** \} */

}  // namespace undo

void undosys_type_register(UndoType *ut)
{
  ut->name = "Edit Curves";
  ut->poll = editable_curves_in_edit_mode_poll;
  ut->step_encode = undo::step_encode;
  ut->step_decode = undo::step_decode;
  ut->step_free = undo::step_free;

  ut->step_foreach_ID_ref = undo::foreach_ID_ref;

  ut->flags = UNDOTYPE_FLAG_NEED_CONTEXT_FOR_ENCODE;

  ut->step_size = sizeof(undo::CurvesUndoStep);
}

}  // namespace blender::ed::curves
