/*
 * Verity para PSP — protótipo.
 *
 *  - Só aparecem o Verity (direita) e a caixa de texto dele (esquerda).
 *  - Analógico (ou D-pad) rola a caixa de texto.
 *  - L + R juntos abrem o teclado do PSP (OSK). Start confirma e já envia.
 *  - SELECT abre o menu; "Configurações de rede" abre o diálogo de rede do próprio PSP
 *    (escolher/conectar o Wi-Fi), sem depender do perfil no key.txt.
 *  - key.txt (mesma pasta do EBOOT.PBP):
 *        linha 1: API key do Gemini
 *        linha 2: (opcional) modelo, padrão gemini-3.8-flash
 *        linha 3: (opcional) nº do perfil de Wi-Fi do PSP (só vale se não houver
 *                 conexão feita pelo menu), padrão 1
 *  - cacert.pem (opcional, mesma pasta): se existir, o certificado do servidor é verificado.
 */
#include <pspctrl.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psputility.h>
#include <psputility_netconf.h>
#include <pspnet_apctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "logic.h"
#include "net.h"
#include "player.h"

PSP_MODULE_INFO("Verity", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-2048);

#define DEFAULT_MODEL "gemini-3.8-flash"
#define MAX_HIST 16
#define MAX_LINES 1400

/* ---- layout ---- */
#define PANEL_X 6
#define PANEL_Y 6
#define PANEL_W 282
#define PANEL_H 260
#define PAD_X 8
#define PAD_Y 6
#define LINE_H (FONT_CELL_H - 1)
#define TEXT_X (PANEL_X + PAD_X)
#define TEXT_Y (PANEL_Y + PAD_Y)
#define TEXT_COLS ((PANEL_W - 2 * PAD_X - 4) / FONT_CELL_W)
#define TEXT_ROWS ((PANEL_H - 2 * PAD_Y) / LINE_H)
#define VERITY_CX 384
#define VERITY_CY 136
#define VERITY_SCALE 2

#define COL_BG       COL(24, 24, 28)
#define COL_PANEL    COL(14, 14, 18)
#define COL_BORDER   COL(90, 90, 110)
#define COL_BORDER_S COL(160, 50, 50)
#define COL_TEXT     COL(235, 235, 235)
#define COL_HINT     COL(130, 130, 140)
#define COL_ERROR    COL(255, 120, 120)
#define COL_BAR      COL(110, 110, 125)

/* ------------------------------------------------------------------ */
/* Estado global                                                       */
/* ------------------------------------------------------------------ */
static char g_base_dir[256] = "ms0:/PSP/GAME/Verity/";
static char g_api_key[160];
static char g_model[64] = DEFAULT_MODEL;
static int  g_profile = 1;
static int  g_config_ok = 0;
static int  g_net_ready = 0;

static Player g_player;
static char  *g_system_prompt = NULL;

static Msg g_hist[MAX_HIST];
static int g_hist_n = 0;

enum { UI_NORMAL, UI_HINT, UI_ERROR };
static int  g_ui_mode = UI_HINT;
static char g_ui_msg[512] = "Aperte L + R para falar com o Verity.\nSELECT abre o menu (rede).";

/* menu (SELECT) */
static int g_menu_open = 0;
static int g_menu_sel = 0;
static const char *const MENU_ITEMS[] = { "Configurações de rede", "Fechar" };
#define MENU_N 2

static float g_scroll = 0.0f;
static int   g_follow = 1;
static int   g_total_lines = 0;
static unsigned g_frame = 0;

static int g_lines_start[MAX_LINES];
static int g_lines_len[MAX_LINES];

/* thread de rede */
static volatile int g_net_state = 0;   /* 0 ocioso, 1 em andamento, 2 terminou */
static char *g_body = NULL;
static char *g_resp = NULL;
static long  g_code = 0;
static int   g_net_ok = 0;
static char  g_net_err[256];
static int   g_wifi_connected = 0;

/* ------------------------------------------------------------------ */
/* Callbacks do HOME                                                   */
/* ------------------------------------------------------------------ */
static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1; (void)arg2; (void)common;
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Config (key.txt)                                                    */
/* ------------------------------------------------------------------ */
static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int load_config(void)
{
    char path[320];
    snprintf(path, sizeof path, "%skey.txt", g_base_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    char line[256];
    int n = 0;
    while (n < 3 && fgets(line, sizeof line, f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        if (n == 0) snprintf(g_api_key, sizeof g_api_key, "%s", line);
        else if (n == 1) snprintf(g_model, sizeof g_model, "%s", line);
        else if (n == 2) { int p = atoi(line); if (p >= 1) g_profile = p; }
        n++;
    }
    fclose(f);
    return g_api_key[0] != '\0';
}

/* ------------------------------------------------------------------ */
/* UI: mensagens                                                       */
/* ------------------------------------------------------------------ */
static void show_error(const char *msg)
{
    snprintf(g_ui_msg, sizeof g_ui_msg, "%s", msg);
    g_ui_mode = UI_ERROR;
    g_scroll = 0;
    g_follow = 0;
}

static void show_hint(const char *msg)
{
    snprintf(g_ui_msg, sizeof g_ui_msg, "%s", msg);
    g_ui_mode = UI_HINT;
    g_scroll = 0;
    g_follow = 0;
}

static int ci_contains(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] &&
               ((hay[i] | 0x20) == (needle[i] | 0x20))) i++;
        if (i == n) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Histórico (só em memória)                                           */
/* ------------------------------------------------------------------ */
static void hist_drop_first(void)
{
    if (g_hist_n == 0) return;
    free(g_hist[0].text);
    memmove(&g_hist[0], &g_hist[1], sizeof(Msg) * (size_t)(g_hist_n - 1));
    g_hist_n--;
}

static void hist_push(int role, const char *text)
{
    if (g_hist_n == MAX_HIST) { hist_drop_first(); hist_drop_first(); }
    /* a conversa sempre começa com uma mensagem do usuário */
    while (g_hist_n > 0 && g_hist[0].role != 0) hist_drop_first();
    g_hist[g_hist_n].role = role;
    g_hist[g_hist_n].text = strdup(text);
    g_hist_n++;
}

static void hist_drop_last(void)
{
    if (g_hist_n == 0) return;
    g_hist_n--;
    free(g_hist[g_hist_n].text);
}

/* ------------------------------------------------------------------ */
/* Rede (thread)                                                       */
/* ------------------------------------------------------------------ */
static int net_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;

    g_net_ok = 0;
    g_net_err[0] = '\0';
    g_code = 0;
    free(g_resp);
    g_resp = NULL;

    if (!g_net_ready) {
        snprintf(g_net_err, sizeof g_net_err, "A rede do PSP não foi inicializada.");
    } else if (net_connect(g_profile) != 0) {
        snprintf(g_net_err, sizeof g_net_err,
                 "Não consegui conectar no Wi-Fi. Aperte SELECT e abra \"Configurações de rede\" (perfil tentado: %d).", g_profile);
    } else {
        char url[256];
        char cacert[320];
        snprintf(url, sizeof url,
                 "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent", g_model);
        snprintf(cacert, sizeof cacert, "%scacert.pem", g_base_dir);
        int rc = net_post_json(url, g_api_key, g_body, file_exists(cacert) ? cacert : NULL,
                               &g_resp, &g_code, g_net_err, sizeof g_net_err);
        g_net_ok = (rc == 0);
    }

    g_net_state = 2;
    sceKernelExitDeleteThread(0);
    return 0;
}

static void begin_send(const char *text)
{
    hist_push(0, text);
    free(g_body);
    g_body = build_request_json(g_hist, g_hist_n, g_system_prompt);
    if (!g_body) {
        hist_drop_last();
        show_error("Sem memória para montar a requisição.");
        return;
    }

    player_clear_display(&g_player);
    g_ui_mode = UI_NORMAL;
    g_follow = 1;
    g_net_state = 1;

    int th = sceKernelCreateThread("verity_net", net_thread, 0x28, 256 * 1024, PSP_THREAD_ATTR_USER, NULL);
    if (th < 0) {
        g_net_state = 0;
        hist_drop_last();
        show_error("Não consegui criar a thread de rede.");
        return;
    }
    sceKernelStartThread(th, 0, NULL);
}

static void poll_net(void)
{
    if (g_net_state != 2) return;
    g_net_state = 0;

    free(g_body);
    g_body = NULL;

    if (!g_net_ok) {
        hist_drop_last();
        show_error(g_net_err[0] ? g_net_err : "Falha de rede.");
        return;
    }

    if (g_code < 200 || g_code >= 300) {
        StrBuf msg;
        sb_init(&msg);
        char head[48];
        snprintf(head, sizeof head, "Erro HTTP %ld: ", g_code);
        sb_append(&msg, head);
        StrBuf m;
        sb_init(&m);
        if (g_resp && json_find_string_value(g_resp, "message", &m)) sb_append(&msg, m.p);
        else sb_append(&msg, "(sem detalhes)");
        if (msg.p && ci_contains(msg.p, "not found"))
            sb_append(&msg, " — troque o modelo na 2ª linha do key.txt.");
        show_error(msg.p ? msg.p : "Erro HTTP");
        sb_free(&m);
        sb_free(&msg);
        hist_drop_last();
        return;
    }

    StrBuf text;
    sb_init(&text);
    if (g_resp && json_extract_text(g_resp, &text)) {
        hist_push(1, text.p);
        g_ui_mode = UI_NORMAL;
        g_follow = 1;
        player_start(&g_player, text.p);
    } else {
        StrBuf why;
        sb_init(&why);
        char msg[160];
        if (g_resp && json_find_string_value(g_resp, "blockReason", &why))
            snprintf(msg, sizeof msg, "Resposta bloqueada (%s).", why.p);
        else if (g_resp && json_find_string_value(g_resp, "finishReason", &why))
            snprintf(msg, sizeof msg, "Resposta vazia do modelo (%s).", why.p);
        else
            snprintf(msg, sizeof msg, "Resposta vazia do modelo.");
        show_error(msg);
        sb_free(&why);
        hist_drop_last();
    }
    sb_free(&text);
    free(g_resp);
    g_resp = NULL;
}

/* ------------------------------------------------------------------ */
/* Renderização                                                        */
/* ------------------------------------------------------------------ */
static void render_menu(void)
{
    const int lh = FONT_CELL_H + 4;
    const int bw = PANEL_W - 40;
    const int bh = 12 + lh * (MENU_N + 2);
    const int bx = PANEL_X + 20;
    const int by = PANEL_Y + (PANEL_H - bh) / 2;

    gfx_fill_rect(bx, by, bw, bh, COL_BORDER);
    gfx_fill_rect(bx + 1, by + 1, bw - 2, bh - 2, COL_BG);
    gfx_text(bx + 8, by + 6, "Menu", 4, COL_TEXT);

    for (int i = 0; i < MENU_N; i++) {
        int y = by + 6 + lh * (i + 1);
        char line[64];
        snprintf(line, sizeof line, "%s%s", i == g_menu_sel ? "> " : "  ", MENU_ITEMS[i]);
        if (i == g_menu_sel) gfx_fill_rect(bx + 4, y - 2, bw - 8, lh, COL_BAR);
        gfx_text(bx + 8, y, line, (int)strlen(line), COL_TEXT);
    }

    const char *hint = "X: ok   O: voltar";
    gfx_text(bx + 8, by + 6 + lh * (MENU_N + 1), hint, (int)strlen(hint), COL_HINT);
}

static void render_scene(void)
{
    gfx_clear(COL_BG);

    /* painel */
    uint32_t border = (g_player.mood == MOOD_SERIOUS) ? COL_BORDER_S : COL_BORDER;
    gfx_fill_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, border);
    gfx_fill_rect(PANEL_X + 1, PANEL_Y + 1, PANEL_W - 2, PANEL_H - 2, COL_PANEL);

    /* texto a mostrar */
    const char *txt;
    uint32_t color = COL_TEXT;
    char dots[8];

    if (g_ui_mode == UI_ERROR) {
        txt = g_ui_msg;
        color = COL_ERROR;
    } else if (g_ui_mode == UI_HINT) {
        txt = g_ui_msg;
        color = COL_HINT;
    } else if (g_net_state == 1 && g_player.disp_len == 0) {
        int n = (int)((g_frame / 20) % 4);
        for (int i = 0; i < n; i++) dots[i] = '.';
        dots[n] = '\0';
        txt = dots;
        color = COL_HINT;
    } else {
        txt = g_player.disp;
    }

    int total = wrap_lines(txt, TEXT_COLS, g_lines_start, g_lines_len, MAX_LINES);
    int maxs = total > TEXT_ROWS ? total - TEXT_ROWS : 0;
    g_total_lines = total;

    if (g_follow) g_scroll = (float)maxs;
    if (g_scroll > (float)maxs) g_scroll = (float)maxs;
    if (g_scroll < 0.0f) g_scroll = 0.0f;
    if (!g_follow && maxs > 0 && g_scroll >= (float)maxs) g_follow = 1;

    int first = (int)g_scroll;
    for (int r = 0; r < TEXT_ROWS; r++) {
        int li = first + r;
        if (li >= total) break;
        gfx_text(TEXT_X, TEXT_Y + r * LINE_H, txt + g_lines_start[li], g_lines_len[li], color);
    }

    /* barra de rolagem */
    if (total > TEXT_ROWS) {
        int track_x = PANEL_X + PANEL_W - 6;
        int track_y = PANEL_Y + 4;
        int track_h = PANEL_H - 8;
        int thumb_h = track_h * TEXT_ROWS / total;
        if (thumb_h < 10) thumb_h = 10;
        int thumb_y = track_y + (int)((float)(track_h - thumb_h) * g_scroll / (float)maxs);
        gfx_fill_rect(track_x, thumb_y, 3, thumb_h, COL_BAR);
    }

    /* Verity */
    gfx_sprite(&SPRITES[player_sprite(&g_player)], VERITY_CX, VERITY_CY, VERITY_SCALE);

    /* marca da versão: se não aparecer, o EBOOT.PBP no PSP é o antigo */
    gfx_text(PANEL_X, PANEL_Y + PANEL_H + 2, "build: menu-rede v2", 19, COL_HINT);

    if (g_menu_open) render_menu();
}

/* ------------------------------------------------------------------ */
/* OSK (teclado do PSP)                                                */
/* ------------------------------------------------------------------ */
static int osk_prompt(char *out, size_t out_cap)
{
    static unsigned short intext[2];
    static unsigned short outtext[256];
    static unsigned short desc[32];
    static const char *title = "Fale com o Verity";

    memset(intext, 0, sizeof intext);
    memset(outtext, 0, sizeof outtext);
    memset(desc, 0, sizeof desc);
    for (int i = 0; title[i] && i < 30; i++) desc[i] = (unsigned short)title[i];

    SceUtilityOskData data;
    memset(&data, 0, sizeof data);
    data.language = 0;          /* PSP_UTILITY_OSK_LANGUAGE_DEFAULT */
    data.lines = 1;
    data.unk_24 = 1;
    data.inputtype = 0;         /* PSP_UTILITY_OSK_INPUTTYPE_ALL */
    data.desc = desc;
    data.intext = intext;
    data.outtextlength = 256;
    data.outtextlimit = 250;
    data.outtext = outtext;

    SceUtilityOskParams params;
    memset(&params, 0, sizeof params);
    params.base.size = sizeof(params);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &params.base.language);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN, &params.base.buttonSwap);
    params.base.graphicsThread = 17;
    params.base.accessThread = 19;
    params.base.fontThread = 18;
    params.base.soundThread = 16;
    params.datacount = 1;
    params.data = &data;

    if (sceUtilityOskInitStart(&params) != 0) return 0;

    int done = 0;
    while (!done) {
        render_scene();      /* a cena fica por trás do teclado */
        gfx_blit();
        gfx_gu_pass();

        switch (sceUtilityOskGetStatus()) {
        case PSP_UTILITY_DIALOG_INIT:     break;
        case PSP_UTILITY_DIALOG_VISIBLE:  sceUtilityOskUpdate(1); break;
        case PSP_UTILITY_DIALOG_QUIT:     sceUtilityOskShutdownStart(); break;
        case PSP_UTILITY_DIALOG_FINISHED: break;
        case PSP_UTILITY_DIALOG_NONE:     done = 1; break;
        default: break;
        }

        gfx_flip();
        g_frame++;
    }

    /* result == 2  ->  PSP_UTILITY_OSK_RESULT_CHANGED (Start confirmou com texto) */
    if (data.result == 2) {
        utf16_to_utf8(outtext, out, out_cap);
        trim(out);
        return out[0] != '\0';
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Diálogo de rede do PSP (Configurações de rede)                      */
/* ------------------------------------------------------------------ */
/* Retorna 1 se terminou conectado (com IP), 0 se não, -1 se não abriu. */
static int netconf_dialog(void)
{
    pspUtilityNetconfData data;
    struct pspUtilityNetconfAdhoc adhoc;

    memset(&data, 0, sizeof data);
    memset(&adhoc, 0, sizeof adhoc);
    data.base.size = sizeof(data);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &data.base.language);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN, &data.base.buttonSwap);
    data.base.graphicsThread = 17;
    data.base.accessThread = 19;
    data.base.fontThread = 18;
    data.base.soundThread = 16;
    data.action = PSP_NETCONF_ACTION_CONNECTAP;   /* lista as conexões salvas e conecta */
    data.adhocparam = &adhoc;

    if (sceUtilityNetconfInitStart(&data) != 0) return -1;

    int done = 0;
    while (!done) {
        render_scene();
        gfx_blit();
        gfx_gu_pass();

        switch (sceUtilityNetconfGetStatus()) {
        case PSP_UTILITY_DIALOG_INIT:     break;
        case PSP_UTILITY_DIALOG_VISIBLE:  sceUtilityNetconfUpdate(1); break;
        case PSP_UTILITY_DIALOG_QUIT:     sceUtilityNetconfShutdownStart(); break;
        case PSP_UTILITY_DIALOG_FINISHED: break;
        case PSP_UTILITY_DIALOG_NONE:     done = 1; break;
        default: break;
        }

        gfx_flip();
        g_frame++;
    }

    return net_is_connected() ? 1 : 0;
}

static void open_network_settings(void)
{
    g_menu_open = 0;

    if (!g_net_ready) {
        /* a rede não subiu no início: tenta de novo (o net_init continua de onde parou) */
        int rc = net_init();
        if (rc == 0) {
            g_net_ready = 1;
        } else {
            char msg[256];
            snprintf(msg, sizeof msg, "Falha ao iniciar a rede do PSP (0x%08X) em: %s.",
                     (unsigned)rc, net_init_failed_step());
            show_error(msg);
            return;
        }
    }

    int r = netconf_dialog();
    if (r < 0) {
        show_error("Não consegui abrir as configurações de rede do PSP.");
    } else if (r == 1) {
        show_hint(g_config_ok ? "Wi-Fi conectado.\nAperte L + R para falar com o Verity."
                              : "Wi-Fi conectado, mas falta o key.txt com a API key.");
    } else {
        show_hint("Sem conexão com o Wi-Fi.\nSELECT abre o menu de rede de novo.");
    }
}

/* ------------------------------------------------------------------ */
/* Entrada                                                             */
/* ------------------------------------------------------------------ */
static int g_prev_both = 0;
static unsigned g_prev_btn = 0;

static void handle_input(const SceCtrlData *pad)
{
    unsigned pressed = pad->Buttons & ~g_prev_btn;   /* só bordas de subida */
    g_prev_btn = pad->Buttons;

    int both = (pad->Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) ==
               (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER);

    /* menu aberto: ele captura a entrada */
    if (g_menu_open) {
        if (pressed & PSP_CTRL_UP)   g_menu_sel = (g_menu_sel + MENU_N - 1) % MENU_N;
        if (pressed & PSP_CTRL_DOWN) g_menu_sel = (g_menu_sel + 1) % MENU_N;
        if ((pressed & PSP_CTRL_CIRCLE) || (pressed & PSP_CTRL_SELECT)) g_menu_open = 0;
        if (pressed & PSP_CTRL_CROSS) {
            if (g_menu_sel == 0) open_network_settings();
            else g_menu_open = 0;
        }
        g_prev_both = both;
        return;
    }

    /* SELECT abre o menu (não durante uma requisição, pra não brigar pela rede) */
    if ((pressed & PSP_CTRL_SELECT) && g_net_state == 0) {
        g_menu_open = 1;
        g_menu_sel = 0;
        g_prev_both = both;
        return;
    }

    /* rolagem: analógico (zona morta 32) + D-pad */
    float delta = 0.0f;
    int ly = (int)pad->Ly - 128;
    if (ly > 32 || ly < -32) delta += (float)ly / 128.0f * 0.45f;
    if (pad->Buttons & PSP_CTRL_UP)   delta -= 0.25f;
    if (pad->Buttons & PSP_CTRL_DOWN) delta += 0.25f;

    if (delta < 0.0f) {
        g_follow = 0;
        g_scroll += delta;
    } else if (delta > 0.0f && !g_follow) {
        g_scroll += delta;
    }

    /* L + R juntos (só na borda de subida) */
    int can_send = g_config_ok && g_net_state == 0 && !player_active(&g_player);

    if (both && !g_prev_both && can_send) {
        char text[1100];
        if (osk_prompt(text, sizeof text)) begin_send(text);
        g_prev_both = 1;     /* evita reabrir se ainda estiver segurando L+R */
        return;
    }
    g_prev_both = both;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);   /* o handshake TLS agradece */

    /* pasta do executável (de onde vem o key.txt) */
    if (argc > 0 && argv[0]) {
        const char *slash = strrchr(argv[0], '/');
        if (slash) {
            size_t n = (size_t)(slash - argv[0]) + 1;
            if (n < sizeof g_base_dir) {
                memcpy(g_base_dir, argv[0], n);
                g_base_dir[n] = '\0';
            }
        }
    }

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    gfx_init();
    player_init(&g_player);
    g_system_prompt = build_system_prompt();

    g_config_ok = load_config();

    int rc = net_init();
    if (rc == 0) g_net_ready = 1;

    if (!g_config_ok || rc != 0) {
        char msg[512];
        size_t n = 0;
        msg[0] = '\0';
        if (!g_config_ok)
            n += (size_t)snprintf(msg + n, sizeof msg - n,
                     "Não achei o key.txt com a API key.\nCrie o arquivo em:\n%s\ne coloque a key do Gemini na primeira linha.\n\n",
                     g_base_dir);
        if (rc != 0)
            n += (size_t)snprintf(msg + n, sizeof msg - n,
                     "Falha ao iniciar a rede do PSP (0x%08X) em: %s.\n\n",
                     (unsigned)rc, net_init_failed_step());
        snprintf(msg + n, sizeof msg - n, "SELECT abre o menu.");
        show_error(msg);
    }

    SceCtrlData pad;
    memset(&pad, 0, sizeof pad);

    for (;;) {
        sceCtrlPeekBufferPositive(&pad, 1);
        handle_input(&pad);
        poll_net();
        player_tick(&g_player);

        render_scene();
        gfx_blit();
        gfx_flip();
        g_frame++;
    }

    return 0;
}
