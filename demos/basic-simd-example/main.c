#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <ubpf.h>

enum { LANE_COUNT = 8 };

// O ASM usa r1 como endereço desta estrutura. Seus offsets são em bytes.
struct simd_context
{
    int32_t input[LANE_COUNT];
    int32_t output[LANE_COUNT];
    uint32_t mask;
    uint32_t sum;
};

// Se o layout em C mudar, o compilador acusa a divergência com o ASM.
_Static_assert(offsetof(struct simd_context, input) == 0, "offset da entrada");
_Static_assert(offsetof(struct simd_context, output) == 32, "offset da saída");
_Static_assert(offsetof(struct simd_context, mask) == 64, "offset da máscara");
_Static_assert(offsetof(struct simd_context, sum) == 68, "offset da soma");
_Static_assert(sizeof(struct simd_context) == 72, "tamanho do contexto");

static int
load_program(struct ubpf_vm* vm, const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return -1;
    }

    // Cada instrução SIMD deste exemplo ocupa um slot de oito bytes.
    long size;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0 || size % 8 != 0 ||
        size > UBPF_MAX_INSTS * 8L || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Tamanho de bytecode inválido em %s\n", path);
        fclose(file);
        return -1;
    }

    void* bytecode = malloc((size_t)size);
    if (!bytecode) {
        fprintf(stderr, "Falha ao alocar memória para o bytecode\n");
        fclose(file);
        return -1;
    }

    size_t bytes_read = fread(bytecode, 1, (size_t)size, file);
    fclose(file);
    if (bytes_read != (size_t)size) {
        fprintf(stderr, "Falha ao ler todo o bytecode de %s\n", path);
        free(bytecode);
        return -1;
    }

    // ubpf_load valida e copia os bytes do programa. A mensagem de erro, se
    // houver, é alocada pela biblioteca e deve ser liberada pelo chamador.
    char* error = NULL;
    int status = ubpf_load(vm, bytecode, (uint32_t)size, &error);
    free(bytecode);
    if (status != 0) {
        fprintf(stderr, "Falha ao carregar %s (%s)\n", path, error ? error : "erro desconhecido");
    }
    free(error);
    return status;
}

static void
print_vector(const char* label, const int32_t values[LANE_COUNT])
{
    printf("%s [", label);
    for (size_t i = 0; i < LANE_COUNT; i++) {
        printf("%s%" PRId32, i == 0 ? "" : ", ", values[i]);
    }
    puts("]");
}

static bool
verify_result(const struct simd_context* context)
{
    // Resultados calculados a partir da entrada fixa de main. O bit i da
    // máscara corresponde ao elemento de índice i, começando em zero.
    const int32_t expected_output[LANE_COUNT] = {-7, 49, 60, 90, 0, 61, 48, 110};
    const uint32_t expected_mask = 0xac;
    const uint32_t expected_sum = 411;
    bool correct = true;

    for (size_t i = 0; i < LANE_COUNT; i++) {
        if (context->output[i] != expected_output[i]) {
            fprintf(
                stderr, "Saída[%zu] = %" PRId32 ", esperado %" PRId32 "\n", i, context->output[i], expected_output[i]);
            correct = false;
        }
    }
    if (context->mask != expected_mask) {
        fprintf(stderr, "Máscara = 0x%02" PRIx32 ", esperado 0x%02" PRIx32 "\n", context->mask, expected_mask);
        correct = false;
    }
    if (context->sum != expected_sum) {
        fprintf(stderr, "Soma = %" PRIu32 ", esperado %" PRIu32 "\n", context->sum, expected_sum);
        correct = false;
    }
    return correct;
}

int
main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Execute %s <bytecode.bin>\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct simd_context context = {
        .input = {-7, 49, 50, 80, 0, 51, 48, 100},
        // Um valor inicial distinto da saída esperada revela faltas de escrita.
        .output = {-1, -1, -1, -1, -1, -1, -1, -1},
    };

    // ubpf_create usa o perfil Legacy por padrão. A extensão SIMD deste
    // exemplo é executada pelo interpretador desse perfil com AVX2.
    struct ubpf_vm* vm = ubpf_create();
    if (!vm) {
        fprintf(stderr, "Falha ao criar a VM do uBPF\n");
        return EXIT_FAILURE;
    }
    if (load_program(vm, argv[1]) != 0) {
        ubpf_destroy(vm);
        return EXIT_FAILURE;
    }

    print_vector("Entrada", context.input);

    // A VM coloca &context em r1 e usa sizeof(context) para delimitar a
    // memória acessível por esse endereço. O ASM grava as saídas na estrutura.
    // ubpf_exec informa o status da execução e entrega r0 separadamente.
    uint64_t r0;
    int status = ubpf_exec(vm, &context, sizeof(context), &r0);
    ubpf_destroy(vm);
    if (status != 0) {
        fprintf(stderr, "Falha ao executar o programa na VM\n");
        return EXIT_FAILURE;
    }
    if (r0 != 0) {
        fprintf(stderr, "O programa retornou r0 = %" PRIu64 ", esperado 0\n", r0);
        return EXIT_FAILURE;
    }

    print_vector("Saída", context.output);
    printf("Máscara = %b\n", context.mask);
    printf("Soma = %" PRIu32 "\n", context.sum);

    if (!verify_result(&context)) {
        return EXIT_FAILURE;
    }
    puts("Verificação passou.");
    return EXIT_SUCCESS;
}
