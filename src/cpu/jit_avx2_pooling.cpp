/*******************************************************************************
* Copyright 2016-2017 Intel Corporation
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

#include "mkldnn_types.h"

#include "c_types_map.hpp"
#include "jit_avx2_pooling.hpp"
#include "type_helpers.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

void jit_avx2_pooling_fwd_t::execute_forward() {
    auto src = reinterpret_cast<const data_t *>(this->input_memory(0));
    auto dst = reinterpret_cast<data_t*>(this->memory(0));
    auto indices = conf_.desc()->alg_kind == alg_kind::pooling_avg ? nullptr
        : reinterpret_cast<int*>(this->memory(1));

    const memory_desc_wrapper src_d(conf_.src_pd());
    const memory_desc_wrapper dst_d(conf_.dst_pd());
    const memory_desc_wrapper indices_d(conf_.workspace_pd());

    const auto &jpp = kernel_->jpp;

    auto ker = [&](int n, int b_c, int oh) {
        jit_pool_call_s arg = {};

        const int ij = oh * jpp.stride_h;
        const int i_t_overflow = nstl::max(0, jpp.t_pad-ij);
        const int i_b_overflow = nstl::max(jpp.ih, ij+jpp.kh-jpp.t_pad)-jpp.ih;
        const int ih = nstl::max(ij - jpp.t_pad, 0);

        arg.src = &src[src_d.blk_off(n, b_c, ih, 0)];
        arg.dst = &dst[dst_d.blk_off(n, b_c, oh, 0)];
        if (indices)
            arg.indices = &indices[indices_d.blk_off(n, b_c, oh, 0)];
        arg.kh_padding = jpp.kh - i_t_overflow - i_b_overflow;
        arg.kh_padding_shift = i_t_overflow*jpp.kw;
        arg.kw_padding = 0;

        (*kernel_)(&arg);
    };

#   pragma omp parallel for collapse(3) schedule(static)
    for (int n = 0; n < jpp.mb; ++n) {
        for (int b_c = 0; b_c < jpp.nb_c; ++b_c) {
            for (int oh = 0; oh < jpp.oh; ++oh) {
                ker (n, b_c, oh);
            }
        }
    }
}

void jit_avx2_pooling_bwd_t::execute_backward() {
    auto diff_dst = reinterpret_cast<const data_t *>(this->input_memory(0));
    auto diff_src = reinterpret_cast<data_t*>(this->memory(0));
    auto indices = conf_.desc()->alg_kind == alg_kind::pooling_avg ? nullptr
        : reinterpret_cast<const int*>(this->input_memory(1));

    const memory_desc_wrapper diff_src_d(conf_.diff_src_pd());
    const memory_desc_wrapper diff_dst_d(conf_.diff_dst_pd());
    const memory_desc_wrapper indices_d(conf_.workspace_pd());

    const auto &jpp = kernel_->jpp;

    auto ker = [&](int n, int b_c, int oh) {
        jit_pool_call_s arg = {};

        const int ij = oh * jpp.stride_h;
        const int i_t_overflow = nstl::max(0, jpp.t_pad-ij);
        const int i_b_overflow = nstl::max(jpp.ih, ij+jpp.kh-jpp.t_pad)-jpp.ih;
        const int ih = nstl::max(ij - jpp.t_pad, 0);

        arg.src = &diff_src[diff_src_d.blk_off(n, b_c, ih, 0)];
        arg.dst = &diff_dst[diff_dst_d.blk_off(n, b_c, oh, 0)];
        if (indices)
            arg.indices = &indices[indices_d.blk_off(n, b_c, oh, 0)];
        arg.kh_padding = jpp.kh - i_t_overflow - i_b_overflow;
        arg.kh_padding_shift = i_t_overflow*jpp.kw;
        arg.kw_padding = 0;

        (*kernel_)(&arg);
    };

#   pragma omp parallel for collapse(2) schedule(static)
    for (int n = 0; n < jpp.mb; ++n) {
        for (int b_c = 0; b_c < jpp.nb_c; ++b_c) {
            data_t *dsrc = diff_src + diff_src_d.blk_off(n, b_c);
            for (int i = 0; i < jpp.ih * jpp.iw * simd_w; ++i) dsrc[i] = 0.;
            for (int oh = 0; oh < jpp.oh; ++oh) {
                ker (n, b_c, oh);
            }
        }
    }
}

}
}
}

// vim: et ts=4 sw=4 cindent cino^=l0,\:0,N-s
