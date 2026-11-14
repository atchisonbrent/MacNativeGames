#pragma once
/* Only the verified compose creates HDR; only the verified FXAA/upscale may
 * propagate it to a new RGB10A2 canvas. Subsequent HUD draws retain that canvas.
 * R11 lighting/bloom/luminance resources are deliberately never promoted. */
static void botw_hdr_prepare_draw(NvProg *pr) {
    g_hdr_draw_native=0;
    if(!hdr_shadow_enabled() || !pr || !pr->sh[1] || R.rt.n!=1 || !R.rt.color[0])return;
    /* The texture the pass will attach (attachment_texture_pass): the target's own copy, never
     * the pending rendering that tex_mtl hands a sampler. A companion made for the alias would
     * leave the HDR twin drawing into the RGB10A2 original. */
    NvShader *fs=pr->sh[1];id<MTLTexture> dst=BOTW_ATTACH_TEX_U(R.rt.color[0]);
    if(!dst || !botw_hdr_shadow_format(dst.pixelFormat) || dst.textureType!=MTLTextureType2D ||
       dst.mipmapLevelCount!=1 || dst.sampleCount!=1 || dst.width<64 || dst.height<64)return;
    const view_t *v=&R.rt.cview[0];
    if(R.rt.has_cview[0] && ((v->set_levels && v->base_level) || (v->set_layers && v->min_layer)))return;
    int origin=fs->hdr_compose==1 && fs->hdr_fn;
    int linked=hdr_shadow_find(dst)!=nil;
    if(!origin && !linked && !strncmp(fs->key,BOTW_MFXUP_KEY,65)) {
        int tid=(int)(S.tex[1][0]&0xfffff);
        id<MTLTexture> src=tid>0?(__bridge id<MTLTexture>)tex_raw_lk(tid):nil;
        linked=hdr_shadow_find(src)!=nil;
    }
    if(!origin && !linked)return;
    if(hdr_shadow_make_draw(dst)) {
        g_hdr_draw_native=1;
        PD("HDR BOTW %.16s target=%llu FP16 %lux%lu origin=%d",fs->key,R.rt.color[0]->serial,
           (unsigned long)dst.width,(unsigned long)dst.height,origin);
    }
}
