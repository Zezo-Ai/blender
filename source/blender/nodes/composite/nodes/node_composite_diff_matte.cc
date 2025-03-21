/* SPDX-FileCopyrightText: 2006 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup cmpnodes
 */

#include "BLI_math_base.hh"
#include "BLI_math_vector.hh"
#include "BLI_math_vector_types.hh"

#include "FN_multi_function_builder.hh"

#include "NOD_multi_function.hh"

#include "UI_interface.hh"
#include "UI_resources.hh"

#include "GPU_material.hh"

#include "node_composite_util.hh"

/* ******************* channel Difference Matte ********************************* */

namespace blender::nodes::node_composite_diff_matte_cc {

NODE_STORAGE_FUNCS(NodeChroma)

static void cmp_node_diff_matte_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>("Image 1")
      .default_value({1.0f, 1.0f, 1.0f, 1.0f})
      .compositor_domain_priority(0);
  b.add_input<decl::Color>("Image 2")
      .default_value({1.0f, 1.0f, 1.0f, 1.0f})
      .compositor_domain_priority(1);
  b.add_output<decl::Color>("Image");
  b.add_output<decl::Float>("Matte");
}

static void node_composit_init_diff_matte(bNodeTree * /*ntree*/, bNode *node)
{
  NodeChroma *c = MEM_callocN<NodeChroma>(__func__);
  node->storage = c;
  c->t1 = 0.1f;
  c->t2 = 0.1f;
}

static void node_composit_buts_diff_matte(uiLayout *layout, bContext * /*C*/, PointerRNA *ptr)
{
  uiLayout *col;

  col = uiLayoutColumn(layout, true);
  uiItemR(col,
          ptr,
          "tolerance",
          UI_ITEM_R_SPLIT_EMPTY_NAME | UI_ITEM_R_SLIDER,
          std::nullopt,
          ICON_NONE);
  uiItemR(
      col, ptr, "falloff", UI_ITEM_R_SPLIT_EMPTY_NAME | UI_ITEM_R_SLIDER, std::nullopt, ICON_NONE);
}

using namespace blender::compositor;

static float get_tolerance(const bNode &node)
{
  return node_storage(node).t1;
}

static float get_falloff(const bNode &node)
{
  return node_storage(node).t2;
}

static int node_gpu_material(GPUMaterial *material,
                             bNode *node,
                             bNodeExecData * /*execdata*/,
                             GPUNodeStack *inputs,
                             GPUNodeStack *outputs)
{
  const float tolerance = get_tolerance(*node);
  const float falloff = get_falloff(*node);

  return GPU_stack_link(material,
                        node,
                        "node_composite_difference_matte",
                        inputs,
                        outputs,
                        GPU_uniform(&tolerance),
                        GPU_uniform(&falloff));
}

static void node_build_multi_function(blender::nodes::NodeMultiFunctionBuilder &builder)
{
  const float tolerance = get_tolerance(builder.node());
  const float falloff = get_falloff(builder.node());

  builder.construct_and_set_matching_fn_cb([=]() {
    return mf::build::SI2_SO2<float4, float4, float4, float>(
        "Difference Key",
        [=](const float4 &color, const float4 &key, float4 &result, float &matte) -> void {
          float difference = math::dot(math::abs(color - key).xyz(), float3(1.0f)) / 3.0f;

          bool is_opaque = difference > tolerance + falloff;
          float alpha = is_opaque ?
                            color.w :
                            math::safe_divide(math::max(0.0f, difference - tolerance), falloff);

          matte = math::min(alpha, color.w);
          result = color * matte;
        },
        mf::build::exec_presets::AllSpanOrSingle());
  });
}

}  // namespace blender::nodes::node_composite_diff_matte_cc

void register_node_type_cmp_diff_matte()
{
  namespace file_ns = blender::nodes::node_composite_diff_matte_cc;

  static blender::bke::bNodeType ntype;

  cmp_node_type_base(&ntype, "CompositorNodeDiffMatte", CMP_NODE_DIFF_MATTE);
  ntype.ui_name = "Difference Key";
  ntype.ui_description =
      "Produce a matte that isolates foreground content by comparing it with a reference "
      "background image";
  ntype.enum_name_legacy = "DIFF_MATTE";
  ntype.nclass = NODE_CLASS_MATTE;
  ntype.declare = file_ns::cmp_node_diff_matte_declare;
  ntype.draw_buttons = file_ns::node_composit_buts_diff_matte;
  ntype.flag |= NODE_PREVIEW;
  ntype.initfunc = file_ns::node_composit_init_diff_matte;
  blender::bke::node_type_storage(
      ntype, "NodeChroma", node_free_standard_storage, node_copy_standard_storage);
  ntype.gpu_fn = file_ns::node_gpu_material;
  ntype.build_multi_function = file_ns::node_build_multi_function;

  blender::bke::node_register_type(ntype);
}
