#include "net.h"

#include <curl/curl.h>
#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <pspnet_resolver.h>
#include <psputility.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logic.h"   /* StrBuf */

#define APCTL_STATE_GOT_IP 4   /* PSP_NET_APCTL_STATE_GOT_IP */

static int s_stage = 0;                 /* quantos passos do init já deram certo */
static const char *s_stage_name = "";   /* passo que falhou (para mostrar na tela) */

const char *net_init_failed_step(void) { return s_stage_name; }

static void net_term_partial(void)
{
    if (s_stage >= 6) sceNetApctlTerm();
    if (s_stage >= 5) sceNetResolverTerm();
    if (s_stage >= 4) sceNetInetTerm();
    if (s_stage >= 3) sceNetTerm();
    s_stage = 0;
    s_stage_name = "";
}

/* Retomável: se falhar, dá pra chamar de novo (ex.: pelo menu) e ele continua do passo
 * que falhou, sem repetir os que já deram certo. */
int net_init(void)
{
    int rc;

    if (s_stage >= 3) {
        s_stage_name = "Reinicializando rede";
        net_term_partial();
    }

    if (s_stage < 1) {
        s_stage_name = "LoadNetModule(COMMON)";
        rc = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
        if (rc < 0) return rc;
        s_stage = 1;
    }
    if (s_stage < 2) {
        s_stage_name = "LoadNetModule(INET)";
        rc = sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
        if (rc < 0) return rc;
        s_stage = 2;
    }
    if (s_stage < 3) {
        s_stage_name = "sceNetInit";
        rc = sceNetInit(128 * 1024, 42, 4 * 1024, 42, 4 * 1024);
        if (rc < 0) return rc;
        s_stage = 3;
    }
    if (s_stage < 4) {
        s_stage_name = "sceNetInetInit";
        rc = sceNetInetInit();
        if (rc < 0) return rc;
        s_stage = 4;
    }
    if (s_stage < 5) {
        s_stage_name = "sceNetResolverInit";
        rc = sceNetResolverInit();
        if (rc < 0) return rc;
        s_stage = 5;
    }
    if (s_stage < 6) {
        s_stage_name = "sceNetApctlInit";
        rc = sceNetApctlInit(0x8000, 48);
        if (rc < 0) return rc;
        s_stage = 6;
    }
    if (s_stage < 7) {
        s_stage_name = "curl_global_init";
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return -1;
        s_stage = 7;
    }
    s_stage_name = "";
    return 0;
}

int net_is_connected(void)
{
    int state = 0;
    return sceNetApctlGetState(&state) == 0 && state == APCTL_STATE_GOT_IP;
}

int net_connect(int profile)
{
    int state = 0;

    if (sceNetApctlGetState(&state) == 0 && state == APCTL_STATE_GOT_IP) return 0;

    int rc = sceNetApctlConnect(profile);
    if (rc != 0) return rc;

    for (int i = 0; i < 300; i++) {            /* até ~30 s */
        rc = sceNetApctlGetState(&state);
        if (rc != 0) return rc;
        if (state == APCTL_STATE_GOT_IP) return 0;
        sceKernelDelayThread(100 * 1000);
    }
    return -1;   /* timeout */
}

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    StrBuf *sb = (StrBuf *)userdata;
    size_t n = size * nmemb;
    if (!sb_append_n(sb, (const char *)ptr, n)) return 0;   /* sem memória: aborta */
    return n;
}

int net_post_json(const char *url, const char *api_key, const char *body,
                  const char *cacert_path, char **out, long *http_code,
                  char *err, size_t err_cap)
{
    *out = NULL;
    *http_code = 0;
    if (err_cap) err[0] = '\0';

    CURL *c = curl_easy_init();
    if (!c) {
        snprintf(err, err_cap, "curl_easy_init falhou");
        return -1;
    }

    char keyhdr[256];
    snprintf(keyhdr, sizeof keyhdr, "x-goog-api-key: %s", api_key);

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    hdrs = curl_slist_append(hdrs, keyhdr);

    StrBuf resp;
    sb_init(&resp);

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

    if (cacert_path) {
        curl_easy_setopt(c, CURLOPT_CAINFO, cacert_path);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    } else {
        /* O PSP não tem store de CAs. Sem cacert.pem ao lado do EBOOT, não verificamos. */
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    CURLcode rc = curl_easy_perform(c);
    int ret = 0;
    if (rc != CURLE_OK) {
        snprintf(err, err_cap, "Falha de rede: %s", curl_easy_strerror(rc));
        ret = -1;
        sb_free(&resp);
    } else {
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, http_code);
        *out = sb_take(&resp);
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    return ret;
}
