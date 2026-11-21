# Benchmark de ACL IPv4 escalar e SIMD no uBPF

A demo classifica pacotes de uma captura Ethernet com uma lista de controle de acesso (ACL) experimental. A política tem 30 regras que podem combinar origem e destino IPv4, protocolo e portas de origem e destino. A primeira regra que corresponde ao pacote determina a ação `PASS` ou `DROP`. Se nenhuma regra corresponder, a ação é `PASS`. O benchmark registra essas decisões em uma leitura offline do PCAP.

## Compilar e executar

Use Linux x86-64 com AVX2, CMake, compiladores nativos de C e C++, Clang com backend BPF, `llvm-mc`, `llvm-readelf`, `llvm-objcopy`, os arquivos de desenvolvimento da libpcap, `pkg-config` e Python 3. Execute a partir da raiz do uBPF.

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build --target acl_benchmark

./demos/build/acl/acl_benchmark traces/processed/sample-100k.dump
./demos/build/acl/acl_benchmark traces/raw/201001011400.dump 8
./demos/build/acl/acl_benchmark traces/raw/201001011400.dump 16 --invert
```

Substitua o caminho da captura pelo PCAP Ethernet a analisar. O argumento opcional `num_rules` aceita de 1 a 30 e o padrão é 30. O host seleciona as primeiras `N` regras e aplica `--invert` sobre esse conjunto. Por exemplo, `8 --invert` começa por `R07` e termina em `R00`. Use apenas `--invert` depois do caminho para inverter as 30 regras.

O alvo gera o executável e os dois bytecodes em `demos/build/acl/`. O Clang gera o assembly eBPF de `acl_serial.bpf.c`, o `llvm-mc` o monta em ELF e o build verifica as relocações com `llvm-readelf` antes de extrair a seção `.text` com `llvm-objcopy`. O assembler `bin/ubpf-assembler` monta `acl_simd.asm` diretamente em bytecode bruto. O host carrega os dois arquivos `.bin` do diretório de build.

O CMake informa quando precisa pular o alvo por falta de dependências ou do backend AVX2. Use `-DUBPF_BPF_CLANG=/caminho/para/clang` para indicar um Clang com suporte a BPF, `-DUBPF_SKIP_EXTERNAL=ON` se o submódulo de conformidade estiver ausente ou `-DUBPF_ENABLE_ACL=OFF` para desativar a demo.

## Política e classificação

Os prefixos são os quatro `/24` mais frequentes em cada direção entre os 987.404 pacotes elegíveis no primeiro milhão de registros do dump raw `201001011400.dump`. A tabela em `main.c` usa os mesmos prefixos em todas as execuções.

| ID | Prefixo de origem | ID | Prefixo de destino |
| --- | --- | --- | --- |
| S1 | `163.234.176.0/24` | D1 | `208.108.253.0/24` |
| S2 | `208.108.253.0/24` | D2 | `163.234.176.0/24` |
| S3 | `208.108.242.0/24` | D3 | `163.234.102.0/24` |
| S4 | `163.234.102.0/24` | D4 | `199.221.50.0/24` |

O host usa o leitor compartilhado de PCAP e extrai a chave de pacotes IPv4 não fragmentados em Ethernet, inclusive com tags VLAN. O parser exige o cabeçalho TCP completo e pelo menos oito bytes de UDP ou ICMP. Ele aceita cortes na carga útil. ICMP e os demais protocolos IPv4 elegíveis recebem um valor reservado para portas ausentes. As regras ICMP usam curinga para as duas portas.

O host codifica as regras ativas na ordem de prioridade solicitada e prepara um contexto para cada VM. Ambos recebem a mesma chave de cinco palavras de 32 bits. Uma condição bate quando `(campo & máscara) == valor`. Comparações de prefixo usam a máscara `0xffffff00`, condições exatas usam todos os bits e curingas usam máscara e valor zero.

- O escalar guarda as regras em um vetor linear. Um único laço percorre até `rule_count` entradas e retorna na primeira correspondência.
- O SIMD guarda cinco pares de vetores de máscaras e valores, oito resultados e uma máscara de lanes ativas por grupo. Os offsets do assembly são verificados em `acl_simd_context.h`. O programa replica os cinco campos do pacote uma vez, combina os resultados das comparações com `VAND`, extrai um bit por lane com `VMOVMSK.32` e aplica a máscara de lanes ativas. Uma máscara vazia leva ao próximo grupo, se houver. Quando há correspondência, o bit de menor índice indica a regra vencedora e a VM retorna imediatamente. Com 30 regras, o último grupo tem seis lanes ativas.

O retorno em `R0` usa o bit baixo para a ação (`0` para `PASS` e `1` para `DROP`). Os bits superiores guardam o ID original da regra mais um. O retorno zero representa o `PASS` padrão. Assim, a validação também detecta trocas de prioridade entre regras com a mesma ação.

## Medição e validação

As duas VMs usam o interpretador Legacy com checagem de limites. O host prepara blocos de até 2 milhões de pacotes elegíveis e executa primeiro o escalar e depois o SIMD em cada bloco, com uma chamada por pacote em cada VM. O tempo acumulado vem das leituras de `CLOCK_MONOTONIC_RAW` em torno de cada chamada a `ubpf_exec()`. A leitura do PCAP, a preparação das chaves e da política, a referência nativa e a validação ocorrem fora do intervalo cronometrado. As leituras do relógio e a ordem das execuções podem influenciar a medição.

O relatório inclui pacotes lidos e classificados, chamadas por VM, totais de `PASS` e `DROP`, acertos por regra vencedora, tempo total, ns por pacote classificado e speedup (tempo escalar dividido pelo tempo SIMD). A referência em C percorre a tabela original e compara a ação e o ID com cada retorno das duas VMs. Erros de leitura, opções inválidas, falhas da VM e divergências geram saída diferente de zero.

No trace `sample-100k.dump`, a execução com 30 regras na ordem normal classifica 100.000 pacotes com 92.481 `PASS` e 7.519 `DROP`.
