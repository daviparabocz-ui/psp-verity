#!/bin/sh
# Compila e roda os testes da lógica portável no PC (precisa só de gcc).
set -e
cd "$(dirname "$0")/.."
gcc -std=gnu11 -Wall -Wextra -O1 -fsanitize=address,undefined -o /tmp/verity_tests \
    tests/test_logic.c src/logic.c src/player.c src/assets.c
/tmp/verity_tests
