#pragma once
/* Mac BOTW 1.9.0, translated route B. Keep the shipped SDR body verbatim.
 * The host only selects this twin for a tracked RGB10A2 -> FP16 attachment. */
#include "totk_hdr_compose.h"
#define BOTW_HDR_COMPOSE_KEY "f7ee6fe10b925543e905dfc9437ce67dc21a118d465b03bb3c9eccafdf2e33c4-"
#define BOTW_HDR_COPY_FS "ecb4242efaf4042be5db42db3b06b93d16e1087c47db79882dc534df9e646f73-"
#define BOTW_HDR_COPY_VS "e4b5b0967a3cd0b34a651ab97dd905f69265b56fc2adfbe154fcfb65b4287e76-"
static inline NSString *botw_hdr_compose_source(NSString *source) {
    /* Check every input to the replacement, not just the output assignment.
     * A different translator layout must fail closed, never map the wrong SSA. */
    NSArray<NSString *> *anchors=@[
        @"using namespace metal;", @"[[buffer(0)]]",
        @"float _49 = _47.x;", @"float _51 = _47.y;", @"float _53 = _47.z;",
        @"float _57 = _55.x;", @"float _59 = _55.y;", @"float _61 = _55.z;",
        @"float _63 = precise::min(_49, 10000.0);",
        @"float _65 = precise::min(_51, 10000.0);",
        @"float _71 = precise::min(_53, 10000.0);",
        @"float _67 = _63 + _57;", @"float _69 = _65 + _59;", @"float _75 = _71 + _61;",
        @"spvDescriptorSet0.fp_t_tcb_8.sample(", @"spvDescriptorSet0.fp_t_tcb_A.sample(",
        @"(*spvDescriptorSet0.fp_c1)._m0[0].x", @"(*spvDescriptorSet0.fp_c1)._m0[0].y",
        @"(*spvDescriptorSet0.fp_c1)._m0[0].z", @"(*spvDescriptorSet0.fp_c3)._m0[0].x",
        @"(*spvDescriptorSet0.fp_c3)._m0[0].y",
        /* Which texture feeds which SSA value, the game's own red weight (the one the
         * replacement repeats), and the SDR colour writes sitting before the alpha write:
         * the HDR block goes between them, so it is the last writer of the colour. */
        @"float3 _47 = spvDescriptorSet0.fp_t_tcb_8.sample(", @"float3 _55 = spvDescriptorSet0.fp_t_tcb_A.sample(",
        @"float _73 = _67 * 0.29890000820159912109375;",
        @"    out.m_34.x = _181;\n    out.m_34.y = _183;\n    out.m_34.z = _185;\n    out.m_34.w = 0.0;"
    ];
    for(NSString *a in anchors)if(![source containsString:a])return nil;
    if([source componentsSeparatedByString:@"    out.m_34.w = 0.0;"].count!=2)return nil;
    NSString *s=totk_hdr_uniform(source);if(!s)return nil;
    /* The same tone map variants as TOTK (TOTK_HDR_COMPOSE; default 2, the reduced arithmetic
     * that skips the hue / purity donor step below the pivot). The original, variant 0, cost
     * 1.15 ms a frame at 2560x1440 on an M6 against 0.10 ms for the game's own compose. */
    int variant=totk_hdr_compose_variant();
    NSString *helpers=variant==0?totk_hdr_scene_msl():variant==1?totk_hdr_scene_fast_msl():totk_hdr_scene_reduced_msl();
    if(variant==3) {
        NSString *dark=@"if(!(y<=chroma.z && max(raw.r,max(raw.g,raw.b))<=chroma.z)) {";
        if(![helpers containsString:dark])return nil;
        helpers=[helpers stringByReplacingOccurrencesOfString:dark withString:@"{"];
    }
    /* Neutral raw .18 maps to m=1-exp(-.18) in BOTW. The shared exponential
     * toe already matches that value; compensate Neutwo's small midtone loss
     * before its shoulder: N_p(m/sqrt(1-(m/p)^2)) = m. This gain depends on
     * peak/scene-white and leaves exposure=0 neutral. User grading follows
     * the same controls as TOTK. No guessed game exposure multiplier. */
    NSString *shoulder=@"    float p=max(peak,0.25);";
    if([helpers componentsSeparatedByString:shoulder].count!=2)return nil;
    helpers=[helpers stringByReplacingOccurrencesOfString:shoulder withString:
        @"    float p=max(peak,0.25);\n    float mid=hdr_curve(0.18);\n    wide*=rsqrt(1.0-(mid/p)*(mid/p));"];
    s=[s stringByReplacingOccurrencesOfString:@"using namespace metal;" withString:[@"using namespace metal;\n" stringByAppendingString:helpers]];
    NSString *body=@"    if (hdrConfig.display.x > 0.5) {\n"
        "        out.m_34.xyz = hdr_scene_map(float3(_63,_65,_71) + float3(_57,_59,_61)*hdrConfig.color.y, float3(0.2989000082,(*spvDescriptorSet0.fp_c1)._m0[0].x,(*spvDescriptorSet0.fp_c1)._m0[0].y), (*spvDescriptorSet0.fp_c1)._m0[0].z, (*spvDescriptorSet0.fp_c3)._m0[0].xy, hdrConfig.display.y, hdrConfig.tone, hdrConfig.color, hdrConfig.art) * hdrConfig.display.z;\n"
        "    }\n    out.m_34.w = 0.0;";
    return [s stringByReplacingOccurrencesOfString:@"    out.m_34.w = 0.0;" withString:body];
}
