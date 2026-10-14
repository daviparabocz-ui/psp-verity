#ifndef NET_H
#define NET_H

#include <stddef.h>

/* Carrega módulos de rede, inicia sceNet* e libcurl. Retorna 0 se ok. */
int net_init(void);

/* Nome do passo do net_init que falhou (string vazia se deu tudo certo). */
const char *net_init_failed_step(void);

/* 1 se o Wi-Fi já está conectado e com IP. */
int net_is_connected(void);

/* Conecta ao perfil de Wi-Fi salvo no PSP (1 = primeiro da lista) e espera pegar IP.
 * Bloqueante (até ~30 s) — chamar de uma thread. Retorna 0 se ok. */
int net_connect(int profile);

/* POST JSON via HTTPS. Bloqueante — chamar de uma thread.
 * Header usado: x-goog-api-key. Se cacert_path != NULL, verifica o certificado do
 * servidor contra esse arquivo; se for NULL, NÃO verifica (protótipo pessoal).
 * *out recebe o corpo da resposta (malloc; o chamador dá free).
 * Retorna 0 se a requisição foi feita (mesmo com HTTP != 200; veja *http_code),
 * ou != 0 com a mensagem em err. */
int net_post_json(const char *url, const char *api_key, const char *body,
                  const char *cacert_path, char **out, long *http_code,
                  char *err, size_t err_cap);

#endif
