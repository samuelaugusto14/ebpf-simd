# Primeiro exemplo de SIMD no uBPF

O programa recebe oito inteiros de 32 bits com sinal. Soma `10` aos valores maiores que `49` e mantém os demais. Ele grava o vetor resultante, uma máscara que identifica os elementos modificados e a soma dos oito resultados.

## Montar o bytecode, compilar e executar

Use Linux x86-64 com AVX2, GNU Make, CMake, compiladores nativos de C e C++ e Python 3 com Parcon. O interpretador Legacy do uBPF executa as instruções SIMD da ISA v0.7 com o backend AVX2.

Rode os comandos na raiz do repositório. Se precisar preparar um ambiente Python, use as dependências do projeto.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
```

```sh
make -C demos/basic-simd-example
cmake -S . -B build
cmake --build build --target basic-simd-example
./demos/build/basic-simd-example/basic-simd-example \
  demos/build/basic-simd-example/basic-simd-example.bin
```

O Makefile chama `bin/ubpf-assembler` com `basic-simd-example.asm` e gera `basic-simd-example.bin` com os bytes brutos das instruções eBPF. Para escolher o Python sem ativar o ambiente virtual, use `make -C demos/basic-simd-example PYTHON="$PWD/.venv/bin/python"`.

O CMake da raiz descobre a demo, compila `main.c` e vincula o executável à biblioteca `ubpf`. O executável e o bytecode ficam em `demos/build/basic-simd-example/`. A opção `-DUBPF_ENABLE_BASIC_SIMD_EXAMPLE=ON` reativa a demo se ela estiver desativada no cache do CMake. Em um ambiente sem Clang para o `basic-filter`, acrescente `-DUBPF_ENABLE_BASIC_FILTER=OFF` ao comando de configuração.

Se o submódulo `external/bpf_conformance` estiver ausente, acrescente `-DUBPF_SKIP_EXTERNAL=ON` ao comando de configuração para compilar as demos sem os testes externos de conformidade.

## Como os dados passam pela VM

O `main.c` lê o `.bin` e chama `ubpf_load()` para carregar o programa na VM. Em seguida, `ubpf_exec()` recebe o endereço e o tamanho da estrutura `simd_context` e disponibiliza seu endereço no registrador escalar `r1`.

| Offset a partir de `r1` | Tamanho | Campo |
| ---: | ---: | --- |
| 0 | 32 bytes | Oito inteiros de entrada |
| 32 | 32 bytes | Oito inteiros de saída |
| 64 | 4 bytes | Máscara de elementos modificados |
| 68 | 4 bytes | Soma dos resultados |

1. `vld` carrega a entrada em `v0`, um registrador de 256 bits que contém os oito elementos (lanes). `vcmp.gt.s32 k` compara cada lane com `49`, usando comparação com sinal, e escreve a máscara em `v15`.
2. `vmov` copia o vetor para `v1`, onde `vadd.32 k` soma `10` a todas as lanes. `vmovm.32` copia de volta para `v0` apenas as lanes marcadas em `v15`. `vst` grava as oito lanes de `v0` no campo de saída.
3. `vmovmsk.32` coleta os bits da máscara em `r3`, usando o bit `i` para o elemento de índice `i`. `vreduce.add.32` soma as lanes do vetor final e coloca o resultado em `r4`. As duas instruções `stxw` gravam esses resultados no contexto. A adição por lane e a redução são feitas módulo `2^32`, conforme a ISA.

## Resultado esperado

```text
Entrada [-7, 49, 50, 80, 0, 51, 48, 100]
Saída [-7, 49, 60, 90, 0, 61, 48, 110]
Máscara = 0xac
Soma = 411
Verificação passou.
```

A máscara `0xac` marca os índices `2`, `3`, `5` e `7`, contados a partir de zero. O valor `49` permanece igual porque a comparação é estrita. A função `verify_result()` compara cada elemento da saída, a máscara e a soma com os valores esperados para essa entrada. Se houver divergência, o executável informa o campo e termina com código de saída diferente de zero.
