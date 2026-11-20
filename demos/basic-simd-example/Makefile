PYTHON ?= python3
UBPF_ROOT := ../..
ASSEMBLER := $(UBPF_ROOT)/bin/ubpf-assembler
BYTECODE := ../build/basic-simd-example/basic-simd-example.bin

.PHONY: all
.DELETE_ON_ERROR:
all: $(BYTECODE)

$(BYTECODE): basic-simd-example.asm $(ASSEMBLER) $(UBPF_ROOT)/ubpf/assembler.py $(UBPF_ROOT)/ubpf/asm_parser.py
	mkdir -p "$(@D)"
	"$(PYTHON)" "$(ASSEMBLER)" "$<" "$@"
