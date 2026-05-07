#!/bin/bash

# Nome do arquivo de saída
OUTPUT="parallel-histo"

# Arquivo fonte (baseado no código fornecido)
SOURCE="mainv2.c"

echo "Compilando $SOURCE..."

# Compilação com otimização O3 e suporte a pthreads
gcc -O3 -o $OUTPUT $SOURCE -lpthread

if [ $? -eq 0 ]; then
    echo "Compilação concluída com sucesso: ./$OUTPUT"
    chmod +x $OUTPUT
else
    echo "Erro na compilação!"
    exit 1
fi