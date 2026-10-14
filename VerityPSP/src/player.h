/*
 * player.h — reprodução de uma resposta do Verity (portável, sem PSP).
 *
 * Equivale ao ChatViewModel.playResponse + VerityAnimator do app Android,
 * mas como máquina de estados chamada 1x por frame (60 fps), em vez de
 * coroutines com delay(). Por isso o loop principal do PSP nunca trava.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include <stddef.h>
#include "logic.h"

#define FPS 60
#define DISP_CAP 16384
#define WORD_FRAMES 5            /* frames entre uma palavra e outra (~83 ms) */
#define POST_ANIMATION_MS 230    /* buffer depois de spin/wink/glitch */

typedef struct {
    /* fila de eventos da resposta atual */
    EventList ev;
    int idx;
    size_t tpos;
    int word_timer;
    int active;

    /* humor persistente (sobrevive entre mensagens) */
    Mood mood;

    /* animação */
    int loop_action, loop_frame, loop_timer;           /* fala em loop */
    int block_action, block_frame, block_timer;        /* spin/wink/glitch */
    int post_timer, post_sprite;

    /* texto já "digitado" */
    char disp[DISP_CAP];
    size_t disp_len;
} Player;

void player_init(Player *p);
void player_free(Player *p);
void player_clear_display(Player *p);
void player_start(Player *p, const char *raw_response);
void player_tick(Player *p);              /* chamar uma vez por frame */
int  player_sprite(const Player *p);      /* índice SPR_* a desenhar agora */
int  player_active(const Player *p);

int  ms_to_frames(int ms);

#endif
