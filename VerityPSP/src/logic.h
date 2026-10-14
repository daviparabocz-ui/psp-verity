/*
 * logic.h — lógica PORTÁVEL do Verity (sem nada específico de PSP).
 * Tudo aqui compila e é testado no PC (ver tests/run.sh).
 */
#ifndef LOGIC_H
#define LOGIC_H

#include <stddef.h>
#include <stdint.h>
#include "assets.h"

#define MAX_OUTPUT_TOKENS 512
#define GEMINI_TEMPERATURE "0.9"

/* ------------------------------------------------------------------ */
/* Ações (emotes) e humores — espelham VerityAction.kt / VerityMood    */
/* ------------------------------------------------------------------ */
typedef enum {
    ACT_IDLE, ACT_SERIOUS_IDLE, ACT_SPIN, ACT_WINK, ACT_GLITCH,
    ACT_SPEAK_OPEN, ACT_SPEAK_CLOSED, ACT_SERIOUS_SPEAK, ACT_COUNT
} Action;

typedef enum { MOOD_NORMAL, MOOD_SERIOUS, MOOD_COUNT } Mood;

typedef struct {
    const char *tag;
    const char *prompt_hint;   /* trecho que entra no system prompt */
    int nframes;
    int frames[3];             /* índices SPR_* */
    int frame_ms;
} ActionInfo;

extern const ActionInfo ACTIONS[ACT_COUNT];

int    action_from_tag(const char *tag);   /* -1 se desconhecida */
int    mood_from_tag(const char *tag);     /* -1 se desconhecido */
int    action_is_speaking(int action);     /* speak_open / speak_closed */
Action mood_idle_action(Mood m);

/* ------------------------------------------------------------------ */
/* Buffer de string dinâmico                                           */
/* ------------------------------------------------------------------ */
typedef struct { char *p; size_t len, cap; } StrBuf;

void   sb_init(StrBuf *sb);
int    sb_append_n(StrBuf *sb, const char *s, size_t n);
int    sb_append(StrBuf *sb, const char *s);
int    sb_append_char(StrBuf *sb, char c);
char  *sb_take(StrBuf *sb);   /* devolve o buffer (malloc) e zera o sb */
void   sb_free(StrBuf *sb);

/* ------------------------------------------------------------------ */
/* Parser de tags: texto cru -> fila de eventos                        */
/* ------------------------------------------------------------------ */
typedef enum { EV_TEXT, EV_EMOTE, EV_MOOD } EventType;

typedef struct {
    EventType type;
    int value;     /* Action (EV_EMOTE) ou Mood (EV_MOOD) */
    char *text;    /* EV_TEXT: malloc'd */
} Event;

typedef struct { Event *items; int count; int cap; } EventList;

void events_parse(const char *raw, EventList *out);
void events_free(EventList *l);

/* ------------------------------------------------------------------ */
/* UTF-8 / UTF-16                                                      */
/* ------------------------------------------------------------------ */
/* Decodifica 1 codepoint de s (n bytes disponíveis). Retorna bytes consumidos (>=1). */
int    utf8_decode(const char *s, size_t n, uint32_t *cp);
size_t utf8_encode(uint32_t cp, char *out);   /* escreve até 4 bytes, retorna quantos */
size_t utf16_to_utf8(const unsigned short *in, char *out, size_t cap);

/* ------------------------------------------------------------------ */
/* Texto: quebra de linha e "palavra seguinte" (typewriter por palavra) */
/* ------------------------------------------------------------------ */
/* Quebra 'text' em linhas de até 'cols' caracteres (palavras inteiras quando
 * possível; '\n' força quebra). Preenche starts/lens (em bytes). Retorna nº de linhas. */
int    wrap_lines(const char *text, int cols, int *starts, int *lens, int max_lines);

/* A partir de pos: consome espaços/quebras e depois uma palavra. Retorna novo pos. */
size_t next_word_end(const char *s, size_t pos);

/* ------------------------------------------------------------------ */
/* Gemini: prompt, requisição e resposta                               */
/* ------------------------------------------------------------------ */
typedef struct { int role; char *text; } Msg;   /* role: 0 = usuário, 1 = modelo */

char  *build_system_prompt(void);                                         /* malloc */
char  *build_request_json(const Msg *hist, int n, const char *sys_prompt); /* malloc */

/* Concatena todos os "text" de candidates[0].content.parts. Retorna 1 se achou algo. */
int    json_extract_text(const char *json, StrBuf *out);
/* Acha o primeiro  "key": "valor"  e devolve o valor decodificado. Retorna 1 se achou. */
int    json_find_string_value(const char *json, const char *key, StrBuf *out);

#endif
