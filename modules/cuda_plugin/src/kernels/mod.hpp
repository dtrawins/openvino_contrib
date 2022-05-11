// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "cuda_type_traits.hpp"
#include "elementwise_binary.cuh"

namespace CUDAPlugin {
namespace kernel {

template <typename T>
struct ModOpImpl;

/**
 * Performs element-wise Mod operation with two given tensors applying numpy
 * broadcasting if needed.
 */
class Mod {
public:
    Mod(Type_t element_type, size_t max_threads_per_block, size_t out_num_elements);

    void operator()(cudaStream_t stream,
                    const void* in0,
                    const NumpyBroadcastMapper& in0_mapper,
                    const void* in1,
                    const NumpyBroadcastMapper& in1_mapper,
                    void* out) const;

private:
    ElementwiseBinary<AllElementTypesSwitch, ModOpImpl> impl_;
};

}  // namespace kernel
}  // namespace CUDAPlugin