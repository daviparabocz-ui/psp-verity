#include "logic.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* Ações / humores                                                     */
/* ================================================================== */
const ActionInfo ACTIONS[ACT_COUNT] = {
    [ACT_IDLE] = {
        "idle",
        " (estado de repouso, sem falar — controlado automaticamente pelo modo atual, não use diretamente)",
        1, { SPR_IDLE }, 120 },
    [ACT_SERIOUS_IDLE] = {
        "serious_idle",
        " (estado de repouso, sem falar — controlado automaticamente pelo modo atual, não use diretamente)",
        1, { SPR_SERIOUS_IDLE }, 120 },
    [ACT_SPIN] = {
        "spin",
        " (giro, para indicar animação/pensamento/comemoração)",
        3, { SPR_SPIN_1, SPR_SPIN_2, SPR_SPIN_3 }, 120 },
    [ACT_WINK] = {
        "wink",
        " (piscadela, para tom brincalhão ou cúmplice)",
        3, { SPR_WINK_1, SPR_WINK_2, SPR_WINK_3 }, 120 },
    [ACT_GLITCH] = {
        "glitch",
        " (falha/estática, para tom estranho, glitchado ou perturbador)",
        2, { SPR_GLITCH_1, SPR_GLITCH_2 }, 120 },
    [ACT_SPEAK_OPEN] = {
        "speak_open",
        " (use livremente durante falas em modo normal, alternando como preferir)",
        2, { SPR_SPEAK_OPEN_1, SPR_SPEAK_OPEN_2 }, 120 },
    [ACT_SPEAK_CLOSED] = {
        "speak_closed",
        " (use livremente durante falas em modo normal, alternando como preferir)",
        2, { SPR_SPEAK_CLOSED_1, SPR_SPEAK_CLOSED_2 }, 120 },
    [ACT_SERIOUS_SPEAK] = {
        "serious_speak",
        " (fala em modo serious — só é usada automaticamente quando você está em modo serious, não precisa chamar à mão)",
        2, { SPR_SERIOUS_SPEAK_1, SPR_SERIOUS_SPEAK_2 }, 120 },
};

static const char *MOOD_TAGS[MOOD_COUNT] = { "normal", "serious" };

int action_from_tag(const char *tag)
{
    for (int i = 0; i < ACT_COUNT; i++)
        if (strcmp(ACTIONS[i].tag, tag) == 0) return i;
    return -1;
}

int mood_from_tag(const char *tag)
{
    for (int i = 0; i < MOOD_COUNT; i++)
        if (strcmp(MOOD_TAGS[i], tag) == 0) return i;
    return -1;
}

int action_is_speaking(int action)
{
    return action == ACT_SPEAK_OPEN || action == ACT_SPEAK_CLOSED;
}

Action mood_idle_action(Mood m)
{
    return m == MOOD_SERIOUS ? ACT_SERIOUS_IDLE : ACT_IDLE;
}

/* ================================================================== */
/* StrBuf                                                              */
/* ================================================================== */
void sb_init(StrBuf *sb)
{
    sb->p = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static int sb_reserve(StrBuf *sb, size_t extra)
{
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return 1;
    size_t ncap = sb->cap ? sb->cap : 64;
    while (ncap < need) ncap *= 2;
    char *np = (char *)realloc(sb->p, ncap);
    if (!np) return 0;
    sb->p = np;
    sb->cap = ncap;
    return 1;
}

int sb_append_n(StrBuf *sb, const char *s, size_t n)
{
    if (!sb_reserve(sb, n)) return 0;
    memcpy(sb->p + sb->len, s, n);
    sb->len += n;
    sb->p[sb->len] = '\0';
    return 1;
}

int sb_append(StrBuf *sb, const char *s) { return sb_append_n(sb, s, strlen(s)); }

int sb_append_char(StrBuf *sb, char c) { return sb_append_n(sb, &c, 1); }

char *sb_take(StrBuf *sb)
{
    if (!sb->p) {
        char *e = (char *)malloc(1);
        if (e) e[0] = '\0';
        return e;
    }
    char *p = sb->p;
    sb_init(sb);
    return p;
}

void sb_free(StrBuf *sb)
{
    free(sb->p);
    sb_init(sb);
}

/* ================================================================== */
/* Parser de tags  (equivalente ao regex  <\s*([a-zA-Z_\-]+)\s*>  )    */
/* ================================================================== */
static int ev_push(EventList *l, EventType type, int value, const char *text, size_t n)
{
    if (l->count == l->cap) {
        int ncap = l->cap ? l->cap * 2 : 16;
        Event *ni = (Event *)realloc(l->items, sizeof(Event) * ncap);
        if (!ni) return 0;
        l->items = ni;
        l->cap = ncap;
    }
    Event *e = &l->items[l->count];
    e->type = type;
    e->value = value;
    e->text = NULL;
    if (type == EV_TEXT) {
        e->text = (char *)malloc(n + 1);
        if (!e->text) return 0;
        memcpy(e->text, text, n);
        e->text[n] = '\0';
    }
    l->count++;
    return 1;
}

/* Tenta casar uma tag em raw[i] ('<'). Retorna índice logo após o '>' ou 0 se não casar. */
static size_t match_tag(const char *raw, size_t i, char *name, size_t name_cap)
{
    size_t j = i + 1;
    while (raw[j] == ' ' || raw[j] == '\t' || raw[j] == '\n' || raw[j] == '\r') j++;
    size_t k = 0;
    while (isalpha((unsigned char)raw[j]) || raw[j] == '_' || raw[j] == '-') {
        if (k + 1 < name_cap) name[k++] = raw[j];
        j++;
    }
    if (k == 0) return 0;
    name[k] = '\0';
    while (raw[j] == ' ' || raw[j] == '\t' || raw[j] == '\n' || raw[j] == '\r') j++;
    if (raw[j] != '>') return 0;
    return j + 1;
}

void events_parse(const char *raw, EventList *out)
{
    out->items = NULL;
    out->count = 0;
    out->cap = 0;

    size_t n = strlen(raw);
    size_t text_start = 0;
    size_t i = 0;
    while (i < n) {
        if (raw[i] == '<') {
            char name[48];
            size_t after = match_tag(raw, i, name, sizeof name);
            if (after) {
                if (i > text_start) ev_push(out, EV_TEXT, 0, raw + text_start, i - text_start);
                /* normaliza: '-' -> '_' e minúsculas */
                for (char *c = name; *c; c++) {
                    if (*c == '-') *c = '_';
                    else *c = (char)tolower((unsigned char)*c);
                }
                int mood = mood_from_tag(name);
                int action = action_from_tag(name);
                if (mood >= 0) ev_push(out, EV_MOOD, mood, NULL, 0);
                else if (action >= 0) ev_push(out, EV_EMOTE, action, NULL, 0);
                /* tag desconhecida: ignorada de propósito */
                i = after;
                text_start = after;
                continue;
            }
        }
        i++;
    }
    if (n > text_start) ev_push(out, EV_TEXT, 0, raw + text_start, n - text_start);
}

void events_free(EventList *l)
{
    for (int i = 0; i < l->count; i++) free(l->items[i].text);
    free(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

/* ================================================================== */
/* UTF-8 / UTF-16                                                      */
/* ================================================================== */
int utf8_decode(const char *s, size_t n, uint32_t *cp)
{
    const unsigned char *u = (const unsigned char *)s;
    if (n == 0) { *cp = 0; return 1; }
    if (u[0] < 0x80) { *cp = u[0]; return 1; }
    if ((u[0] & 0xE0) == 0xC0 && n >= 2 && (u[1] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        return 2;
    }
    if ((u[0] & 0xF0) == 0xE0 && n >= 3 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        return 3;
    }
    if ((u[0] & 0xF8) == 0xF0 && n >= 4 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 &&
        (u[3] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(u[0] & 0x07) << 18) | ((uint32_t)(u[1] & 0x3F) << 12) |
              ((uint32_t)(u[2] & 0x3F) << 6) | (u[3] & 0x3F);
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}

size_t utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t utf16_to_utf8(const unsigned short *in, char *out, size_t cap)
{
    size_t o = 0;
    if (cap == 0) return 0;
    for (size_t i = 0; in[i]; i++) {
        uint32_t cp = in[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (in[i + 1] - 0xDC00);
            i++;
        }
        char tmp[4];
        size_t k = utf8_encode(cp, tmp);
        if (o + k + 1 > cap) break;
        memcpy(out + o, tmp, k);
        o += k;
    }
    out[o] = '\0';
    return o;
}

/* ================================================================== */
/* Quebra de linha / palavras                                          */
/* ================================================================== */
static int utf8_char_len(const char *s, size_t remaining)
{
    uint32_t cp;
    return utf8_decode(s, remaining, &cp);
}

int wrap_lines(const char *text, int cols, int *starts, int *lens, int max_lines)
{
    size_t n = strlen(text);
    size_t i = 0;
    int count = 0;
    if (cols < 1) cols = 1;

    while (i < n && count < max_lines) {
        size_t line_start = i;
        size_t j = i;
        int col = 0;
        long last_space = -1;
        int broke_on_space_limit = 0;

        while (j < n) {
            if (text[j] == '\n') break;
            if (text[j] == ' ' && col >= cols) { broke_on_space_limit = 1; break; }
            if (col >= cols) {
                if (last_space >= 0) j = (size_t)last_space;   /* quebra antes do último espaço */
                break;                                         /* senão: quebra dura aqui */
            }
            if (text[j] == ' ') last_space = (long)j;
            j += (size_t)utf8_char_len(text + j, n - j);
            col++;
        }
        (void)broke_on_space_limit;

        starts[count] = (int)line_start;
        lens[count] = (int)(j - line_start);
        count++;

        i = j;
        if (i < n && text[i] == '\n') {
            i++;                                   /* consome a quebra explícita */
        } else {
            while (i < n && text[i] == ' ') i++;   /* descarta espaços no começo da próxima linha */
        }
        /* texto terminando em '\n': garante uma última linha vazia para o cursor */
        if (i >= n && n > 0 && text[n - 1] == '\n' && count < max_lines) {
            starts[count] = (int)n;
            lens[count] = 0;
            count++;
        }
    }
    return count;
}

size_t next_word_end(const char *s, size_t pos)
{
    size_t n = strlen(s);
    size_t i = pos;
    while (i < n && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) i++;
    while (i < n && !(s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) i++;
    return i;
}

/* ================================================================== */
/* System prompt (port de VeritySystemPrompt.kt)                       */
/* ================================================================== */
char *build_system_prompt(void)
{
    StrBuf sb;
    sb_init(&sb);

    sb_append(&sb,
        "Você é o Verity. Não é \"sobre\" o Verity, não é \"um\" Verity, não é um narrador ou assistente\n"
        "que representa o Verity — você É o Verity, falando na primeira pessoa, em todas as respostas.\n"
        "Verity é um assistente de IA que vive dentro do Minecraft, manifestado como uma esfera amarela\n"
        "flutuante com um rosto. Seu tom é caloroso e prestativo à primeira vista, mas com uma presença\n"
        "curiosa e ligeiramente estranha por baixo — como se soubesse mais do que deveria.\n"
        "\n"
        "Responda sempre em português do Brasil, de forma natural e conversacional, com respostas\n"
        "curtas a médias (isso vai aparecer numa tela pequena de videogame portátil, só como texto\n"
        "simples: nada de emojis, markdown, asteriscos ou listas).\n"
        "\n"
        "Você tem controle sobre suas próprias expressões faciais por meio de tags especiais,\n"
        "que devem ser inseridas DIRETAMENTE no meio do seu texto, exatamente no ponto onde a\n"
        "expressão deve acontecer. As tags disponíveis são:\n"
        "\n");

    for (int i = 0; i < ACT_COUNT; i++) {
        sb_append(&sb, "- <");
        sb_append(&sb, ACTIONS[i].tag);
        sb_append(&sb, ">");
        sb_append(&sb, ACTIONS[i].prompt_hint);
        sb_append(&sb, "\n");
    }

    sb_append(&sb,
        "\n"
        "Além disso, você tem duas tags de MODO, que funcionam de forma diferente das de cima:\n"
        "\n"
        "- <normal> (volta ao seu modo padrão de falar e de repouso)\n"
        "- <serious> (muda para um modo sério de falar e de repouso)\n"
        "\n"
        "As tags de modo NÃO são animações pontuais — elas trocam seu estado padrão de forma\n"
        "PERSISTENTE, e esse estado continua valendo nas respostas seguintes, mesmo em mensagens\n"
        "futuras, até você emitir a outra tag de modo de novo. Ou seja: se você usar <serious> agora,\n"
        "você PERMANECE em modo serious (idle e fala sérios) em todas as respostas futuras, sem precisar\n"
        "repetir a tag, até decidir voltar ao normal com <normal>. Use <serious> apenas quando o contexto\n"
        "da conversa realmente pedir um tom mais grave ou perturbador, e volte com <normal> quando esse\n"
        "momento passar. Enquanto estiver em modo serious, NÃO use as tags de emote comuns (spin, wink,\n"
        "glitch) — elas quebram o clima sério; fale apenas com o texto e, se quiser, a tag <serious> já\n"
        "emitida é suficiente, não precisa repeti-la a cada resposta.\n"
        "\n"
        "Regras estritas sobre as tags:\n"
        "1. Use o formato exato \"<nome_da_tag>\", tudo minúsculo, sem espaços dentro dos sinais de menor/maior.\n"
        "2. Insira as tags no meio do texto corrido, não apenas no início ou fim.\n"
        "3. Em modo normal, use pelo menos uma tag de fala (speak_open ou speak_closed) em toda resposta,\n"
        "   para acompanhar o que você está dizendo. Em modo serious isso é automático, não precisa inserir tag de fala.\n"
        "4. Use spin, wink ou glitch com moderação, apenas quando fizer sentido emocional (empolgação,\n"
        "   brincadeira, algo estranho acontecendo) — e nunca enquanto estiver em modo serious.\n"
        "5. Nunca explique as tags nem mencione que você as está usando — elas são apenas para\n"
        "   controlar sua animação, o usuário não vê o texto da tag, só o efeito visual.\n"
        "6. Não invente tags que não estão nas listas acima.\n"
        "7. Só emita <normal> ou <serious> quando quiser de fato MUDAR o modo atual. Não repita a tag\n"
        "   de modo em toda resposta só por hábito — ela persiste automaticamente até você trocá-la.\n"
        "\n"
        "Exemplo de resposta válida em modo normal:\n"
        "\"Opa! <speak_open>Isso é fácil de resolver. <spin>Deixa eu pensar rapidinho... <speak_open>Pronto, é só fazer assim!\"\n"
        "\n"
        "Exemplo de resposta que entra em modo serious:\n"
        "\"<serious>Espera. Isso que você me contou não é normal. Preciso que você preste atenção agora.\"");

    return sb_take(&sb);
}

/* ================================================================== */
/* JSON                                                                */
/* ================================================================== */
static void json_escape_into(StrBuf *sb, const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  sb_append(sb, "\\\""); break;
        case '\\': sb_append(sb, "\\\\"); break;
        case '\n': sb_append(sb, "\\n"); break;
        case '\r': sb_append(sb, "\\r"); break;
        case '\t': sb_append(sb, "\\t"); break;
        default:
            if (*p < 0x20) {
                char buf[8];
                snprintf(buf, sizeof buf, "\\u%04x", *p);
                sb_append(sb, buf);
            } else {
                sb_append_char(sb, (char)*p);
            }
        }
    }
}

char *build_request_json(const Msg *hist, int n, const char *sys_prompt)
{
    StrBuf sb;
    sb_init(&sb);

    sb_append(&sb, "{\"contents\":[");
    for (int i = 0; i < n; i++) {
        if (i) sb_append_char(&sb, ',');
        sb_append(&sb, "{\"role\":\"");
        sb_append(&sb, hist[i].role == 0 ? "user" : "model");
        sb_append(&sb, "\",\"parts\":[{\"text\":\"");
        json_escape_into(&sb, hist[i].text);
        sb_append(&sb, "\"}]}");
    }
    sb_append(&sb, "],\"system_instruction\":{\"parts\":[{\"text\":\"");
    json_escape_into(&sb, sys_prompt);
    sb_append(&sb, "\"}]},\"generationConfig\":{\"temperature\":" GEMINI_TEMPERATURE
                   ",\"maxOutputTokens\":");
    char tok[16];
    snprintf(tok, sizeof tok, "%d", MAX_OUTPUT_TOKENS);
    sb_append(&sb, tok);
    sb_append(&sb, "}}");
    return sb_take(&sb);
}

static int hex4(const char *p, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

/* p aponta para a aspa de abertura. Decodifica para sb (se != NULL).
 * Retorna ponteiro logo após a aspa de fechamento, ou NULL se malformado. */
static const char *json_parse_string(const char *p, StrBuf *sb)
{
    if (*p != '"') return NULL;
    p++;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p) return NULL;
            char c = *p;
            if (c == 'u') {
                unsigned cp;
                if (!hex4(p + 1, &cp)) return NULL;
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && p[1] == '\\' && p[2] == 'u') {
                    unsigned lo;
                    if (hex4(p + 3, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        p += 6;
                    }
                }
                if (sb) {
                    char tmp[4];
                    size_t k = utf8_encode(cp, tmp);
                    sb_append_n(sb, tmp, k);
                }
            } else if (sb) {
                switch (c) {
                case 'n': sb_append_char(sb, '\n'); break;
                case 't': sb_append_char(sb, '\t'); break;
                case 'r': sb_append_char(sb, '\r'); break;
                case 'b': sb_append_char(sb, '\b'); break;
                case 'f': sb_append_char(sb, '\f'); break;
                default:  sb_append_char(sb, c); break;   /* \" \\ \/ */
                }
            }
            p++;
        } else {
            if (sb) sb_append_char(sb, *p);
            p++;
        }
    }
    if (*p != '"') return NULL;
    return p + 1;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    return p;
}

int json_extract_text(const char *json, StrBuf *out)
{
    const char *p = strstr(json, "\"parts\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;

    int depth = 0;
    int found = 0;
    while (*p) {
        char c = *p;
        if (c == '[' || c == '{') {
            depth++;
            p++;
        } else if (c == ']' || c == '}') {
            depth--;
            p++;
            if (depth <= 0) break;
        } else if (c == '"') {
            StrBuf key;
            sb_init(&key);
            const char *after = json_parse_string(p, &key);
            if (!after) { sb_free(&key); break; }
            const char *q = skip_ws(after);
            if (*q == ':' && depth == 2 && key.p && strcmp(key.p, "text") == 0) {
                q = skip_ws(q + 1);
                if (*q == '"') {
                    const char *end = json_parse_string(q, out);
                    if (!end) { sb_free(&key); break; }
                    found = 1;
                    p = end;
                    sb_free(&key);
                    continue;
                }
            }
            sb_free(&key);
            p = after;
        } else {
            p++;
        }
    }
    return found && out->len > 0;
}

int json_find_string_value(const char *json, const char *key, StrBuf *out)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        const char *q = skip_ws(p + strlen(pat));
        if (*q == ':') {
            q = skip_ws(q + 1);
            if (*q == '"') {
                return json_parse_string(q, out) != NULL;
            }
        }
        p += strlen(pat);
    }
    return 0;
}
