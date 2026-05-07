#!/bin/bash

# Nome do arquivo de saída
OUTPUT="parallel-histo"
SOURCE="mainv2.c"

echo "Compilando $SOURCE..."

# Adicionado -std=gnu11 para garantir suporte a barreiras e extensões POSIX
gcc -O3 -std=gnu11 -o $OUTPUT $SOURCE -lpthread

if [ $? -eq 0 ]; then
    echo "Compilação concluída com sucesso: ./$OUTPUT"
    chmod +x $OUTPUT
else
    echo "Erro na compilação!"
    exit 1
fi