/* SPDX-FileCopyrightText: 2004-2008 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup spview3d
 */

#include <cstdlib>
#include <cstring>

#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_utildefines.h"

#include "BKE_context.hh"
#include "BKE_editmesh.hh"
#include "BKE_layer.hh"
#include "BKE_object.hh"

#include "DEG_depsgraph.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "UI_interface.hh"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "view3d_intern.hh"

/* -------------------------------------------------------------------- */
/** \name Toggle Matcap Flip Operator
 * \{ */

static wmOperatorStatus toggle_matcap_flip_exec(bContext *C, wmOperator * /*op*/)
{
  View3D *v3d = CTX_wm_view3d(C);

  if (v3d) {
    v3d->shading.flag ^= V3D_SHADING_MATCAP_FLIP_X;
    ED_view3d_shade_update(CTX_data_main(C), v3d, CTX_wm_area(C));
    WM_event_add_notifier(C, NC_SPACE | ND_SPACE_VIEW3D, v3d);
  }
  else {
    Scene *scene = CTX_data_scene(C);
    scene->display.shading.flag ^= V3D_SHADING_MATCAP_FLIP_X;
    DEG_id_tag_update(&scene->id, ID_RECALC_SYNC_TO_EVAL);
    WM_event_add_notifier(C, NC_SCENE | ND_RENDER_OPTIONS, scene);
  }

  return OPERATOR_FINISHED;
}

void VIEW3D_OT_toggle_matcap_flip(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Flip MatCap";
  ot->description = "Flip MatCap";
  ot->idname = "VIEW3D_OT_toggle_matcap_flip";

  /* API callbacks. */
  ot->exec = toggle_matcap_flip_exec;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name UI Templates
 * \{ */

void uiTemplateEditModeSelection(uiLayout *layout, bContext *C)
{
  Object *obedit = CTX_data_edit_object(C);
  if (!obedit || obedit->type != OB_MESH) {
    return;
  }

  BMEditMesh *em = BKE_editmesh_from_object(obedit);
  uiLayout *row = &layout->row(true);

  PointerRNA op_ptr;
  wmOperatorType *ot = WM_operatortype_find("MESH_OT_select_mode", true);
  op_ptr = row->op(ot,
                   "",
                   ICON_VERTEXSEL,
                   blender::wm::OpCallContext::InvokeDefault,
                   (em->selectmode & SCE_SELECT_VERTEX) ? UI_ITEM_O_DEPRESS : UI_ITEM_NONE);
  RNA_enum_set(&op_ptr, "type", SCE_SELECT_VERTEX);
  op_ptr = row->op(ot,
                   "",
                   ICON_EDGESEL,
                   blender::wm::OpCallContext::InvokeDefault,
                   (em->selectmode & SCE_SELECT_EDGE) ? UI_ITEM_O_DEPRESS : UI_ITEM_NONE);
  RNA_enum_set(&op_ptr, "type", SCE_SELECT_EDGE);
  op_ptr = row->op(ot,
                   "",
                   ICON_FACESEL,
                   blender::wm::OpCallContext::InvokeDefault,
                   (em->selectmode & SCE_SELECT_FACE) ? UI_ITEM_O_DEPRESS : UI_ITEM_NONE);
  RNA_enum_set(&op_ptr, "type", SCE_SELECT_FACE);
}

static void uiTemplatePaintModeSelection(uiLayout *layout, bContext *C)
{
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  BKE_view_layer_synced_ensure(scene, view_layer);
  Object *ob = BKE_view_layer_active_object_get(view_layer);

  /* Gizmos aren't used in paint modes */
  if (!ELEM(ob->mode, OB_MODE_SCULPT, OB_MODE_PARTICLE_EDIT)) {
    /* masks aren't used for sculpt and particle painting */
    PointerRNA meshptr = RNA_pointer_create_discrete(
        static_cast<ID *>(ob->data), &RNA_Mesh, ob->data);
    if (ob->mode & OB_MODE_TEXTURE_PAINT) {
      layout->prop(&meshptr, "use_paint_mask", UI_ITEM_R_ICON_ONLY, "", ICON_NONE);
    }
    else {
      uiLayout *row = &layout->row(true);
      row->prop(&meshptr, "use_paint_mask", UI_ITEM_R_ICON_ONLY, "", ICON_NONE);
      row->prop(&meshptr, "use_paint_mask_vertex", UI_ITEM_R_ICON_ONLY, "", ICON_NONE);

      /* Show the bone selection mode icon only if there is a pose mode armature */
      Object *ob_armature = BKE_object_pose_armature_get(ob);
      if (ob_armature) {
        row->prop(&meshptr, "use_paint_bone_selection", UI_ITEM_R_ICON_ONLY, "", ICON_NONE);
      }
    }
  }
}

void uiTemplateHeader3D_mode(uiLayout *layout, bContext *C)
{
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  BKE_view_layer_synced_ensure(scene, view_layer);
  Object *ob = BKE_view_layer_active_object_get(view_layer);
  Object *obedit = CTX_data_edit_object(C);

  bool is_paint = (ob && ELEM(ob->mode,
                              OB_MODE_SCULPT,
                              OB_MODE_VERTEX_PAINT,
                              OB_MODE_WEIGHT_PAINT,
                              OB_MODE_TEXTURE_PAINT));

  uiTemplateEditModeSelection(layout, C);
  if ((obedit == nullptr) && is_paint) {
    uiTemplatePaintModeSelection(layout, C);
  }
}

/** \} */
