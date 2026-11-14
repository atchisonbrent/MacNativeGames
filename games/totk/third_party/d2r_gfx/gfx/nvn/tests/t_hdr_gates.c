// Build once per version; each invocation starts with fresh environment caches.
#ifndef TEST_HDR_RUNTIME
#define TOTK_HDR_FRONTEND 1
#endif
#ifdef TEST_HDR_NONMAC
// Exercise the non-macOS preprocessor branch without requiring an iOS SDK.
#include <TargetConditionals.h>
#undef TARGET_OS_OSX
#define TARGET_OS_OSX 0
#endif
#include "../totk_hdr.h"
#include <stdio.h>
#ifdef TEST_HDR_RUNTIME
static int enabled, parameter_calls;
int front_hdr_supported(void) {
    const char *e=getenv("TOTK_HDR_CONTROL");return !(e && !strcmp(e,"0"));
}
int front_hdr_enabled(void) { return enabled; }
void front_hdr_parameters(float *p) {
    parameter_calls++;p[TOTK_HDR_GAME_WHITE]=3;p[TOTK_HDR_UI_WHITE]=2;
}
#endif
int main(void) {
    const char *e=getenv("TOTK_HDR"),*control=getenv("TOTK_HDR_CONTROL");
    int available=EXPECT_HDR && !(control && !strcmp(control,"0"));
    if(totk_hdr_available()!=available || totk_hdr_requested()!=(EXPECT_HDR && e && !strcmp(e,"1")))return 1;
    TotkHDRUniform u;float peak=totk_hdr_uniform_snapshot(&u,4,1);
#ifdef TEST_HDR_RUNTIME
    if(totk_hdr_active())return 1;
    enabled=1;if(totk_hdr_active()!=available)return 1;
    if(parameter_calls!=EXPECT_HDR || peak!=4 || u.v[3]!=(EXPECT_HDR?2:1) ||
       fabsf(u.v[1]-(EXPECT_HDR?4.0f/3:4))>1e-6f || u.v[2]!=(EXPECT_HDR?1.5f:1))return 1;
#else
    if(sizeof u!=64 || TOTK_HDR_PARAMETER_COUNT!=13 || peak!=4 || u.v[1]!=4 || u.v[2]!=1 || u.v[3]!=1)return 1;
#endif
    puts("PASS HDR gates / uniform");return 0;
}
