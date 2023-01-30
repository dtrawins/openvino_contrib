// Copyright (C) 2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "convert_layout.hpp"
#include "opset/opset.hpp"

#include <openvino/pass/pattern/op/wrap_type.hpp>
#include "transformations/utils/utils.hpp"

using namespace ov;

namespace ArmPlugin {
namespace pass {

static std::vector<int> nchw_to_nhwc{0, 2, 3, 1};
static std::vector<int> nhwc_to_nchw{0, 3, 1, 2};

static std::vector<int> ncdhw_to_ndhwc{0, 2, 3, 4, 1};
static std::vector<int> ndhwc_to_ncdhw{0, 4, 1, 2, 3};

static std::shared_ptr<ArmPlugin::opset::Transpose> transpose_on_input(const Output<Node>& input, size_t rank) {
    switch (rank) {
    case 4:
        return std::make_shared<ArmPlugin::opset::Transpose>(input,
                ArmPlugin::opset::Constant::create(element::i32, Shape{nchw_to_nhwc.size()}, nchw_to_nhwc));
    case 5:
        return std::make_shared<ArmPlugin::opset::Transpose>(input,
                ArmPlugin::opset::Constant::create(element::i32, Shape{ncdhw_to_ndhwc.size()}, ncdhw_to_ndhwc));
    default:
        IE_THROW() << "ConvertLayout: unsupported rank";
    }
}

std::shared_ptr<ArmPlugin::opset::Transpose> transpose_on_output(const Output<Node>& input, size_t rank) {
    switch (rank) {
    case 4:
        return std::make_shared<ArmPlugin::opset::Transpose>(input,
                ArmPlugin::opset::Constant::create(element::i32, Shape{nhwc_to_nchw.size()}, nhwc_to_nchw));
    case 5:
        return std::make_shared<ArmPlugin::opset::Transpose>(input,
                ArmPlugin::opset::Constant::create(element::i32, Shape{ndhwc_to_ncdhw.size()}, ndhwc_to_ncdhw));
    default:
        IE_THROW() << "ConvertLayout: unsupported rank";
    }
}

PartialShape transpose_output_shape(const std::shared_ptr<Node>& node, size_t rank) {
    const auto& shape = node->get_output_partial_shape(0);
    PartialShape new_output_shape;
    new_output_shape.reserve(rank);
    const auto& perm = rank == 4 ? nchw_to_nhwc : ncdhw_to_ndhwc;
    for (size_t i = 0; i < rank; i++) {
        new_output_shape.push_back(shape[perm[i]]);
    }
    return new_output_shape;
}

ConvertArmConvolutionLayout::ConvertArmConvolutionLayout() {
    auto root = ov::pass::pattern::wrap_type<opset::ArmConvolution>(ov::pass::pattern::has_static_rank());

    matcher_pass_callback callback = [=](ov::pass::pattern::Matcher& m) {
        auto node = m.get_match_root();
        if (transformation_callback(node)) {
            return false;
        }
        auto conv = ov::as_type_ptr<opset::ArmConvolution>(node);
        if (!conv) {
            return false;
        }
        size_t rank = conv->get_output_partial_shape(0).size();
        if (rank < 4 || rank > 5) {
            return false;
        }
        auto activations_transpose = transpose_on_input(conv->input_value(0), rank);
        auto weights_transpose = transpose_on_input(conv->input_value(1), rank);
        auto output_shape = transpose_output_shape(conv, rank);
        std::shared_ptr<opset::ArmConvolution> new_conv;
        if (conv->get_input_size() > 2) {
            new_conv = std::make_shared<opset::ArmConvolution>(activations_transpose,
                                                               weights_transpose,
                                                               conv->input_value(2),
                                                               conv->get_strides(),
                                                               conv->get_pads_begin(),
                                                               conv->get_pads_end(),
                                                               conv->get_dilations(),
                                                               conv->get_auto_pad(),
                                                               output_shape);
        } else {
            new_conv = std::make_shared<opset::ArmConvolution>(activations_transpose,
                                                               weights_transpose,
                                                               conv->get_strides(),
                                                               conv->get_pads_begin(),
                                                               conv->get_pads_end(),
                                                               conv->get_dilations(),
                                                               conv->get_auto_pad(),
                                                               output_shape);
        }
        auto transpose = transpose_on_output(new_conv, rank);
        transpose->set_friendly_name(conv->get_friendly_name());
        copy_runtime_info(conv, {new_conv, activations_transpose, weights_transpose, transpose});
        replace_node(conv, transpose);

        return true;
    };

    auto m = std::make_shared<ov::pass::pattern::Matcher>(root, "ConvertArmConvolutionLayout");
    register_matcher(m, callback);
}

ConvertArmMaxPoolV1Layout::ConvertArmMaxPoolV1Layout() {
    auto root = ov::pass::pattern::wrap_type<opset::v1::ArmMaxPool>(ov::pass::pattern::has_static_rank());

    matcher_pass_callback callback = [=](ov::pass::pattern::Matcher& m) {
        auto node = m.get_match_root();
        if (transformation_callback(node)) {
            return false;
        }
        auto pool = ov::as_type_ptr<opset::v1::ArmMaxPool>(node);
        if (!pool) {
            return false;
        }
        size_t rank = pool->get_output_partial_shape(0).size();
        if (rank < 4 || rank > 5) {
            return false;
        }
        auto activations_transpose = transpose_on_input(pool->input_value(0), rank);
        auto output_shape = transpose_output_shape(pool, rank);
        auto new_pool = std::make_shared<opset::v1::ArmMaxPool>(activations_transpose,
                                                                pool->get_strides(),
                                                                pool->get_pads_begin(),
                                                                pool->get_pads_end(),
                                                                pool->get_kernel(),
                                                                pool->get_rounding_type(),
                                                                pool->get_auto_pad(),
                                                                output_shape);
        auto transpose = transpose_on_output(new_pool, rank);
        transpose->set_friendly_name(pool->get_friendly_name());
        copy_runtime_info(pool, {new_pool, activations_transpose, transpose});
        replace_node(pool, transpose);

        return true;
    };

    auto m = std::make_shared<ov::pass::pattern::Matcher>(root, "ConvertArmMaxPoolV1Layout");
    register_matcher(m, callback);
}

ConvertArmMaxPoolV8Layout::ConvertArmMaxPoolV8Layout() {
    auto root = ov::pass::pattern::wrap_type<opset::v8::ArmMaxPool>(ov::pass::pattern::has_static_rank());

    matcher_pass_callback callback = [=](ov::pass::pattern::Matcher& m) {
        auto node = m.get_match_root();
        if (transformation_callback(node)) {
            return false;
        }
        auto pool = ov::as_type_ptr<opset::v8::ArmMaxPool>(node);
        if (!pool) {
            return false;
        }
        size_t rank = pool->get_output_partial_shape(0).size();
        if (rank < 4 || rank > 5) {
            return false;
        }
        auto axis = pool->get_axis();
        if (axis > 1 || (axis < 0 && axis > -static_cast<int64_t>(rank) - 1)) {
            return false;
        }
        auto activations_transpose = transpose_on_input(pool->input_value(0), rank);
        auto output_shape = transpose_output_shape(pool, rank);
        auto new_pool = std::make_shared<opset::v8::ArmMaxPool>(activations_transpose,
                                                                pool->get_strides(),
                                                                pool->get_dilations(),
                                                                pool->get_pads_begin(),
                                                                pool->get_pads_end(),
                                                                pool->get_kernel(),
                                                                pool->get_rounding_type(),
                                                                pool->get_auto_pad(),
                                                                pool->get_index_element_type(),
                                                                pool->get_axis(),
                                                                output_shape);
        auto transpose = transpose_on_output(new_pool->output(0), rank);
        transpose->set_friendly_name(pool->get_friendly_name() + ".0");
        auto transpose_on_indexes = transpose_on_output(new_pool->output(1), rank);
        transpose_on_indexes->set_friendly_name(pool->get_friendly_name() + ".1");
        copy_runtime_info(pool, {new_pool, activations_transpose, transpose, transpose_on_indexes});
        replace_node(pool, {transpose, transpose_on_indexes});

        return true;
    };

    auto m = std::make_shared<ov::pass::pattern::Matcher>(root, "ConvertArmMaxPoolV8Layout");
    register_matcher(m, callback);
}

ConvertArmAvgPoolLayout::ConvertArmAvgPoolLayout() {
    auto root = ov::pass::pattern::wrap_type<opset::v1::ArmAvgPool>(ov::pass::pattern::has_static_rank());

    matcher_pass_callback callback = [=](ov::pass::pattern::Matcher& m) {
        auto node = m.get_match_root();
        if (transformation_callback(node)) {
            return false;
        }
        auto pool = ov::as_type_ptr<opset::v1::ArmAvgPool>(node);
        if (!pool) {
            return false;
        }
        size_t rank = pool->get_output_partial_shape(0).size();
        if (rank < 4 || rank > 5) {
            return false;
        }
        auto activations_transpose = transpose_on_input(pool->input_value(0), rank);
        auto output_shape = transpose_output_shape(pool, rank);
        auto new_pool = std::make_shared<opset::v1::ArmAvgPool>(activations_transpose,
                                                                pool->get_strides(),
                                                                pool->get_pads_begin(),
                                                                pool->get_pads_end(),
                                                                pool->get_kernel(),
                                                                pool->get_exclude_pad(),
                                                                pool->get_rounding_type(),
                                                                pool->get_auto_pad(),
                                                                output_shape);
        auto transpose = transpose_on_output(new_pool, rank);
        transpose->set_friendly_name(pool->get_friendly_name());
        copy_runtime_info(pool, {new_pool, activations_transpose, transpose});
        replace_node(pool, transpose);

        return true;
    };

    auto m = std::make_shared<ov::pass::pattern::Matcher>(root, "ConvertArmAvgPoolLayout");
    register_matcher(m, callback);
}

} // pass
} // ArmPlugin

