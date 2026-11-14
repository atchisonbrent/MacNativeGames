#pragma once
/* Host-only FP16 companions for render and compute scene postprocessing. Guest formats,
 * byte sizes and CPU-visible storage stay intact. No generic format promotion.
 * This file is included after the replay, residency and encoder helpers. */
#if BOTW_HDR_SUPPORTED
#include "botw_hdr_shadow.h"
#endif
static inline int hdr_shadow_format(MTLPixelFormat format) {
#if BOTW_HDR_SUPPORTED
    return botw_hdr_shadow_format(format);
#else
    return format==MTLPixelFormatRG11B10Float;
#endif
}
#define HDR_SHADOW_MAX 24
static struct {
    id<MTLTexture> base, texture;   // strong: a retired base cannot be recycled at the same address
    NSMutableDictionary<NSNumber *,id<MTLTexture>> *reads;
    uint64_t frame, used, generation;
    int fresh, seed_only;   // seed_only: no unbounded FP16 writes since the seed
    int dirty;   // companion contains writes not yet mirrored into base
} g_hdr_shadow[HDR_SHADOW_MAX];
static struct { id<MTLTexture> base, texture; int index; } g_hdr_stores[8];
static unsigned g_hdr_nstores;
static uint64_t g_hdr_shadow_clock, g_hdr_shadow_live_frame=UINT64_MAX;
static int hdr_shadow_enabled(void) {
    /* mtl_set_frame already gates this by platform, game version and user mode. */
    return atomic_load_explicit(&g_hdr_frame_enabled,memory_order_relaxed);
}
/* Companions take shader writes only where a game's compute passes store to them (Bananza's final upscale); TotK and BotW
   render to and sample theirs, and keep the usage they always had. The dispatch probe turns it on. */
#ifdef GAME_NS2
static int g_hdr_companion_store = 1;
#else
static int g_hdr_companion_store;
#endif
static id<MTLTexture> hdr_shadow_base(id<MTLTexture> t);
static id<MTLTexture> hdr_shadow_find(id<MTLTexture> source) {
    if(!source || !hdr_shadow_enabled() || !hdr_shadow_format(source.pixelFormat))return nil;
    id<MTLTexture> base=hdr_shadow_base(source);
    if(source.width!=base.width || source.height!=base.height || source.mipmapLevelCount!=1)return nil;
    uint64_t frame=atomic_load(&g_cur_frame);
    for(unsigned i=0;i<HDR_SHADOW_MAX;i++)
        if(g_hdr_shadow[i].base==base && g_hdr_shadow[i].frame==frame)return g_hdr_shadow[i].texture;
    return nil;
}
/* R11G11B10F has no stored alpha: reading A yields one. A promoted RGBA16
 * target can contain the compose shader's A=0, or a UI shader's opacity. Keep
 * that storage private and expose the original RGB-only sampling contract.
 * The registered view's RGB swizzle is retained, including explicit constants. */
static id<MTLTexture> hdr_shadow_sample(id<MTLTexture> source) {
    id<MTLTexture> hdr=hdr_shadow_find(source);if(!hdr)return nil;
    MTLTextureSwizzleChannels sw=source.swizzle;
#if !BOTW_HDR_SUPPORTED
    if(sw.red==MTLTextureSwizzleAlpha)sw.red=MTLTextureSwizzleOne;
    if(sw.green==MTLTextureSwizzleAlpha)sw.green=MTLTextureSwizzleOne;
    if(sw.blue==MTLTextureSwizzleAlpha)sw.blue=MTLTextureSwizzleOne;
    if(sw.alpha==MTLTextureSwizzleAlpha)sw.alpha=MTLTextureSwizzleOne;
#endif
    uint64_t bits=(uint64_t)sw.red|((uint64_t)sw.green<<8)|((uint64_t)sw.blue<<16)|((uint64_t)sw.alpha<<24)|((uint64_t)source.textureType<<32);
    for(unsigned i=0;i<HDR_SHADOW_MAX;i++)if(g_hdr_shadow[i].texture==hdr){
        if(!g_hdr_shadow[i].reads)g_hdr_shadow[i].reads=[NSMutableDictionary dictionary];
        id<MTLTexture> view=g_hdr_shadow[i].reads[@(bits)];
        if(!view){
            view=[hdr newTextureViewWithPixelFormat:MTLPixelFormatRGBA16Float textureType:source.textureType levels:NSMakeRange(0,1) slices:NSMakeRange(0,1) swizzle:sw];
            if(!view)MDIE("HDR RGB-only sampling view creation failed");
            res_add(view);g_hdr_shadow[i].reads[@(bits)]=view;
        }
        return view;
    }
    return nil;
}
/* Blend hardware reads the attachment, not its sampling view. In an RGB-only
 * destination Ad=1: DstAlpha -> One, OneMinusDstAlpha -> Zero, and saturated
 * source alpha min(As,1-Ad) -> Zero. Source-alpha opacity remains unchanged. */
static int hdr_shadow_rgb_factor(int factor) {
    switch(factor){
        case NVN_BLEND_FUNC_DST_ALPHA:return NVN_BLEND_FUNC_ONE;
        case NVN_BLEND_FUNC_ONE_MINUS_DST_ALPHA:
        case NVN_BLEND_FUNC_SRC_ALPHA_SATURATE:return NVN_BLEND_FUNC_ZERO;
        default:return factor;
    }
}
/* A render copy converts formats, unlike a blit. Guest readers use a synced
 * original; rendering and full-size sampling keep the FP16 companion. */
static void hdr_shadow_copy(id<MTLTexture> src,id<MTLTexture> dst) {
    static id<MTLRenderPipelineState> to16,toGuest;
    id<MTLRenderPipelineState> pso=dst.pixelFormat==MTLPixelFormatRGBA16Float?to16:toGuest;
    if(!pso) {
        NSString *msl=totk_hdr_copy_source();
        NSError *e=nil;id<MTLLibrary> l=[g_dev newLibraryWithSource:msl options:nil error:&e];
        MTLRenderPipelineDescriptor *p=[MTLRenderPipelineDescriptor new];
        p.vertexFunction=[l newFunctionWithName:@"vs"];p.fragmentFunction=[l newFunctionWithName:@"fs"];
        p.colorAttachments[0].pixelFormat=dst.pixelFormat;
        pso=[g_dev newRenderPipelineStateWithDescriptor:p error:&e];
        if(!pso)MDIE("HDR format conversion compile: %s",e.localizedDescription.UTF8String);
        if(dst.pixelFormat==MTLPixelFormatRGBA16Float)to16=pso;else toGuest=pso;
    }
    blit_close();rs_sync();
    MTL4RenderPassDescriptor *p=[MTL4RenderPassDescriptor new];
    p.colorAttachments[0].texture=dst;p.colorAttachments[0].loadAction=MTLLoadActionDontCare;p.colorAttachments[0].storeAction=MTLStoreActionStore;
    int seed=dst.pixelFormat==MTLPixelFormatRGBA16Float;
    hdr_copy_note(seed?HDR_COPY_SEED:HDR_COPY_MIRROR,0,dst.width,dst.height);
    pt_mark(g_cb,seed?'s':'m',p);
    id<MTL4RenderCommandEncoder> e=[g_cb renderCommandEncoderWithDescriptor:p];
    [e barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    [g_bloomq_args setTexture:src.gpuResourceID atIndex:0];[e setArgumentTable:g_bloomq_args atStages:MTLRenderStageFragment];
    [e setViewport:(MTLViewport){0,0,dst.width,dst.height,0,1}];[e setRenderPipelineState:pso];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];m4_end(e);
}
/* These helpers belong to the replay owner, never the display thread. Every
 * submission flushes before publishing its fence: CPU readers may run on a
 * different thread after that fence, without owning an encoder. */
static id<MTLTexture> hdr_shadow_base(id<MTLTexture> t) {
    id<MTLTexture> base=t;
    while(base.parentTexture)base=base.parentTexture;
    return base;
}
/* No encoders or locks here: also called under the registry lock and by upload
 * workers. Other threads invalidate by epoch, never by touching replay's slots. */
static void hdr_shadow_invalidate(id<MTLTexture> source) {
    /* Only an eligible base can have a slot, and a slot is fresh only when its base has neither a
     * heap nor a buffer behind it (hdr_shadow_make_draw), so no other object aliases its pages. */
    id<MTLTexture> base=hdr_shadow_base(source);if(!base || !hdr_shadow_format(base.pixelFormat))return;
    if(!on_sub_thread()){hdr_shadow_invalidate_all();return;}
    for(unsigned i=0;i<HDR_SHADOW_MAX;i++)if(g_hdr_shadow[i].base==base)g_hdr_shadow[i].fresh=0;
}
static void hdr_shadow_invalidate_pass(MTL4RenderPassDescriptor *pd) {
    /* Cached descriptors and replayed passes must mark at encode, too. The
     * mirror uses its own encoder, deliberately outside this boundary. */
    if(!hdr_shadow_enabled() || !hdr_seed_on())return;   // disabled frames invalidate the whole epoch
    MTLRenderPassColorAttachmentDescriptorArray *all=pd.colorAttachments;
    for(unsigned i=0;i<8;i++) {
        MTLRenderPassColorAttachmentDescriptor *a=all[i];id<MTLTexture> t=a.texture;if(!t)continue;
        hdr_shadow_invalidate(t);
        id<MTLTexture> r=a.resolveTexture;if(r)hdr_shadow_invalidate(r);
    }
}
static void hdr_shadow_sync(id<MTLTexture> source) {
    /* Async CPU readback drained replay and waited its fence. It must not
     * inspect the replay-owned table while a later submission can start. */
    if(!on_sub_thread())return;
    id<MTLTexture> base=hdr_shadow_base(source);if(!base)return;
    for(unsigned i=0;i<HDR_SHADOW_MAX;i++)if(g_hdr_shadow[i].base==base) {
        int attached=0;
        for(unsigned j=0;j<g_hdr_nstores;j++)attached|=g_hdr_stores[j].base==base;
        if(!attached && !g_hdr_shadow[i].dirty)return;
        /* No command buffer between submissions: nothing can be encoded here.
         * Every submission flushes before its commit, so this is not expected;
         * if it happens, the companion stays dirty and the next submission's
         * flush mirrors it. One late mirror is better than ending the game. */
        if(!g_cb) {
            static int said;
            if(!said){said=1;MLOG("[hdr] a companion was dirty outside a submission; it is mirrored at the next commit");}
            return;
        }
        /* Closing the pass can itself mark this companion dirty. */
        rp_end();
        if(g_hdr_shadow[i].dirty) {
            hdr_shadow_copy(g_hdr_shadow[i].texture,base);
            g_hdr_shadow[i].dirty=0;
        }
        return;
    }
}
static void hdr_shadow_flush(void) {
    if(!on_sub_thread())return;
    rp_end();
    for(unsigned i=0;i<HDR_SHADOW_MAX;i++)
        if(g_hdr_shadow[i].dirty)hdr_shadow_sync(g_hdr_shadow[i].base);
}
/* CPU read/write helpers use the legacy queue. If reached inside replay,
 * merely encoding a mirror is insufficient: submit and wait for this chunk
 * before handing the original to g_q. Outside replay, the caller already
 * drained submissions and waits their final fences (which include mirrors). */
static void hdr_shadow_sync_cpu(id<MTLTexture> source) {
    if(!on_sub_thread())return;
    hdr_shadow_sync(source);
    if(!g_cb)return;
    id<MTLTexture> base=hdr_shadow_base(source);if(!base)return;
    for(unsigned i=0;i<HDR_SHADOW_MAX;i++)if(g_hdr_shadow[i].base==base) {
        rp_end();blit_close();
        dispatch_semaphore_t done=dispatch_semaphore_create(0);
        mtl_sub_split_done(done);dispatch_semaphore_wait(done,DISPATCH_TIME_FOREVER);
        return;
    }
}
/* An original-side write first preserves untouched pixels. As before lazy
 * mirroring, the live FP16 companion retains its previous contents; a later
 * companion pass can supersede that write. Clearing dirty here prevents a
 * deferred mirror alone from undoing a newer original-side write. */
static void hdr_shadow_original_write(id<MTLTexture> base) { hdr_shadow_sync(base);hdr_shadow_invalidate(base); }
static id<MTLTexture> hdr_shadow_make_draw(id<MTLTexture> source) {
    id<MTLTexture> base=hdr_shadow_base(source);   // views and writers must agree on the slot key
    if(!base || source.width!=base.width || source.height!=base.height || base.textureType!=MTLTextureType2D || base.mipmapLevelCount!=1 || base.sampleCount!=1 ||
       !hdr_shadow_format(base.pixelFormat))return nil;
    id<MTLTexture> found=hdr_shadow_find(base);if(found)return found;
    unsigned slot=HDR_SHADOW_MAX;uint64_t oldest=UINT64_MAX,frame=atomic_load(&g_cur_frame);
    for(unsigned i=0;i<HDR_SHADOW_MAX;i++) {
        if(g_hdr_shadow[i].base==base){slot=i;break;}
        if((!g_hdr_shadow[i].base || g_hdr_shadow[i].frame!=frame) && g_hdr_shadow[i].used<oldest){oldest=g_hdr_shadow[i].used;slot=i;}
    }
    if(slot==HDR_SHADOW_MAX)MDIE("HDR postprocess resource limit exceeded");
    rp_end();
    hdr_shadow_sync(base);
    hdr_shadow_sync(g_hdr_shadow[slot].base);   // eviction must preserve its original too
    if(g_hdr_shadow[slot].base!=base) {
        g_hdr_shadow[slot].fresh=0;
        for(id<MTLTexture> view in g_hdr_shadow[slot].reads.allValues)defer_obj_release(view);
        g_hdr_shadow[slot].reads=nil;
        if(g_hdr_shadow[slot].texture)defer_obj_release(g_hdr_shadow[slot].texture);
        MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:base.width height:base.height mipmapped:NO];
        d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead|(g_hdr_companion_store?MTLTextureUsageShaderWrite:0);
        id<MTLTexture> t=[g_dev newTextureWithDescriptor:d];
        if(!t)MDIE("HDR postprocess FP16 allocation failed");
        res_add(t);res_commit();g_hdr_shadow[slot].base=base;g_hdr_shadow[slot].texture=t;
        MLOG("[hdr] FP16 postprocess companion %lux%lu",(unsigned long)base.width,(unsigned long)base.height);
    }
    /* A fresh companion is the authority and the original is its R11G11B10 projection: only
     * the mirror has written the original since the companion was last seeded. Skipping
     * the seed leaves one difference. Pixels the coming passes do not overwrite keep their
     * exact FP16 values (compose can write negative wide-gamut RGB, and nothing proves RCAS or
     * guest shaders stay inside the R11 range) where the seed would have handed back values
     * that went through R11G11B10. Those are the values the previous frame presented, so the
     * default accepts that; TOTK_HDR_SEED=2 keeps the seed after any companion write. Alpha
     * is hidden by sampling swizzles and RGB blend-factor remapping. Never treat a pending
     * clear as initialization: intercepts can consume it. */
    uint64_t generation=atomic_load_explicit(&g_hdr_shadow_generation,memory_order_acquire);
    int mode=hdr_seed_on();
    /* Why a seed was made, for the copies line: the original was written, everything was
     * invalidated (another thread, HDR off, resize, error), or strict mode after a companion write. */
    int why=!mode?-1:!g_hdr_shadow[slot].fresh?0:g_hdr_shadow[slot].generation!=generation?1:mode==2 && !g_hdr_shadow[slot].seed_only?2:-1;
    if(mode && why<0)hdr_copy_note(HDR_COPY_SEED,1,base.width,base.height);
    else {
        if(why>=0)atomic_fetch_add_explicit(&g_hdr_seed_why[why],1,memory_order_relaxed);
        hdr_shadow_copy(base,g_hdr_shadow[slot].texture);
        g_hdr_shadow[slot].seed_only=1;
    }
    /* Buffer/heap aliases can change through CPU or buffer writes without a
     * texture destination. These allocations never qualify for seed reuse. */
    g_hdr_shadow[slot].fresh=!base.buffer && !base.heap;
    g_hdr_shadow[slot].generation=generation;
    g_hdr_shadow[slot].dirty=0;
    g_hdr_shadow[slot].frame=frame;g_hdr_shadow[slot].used=++g_hdr_shadow_clock;
    g_hdr_shadow_live_frame=frame;
    return g_hdr_shadow[slot].texture;
}
/* Conservative export: callers can write without opening a tracked pass.
 * Deferred helpers must mark again when they actually encode (a reader may
 * have synced the export in the meantime). */
static void hdr_shadow_external(id<MTLTexture> texture) {
    if(!texture)return;
    hdr_shadow_invalidate(texture);   // deferred finish/MetalFX may still target an original (off the submission thread it bumps the epoch)
    for(unsigned i=0;i<HDR_SHADOW_MAX;i++)if(g_hdr_shadow[i].texture==texture){
        g_hdr_shadow[i].dirty=1;g_hdr_shadow[i].seed_only=0;
    }
}
static id<MTLTexture> hdr_shadow_make(id<MTLTexture> source) {
    id<MTLTexture> texture=hdr_shadow_make_draw(source);hdr_shadow_external(texture);return texture;
}
static void hdr_shadow_store_begin(MTL4RenderPassDescriptor *pd) {
    g_hdr_nstores=0;
    if(!hdr_shadow_enabled())return;
    for(unsigned i=0;i<8;i++) {
        MTLRenderPassColorAttachmentDescriptor *a=pd.colorAttachments[i];
        id<MTLTexture> texture=a.texture;if(!texture)continue;
        for(unsigned j=0;j<HDR_SHADOW_MAX;j++)if(texture==g_hdr_shadow[j].texture) {
            unsigned n=g_hdr_nstores++;
            g_hdr_stores[n].base=g_hdr_shadow[j].base;g_hdr_stores[n].texture=texture;g_hdr_stores[n].index=(int)i;
            break;
        }
    }
}
static void hdr_shadow_store(void) {
    unsigned n=g_hdr_nstores;g_hdr_nstores=0;
    for(unsigned i=0;i<n;i++) {
        id<MTLTexture> texture=g_hdr_stores[i].texture;
        hdr_shadow_external(texture);
        if(hdr_mirror_on())hdr_copy_note(HDR_COPY_MIRROR,1,texture.width,texture.height);
        else hdr_shadow_sync(g_hdr_stores[i].base);
        g_hdr_stores[i].base=nil;g_hdr_stores[i].texture=nil;
    }
}
