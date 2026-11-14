/* Included by nvn_mtl.m after its texture, encoder and residency helpers. */
static pthread_mutex_t g_hdr_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { uint64_t serial, frame; id<MTLTexture> texture, base; float ui_white,peak; } g_hdr_swap[8];
static unsigned g_hdr_nswap;
static __thread float t_hdr_present_white=1,t_hdr_present_peak=1;
/* TOTK_HDR_SURFACE_KEEP (default 1; 0 is the surface switched frame by frame, as before): while HDR is on, the surface
 * stays FP16 / EDR once it is, and a frame without an HDR picture (a menu drawn over a still of the scene, a title, a
 * movie) is decoded from sRGB into it at SDR white. Switching the surface for such a frame drains the GPU and changes
 * the layer's format and colour space twice, at the frame's start and end: Breath of the Wild 1.9.0 did that at every
 * opening and closing of the weapon wheel (measured 2026-11-14). */
static __thread int t_hdr_present_sdr;   /* this present puts an SDR picture on the kept FP16 surface */
static int hdr_surface_keep(void) {
    static _Atomic int set=-1;
    int v=atomic_load_explicit(&set,memory_order_relaxed);
    if(v<0) { const char *e=getenv("TOTK_HDR_SURFACE_KEEP");v=!(e && !strcmp(e,"0"));atomic_store(&set,v); }
    return v;
}
static int hdr_on(void) {
#if TARGET_OS_OSX && (!defined(GAME_BOTW) || BOTW_HDR_SUPPORTED)
    return totk_hdr_available();
#else
    return 0;
#endif
}
static void hdr_note_swap(const tex_t *t) {
    if (!hdr_on() || !t) return;
    pthread_mutex_lock(&g_hdr_mu);
    unsigned i=0;for (;i<g_hdr_nswap;i++) if(g_hdr_swap[i].serial==t->serial)break;
    if(i==g_hdr_nswap && i<8)g_hdr_swap[g_hdr_nswap++].serial=t->serial;
    pthread_mutex_unlock(&g_hdr_mu);
}
static id<MTLTexture> hdr_swap_texture(uint64_t serial, uint64_t frame,float *white,float *peak) {
    id<MTLTexture> result=nil;
    if(hdr_on()) { pthread_mutex_lock(&g_hdr_mu);
        for(unsigned i=0;i<g_hdr_nswap;i++)if(g_hdr_swap[i].serial==serial && (frame==UINT64_MAX || g_hdr_swap[i].frame==frame)){
            result=g_hdr_swap[i].texture;if(white)*white=g_hdr_swap[i].ui_white;if(peak)*peak=g_hdr_swap[i].peak;
        }
        pthread_mutex_unlock(&g_hdr_mu); }
    return result;
}
/* Transfer pixels, not just a reference to a reusable serial-indexed texture.
 * The spare belongs to a retired, non-busy ring slot. Both textures stay resident;
 * capture validates its size/format before writing the spare on the same queue. */
static id<MTLTexture> hdr_swap_take(uint64_t serial,uint64_t frame,id<MTLTexture> base,id<MTLTexture> spare,float *white,float *peak) {
    id<MTLTexture> result=nil;
    if(!hdr_ring_on() || !atomic_load(&g_hdr_frame_enabled))return nil;
    pthread_mutex_lock(&g_hdr_mu);
    for(unsigned i=0;i<g_hdr_nswap;i++)if(g_hdr_swap[i].serial==serial && g_hdr_swap[i].frame==frame && g_hdr_swap[i].base==base) {
        result=g_hdr_swap[i].texture;
        if(result) {
            *white=g_hdr_swap[i].ui_white;*peak=g_hdr_swap[i].peak;
            g_hdr_swap[i].texture=spare;g_hdr_swap[i].base=nil;g_hdr_swap[i].frame=UINT64_MAX;
        }
        break;
    }
    pthread_mutex_unlock(&g_hdr_mu);return result;
}
/* The compose is the only origin of new HDR data. Propagate its FP16
 * companion through draws that consume it, then keep using it for UI blending.
 * Resource identity + current frame prevent unrelated R11 textures being upgraded. */
#if BOTW_HDR_SUPPORTED
#include "botw_hdr_render.h"
#endif
static void hdr_prepare_draw(NvProg *pr) {
#if BOTW_HDR_SUPPORTED
    botw_hdr_prepare_draw(pr);
#else
    g_hdr_draw_native=0;
    if(!hdr_shadow_enabled() || !pr || !pr->sh[1] || R.rt.n!=1 || !R.rt.color[0])return;
    NvShader *fs=pr->sh[1];
    id<MTLTexture> dst=tex_mtl(R.rt.color[0]);
    if(!dst || dst.pixelFormat!=MTLPixelFormatRG11B10Float || dst.textureType!=MTLTextureType2D ||
       dst.mipmapLevelCount!=1 || dst.sampleCount!=1 || dst.width<64 || dst.height<64)return;
    const view_t *v=&R.rt.cview[0];
    if(R.rt.has_cview[0] && ((v->set_levels && v->base_level) || (v->set_layers && v->min_layer)))return;
    BOOL origin=fs->hdr_compose==1;
    BOOL linked=hdr_shadow_find(dst)!=nil;
    if(!linked && !origin)for(int i=0;i<fs->rf.nb;i++) {
        const nvn_rbind *b=&fs->rf.b[i];
        if(b->kind!=NVN_RB_TEX || b->reg>=128)continue;
        int tid=(int)(S.tex[1][b->reg]&0xfffff);
        void *raw=tid>0?tex_raw_lk(tid):NULL;
        if(hdr_shadow_find((__bridge id<MTLTexture>)raw)){linked=YES;break;}
    }
    if(!origin && !linked)return;
    /* Intercepts below may consume a pending clear without opening a pass. */
    if(hdr_shadow_make_draw(dst))g_hdr_draw_native=1;
#endif
}
/* Preserve the float canvas before the exact final copy writes to the 8-bit
 * sRGB swapchain. Never promote arbitrary guest textures or their CPU storage. */
static void hdr_capture(NvProg *pr) {
    if(!hdr_on() || !atomic_load(&g_hdr_frame_enabled) || !pr || !pr->sh[0] || !pr->sh[1] || R.rt.n!=1 || !R.rt.color[0])return;
#if BOTW_HDR_SUPPORTED
    if(strncmp(pr->sh[1]->key,BOTW_HDR_COPY_FS,65) || strncmp(pr->sh[0]->key,BOTW_HDR_COPY_VS,65))return;
#else
    if(strncmp(pr->sh[1]->key,"dd3d1e0ebf73242845970b5655fac199a30f5acf747975c5e771caa49af8b12b-",65) ||
       strncmp(pr->sh[0]->key,"1a40e5f5ff257e13293c01b8e293a619d2ae8cdcffd8e4083e04dd7f59fd2273-",65))return;
#endif
    unsigned slot=8;pthread_mutex_lock(&g_hdr_mu);
    for(unsigned i=0;i<g_hdr_nswap;i++)if(g_hdr_swap[i].serial==R.rt.color[0]->serial)slot=i;
    pthread_mutex_unlock(&g_hdr_mu);if(slot==8)return;
    int tid=(int)(S.tex[1][0]&0xfffff);
#if BOTW_HDR_SUPPORTED
    /* This copy shader also exists outside gameplay. Only a current-frame
     * companion is evidence that its source passed through our scene map. */
    if(tid<=0 || !hdr_shadow_find((__bridge id<MTLTexture>)tex_raw_lk(tid)))return;
#endif
    id<MTLTexture> src=tid>0?(__bridge id<MTLTexture>)tex_fast_lk(tid):nil;
    id<MTLTexture> dst=tex_mtl(R.rt.color[0]);
    if(!src || !dst || src.pixelFormat!=MTLPixelFormatRGBA16Float || src.textureType!=MTLTextureType2D ||
       src.width>dst.width || src.height>dst.height || !S.has_vp || S.vp[0][0]!=0 || S.vp[0][1]!=0 ||
       S.vp[0][2]!=(float)src.width || S.vp[0][3]!=(float)src.height || (S.color.blend_en&1) || S.ds.dt || S.ds.st)return;
    rp_end();blit_close();rs_sync();
    id<MTLTexture> out=hdr_swap_texture(R.rt.color[0]->serial,UINT64_MAX,NULL,NULL);
    if(!out || out.width!=dst.width || out.height!=dst.height || out.pixelFormat!=MTLPixelFormatRGBA16Float || !(out.usage&MTLTextureUsageRenderTarget)) {
        MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:dst.width height:dst.height mipmapped:NO];
        d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;
        if(out)defer_obj_release(out);
        out=[g_dev newTextureWithDescriptor:d];if(!out)MDIE("HDR float swapchain allocation failed");
        res_add(out);res_commit();pthread_mutex_lock(&g_hdr_mu);g_hdr_swap[slot].texture=out;pthread_mutex_unlock(&g_hdr_mu);
        MLOG("[hdr] float swapchain %llu: %lux%lu",R.rt.color[0]->serial,(unsigned long)dst.width,(unsigned long)dst.height);
    }
    /* A render copy preserves the FP16 canvas, including negative wide-gamut RGB.
     * present_encode uses the current viewport below, with no output upscale.
     * This private full-screen triangle has no guest scissor, blend or discard;
     * matching extents therefore overwrite every pixel without a clear. */
    static id<MTLRenderPipelineState> pso;
    if(!pso) {
        NSString *msl=totk_hdr_copy_source();
        NSError *e=nil;id<MTLLibrary> l=[g_dev newLibraryWithSource:msl options:nil error:&e];
        MTLRenderPipelineDescriptor *pd=[MTLRenderPipelineDescriptor new];pd.vertexFunction=[l newFunctionWithName:@"vs"];pd.fragmentFunction=[l newFunctionWithName:@"fs"];pd.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA16Float;
        pso=[g_dev newRenderPipelineStateWithDescriptor:pd error:&e];if(!pso)MDIE("HDR copy pipeline: %s",e.localizedDescription.UTF8String);
    }
    MTL4RenderPassDescriptor *pd=[MTL4RenderPassDescriptor new];pd.colorAttachments[0].texture=out;pd.colorAttachments[0].loadAction=hdr_capture_load_on() && src.width==out.width && src.height==out.height?MTLLoadActionDontCare:MTLLoadActionClear;pd.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);pd.colorAttachments[0].storeAction=MTLStoreActionStore;
    hdr_copy_note(HDR_COPY_CAPTURE,0,src.width,src.height);
    hdr_copy_note(HDR_COPY_CLEAR,pd.colorAttachments[0].loadAction==MTLLoadActionDontCare,out.width,out.height);
    pt_mark(g_cb,'h',pd);
    id<MTL4RenderCommandEncoder> en=[g_cb renderCommandEncoderWithDescriptor:pd];
    [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    [g_bloomq_args setTexture:src.gpuResourceID atIndex:0];[en setArgumentTable:g_bloomq_args atStages:MTLRenderStageFragment];
    [en setViewport:(MTLViewport){0,0,src.width,src.height,0,1}];[en setRenderPipelineState:pso];[en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];m4_end(en);
    pthread_mutex_lock(&g_hdr_mu);g_hdr_swap[slot].base=dst;g_hdr_swap[slot].frame=atomic_load(&g_cur_frame);g_hdr_swap[slot].ui_white=g_hdr_frame_uniform.v[3];g_hdr_swap[slot].peak=g_hdr_frame_peak;pthread_mutex_unlock(&g_hdr_mu);
    static uint64_t n;if(++n%1200==1)MLOG("[hdr] preserved float canvas before SDR copy (%llu frames)",n);
}
