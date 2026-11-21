# Guia de demos escalares e SIMD no uBPF

Uma demo comparativa usa um executável hospedeiro para preparar uma entrada comum, executar as versões escalar e SIMD no uBPF e medir o custo de cada versão. Este guia descreve como organizar o código, gerar os bytecodes e conferir se os programas produzem resultados equivalentes.

## Organizar os arquivos

| Arquivo | Papel na demo |
| --- | --- |
| `main.c` | Executável nativo que prepara as entradas, carrega o bytecode, chama o uBPF e valida os resultados. |
| `*.bpf.c` | Fonte C do programa escalar, compilado para eBPF pelo Clang. |
| `*_simd.asm` | Programa escrito na sintaxe do assembler do uBPF, com instruções escalares e vetoriais. |
| Cabeçalho de contexto | Define o layout de memória que o host entrega à VM. Use `offsetof()` e `_Static_assert` para conferir os offsets usados diretamente no assembly. |
| `CMakeLists.txt` | Define como gerar o executável e os dois bytecodes e em que ordem esses alvos precisam ser construídos. |

Para workloads de rede, reutilize o [leitor em `demos/utils/`](utils/trace_reader.h) para obter os registros do PCAP. O `main.c` da demo decide como interpretar cada registro e transformá-lo na entrada da VM.

## Entender o build

O [CMake de `demos/`](CMakeLists.txt) descobre os subdiretórios com `CMakeLists.txt` e cria uma opção `UBPF_ENABLE_<DEMO>` para cada um. A configuração com `cmake -S . -B build` gera as regras de build. O comando `cmake --build` executa as regras necessárias para o alvo escolhido.

- `add_executable()` declara o alvo nativo a partir de `main.c`. Use `target_link_libraries()` para vinculá-lo à biblioteca `ubpf` e às dependências da demo.
- `add_custom_command(OUTPUT ... COMMAND ... DEPENDS ...)` descreve qual arquivo uma etapa produz, qual ferramenta o gera e quais fontes ou cabeçalhos podem exigir sua regeneração.
- `add_custom_target(... DEPENDS ...)` agrupa os bytecodes. Use `add_dependencies()` para que o alvo do executável também construa esse grupo.

Use `CMAKE_CURRENT_BINARY_DIR` como destino dos artefatos. Nesta árvore, eles ficam em `demos/build/<demo>/`. Se o host precisar localizar os bytecodes automaticamente, `target_compile_definitions()` pode passar esse diretório ao compilador como uma macro.

```text
main.c            → compilador nativo → executável hospedeiro
escalar.bpf.c     → Clang → escalar.asm → llvm-mc → escalar.o → llvm-objcopy → escalar.bin
workload_simd.asm → bin/ubpf-assembler → simd.bin
```

O assembler transforma mnemônicos e operandos em instruções codificadas. O Clang usa `--target=bpfel -S` para gerar assembly LLVM, em que `w2 += 1` incrementa um registrador de 32 bits. O `llvm-mc` monta essa sintaxe. O assembler Python do uBPF usa `add32 %r2, 1` para a mesma operação escalar e `vadd.32 k %v3, 1` para incrementar cada lane de 32 bits de um vetor.

O `llvm-mc` gera um objeto ELF. Para carregar bytecode bruto com `ubpf_load()`, confira com `llvm-objdump -d` se o código está em `.text` e com `llvm-readelf --relocations` se o objeto está livre de relocações. Uma relocação indica um campo que precisa de ajuste, por exemplo para resolver uma referência a um símbolo. Faça essa verificação antes de extrair `.text` com `llvm-objcopy`. O assembler Python gera o bytecode bruto diretamente.

## Fazer uma comparação equivalente

- Fixe o formato da entrada, a ordem dos campos, a aritmética e os parâmetros compartilhados entre as versões.
- Use a implementação escalar e seu assembly gerado como base para escrever o SIMD. Identifique as operações independentes e documente o que cada lane representa.
- Prepare a entrada uma vez e entregue os mesmos dados, na mesma ordem, às duas VMs. Use contextos independentes e preserve o estado de cada programa entre as chamadas. Nesta árvore, execute ambos no interpretador Legacy com checagem de limites para comparar com o SIMD.

## Medir o custo da VM

Defina quantos itens cada chamada processa e, se a entrada for dividida em blocos, documente o tamanho do bloco. Para medir apenas o tempo da VM, leia `CLOCK_MONOTONIC_RAW` imediatamente antes e depois de cada `ubpf_exec()` e some os intervalos por versão. Parsing, preparação do contexto, cálculo da referência e validação ficam fora desses intervalos. As leituras do relógio e a ordem de execução das versões podem influenciar a medição.

Reporte itens processados, chamadas por VM, tempo total, ns por item e speedup (`tempo escalar / tempo SIMD`). Registre a entrada usada, a revisão do código, a CPU e a configuração do build para que outra pessoa possa repetir a comparação.

## Validar a corretude

Compare as saídas entre si e com uma implementação nativa independente. Use `memcmp()` quando a representação em bytes for determinística. Inclua um caso pequeno com resultado conhecido e verifique invariantes do algoritmo, como a conservação do total em um workload de contagem. Em programas com estado, um caso que ultrapasse um bloco ajuda a verificar a continuidade. Faça o executável retornar falha se houver erro na VM ou divergência na validação.

## Compilar e reproduzir uma demo

Para o fluxo descrito aqui, use Linux x86-64 com AVX2, CMake, compiladores nativos, Clang com backend BPF, ferramentas LLVM e Python 3 com as dependências de `requirements.txt`. Documente no README da demo as dependências específicas, como libpcap para leitura de PCAP, e uma entrada de teste com resultados esperados.

Execute da raiz do repositório, com as dependências Python instaladas na `.venv`. Substitua o diretório, o alvo e os argumentos pelos da demo.

```sh
demo_dir=nome-da-demo
demo_target=nome-do-alvo
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build --target "$demo_target"
"./demos/build/$demo_dir/$demo_target" caminho/para/entrada
```

O repositório contém uma aplicação completa deste fluxo com um trace de exemplo. [Demo de Count-Min Sketch](cms/README.md)
