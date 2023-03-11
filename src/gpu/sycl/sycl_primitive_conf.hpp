/*******************************************************************************
* Copyright 2022 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef GPU_SYCL_SYCL_PRIMITIVE_CONF_HPP
#define GPU_SYCL_SYCL_PRIMITIVE_CONF_HPP

#include "gpu/sycl/sycl_post_ops.hpp"
#include "gpu/sycl/sycl_types.hpp"

namespace dnnl {
namespace impl {
namespace gpu {
namespace sycl {

struct sycl_binary_conf_t {
    sycl_md_t src0_md;
    sycl_md_t src1_md;
    sycl_md_t dst_md;

    alg_kind_t alg_kind;
    bool do_scale_src0;
    bool do_scale_src1;
    int broadcast_dims[sycl_md_t::max_dims];
    int ndims;
    bool is_tensor_op;

    int block_size;
    int wg_size;

    sycl_post_ops_t post_ops;
};

struct sycl_pooling_conf_t {
    sycl_md_t src_md;
    sycl_md_t src1_md[8];
    sycl_md_t dst_md;
    sycl_md_t ws_md;
    sycl_md_t diff_src_md;
    sycl_md_t diff_dst_md;
    int ndims;
    int po_len;
    bool zero_dims;
    int block_size;
    int wg_size;
    size_t n_thr;
    alg_kind_t alg;
    dim_t MB;
    dim_t OC;
    dim_t OD;
    dim_t OH;
    dim_t OW;
    dim_t ID;
    dim_t IH;
    dim_t IW;
    dim_t KD;
    dim_t KH;
    dim_t KW;
    dim_t SD;
    dim_t SH;
    dim_t SW;
    dim_t padF;
    dim_t padT;
    dim_t padL;
    dim_t DD;
    dim_t DH;
    dim_t DW;
    dims_t dst_dims;
    int dst_ndims;
    sycl_post_ops_t post_ops;
};

CHECK_SYCL_KERNEL_ARG_TYPE(sycl_pooling_conf_t);
CHECK_SYCL_KERNEL_ARG_TYPE(sycl_binary_conf_t);

} // namespace sycl
} // namespace gpu
} // namespace impl
} // namespace dnnl

#endif
