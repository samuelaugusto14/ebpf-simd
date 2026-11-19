#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <ubpf.h>

#include "filter.h"

static int
load_filter(struct ubpf_vm* vm, const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return -1;
    }

    long size;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Cannot determine the size of %s or the file is empty\n", path);
        fclose(file);
        return -1;
    }

    size_t elf_size = (size_t)size;
    void* elf = malloc(elf_size);
    if (!elf) {
        fprintf(stderr, "Cannot allocate a buffer for %s\n", path);
        fclose(file);
        return -1;
    }

    size_t bytes_read = fread(elf, 1, elf_size, file);
    fclose(file);
    if (bytes_read != elf_size) {
        fprintf(stderr, "Cannot read all of %s\n", path);
        free(elf);
        return -1;
    }

    char* error = NULL;
    int status = ubpf_load_elf(vm, elf, elf_size, &error);
    free(elf);
    if (status != 0) {
        fprintf(stderr, "Cannot load %s: %s\n", path, error ? error : "unknown uBPF error");
    }
    free(error);
    return status;
}

int
main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filter.o>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const uint32_t expected[] = {1, 0, 0, 1};
    const uint64_t expected_accepted = 2;
    struct filter_batch batch = {
        .count = sizeof(expected) / sizeof(expected[0]),
        .protocol = {6, 17, 6, 6},
        .dst_port = {443, 443, 80, 443},
        .results = {2, 2, 2, 2},
    };

    struct ubpf_vm* vm = ubpf_create();
    if (!vm) {
        fprintf(stderr, "Cannot create the uBPF VM\n");
        return EXIT_FAILURE;
    }
    if (load_filter(vm, argv[1]) != 0) {
        ubpf_destroy(vm);
        return EXIT_FAILURE;
    }

    uint64_t accepted;
    int status = ubpf_exec(vm, &batch, sizeof(batch), &accepted);
    ubpf_destroy(vm);
    if (status != 0) {
        fprintf(stderr, "Cannot execute the filter in the uBPF VM\n");
        return EXIT_FAILURE;
    }

    int exit_status = EXIT_SUCCESS;
    for (uint32_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        printf(
            "Packet %" PRIu32 ": protocol=%" PRIu32 " dst_port=%" PRIu32 " result=%" PRIu32 "\n",
            i + 1,
            batch.protocol[i],
            batch.dst_port[i],
            batch.results[i]);
        if (batch.results[i] != expected[i]) {
            fprintf(stderr, "Packet %" PRIu32 ": expected result=%" PRIu32 "\n", i + 1, expected[i]);
            exit_status = EXIT_FAILURE;
        }
    }

    printf("Accepted: %" PRIu64 " of %" PRIu32 "\n", accepted, batch.count);
    if (accepted != expected_accepted) {
        fprintf(stderr, "Expected %" PRIu64 " accepted packets\n", expected_accepted);
        exit_status = EXIT_FAILURE;
    }
    return exit_status;
}
