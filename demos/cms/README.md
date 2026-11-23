# Benchmark de Count-Min Sketch no uBPF

O Count-Min Sketch (CMS) estima a frequência de uma chave em um fluxo de dados com uma matriz de tamanho fixo. Este benchmark usa oito linhas com 1.024 contadores de 32 bits cada, o que ocupa 32 KiB por matriz. Cada pacote incrementa um contador em cada linha. Para consultar a frequência de uma chave, calcule os mesmos oito índices e tome o menor dos oito contadores. Colisões podem elevar a estimativa. [Artigo original de Cormode e Muthukrishnan](https://www.cs.helsinki.fi/u/jilu/paper/countMin.pdf)

## Compilar e executar

Use Linux x86-64 com AVX2, CMake, compiladores nativos de C e C++, Clang com backend BPF, as ferramentas LLVM `llvm-mc`, `llvm-readelf` e `llvm-objcopy`, os arquivos de desenvolvimento da libpcap com `pkg-config` e Python 3 com as dependências do assembler. Execute na raiz do repositório.

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build --target cms_benchmark
./demos/build/cms/cms_benchmark /path/to/trace.pcap
```

O CMake informa quando precisa pular a demo por falta de uma dependência ou do backend AVX2. Use `-DUBPF_ENABLE_CMS=OFF` para desativá-la. Se o CMake encontrar um Clang sem suporte a BPF, configure `-DUBPF_BPF_CLANG=/caminho/para/clang`. Com o submódulo de conformidade ausente, acrescente `-DUBPF_SKIP_EXTERNAL=ON`.

O alvo `cms_benchmark` gera o executável e os dois programas em `demos/build/cms/`. O executável carrega os arquivos `.bin` desse diretório de build por meio de `ubpf_load()`.

- O Clang compila `cms_serial.bpf.c` para `cms_serial.asm` com `--target=bpfel -mcpu=v3 -O2`. O `llvm-mc` monta `cms_serial.o` com a sintaxe do LLVM. O build verifica a ausência de relocações com `llvm-readelf` antes de extrair `.text` para `cms_serial.bin` com `llvm-objcopy`. Use `llvm-objdump -d demos/build/cms/cms_serial.o` para inspecionar o programa escalar.
- O assembler Python `bin/ubpf-assembler` monta `cms_simd.asm` diretamente em `cms_simd.bin` com as instruções SIMD da v0.7.

## Chave e atualização do sketch

O leitor em `demos/utils/` fornece os registros da captura em ordem. O `main.c` extrai a 5-tupla direcional de IPv4 TCP/UDP em Ethernet, incluindo tags VLAN 802.1Q e 802.1ad. Ele descarta fragmentos IPv4 e exige cabeçalhos completos. Uma captura que corte apenas a parte de dados do pacote ainda pode fornecer uma chave válida. O parâmetro `CMS_BATCH_CAPACITY` em `main.c` define o máximo de chaves válidas por bloco. O valor padrão é 2.000.000, correspondente a 40 MB de chaves. Cada VM recebe todos os blocos na mesma ordem e mantém sua própria matriz entre eles.

A entrada do MurmurHash3 x86-32 tem 20 bytes. Ela contém cinco palavras de 32 bits em little-endian com os valores numéricos de IP de origem, IP de destino, porta de origem, porta de destino e protocolo, nessa ordem. Portas e protocolo são estendidos com zeros. A aritmética é módulo 2³² e os deslocamentos para a direita são lógicos. A coluna de cada linha é `hash & 1023`. As sementes fixadas em `main.c` seguem a ordem das linhas. [Implementação original do MurmurHash3](https://github.com/aappleby/smhasher/blob/master/src/MurmurHash3.cpp)

| Campo do contexto | Offset em bytes |
| --- | ---: |
| Chave atual | 0 |
| Matriz de contadores | 20 |
| Oito sementes | 32788 |

Os dois programas misturam as cinco palavras uma vez por pacote dentro da VM. O escalar percorre as oito sementes e atualiza um contador por linha. O SIMD carrega as sementes em um vetor, replica uma palavra misturada por vez e combina essa palavra com as oito lanes do hash. Depois da mistura final, a lane `i` usa o índice linear `i × 1024 + coluna`. Assim, `VGATHER.32` lê posições distintas e `VADD.32` e `VSCATTER.32` incrementam e gravam os oito contadores.

## Medição e validação

O host executa primeiro o escalar e depois o SIMD em cada bloco, usando o interpretador Legacy com checagem de limites. Mede cada chamada a `ubpf_exec()` com `CLOCK_MONOTONIC_RAW` e soma os intervalos por VM. A leitura e a preparação das chaves, a referência em C nativo e a validação ficam fora desses intervalos. As duas leituras do relógio por pacote podem influenciar a execução. O speedup é o tempo escalar dividido pelo tempo SIMD e as métricas por pacote usam o número de pacotes válidos.

O resultado inclui o número de pacotes lidos, a quantidade de chaves válidas, o número de chamadas por VM e os tempos. O host compara integralmente as matrizes escalar, SIMD e de referência e verifica se cada linha de cada uma soma a quantidade de pacotes válidos. Erros de leitura, falhas na VM, divergências ou overflow detectado pela referência geram um código de saída diferente de zero.
