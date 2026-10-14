#include "player.h"

#include <string.h>

int ms_to_frames(int ms)
{
    int f = (ms * FPS + 999) / 1000;
    return f < 1 ? 1 : f;
}

void player_init(Player *p)
{
    memset(p, 0, sizeof *p);
    p->mood = MOOD_NORMAL;
    p->loop_action = -1;
    p->block_action = -1;
}

void player_free(Player *p)
{
    events_free(&p->ev);
}

void player_clear_display(Player *p)
{
    p->disp[0] = '\0';
    p->disp_len = 0;
}

static void stop_loop(Player *p)
{
    p->loop_action = -1;
}

static void start_loop(Player *p, int action)
{
    p->loop_action = action;
    p->loop_frame = 0;
    p->loop_timer = ms_to_frames(ACTIONS[action].frame_ms);
}

static void start_block(Player *p, int action)
{
    stop_loop(p);
    p->block_action = action;
    p->block_frame = 0;
    p->block_timer = ms_to_frames(ACTIONS[action].frame_ms);
    p->post_timer = 0;
}

void player_start(Player *p, const char *raw_response)
{
    events_free(&p->ev);
    events_parse(raw_response, &p->ev);
    p->idx = 0;
    p->tpos = 0;
    p->word_timer = 0;
    p->active = 1;
    p->loop_action = -1;
    p->block_action = -1;
    p->post_timer = 0;
    /* p->mood é mantido de propósito: o humor persiste entre mensagens. */
}

int player_active(const Player *p)
{
    return p->active;
}

/* Em modo serious, falas normais viram serious_speak (igual ao app Android). */
static int resolve_for_mood(const Player *p, int action)
{
    if (p->mood == MOOD_SERIOUS && action_is_speaking(action)) return ACT_SERIOUS_SPEAK;
    return action;
}

static void append_text(Player *p, const char *s, size_t n)
{
    if (p->disp_len + n + 1 > DISP_CAP) {
        n = DISP_CAP - p->disp_len - 1;      /* trunca em vez de estourar */
    }
    memcpy(p->disp + p->disp_len, s, n);
    p->disp_len += n;
    p->disp[p->disp_len] = '\0';
}

static void finish(Player *p)
{
    p->active = 0;
    stop_loop(p);
    p->block_action = -1;
    p->post_timer = 0;
}

void player_tick(Player *p)
{
    if (!p->active) return;

    /* 1) ação bloqueante em andamento (spin / wink / glitch) */
    if (p->block_action >= 0) {
        if (--p->block_timer <= 0) {
            const ActionInfo *ai = &ACTIONS[p->block_action];
            if (++p->block_frame >= ai->nframes) {
                p->post_sprite = ai->frames[ai->nframes - 1];
                p->block_action = -1;
                p->post_timer = ms_to_frames(POST_ANIMATION_MS);
            } else {
                p->block_timer = ms_to_frames(ai->frame_ms);
            }
        }
        return;
    }
    if (p->post_timer > 0) {
        p->post_timer--;
        return;
    }

    /* 2) loop de fala */
    if (p->loop_action >= 0 && --p->loop_timer <= 0) {
        const ActionInfo *ai = &ACTIONS[p->loop_action];
        p->loop_frame = (p->loop_frame + 1) % ai->nframes;
        p->loop_timer = ms_to_frames(ai->frame_ms);
    }

    /* 3) próximos eventos */
    while (p->idx < p->ev.count) {
        Event *e = &p->ev.items[p->idx];

        if (e->type == EV_MOOD) {
            p->mood = (Mood)e->value;
            p->idx++;
            continue;
        }

        if (e->type == EV_EMOTE) {
            int action = resolve_for_mood(p, e->value);
            p->idx++;
            if (action_is_speaking(action) || action == ACT_SERIOUS_SPEAK) {
                start_loop(p, action);      /* não bloqueia */
                continue;
            }
            start_block(p, action);          /* bloqueia até terminar + buffer */
            return;
        }

        /* EV_TEXT: libera UMA palavra por vez */
        if (p->word_timer > 0) {
            p->word_timer--;
            return;
        }
        size_t len = strlen(e->text);
        if (p->tpos >= len) {
            p->idx++;
            p->tpos = 0;
            continue;
        }
        size_t end = next_word_end(e->text, p->tpos);
        if (end <= p->tpos) {               /* só sobrou espaço em branco */
            p->idx++;
            p->tpos = 0;
            continue;
        }
        append_text(p, e->text + p->tpos, end - p->tpos);
        p->tpos = end;
        p->word_timer = WORD_FRAMES - 1;
        return;
    }

    finish(p);
}

int player_sprite(const Player *p)
{
    if (p->block_action >= 0) return ACTIONS[p->block_action].frames[p->block_frame];
    if (p->post_timer > 0) return p->post_sprite;
    if (p->loop_action >= 0) return ACTIONS[p->loop_action].frames[p->loop_frame];
    return ACTIONS[mood_idle_action(p->mood)].frames[0];
}
