// GPU-free: always exercise the rewrite on synthetic MSL, optionally game MSL too.
// BOTW_HDR_COMPOSE_MSL=/path/to/f7ee..._b.metal; never check that file in.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <math.h>
#include "../botw_hdr_compose.h"
#include "../botw_hdr_shadow.h"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)
static int check_twin(const char *path,const char *output) {
    printf("Checking BOTW HDR twin: %s\n",path);
    NSError *error=nil;
    NSString *source=[NSString stringWithContentsOfFile:@(path) encoding:NSUTF8StringEncoding error:&error];
    CHECK(source!=nil);
    NSString *twin=botw_hdr_compose_source(source);
    CHECK(twin!=nil);
    CHECK([twin containsString:@"[[buffer(30)]]"]);
    CHECK([twin containsString:@"float3(_63,_65,_71) + float3(_57,_59,_61)*hdrConfig.color.y"]);
    CHECK([twin containsString:@"if (hdrConfig.display.x > 0.5)"]);
    CHECK([twin containsString:@"wide*=rsqrt(1.0-(mid/p)*(mid/p));"]);
    // The HDR block is the last writer of the colour: after the game's SDR writes, before alpha.
    CHECK([twin containsString:@"    out.m_34.z = _185;\n    if (hdrConfig.display.x > 0.5) {"]);
    NSRange map=[twin rangeOfString:@"out.m_34.xyz = hdr_scene_map("];
    NSRange sdr=[twin rangeOfString:@"out.m_34.z = _185;" options:NSBackwardsSearch];
    CHECK(map.location!=NSNotFound && sdr.location!=NSNotFound);
    CHECK([twin componentsSeparatedByString:@"out.m_34.xyz = hdr_scene_map("].count==2);
    CHECK(sdr.location<map.location);
    // The tone map is the variant TOTK_HDR_COMPOSE names (the Makefile runs each in its own process): 2, the default,
    // and only 2, skips the donor step below the pivot; every variant gets the middle-grey line once.
    const char *e=getenv("TOTK_HDR_COMPOSE");int variant=e && !strcmp(e,"0")?0:e && !strcmp(e,"1")?1:e && !strcmp(e,"3")?3:2;
    NSString *dark=@"if(!(y<=chroma.z && max(raw.r,max(raw.g,raw.b))<=chroma.z)) {";
    CHECK([twin containsString:dark]==(variant==2));
    CHECK([twin containsString:@"float3 sdr=hdr_curve3(raw);"]==(variant>=1));
    CHECK([twin containsString:@"bool3 upper=wide>p;"]==(variant>=2));
    CHECK([twin componentsSeparatedByString:@"    float p=max(peak,0.25);\n    float mid=hdr_curve(0.18);\n    wide*=rsqrt(1.0-(mid/p)*(mid/p));"].count==2);
    // All original SDR statements remain, in their original order.
    NSUInteger cursor=0;
    for(NSString *line in [source componentsSeparatedByString:@"\n"]) {
        if(!line.length || [line containsString:@"fragment main0_out"])continue;
        NSRange r=[twin rangeOfString:line options:0 range:NSMakeRange(cursor,twin.length-cursor)];
        CHECK(r.location!=NSNotFound);cursor=NSMaxRange(r);
    }
    // Deliberate translator drift must disable HDR, including a binding conflict.
    for(NSString *a in @[@"float _63 = precise::min(_49, 10000.0);",
        @"float _65 = precise::min(_51, 10000.0);", @"float _71 = precise::min(_53, 10000.0);",
        @"float _67 = _63 + _57;", @"float _69 = _65 + _59;", @"float _75 = _71 + _61;",
        @"(*spvDescriptorSet0.fp_c3)._m0[0].y", @"out.m_34.w = 0.0;", @"[[buffer(0)]]",
        @"spvDescriptorSet0.fp_t_tcb_A.sample(", @"float3 _47 = spvDescriptorSet0.fp_t_tcb_8.sample(",
        @"float3 _55 = ", @"float3 _47 = ", @"    out.m_34.x = _181;", @"    out.m_34.y = _183;",
        @"float _73 = _67 * 0.29890000820159912109375;", @"    out.m_34.z = _185;\n    out.m_34.w"]) {
        CHECK([source containsString:a]);
        CHECK(botw_hdr_compose_source([source stringByReplacingOccurrencesOfString:a withString:@"DRIFT"])==nil);
    }
    // A second alpha write would get a second HDR block.
    CHECK(botw_hdr_compose_source([source stringByReplacingOccurrencesOfString:@"    return out;" withString:@"    out.m_34.w = 0.0;\n    return out;"])==nil);
    CHECK([source containsString:@"    return out;"]);
    CHECK(botw_hdr_compose_source([source stringByAppendingString:@"// [[buffer(30)]]"])==nil);
    CHECK(botw_hdr_compose_source(nil)==nil);
    if(![twin writeToFile:@(output) atomically:NO encoding:NSUTF8StringEncoding error:&error]) {
        fprintf(stderr,"write twin: %s\n",error.localizedDescription.UTF8String);return 1;
    }
    puts("PASS BOTW HDR twin, SDR preservation and fail-closed anchors");
    return 0;
}

int main(int argc,const char **argv) { @autoreleasepool {
    // Arguments: synthetic fixture, synthetic twin output, optional game twin output.
    CHECK(argc==4);
    CHECK(botw_hdr_shadow_format(MTLPixelFormatRGB10A2Unorm));
    CHECK(!botw_hdr_shadow_format(MTLPixelFormatRG11B10Float));
    CHECK(!botw_hdr_shadow_format(MTLPixelFormatRGBA8Unorm_sRGB));
    // The neutral default must retain BOTW's .18 grey at every allowed peak.
    double m=-expm1(-0.18);
    for(double p=0.25;p<=64;p*=2) {
        double x=m/sqrt(1-(m/p)*(m/p));
        CHECK(fabs(p*x/hypot(x,p)-m)<1e-12);
        CHECK(p*100/hypot(100,p)<p);
    }
    puts("PASS BOTW HDR policy / midgrey");
    CHECK(check_twin(argv[1],argv[2])==0);
    const char *path=getenv("BOTW_HDR_COMPOSE_MSL");
    if(path && *path)CHECK(check_twin(path,argv[3])==0);
    return 0;
} }
