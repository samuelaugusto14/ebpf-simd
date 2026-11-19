# Basic filter no uBPF

O demo executa um filtro eBPF escalar no interpretador do uBPF. O `main.c` contém quatro registros sintéticos de pacotes. O filtro aceita registros com protocolo TCP (`6`) e porta de destino `443`.

## Hierarquia do CMake

```text
ubpf/CMakeLists.txt
├── vm/CMakeLists.txt                      biblioteca nativa ubpf
└── demos/CMakeLists.txt                   opção UBPF_ENABLE_BASIC_FILTER
    └── basic-filter/CMakeLists.txt        executável nativo e objeto eBPF
```

- O CMake da raiz configura o projeto e carrega `vm/` e `demos/`. O CMake de `vm/` define o alvo `ubpf` e expõe seus cabeçalhos públicos.
- `demos/CMakeLists.txt` inclui `basic-filter/` quando `UBPF_ENABLE_BASIC_FILTER` está habilitada. A opção vem desabilitada por padrão.
- `basic-filter/CMakeLists.txt` define os dois alvos do demo.
  - `basic-filter-bpf` usa `add_custom_command` para chamar o Clang e gerar o ELF relocável `filter.o`. `--target=bpfel` seleciona eBPF little-endian, `-mcpu=v1` usa o conjunto básico de instruções e `-ffreestanding` permite usar os tipos de `<stdint.h>` sem recorrer aos cabeçalhos da libc do host. O objeto é recompilado quando `filter.c` ou `filter.h` muda.
  - `basic-filter` compila `main.c` com o compilador C do host e vincula o executável à biblioteca `ubpf` com `target_link_libraries`. `add_dependencies` faz esse alvo também gerar `filter.o` por meio de `basic-filter-bpf`.

## Configurar e compilar

Os comandos abaixo são para Linux little-endian com CMake, Make, compiladores nativos de C e C++ e Clang com suporte ao alvo BPF. A partir da raiz do repositório `ubpf`, configure o projeto e compile o demo completo.

```sh
cmake -S . -B demos/basic-filter/build -G "Unix Makefiles" \
  -DUBPF_ENABLE_BASIC_FILTER=ON \
  -DUBPF_SKIP_EXTERNAL=ON

cmake --build demos/basic-filter/build --target basic-filter
```

- `-S .` indica que o `CMakeLists.txt` principal está no diretório atual, a raiz do `ubpf`.
- `-B demos/basic-filter/build` define onde ficam o cache do CMake, os arquivos gerados pelo CMake e os artefatos compilados.
- `-G "Unix Makefiles"` seleciona o gerador de arquivos para o Make. O CMake chama o Make ao executar `cmake --build`.
- `-DUBPF_ENABLE_BASIC_FILTER=ON` habilita a opção que inclui `demos/basic-filter` e registra os alvos do demo.
- `-DUBPF_SKIP_EXTERNAL=ON` desativa o projeto externo de testes `Conformance`. Isso dispensa a dependência de Boost desse projeto no build do demo.
- `--build` usa o diretório já configurado. `--target basic-filter` seleciona o executável e aciona suas dependências, a biblioteca `ubpf` e o objeto eBPF `filter.o`.

As opções `-D` definem variáveis no cache do CMake. Para escolher um Clang específico, acrescente `-DBASIC_FILTER_CLANG=/caminho/para/clang` ao comando de configuração. Para recompilar após editar `main.c`, `filter.c` ou `filter.h`, basta repetir o segundo comando.

## Executar e ler o resultado

Ainda na raiz do repositório, passe o caminho do objeto eBPF ao executável nativo. O CMake reproduz a hierarquia dos subdiretórios de origem dentro da pasta de build, por isso `filter.o` fica em `build/demos/basic-filter/` dentro do demo.

```sh
./demos/basic-filter/build/bin/basic-filter \
  demos/basic-filter/build/demos/basic-filter/filter.o
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
