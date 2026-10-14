/* GERADO por tools/gen_assets.py — não edite à mão. */
#ifndef ASSETS_H
#define ASSETS_H

#include <stdint.h>

typedef struct {
    int w, h;
    const uint8_t *idx;   /* w*h índices na paleta */
    const uint32_t *pal;  /* ABGR8888; entrada 0 pode ser transparente */
} Sprite;

enum {
    SPR_IDLE,
    SPR_SERIOUS_IDLE,
    SPR_SPIN_1,
    SPR_SPIN_2,
    SPR_SPIN_3,
    SPR_WINK_1,
    SPR_WINK_2,
    SPR_WINK_3,
    SPR_GLITCH_1,
    SPR_GLITCH_2,
    SPR_SPEAK_OPEN_1,
    SPR_SPEAK_OPEN_2,
    SPR_SPEAK_CLOSED_1,
    SPR_SPEAK_CLOSED_2,
    SPR_SERIOUS_SPEAK_1,
    SPR_SERIOUS_SPEAK_2,
    SPR_COUNT
};

extern const Sprite SPRITES[SPR_COUNT];

/* Fonte bitmap monoespaçada (alpha 8 bits por pixel). */
#define FONT_CELL_W 7
#define FONT_CELL_H 14
#define FONT_NGLYPHS 201
extern const uint16_t FONT_CPS[FONT_NGLYPHS];  /* codepoints, ordenados */
extern const uint8_t FONT_DATA[FONT_NGLYPHS * FONT_CELL_W * FONT_CELL_H];

#endif
