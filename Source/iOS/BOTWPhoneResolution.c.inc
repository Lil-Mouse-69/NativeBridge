static void patch_phone_render(Module *m) {
    if (!ios_code()) return;
    const struct { uint32_t off, original; } check[] = {{PHONE_RENDER, 0xd2a89408u}, {PHONE_RENDER + 4, 0xf2e89408u}, {PHONE_RENDER + 8, PHONE_RENDER_STR}};
    for (size_t i = 0; !CODE_PATCHED_AT_BUILD && i < sizeof check / sizeof *check; i++) {
        uint32_t w; memcpy(&w, m->base + check[i].off, 4);
        if (w != check[i].original) runtime_fail("NativeBridge: unexpected BOTW handheld display-mode instructions");
    }
    /* The loader's RW page: +0 frame step, +8 pad tick, +16..31 Mac output,
       +32 aspect ratio. +40..47 is our {canvas width, scene width} float pair.
       Export patches code once; iOS only validates those words and writes data. */
    uint64_t data = (m->file_size + 0x3fff) & ~0x3fffULL;
    if (data + 0x4000 != m->size) runtime_fail("NativeBridge: BOTW render data-page layout");
    uint32_t words[] = {
        adrp_reg(8, (uintptr_t)m->base + PHONE_RENDER, (uintptr_t)m->base + data),
        0xf9401508u, /* ldr x8, [x8, #40] */
        PHONE_RENDER_STR
    };
    put_words(m, PHONE_RENDER, words, 3, "NativeBridge handheld resolution");
    const char *choice = getenv("TOTK_RENDER_RES");
    float width = choice && !strcmp(choice, "1280x720") ? 1280.0f : 960.0f;
    float sizes[2] = {1280.0f, width};
    memcpy(m->base + data + 40, sizes, sizeof sizes);
    fprintf(stderr, "[botw] NativeBridge handheld scene %.0fx%.0f; canvas 1280x720 (restart to change)\n", width, width * 9.0f / 16.0f);
}
