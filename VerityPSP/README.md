# Verity para PSP (protótipo)

Port do app Android do Verity: o Verity (sprites + emotes por tags) e uma caixa de
texto rolável, falando com o Gemini direto pelo Wi-Fi do PSP (libcurl + mbedTLS).

## Controles
- **L + R juntos**: abre o teclado do PSP. **Start** confirma e já envia.
- **Analógico** (ou D-pad cima/baixo): rola a caixa de texto.
- **Select**: abre o menu. "Configurações de rede" abre o diálogo de rede do próprio PSP
  (escolher a conexão e conectar). **X** confirma, **O** volta.
- Só dá pra enviar quando o Verity terminou de falar (igual ao app).

## Configuração (arquivo `key.txt` na mesma pasta do EBOOT.PBP)
```
SUA_API_KEY_DO_GEMINI
gemini-3.8-flash        <- opcional: modelo
1                       <- opcional: nº do perfil de Wi-Fi (só usado se você não conectar pelo menu)
```
Opcional: `cacert.pem` na mesma pasta. Se existir, o certificado do servidor é
verificado; se não, a verificação é desligada (o PSP não tem lista de CAs).

## Compilar
Precisa do PSPDEV com os pacotes `curl` e `mbedtls`:
```
psp-pacman -S curl mbedtls zlib     # ou: psp-pacman -Syu curl mbedtls
make
```
Copie `EBOOT.PBP` + `key.txt` para `ms0:/PSP/GAME/Verity/`.

## Testes da lógica (no PC, só gcc)
`sh tests/run.sh`  — parser de tags, JSON, UTF-8, quebra de linha e a máquina de
estados do typewriter/emotes.

## Estrutura
- `src/logic.c`  tags/emotes, JSON, UTF-8, wrap, system prompt (portável)
- `src/player.c` typewriter por palavra + animação (portável, 1 tick por frame)
- `src/gfx.c`    desenho por CPU num backbuffer; GU só para swap e para o OSK
- `src/net.c`    sceNet/apctl + libcurl
- `src/main.c`   UI, entrada, OSK, thread de rede
- `tools/gen_assets.py` regenera `src/assets.c` (sprites + fonte) a partir de `assets_src/`

## Diferenças em relação ao app Android
- Sem anexos, sem histórico em disco (só memória, últimas 16 mensagens).
- Texto aparece palavra por palavra.
- O system prompt ganhou uma linha pedindo texto simples (sem emojis/markdown).
- Fonte: bitmap derivada de DejaVu Sans Mono (licença livre), cobre Latin-1 (acentos).
- A API key vai no header `x-goog-api-key` em vez da URL.

## Aviso
Escrito sem acesso ao toolchain do PSP: a lógica portável foi testada no PC, mas o
código específico de PSP (gfx/net/main) nunca foi compilado. Se o `make` reclamar,
mande o erro que eu ajusto.
