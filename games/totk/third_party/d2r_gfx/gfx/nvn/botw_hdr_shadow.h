#pragma once
/* Policy only; allocation, epochs, mirroring, residency and retirement are
 * identical to TOTK and live in totk_hdr_shadow.h. BOTW has real stored alpha:
 * retain all view swizzles and all blend factors, including destination alpha. */
static inline int botw_hdr_shadow_format(MTLPixelFormat format) {
    return format==MTLPixelFormatRGB10A2Unorm;
}
