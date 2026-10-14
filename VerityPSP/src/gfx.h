#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include "assets.h"

#define SCREEN_W 480
#define SCREEN_H 272

/* Cor opaca no formato nativo do PSP (ABGR8888). */
#define COL(r, g, b) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | 0xFF000000u)

void gfx_init(void);

/* Desenho (tudo vai para um backbuffer em RAM; nada aparece até gfx_blit+gfx_flip). */
void gfx_clear(uint32_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void gfx_sprite(const Sprite *s, int cx, int cy, int scale);   /* centrado em (cx,cy) */
void gfx_text(int x, int y, const char *utf8, int len, uint32_t color);

/* Apresentação */
void gfx_blit(void);      /* copia o backbuffer para o buffer de desenho da VRAM */
void gfx_gu_pass(void);   /* passada vazia do GE (limpa depth) — necessária enquanto o OSK roda */
void gfx_flip(void);      /* espera o vblank e troca os buffers */

#endif
