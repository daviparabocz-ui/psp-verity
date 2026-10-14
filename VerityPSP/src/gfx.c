#include "gfx.h"

#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <string.h>

#include "logic.h"   /* utf8_decode */

#define BUF_W      512
#define FRAME_SIZE (BUF_W * SCREEN_H * 4)

static unsigned int __attribute__((aligned(16))) gu_list[262144];

/* Backbuffer em RAM (cacheada): evita LER da VRAM (lentíssimo) ao fazer alpha blend. */
static uint32_t __attribute__((aligned(64))) backbuf[BUF_W * SCREEN_H];

static int draw_idx = 0;           /* qual dos 2 buffers da VRAM está sendo "desenhado" */
static uint32_t *vram_draw = NULL; /* mirror não-cacheado do buffer de desenho */

static void update_vram_ptr(void)
{
    uint32_t base = (uint32_t)sceGeEdramGetAddr() + (draw_idx ? FRAME_SIZE : 0);
    vram_draw = (uint32_t *)(0x40000000u | base);   /* 0x40000000 = bypass de cache */
}

void gfx_init(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, gu_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, (void *)FRAME_SIZE, BUF_W);
    sceGuDepthBuffer((void *)(2 * FRAME_SIZE), BUF_W);
    sceGuOffset(2048 - (SCREEN_W / 2), 2048 - (SCREEN_H / 2));
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuDepthRange(0xc350, 0x2710);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    draw_idx = 0;
    update_vram_ptr();
    memset(backbuf, 0, sizeof backbuf);
}

/* ------------------------------------------------------------------ */
/* Desenho no backbuffer                                               */
/* ------------------------------------------------------------------ */
void gfx_clear(uint32_t color)
{
    for (int i = 0; i < BUF_W * SCREEN_H; i++) backbuf[i] = color;
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > SCREEN_W ? SCREEN_W : x + w;
    int y1 = y + h > SCREEN_H ? SCREEN_H : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = backbuf + yy * BUF_W;
        for (int xx = x0; xx < x1; xx++) row[xx] = color;
    }
}

static inline uint32_t blend(uint32_t dst, uint32_t src, uint32_t a)
{
    uint32_t ia = 255 - a;
    uint32_t r = ((src & 0xFF) * a + (dst & 0xFF) * ia) / 255;
    uint32_t g = (((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * ia) / 255;
    uint32_t b = (((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * ia) / 255;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

void gfx_sprite(const Sprite *s, int cx, int cy, int scale)
{
    int dw = s->w * scale;
    int dh = s->h * scale;
    int x0 = cx - dw / 2;
    int y0 = cy - dh / 2;

    for (int dy = 0; dy < dh; dy++) {
        int y = y0 + dy;
        if (y < 0 || y >= SCREEN_H) continue;
        const uint8_t *src = s->idx + (dy / scale) * s->w;
        uint32_t *row = backbuf + y * BUF_W;
        for (int dx = 0; dx < dw; dx++) {
            int x = x0 + dx;
            if (x < 0 || x >= SCREEN_W) continue;
            uint32_t c = s->pal[src[dx / scale]];
            uint32_t a = c >> 24;
            if (a == 0) continue;
            row[x] = (a == 255) ? c : blend(row[x], c, a);
        }
    }
}

static int glyph_index(uint32_t cp)
{
    int lo = 0, hi = FONT_NGLYPHS - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (FONT_CPS[mid] == cp) return mid;
        if (FONT_CPS[mid] < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

void gfx_text(int x, int y, const char *s, int len, uint32_t color)
{
    static int q_idx = -2;
    if (q_idx == -2) q_idx = glyph_index('?');

    int i = 0;
    while (i < len) {
        uint32_t cp;
        i += utf8_decode(s + i, (size_t)(len - i), &cp);

        int g = glyph_index(cp);
        if (g < 0) g = q_idx;
        if (g >= 0 && cp != ' ') {
            const uint8_t *gd = FONT_DATA + (size_t)g * FONT_CELL_W * FONT_CELL_H;
            for (int gy = 0; gy < FONT_CELL_H; gy++) {
                int py = y + gy;
                if (py < 0 || py >= SCREEN_H) continue;
                uint32_t *row = backbuf + py * BUF_W;
                for (int gx = 0; gx < FONT_CELL_W; gx++) {
                    uint32_t a = gd[gy * FONT_CELL_W + gx];
                    if (!a) continue;
                    int px = x + gx;
                    if (px < 0 || px >= SCREEN_W) continue;
                    row[px] = (a == 255) ? color : blend(row[px], color, a);
                }
            }
        }
        x += FONT_CELL_W;
    }
}

/* ------------------------------------------------------------------ */
/* Apresentação                                                        */
/* ------------------------------------------------------------------ */
void gfx_blit(void)
{
    memcpy(vram_draw, backbuf, sizeof backbuf);
}

void gfx_gu_pass(void)
{
    sceGuStart(GU_DIRECT, gu_list);
    sceGuClearDepth(0);
    sceGuClear(GU_DEPTH_BUFFER_BIT);
    sceGuFinish();
    sceGuSync(0, 0);
}

void gfx_flip(void)
{
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    draw_idx ^= 1;
    update_vram_ptr();
}
