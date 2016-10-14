/*******************************************************************************
* Copyright 2016 Intel Corporation
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

#include "mkldnn_test_common.hpp"
#include "gtest/gtest.h"

#include "mkldnn.hpp"

namespace mkldnn {

template <typename data_t>
void compute_ref_conv_bwd_data(test_convolution_descr_t c, memory diff_src,
        memory weights, memory diff_dst)
{
    data_t *diff_dst_data = (data_t *)diff_dst.get_data_handle();
    data_t *weights_data = (data_t *)weights.get_data_handle();
    data_t *diff_src_data = (data_t *)diff_src.get_data_handle();

    const memory::desc diff_src_d = diff_src.get_primitive_desc().desc();
    const memory::desc weights_d = weights.get_primitive_desc().desc();
    const memory::desc diff_dst_d = diff_dst.get_primitive_desc().desc();

#   pragma omp parallel for collapse(5) schedule(static)
    for (int mb = 0; mb < c.mb; ++mb) {
        for (int g = 0; g < c.ng; ++g) {
            for (int ic = 0; ic < c.ic / c.ng; ++ic) {
                for (int ih = 0; ih < c.ih; ++ih) {
                    for (int iw = 0; iw < c.iw; ++iw) {
                        int sidx = mb * c.ic * c.ih * c.iw
                                + g * c.ic / c.ng * c.ih * c.iw
                                + ic * c.ih * c.iw + ih * c.iw + iw;
                        diff_src_data[map_index(diff_src_d, sidx)] = data_t(0);
                        for (int oc = 0; oc < c.oc / c.ng; oc++) {
                            for (int kh = 0; kh < c.kh; kh++) {
                                for (int kw = 0; kw < c.kw; kw++) {
                                    if (iw + c.padw < kw || ih + c.padh < kh)
                                        continue;
                                    int ow = iw - kw + c.padw;
                                    int oh = ih - kh + c.padh;
                                    if (ow % c.strw != 0 || oh % c.strh != 0)
                                        continue;
                                    ow /= c.strw;
                                    oh /= c.strh;

                                    if (oh < c.oh && ow < c.ow) {
                                        int didx = mb * c.oc * c.oh * c.ow
                                                + g * c.oc / c.ng * c.oh * c.ow
                                                + oc * c.oh * c.ow + oh * c.ow
                                                + ow;
                                        int widx = g * c.oc / c.ng * c.ic
                                                        / c.ng * c.kh * c.kw
                                                + oc * c.ic / c.ng * c.kh * c.kw
                                                + ic * c.kh * c.kw + kh * c.kw
                                                + kw;

                                        diff_src_data[map_index(diff_src_d, sidx)]
                                            += diff_dst_data[map_index(diff_dst_d, didx)]
                                            * weights_data[map_index(
                                                    weights_d, widx)];
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

struct conv_bwd_data_test_params {
    const engine::kind engine_kind;
    algorithm aalgorithm;
    memory::format dst_format;
    memory::format weights_format;
    memory::format src_format;
    test_convolution_descr_t test_cd;
};

template <typename data_t>
class convolution_backward_data_test
            : public ::testing::TestWithParam<conv_bwd_data_test_params> {
protected:
    virtual void SetUp()
    {
        conv_bwd_data_test_params p
                = ::testing::TestWithParam<
                conv_bwd_data_test_params>::GetParam();

        ASSERT_TRUE(p.engine_kind == engine::kind::cpu);
        ASSERT_EQ(p.aalgorithm, convolution_direct);
        auto eng =  engine(p.engine_kind, 0);
        memory::data_type data_type = data_traits<data_t>::data_type;
        ASSERT_EQ(data_type, mkldnn::memory::data_type::f32);

        test_convolution_descr_t cd = p.test_cd;

        auto c_src_desc
                = create_md({ cd.mb, cd.ic, cd.ih, cd.iw }, data_type, p.src_format);
        auto c_weights_desc = cd.ng > 1 ?
                create_md({ cd.ng, cd.oc / cd.ng, cd.ic / cd.ng, cd.kh, cd.kw },
                        data_type, p.weights_format) :
                create_md(
                        { cd.oc, cd.ic, cd.kh, cd.kw }, data_type, p.weights_format);
        auto c_dst_desc
                = create_md({ cd.mb, cd.oc, cd.oh, cd.ow }, data_type, p.dst_format);

        auto src_primitive_desc = memory::primitive_desc(c_src_desc, eng);
        auto weights_primitive_desc = memory::primitive_desc(c_weights_desc, eng);
        auto dst_primitive_desc = memory::primitive_desc(c_dst_desc, eng);

        auto c_diff_src = memory(src_primitive_desc);
        auto c_weights = memory(weights_primitive_desc);
        auto c_diff_dst = memory(dst_primitive_desc);

        std::vector<int> padR = { cd.padh, cd.padw };
        for (int i = 0; i < 2; ++i) {
        if ((cd.ih + cd.padh + padR[0] - cd.kh)/cd.strh + 1 != cd.oh) ++padR[0];
        if ((cd.iw + cd.padw + padR[1] - cd.kw)/cd.strw + 1 != cd.ow) ++padR[1];
        }

        auto conv_desc = convolution_forward::desc(prop_kind::forward_training,
                p.aalgorithm, c_src_desc, c_weights_desc, c_dst_desc,
                { cd.strh, cd.strw }, { cd.padh, cd.padw }, padR,
                padding_kind::zero);

        auto conv_bwd_data_desc = convolution_backward_data::desc(p.aalgorithm,
                c_src_desc, c_weights_desc, c_dst_desc,
                { cd.strh, cd.strw }, { cd.padh, cd.padw }, padR,
                padding_kind::zero);

        auto conv_primitive_desc = convolution_forward::primitive_desc(
                conv_desc, eng);

        auto conv_bwd_data_primitive_desc =
                convolution_backward_data::primitive_desc(conv_bwd_data_desc,
                        eng, conv_primitive_desc);

        // Only true for dense format
        fill_data<data_t>(
                c_weights.get_primitive_desc().get_size() / sizeof(data_t),
                (data_t *)c_weights.get_data_handle());
        fill_data<data_t>(
                c_diff_dst.get_primitive_desc().get_size() / sizeof(data_t),
                (data_t *)c_diff_dst.get_data_handle());

        auto conv_bwd_data =
                convolution_backward_data(conv_bwd_data_primitive_desc,
                        c_diff_dst, c_weights, c_diff_src);

        std::vector<primitive> pipeline;
        pipeline.push_back(conv_bwd_data);
        stream(stream::kind::lazy).submit(pipeline).wait();

        auto ref_memory = memory(memory::primitive_desc(c_src_desc, eng));
        compute_ref_conv_bwd_data<data_t>(cd, ref_memory, c_weights, c_diff_dst);
        compare_data<data_t>(ref_memory, c_diff_src);

    }
};

using convolution_backward_data_test_float
        = convolution_backward_data_test<float>;
using conv_bwd_data_test_params_float = conv_bwd_data_test_params;

TEST_P(convolution_backward_data_test_float, TestsConvolution)
{
}

INSTANTIATE_TEST_CASE_P(
        TestConvolutionBackwardData,
        convolution_backward_data_test_float,
        ::testing::Values(
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nchw,
                        memory::format::oihw, memory::format::nchw,
                        { 2, 1, 4, 4, 4, 6, 4, 4, 3, 3, 1, 1, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nchw,
                        memory::format::oihw, memory::format::nchw,
                        { 2, 1, 4, 4, 4, 6, 2, 2, 3, 3, 0, 0, 1, 1 } }));

INSTANTIATE_TEST_CASE_P(
        TestConvolutionBackwardDataNoBias,
        convolution_backward_data_test_float,
        ::testing::Values(
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nchw,
                        memory::format::oihw, memory::format::nchw,
                        { 2, 1, 4, 4, 4, 6, 4, 4, 3, 3, 1, 1, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nchw,
                        memory::format::oihw, memory::format::nchw,
                        { 2, 1, 4, 4, 4, 6, 2, 2, 3, 3, 0, 0, 1, 1 } }));

INSTANTIATE_TEST_CASE_P(
        TestConvolutionBackwardDataNHWC,
        convolution_backward_data_test_float,
        ::testing::Values(
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nhwc,
                        memory::format::oihw, memory::format::nhwc,
                        { 2, 1, 4, 4, 4, 6, 4, 4, 3, 3, 1, 1, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nhwc,
                        memory::format::oihw, memory::format::nhwc,
                        { 2, 1, 4, 4, 4, 6, 2, 2, 3, 3, 0, 0, 1, 1 } }));

INSTANTIATE_TEST_CASE_P(
        TestConvolutionBackwardDataBlocked,
        convolution_backward_data_test_float,
        ::testing::Values(
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nChw8c,
                        memory::format::OIhw8i8o, memory::format::nChw8c,
                        { 2, 1, 32, 13, 13, 32, 12, 12, 3, 3, 0, 0, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nChw8c,
                        memory::format::OIhw8i8o, memory::format::nChw8c,
                        { 2, 1, 32, 3, 3, 32, 4, 4, 3, 3, 1, 1, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nChw8c,
                        memory::format::OIhw8i8o, memory::format::nChw8c,
                        { 2, 1, 32, 4, 4, 32, 4, 4, 3, 3, 0, 0, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nChw8c,
                        memory::format::OIhw8i8o, memory::format::nChw8c,
                        { 2, 1, 32, 3, 3, 32, 2, 2, 3, 3, 0, 0, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nChw8c,
                        memory::format::OIhw8i8o, memory::format::nChw8c,
                        { 2, 1, 32, 2, 2, 32, 2, 2, 3, 3, 1, 1, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nChw8c,
                        memory::format::OIhw8i8o, memory::format::nChw8c,
                        { 2, 1, 32, 13, 13, 48, 13, 13, 3, 3, 1, 1, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nChw8c,
                        memory::format::OIhw8i8o, memory::format::nChw8c,
                        { 2, 1, 32, 13, 13, 48, 11, 11, 3, 3, 0, 0, 1, 1 } }));

INSTANTIATE_TEST_CASE_P(
        TestConvolutionAlexnetBackwardDataNCHW,
        convolution_backward_data_test_float,
        ::testing::Values(
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nchw,
                        memory::format::oihw, memory::format::nchw,
                        { 2, 1, 3, 227, 227, 96, 55, 55, 11, 11, 0, 0, 4, 4 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nchw,
                        memory::format::goihw, memory::format::nchw,
                        { 2, 2, 96, 27, 27, 256, 27, 27, 5, 5, 2, 2, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nchw,
                        memory::format::oihw, memory::format::nchw,
                        { 2, 1, 256, 13, 13, 384, 13, 13, 3, 3, 1, 1, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nchw,
                        memory::format::goihw, memory::format::nchw,
                        { 2, 2, 384, 13, 13, 384, 13, 13, 3, 3, 1, 1, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nchw,
                        memory::format::goihw, memory::format::nchw,
                        { 2, 2, 384, 13, 13, 256, 13, 13, 3, 3, 1, 1, 1,
                                1 } }));

INSTANTIATE_TEST_CASE_P(
        TestConvolutionAlexnetBackwardDataBlocked,
        convolution_backward_data_test_float,
        ::testing::Values(
#if 0
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nchw,
                        memory::format::Ohwi8o, memory::format::nChw8c,
                        { 2, 1, 3, 227, 227, 96, 55, 55, 11, 11, 0, 0, 4, 4 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nhwc,
                        memory::format::Ohwi8o, memory::format::nChw8c,
                        { 2, 1, 3, 227, 227, 96, 55, 55, 11, 11, 0, 0, 4, 4 } },
#endif
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nChw8c,
                        memory::format::gOIhw8i8o, memory::format::nChw8c,
                        { 2, 2, 96, 27, 27, 256, 27, 27, 5, 5, 2, 2, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nChw8c,
                        memory::format::OIhw8i8o, memory::format::nChw8c,
                        { 2, 1, 256, 13, 13, 384, 13, 13, 3, 3, 1, 1, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nChw8c,
                        memory::format::gOIhw8i8o, memory::format::nChw8c,
                        { 2, 2, 384, 13, 13, 384, 13, 13, 3, 3, 1, 1, 1, 1 } },
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nChw8c,
                        memory::format::gOIhw8i8o, memory::format::nChw8c,
                        { 2, 2, 384, 13, 13, 256, 13, 13, 3, 3, 1, 1, 1,
                                1 } }));
#if 0
INSTANTIATE_TEST_CASE_P(
        TestConvolutionCifar10BackwardDataBlocked,
        convolution_backward_data_test_float,
        ::testing::Values(
                conv_bwd_data_test_params_float{ engine::kind::cpu,
                        convolution_direct, memory::format::nchw,
                        memory::format::Ohwi8o, memory::format::nChw8c,
                        { 2, 1, 3, 32, 32, 32, 32, 32, 5, 5, 2, 2, 1, 1 } }));
#endif
}
