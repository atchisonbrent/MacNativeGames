// Synthetic shader written from scratch only to exercise the HDR rewrite.
#include <metal_stdlib>
using namespace metal;

struct SyntheticConstants {
    float4 _m0[1];
};

struct SyntheticDescriptorSet {
    texture2d<float> fp_t_tcb_8 [[id(0)]];
    texture2d<float> fp_t_tcb_A [[id(1)]];
    sampler sample8 [[id(2)]];
    sampler sampleA [[id(3)]];
    constant SyntheticConstants* fp_c1 [[id(4)]];
    constant SyntheticConstants* fp_c3 [[id(5)]];
};

struct main0_out {
    float4 m_34 [[color(0)]];
};

fragment main0_out main0(constant SyntheticDescriptorSet& spvDescriptorSet0 [[buffer(0)]]) {
    main0_out out;
    float3 _47 = spvDescriptorSet0.fp_t_tcb_8.sample(spvDescriptorSet0.sample8, float2(0.5)).xyz;
    float3 _55 = spvDescriptorSet0.fp_t_tcb_A.sample(spvDescriptorSet0.sampleA, float2(0.5)).xyz;
    float _49 = _47.x;
    float _51 = _47.y;
    float _53 = _47.z;
    float _57 = _55.x;
    float _59 = _55.y;
    float _61 = _55.z;
    float _63 = precise::min(_49, 10000.0);
    float _65 = precise::min(_51, 10000.0);
    float _71 = precise::min(_53, 10000.0);
    float _67 = _63 + _57;
    float _69 = _65 + _59;
    float _75 = _71 + _61;
    float _73 = _67 * 0.29890000820159912109375;
    float greenWeight = (*spvDescriptorSet0.fp_c1)._m0[0].x;
    float blueWeight = (*spvDescriptorSet0.fp_c1)._m0[0].y;
    float gain = (*spvDescriptorSet0.fp_c1)._m0[0].z;
    float bias = (*spvDescriptorSet0.fp_c3)._m0[0].x;
    float scale = (*spvDescriptorSet0.fp_c3)._m0[0].y;
    float _181 = (_73 + bias) * scale;
    float _183 = (_69 * greenWeight + bias) * gain;
    float _185 = (_75 * blueWeight + bias) * gain;
    out.m_34.x = _181;
    out.m_34.y = _183;
    out.m_34.z = _185;
    out.m_34.w = 0.0;
    return out;
}
