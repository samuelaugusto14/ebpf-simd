# Basic filter no uBPF

O demo executa um filtro eBPF escalar no interpretador do uBPF. O `main.c` contém quatro registros sintéticos de pacotes. O filtro aceita registros com protocolo TCP (`6`) e porta de destino `443`.

## Hierarquia do CMake

```text
ubpf/CMakeLists.txt
├── vm/CMakeLists.txt                      biblioteca nativa ubpf
└── demos/CMakeLists.txt                   descoberta de demos e compilação eBPF
    └── basic-filter/CMakeLists.txt        alvos do filtro e do executável
```

- O CMake da raiz carrega `vm/` e `demos/` e fixa `demos/build/` como diretório binário das demos. O CMake de `vm/` define a biblioteca `ubpf` e expõe seus cabeçalhos públicos.
- `demos/CMakeLists.txt` descobre as subpastas com `CMakeLists.txt` e cria uma opção ligada por padrão para cada uma. Para `basic-filter/`, a opção é `UBPF_ENABLE_BASIC_FILTER`.
- `basic-filter/CMakeLists.txt` registra o alvo `basic-filter-bpf` com `add_bpf_program(basic-filter-bpf filter.c filter.h)`. A função compartilhada chama o Clang e gera `filter.o` no diretório binário da demo. O objeto é recompilado quando `filter.c` ou `filter.h` muda.
- O alvo `basic-filter` compila `main.c` com o compilador C do host e vincula o executável à biblioteca `ubpf` com `target_link_libraries`. `add_dependencies` faz esse alvo também gerar `filter.o` por meio de `basic-filter-bpf`.

Na compilação eBPF, `--target=bpfel` seleciona eBPF little-endian, `-mcpu=v1` usa o conjunto básico de instruções, `-ffreestanding` permite usar os tipos de `<stdint.h>` sem recorrer aos cabeçalhos da libc do host e `-O2` ativa as otimizações do Clang.

## Configurar e compilar

Os comandos abaixo são para Linux little-endian com CMake, Make, compiladores nativos de C e C++, Clang com suporte a BPF e os submódulos do repositório inicializados. Rode na raiz do `ubpf` para configurar e compilar a VM e todas as demos habilitadas.

```sh
cmake -S . -B build && cmake --build build
```

- `-S .` indica que o `CMakeLists.txt` principal está no diretório atual, a raiz do `ubpf`.
- `-B build` define o diretório do cache e da saída da VM. O CMake da raiz direciona a saída das demos para `demos/build/`.
- `--build build` compila os alvos padrão, incluindo a biblioteca `ubpf` e as demos habilitadas. Para recompilar apenas o filtro, use `cmake --build build --target basic-filter-bpf`.
- `-DUBPF_ENABLE_BASIC_FILTER=OFF` desativa o demo. Se o cache de um build anterior já tiver essa opção em `OFF`, use `-DUBPF_ENABLE_BASIC_FILTER=ON` para reativá-lo.
- `-DUBPF_SKIP_EXTERNAL=ON` pula o projeto de testes `Conformance` e sua dependência de Boost.
- `-G "Unix Makefiles"` seleciona o gerador que usa Make ao configurar um build novo.

As opções `-D` definem variáveis no cache do CMake. Para indicar um diretório de busca alternativo para o Clang, use `-DUBPF_ALTERNATE_LLVM_PATH=/caminho/para/bin`. Para indicar o executável diretamente, use `-DUBPF_BPF_CLANG=/caminho/para/clang`. Use um diretório de build da raiz por cópia do repositório, pois as configurações compartilham `demos/build/`.

## Executar e ler o resultado

Ainda na raiz do repositório, passe o caminho do objeto eBPF ao executável nativo. Ambos ficam em `demos/build/basic-filter/`.

```sh
./demos/build/basic-filter/basic-filter demos/build/basic-filter/filter.o
```

```text
Packet 1: protocol=6 dst_port=443 result=1
Packet 2: protocol=17 dst_port=443 result=0
Packet 3: protocol=6 dst_port=80 result=0
Packet 4: protocol=6 dst_port=443 result=1
Accepted: 2 of 4
```

O `main.c` cria a VM com `ubpf_create()` e entrega a ela os bytes do ELF com `ubpf_load_elf()`. O carregador do uBPF encontra a função de entrada na seção `.text` e carrega as instruções para o interpretador. O objeto `filter.o` é lido na execução e fica fora da etapa de vinculação do executável nativo.

`ubpf_exec()` recebe o endereço e o tamanho do lote contíguo. O interpretador coloca o endereço em `r1`, que é o argumento `batch` de `filter.c`. O `filter.h` dá aos dois compiladores a mesma definição do contexto, com um array por campo (SoA). O filtro percorre até `count` registros, limitado à capacidade de oito, grava cada decisão em `results` e retorna o total de aceitos em `r0`. O `main.c` confere as quatro decisões e o total. Qualquer divergência encerra o demo com status diferente de zero.
