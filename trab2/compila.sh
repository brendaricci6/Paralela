#!/bin/bash
# compila.sh — compila o trabalho 2 CI1316
# Uso: bash compila.sh

set -e

PROG="MPI+Pthreads-Multipartition"
SRC="mpi_multipartition.c"

echo "==> Compilando $SRC ..."

mpicc -O2 -Wall -Wextra \
      -D_XOPEN_SOURCE=600 \
      -D_POSIX_C_SOURCE=200112L \
      -o "$PROG" "$SRC" \
      -lpthread -lm

echo "==> Binario gerado: $PROG"
echo "==> OK"
