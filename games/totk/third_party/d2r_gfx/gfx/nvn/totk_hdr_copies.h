#pragma once
/* Replay counts are folded once per game present, including discarded frames.
 * Ring costs enter only from the drawable's presented callback: queued/dropped
 * pictures did not show their capture. Those callbacks can lag the window.
 * Values are operations / megapixels per game frame, averaged over 600 frames.
 * TOTK_PASS_GPU kinds: s = seed, m = mirror, h = capture. */
enum { HDR_COPY_SEED, HDR_COPY_MIRROR, HDR_COPY_CAPTURE, HDR_COPY_RING, HDR_COPY_CLEAR, HDR_COPY_COUNT };
static pthread_mutex_t g_hdr_copies_mu=PTHREAD_MUTEX_INITIALIZER;
static struct { _Atomic uint64_t n[HDR_COPY_COUNT][2], pixels[HDR_COPY_COUNT][2]; uint64_t frames, hdr_frames; } g_hdr_copies;
static _Atomic uint64_t g_hdr_seed_why[3];   // seeds made although TOTK_HDR_SEED is on: original written, all invalidated, strict mode
static void hdr_copy_note(int kind,int skipped,NSUInteger w,NSUInteger h) {
    /* Replay is complete before mtl_present folds the window. Only the ring
     * callback can straddle that fold; keep its count/pixel pair together. */
    if(kind==HDR_COPY_RING)pthread_mutex_lock(&g_hdr_copies_mu);
    atomic_fetch_add_explicit(&g_hdr_copies.n[kind][skipped],1,memory_order_relaxed);
    atomic_fetch_add_explicit(&g_hdr_copies.pixels[kind][skipped],(uint64_t)w*h,memory_order_relaxed);
    if(kind==HDR_COPY_RING)pthread_mutex_unlock(&g_hdr_copies_mu);
}
static void hdr_copies_frame(void) {
    g_hdr_copies.hdr_frames+=atomic_load_explicit(&g_hdr_frame_enabled,memory_order_relaxed)!=0;
    if(++g_hdr_copies.frames==600) {
        pthread_mutex_lock(&g_hdr_copies_mu);
        uint64_t n[HDR_COPY_COUNT][2],pixels[HDR_COPY_COUNT][2];
        for(int i=0;i<HDR_COPY_COUNT;i++)for(int j=0;j<2;j++) {
            n[i][j]=atomic_exchange_explicit(&g_hdr_copies.n[i][j],0,memory_order_relaxed);
            pixels[i][j]=atomic_exchange_explicit(&g_hdr_copies.pixels[i][j],0,memory_order_relaxed);
        }
        pthread_mutex_unlock(&g_hdr_copies_mu);
        static const char *names[]={"seed","mirror","capture","ring8-shown","capture-clear"};
        char line[768];size_t at=0;
        for(int i=0;i<HDR_COPY_COUNT;i++)at+=(size_t)snprintf(line+at,sizeof line-at,
            " %s=%.3f/%.3fMP skipped=%.3f/%.3fMP",names[i],n[i][0]/600.0,
            pixels[i][0]/600e6,n[i][1]/600.0,pixels[i][1]/600e6);
        uint64_t why[3];for(int i=0;i<3;i++)why[i]=atomic_exchange_explicit(&g_hdr_seed_why[i],0,memory_order_relaxed);
        if(g_hdr_copies.hdr_frames)MLOG("[hdr] copies per frame: last 600 game frames;%s (ring: presented callbacks); seeds made because: original written %.3f, all invalidated %.3f, strict %.3f",line,why[0]/600.0,why[1]/600.0,why[2]/600.0);
        g_hdr_copies.frames=g_hdr_copies.hdr_frames=0;
    }
}
/* Independent, immutable run controls. Exact 0 disables each optimization;
 * counters and deferred ring-texture retirement remain enabled. */
static pthread_once_t g_hdr_copies_once=PTHREAD_ONCE_INIT;
static int g_hdr_capture_load,g_hdr_ring,g_hdr_mirror,g_hdr_seed;
static void hdr_copies_init(void) {
    const char *e=getenv("TOTK_HDR_CAPTURE_LOAD");g_hdr_capture_load=!(e&&!strcmp(e,"0"));
    e=getenv("TOTK_HDR_RING");g_hdr_ring=!(e&&!strcmp(e,"0"));
    e=getenv("TOTK_HDR_MIRROR");g_hdr_mirror=!(e&&!strcmp(e,"0"));
    /* TOTK_HDR_SEED: 0 = seed a companion from its original at first use every frame (as before);
     * 2 = skip the seed only while the companion still holds nothing but an earlier seed (strict:
     * every value in it has been through R11G11B10); anything else, the default = skip it whenever
     * no one but the mirror has written the original since the two last agreed. */
    e=getenv("TOTK_HDR_SEED");g_hdr_seed=e&&!strcmp(e,"0")?0:e&&!strcmp(e,"2")?2:1;
    /* Diagnostic snapshots can run inside pass teardown or replay a saved
     * pass. Preserve their eager-original contract instead of splitting it. */
    const char *eager[]={"DM_NVN_HDRPASS_LOG","OOT_PASS_PIXELS","DM_NVN_TONEIN_LOG",
        "DM_NVN_TEXRAW","DM_NVN_TEXPNG","DM_NVN_TEXPNG_FS","DM_NVN_FLASHHDR",
        "DM_NVN_REGLUM","DM_NVN_FUSE_CHECK","DM_NVN_RT_RANGE_CHECK",
        "DM_RT_AB","DM_RT_AB_TOGGLE","OOT_CS_RESOURCE_CAPTURE"};
    for(unsigned i=0;i<sizeof eager/sizeof eager[0];i++)if(getenv(eager[i]))g_hdr_mirror=g_hdr_seed=0;
}
static int hdr_capture_load_on(void) { pthread_once(&g_hdr_copies_once,hdr_copies_init);return g_hdr_capture_load; }
static int hdr_ring_on(void) { pthread_once(&g_hdr_copies_once,hdr_copies_init);return g_hdr_ring; }
static int hdr_mirror_on(void) { pthread_once(&g_hdr_copies_once,hdr_copies_init);return g_hdr_mirror; }
static int hdr_seed_on(void) { pthread_once(&g_hdr_copies_once,hdr_copies_init);return g_hdr_seed; }
