/* Testes no PC da lógica portável. Rode:  sh tests/run.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/logic.h"
#include "../src/player.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FALHOU: %s (linha %d)\n", msg, __LINE__); fails++; } } while (0)

static void test_parser(void)
{
    EventList l;
    events_parse("Opa! <speak_open>Isso é fácil. <SPIN>Hmm <wink-x> <serious>fim <  glitch  >", &l);
    /* esperado: TEXT, EMOTE(speak_open), TEXT, EMOTE(spin), TEXT, (wink-x desconhecida some), TEXT " ", MOOD(serious), TEXT, EMOTE(glitch) */
    int emotes = 0, moods = 0, texts = 0;
    for (int i = 0; i < l.count; i++) {
        if (l.items[i].type == EV_EMOTE) emotes++;
        if (l.items[i].type == EV_MOOD) moods++;
        if (l.items[i].type == EV_TEXT) texts++;
    }
    CHECK(emotes == 3, "3 emotes (speak_open, spin, glitch)");
    CHECK(moods == 1, "1 mood");
    CHECK(texts >= 4, "textos");
    CHECK(l.items[0].type == EV_TEXT && strcmp(l.items[0].text, "Opa! ") == 0, "primeiro texto");
    CHECK(l.items[1].type == EV_EMOTE && l.items[1].value == ACT_SPEAK_OPEN, "speak_open");
    events_free(&l);

    events_parse("a < b e c > d 3 < 5", &l);
    CHECK(l.count == 1 && strcmp(l.items[0].text, "a < b e c > d 3 < 5") == 0, "'<' solto vira texto");
    events_free(&l);
}

static void test_json(void)
{
    const char *resp =
        "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"Ol\\u00e1 \\\"mundo\\\"\\nlinha2 \\ud83d\\ude00\"},"
        "{\"text\":\" fim\"}],\"role\":\"model\"},\"finishReason\":\"STOP\"}],\"usageMetadata\":{\"x\":1}}";
    StrBuf sb; sb_init(&sb);
    CHECK(json_extract_text(resp, &sb), "extrai texto");
    CHECK(strcmp(sb.p, "Olá \"mundo\"\nlinha2 \xF0\x9F\x98\x80 fim") == 0, "texto decodificado");
    sb_free(&sb);

    StrBuf fr; sb_init(&fr);
    CHECK(json_find_string_value(resp, "finishReason", &fr) && strcmp(fr.p, "STOP") == 0, "finishReason");
    sb_free(&fr);

    const char *err = "{\"error\":{\"code\":404,\"message\":\"models/x is not found for API\",\"status\":\"NOT_FOUND\"}}";
    StrBuf em; sb_init(&em);
    CHECK(json_find_string_value(err, "message", &em) && strstr(em.p, "not found"), "mensagem de erro");
    sb_free(&em);

    const char *empty = "{\"candidates\":[{\"finishReason\":\"MAX_TOKENS\",\"content\":{\"role\":\"model\"}}]}";
    StrBuf e2; sb_init(&e2);
    CHECK(!json_extract_text(empty, &e2), "sem parts -> vazio");
    sb_free(&e2);

    Msg h[2] = { {0, "oi \"você\"\n"}, {1, "<speak_open>Olá!"} };
    char *sp = build_system_prompt();
    char *body = build_request_json(h, 2, sp);
    CHECK(strstr(body, "\"role\":\"user\"") && strstr(body, "\"role\":\"model\""), "roles");
    CHECK(strstr(body, "oi \\\"você\\\"\\n"), "escape");
    CHECK(strstr(body, "\"maxOutputTokens\":512"), "maxOutputTokens");
    CHECK(strstr(sp, "- <spin> (giro") && strstr(sp, "- <serious_speak> (fala em modo serious"), "lista de tags no prompt");
    free(sp); free(body);
}

static void test_utf(void)
{
    unsigned short in[] = { 'O', 0xE1, 0x2026, 0xD83D, 0xDE00, 0 };
    char out[32];
    utf16_to_utf8(in, out, sizeof out);
    CHECK(strcmp(out, "O\xC3\xA1\xE2\x80\xA6\xF0\x9F\x98\x80") == 0, "utf16->utf8");
    uint32_t cp;
    CHECK(utf8_decode("\xC3\xA7", 2, &cp) == 2 && cp == 0xE7, "decode ç");
    CHECK(utf8_decode("\xFF", 1, &cp) == 1 && cp == 0xFFFD, "byte inválido");
}

static void test_wrap(void)
{
    int st[64], ln[64];
    const char *t = "O rato roeu a roupa do rei de Roma\nsegunda linha";
    int n = wrap_lines(t, 10, st, ln, 64);
    for (int i = 0; i < n; i++) {
        char buf[128];
        memcpy(buf, t + st[i], ln[i]); buf[ln[i]] = 0;
        CHECK(ln[i] <= 10 * 2, "linha curta");
        printf("  [%s]\n", buf);
    }
    CHECK(n == 7, "7 linhas");
    n = wrap_lines("aaaaaaaaaaaaaaaaaaaa", 8, st, ln, 64);
    CHECK(n == 3 && ln[0] == 8 && ln[1] == 8 && ln[2] == 4, "quebra dura");
    n = wrap_lines("ação ção é", 4, st, ln, 64);
    CHECK(n >= 2, "utf-8 conta caracteres, não bytes");
    n = wrap_lines("", 10, st, ln, 64);
    CHECK(n == 0, "vazio");
}

static int run_until_done(Player *p, int max_frames, int *words_seen, int *max_per_frame)
{
    int frames = 0;
    size_t last = 0;
    int words = 0;
    while (player_active(p) && frames < max_frames) {
        player_tick(p);
        if (p->disp_len != last) {
            words++;
            int added = 0;
            for (size_t i = last; i < p->disp_len; i++) if (p->disp[i] == ' ') added++;
            if (added > *max_per_frame) *max_per_frame = added;   /* nunca deve liberar 2 palavras no mesmo frame */
            last = p->disp_len;
        }
        frames++;
    }
    *words_seen = words;
    return frames;
}

static void test_player(void)
{
    Player *p = (Player *)malloc(sizeof(Player));
    player_init(p);

    /* word-by-word */
    player_start(p, "<speak_open>um dois tres quatro");
    int words = 0, mpf = 0;
    int frames = run_until_done(p, 2000, &words, &mpf);
    CHECK(strcmp(p->disp, "um dois tres quatro") == 0, "texto completo");
    CHECK(words == 4, "4 liberações (uma por palavra)");
    CHECK(mpf <= 1, "no máximo 1 espaço novo por frame");
    CHECK(frames >= 3 * WORD_FRAMES, "ritmo respeita WORD_FRAMES");
    CHECK(!player_active(p), "terminou");
    CHECK(player_sprite(p) == SPR_IDLE, "volta ao idle");

    /* ação bloqueante: texto depois só aparece após o spin + buffer */
    player_clear_display(p);
    player_start(p, "Oi <spin>depois");
    int saw_spin = 0, text_before_spin_end = 0;
    for (int f = 0; f < 600 && player_active(p); f++) {
        player_tick(p);
        int s = player_sprite(p);
        if (s == SPR_SPIN_1 || s == SPR_SPIN_2 || s == SPR_SPIN_3) saw_spin = 1;
        if (saw_spin && strstr(p->disp, "depois") && (s == SPR_SPIN_1 || s == SPR_SPIN_2 || s == SPR_SPIN_3))
            text_before_spin_end = 1;
    }
    CHECK(saw_spin, "spin tocou");
    CHECK(!text_before_spin_end, "spin bloqueia o texto seguinte");
    CHECK(strcmp(p->disp, "Oi depois") == 0, "texto final");

    /* humor persistente + redirecionamento de fala em serious */
    player_clear_display(p);
    player_start(p, "<serious>Atenção. <speak_open>Presta atenção");
    int saw_serious_speak = 0;
    for (int f = 0; f < 600 && player_active(p); f++) {
        player_tick(p);
        int s = player_sprite(p);
        if (s == SPR_SERIOUS_SPEAK_1 || s == SPR_SERIOUS_SPEAK_2) saw_serious_speak = 1;
        CHECK(s != SPR_SPEAK_OPEN_1 && s != SPR_SPEAK_OPEN_2, "serious não usa speak_open");
    }
    CHECK(saw_serious_speak, "usou serious_speak");
    CHECK(p->mood == MOOD_SERIOUS, "humor ficou serious");
    CHECK(player_sprite(p) == SPR_SERIOUS_IDLE, "idle sério");

    player_clear_display(p);
    player_start(p, "Voltei <normal>ao normal");
    for (int f = 0; f < 600 && player_active(p); f++) player_tick(p);
    CHECK(p->mood == MOOD_NORMAL && player_sprite(p) == SPR_IDLE, "voltou ao normal");

    player_free(p);
    free(p);
}

int main(void)
{
    test_parser();
    test_json();
    test_utf();
    test_wrap();
    test_player();
    if (fails) { printf("%d teste(s) falharam\n", fails); return 1; }
    printf("todos os testes passaram\n");
    return 0;
}
