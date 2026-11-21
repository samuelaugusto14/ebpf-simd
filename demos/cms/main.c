// SPDX-License-Identifier: Apache-2.0

#include "cms_context.h"
#include "trace_reader.h"

#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ubpf.h>

#ifndef CMS_BATCH_CAPACITY
#define CMS_BATCH_CAPACITY 2000000u
#endif

_Static_assert(CMS_BATCH_CAPACITY > 0 && CMS_BATCH_CAPACITY <= SIZE_MAX / sizeof(flow_key_t), "flow buffer capacity");

enum
{
    ETHERNET_HEADER_SIZE = 14,
    VLAN_TAG_SIZE = 4,
    ETHERTYPE_IPV4 = 0x0800,
    ETHERTYPE_VLAN = 0x8100,
    ETHERTYPE_QINQ = 0x88a8,
    IPV4_MIN_HEADER_SIZE = 20,
    IPV4_FRAGMENT_MASK = 0x3fff, /* More Fragments and fragment offset. */
    TCP_MIN_HEADER_SIZE = 20,
    UDP_HEADER_SIZE = 8,
    BPF_INSTRUCTION_SIZE = 8,
};

static const uint32_t cms_seeds[CMS_DEPTH] = {
    0x243f6a88u, 0x85a308d3u, 0x13198a2eu, 0x03707344u,
    0xa4093822u, 0x299f31d0u, 0x082efa98u, 0xec4e6c89u,
};

typedef struct
{
    const char* name;
    struct ubpf_vm* vm;
    cms_context_t context;
    uint64_t elapsed_ns;
} cms_vm_t;

typedef struct
{
    cms_vm_t scalar;
    cms_vm_t simd;
    cms_counters_t reference;
    uint64_t packets_read;
    uint64_t valid_packets;
} cms_benchmark_t;

static uint16_t
read_be16(const unsigned char* bytes)
{
    return (uint16_t)((uint16_t)bytes[0] << 8 | bytes[1]);
}

static uint32_t
read_be32(const unsigned char* bytes)
{
    return (uint32_t)read_be16(bytes) << 16 | read_be16(bytes + 2);
}

/* Packet parsing stays in the workload. The trace reader provides raw records. */
static bool
find_ipv4_offset(const unsigned char* data, size_t captured, size_t* offset)
{
    if (captured < ETHERNET_HEADER_SIZE) {
        return false;
    }

    *offset = ETHERNET_HEADER_SIZE;
    uint16_t ether_type = read_be16(data + 12);
    while (ether_type == ETHERTYPE_VLAN || ether_type == ETHERTYPE_QINQ) {
        if (captured - *offset < VLAN_TAG_SIZE) {
            return false;
        }
        ether_type = read_be16(data + *offset + 2);
        *offset += VLAN_TAG_SIZE;
    }
    return ether_type == ETHERTYPE_IPV4;
}

static bool
has_transport_header(uint8_t protocol, const unsigned char* data, size_t captured, size_t ip_payload_size)
{
    if (protocol == IPPROTO_TCP) {
        if (captured < TCP_MIN_HEADER_SIZE || ip_payload_size < TCP_MIN_HEADER_SIZE) {
            return false;
        }
        const size_t header_size = (data[12] >> 4) * 4u;
        return header_size >= TCP_MIN_HEADER_SIZE && header_size <= captured && header_size <= ip_payload_size;
    }
    if (protocol == IPPROTO_UDP && captured >= UDP_HEADER_SIZE && ip_payload_size >= UDP_HEADER_SIZE) {
        const uint16_t udp_size = read_be16(data + 4);
        return udp_size >= UDP_HEADER_SIZE && udp_size <= ip_payload_size;
    }
    return false;
}

static bool
parse_flow_key(const trace_packet_t* packet, flow_key_t* key)
{
    const size_t captured = packet->header->caplen;
    size_t ip_offset;
    if (!find_ipv4_offset(packet->data, captured, &ip_offset) || captured - ip_offset < IPV4_MIN_HEADER_SIZE) {
        return false;
    }

    const unsigned char* ip = packet->data + ip_offset;
    const size_t ip_header_size = (ip[0] & 0x0fu) * 4u;
    const size_t ip_size = read_be16(ip + 2);
    if (ip[0] >> 4 != 4 || ip_header_size < IPV4_MIN_HEADER_SIZE || ip_header_size > captured - ip_offset ||
        ip_size < ip_header_size || ip_offset > packet->header->len || ip_size > packet->header->len - ip_offset ||
        (read_be16(ip + 6) & IPV4_FRAGMENT_MASK) != 0) {
        return false;
    }

    /* The capture may omit payload bytes. It must contain both headers. */
    const unsigned char* transport = ip + ip_header_size;
    const uint8_t protocol = ip[9];
    if (!has_transport_header(protocol, transport, captured - ip_offset - ip_header_size, ip_size - ip_header_size)) {
        return false;
    }

    *key = (flow_key_t){
        .src_ip = read_be32(ip + 12),
        .dst_ip = read_be32(ip + 16),
        .src_port = read_be16(transport),
        .dst_port = read_be16(transport + 2),
        .protocol = protocol,
    };
    return true;
}

/* A batch may contain valid keys even when the reader reaches EOF. */
static int
read_flow_batch(
    trace_reader_t* reader,
    flow_key_t* keys,
    size_t* count,
    uint64_t* packets_read,
    char error[PCAP_ERRBUF_SIZE])
{
    *count = 0;
    while (*count < CMS_BATCH_CAPACITY) {
        trace_packet_t packet;
        const int status = trace_reader_next(reader, &packet, error);
        if (status <= 0) {
            return status;
        }
        ++*packets_read;
        if (parse_flow_key(&packet, &keys[*count])) {
            ++*count;
        }
    }
    return 1;
}

static bool
load_bytecode(struct ubpf_vm* vm, const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return false;
    }

    long size;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0 ||
        size % BPF_INSTRUCTION_SIZE != 0 || size > UBPF_MAX_INSTS * (long)BPF_INSTRUCTION_SIZE ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Invalid bytecode size in %s\n", path);
        fclose(file);
        return false;
    }

    void* bytecode = malloc((size_t)size);
    if (!bytecode) {
        perror("Allocating bytecode");
        fclose(file);
        return false;
    }
    const size_t bytes_read = fread(bytecode, 1, (size_t)size, file);
    fclose(file);
    if (bytes_read != (size_t)size) {
        fprintf(stderr, "Could not read all bytecode from %s\n", path);
        free(bytecode);
        return false;
    }

    char* error = NULL;
    const int status = ubpf_load(vm, bytecode, (uint32_t)size, &error);
    free(bytecode); /* ubpf_load() copies the instructions. */
    if (status != 0) {
        fprintf(stderr, "Could not load %s (%s)\n", path, error ? error : "unknown error");
    }
    free(error);
    return status == 0;
}

static bool
init_vm(cms_vm_t* program, const char* path)
{
    /* Both programs use the default Legacy interpreter with bounds checks. */
    program->vm = ubpf_create();
    if (!program->vm) {
        fprintf(stderr, "Could not create the %s VM\n", program->name);
        return false;
    }
    memcpy(program->context.seeds, cms_seeds, sizeof(cms_seeds));
    return load_bytecode(program->vm, path);
}

static uint64_t
elapsed_ns(struct timespec start, struct timespec end)
{
    const int64_t seconds = (int64_t)end.tv_sec - (int64_t)start.tv_sec;
    return (uint64_t)(seconds * INT64_C(1000000000) + end.tv_nsec - start.tv_nsec);
}

static bool
run_vm_batch(cms_vm_t* program, const flow_key_t* keys, size_t count, uint64_t previous_packets)
{
    for (size_t index = 0; index < count; index++) {
        program->context.flow = keys[index];
        uint64_t result = 0;
        struct timespec start, end;

        /* Only ubpf_exec() runs between the two clock samples. */
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &start) != 0) {
            perror("Reading CLOCK_MONOTONIC_RAW");
            return false;
        }
        const int status = ubpf_exec(program->vm, &program->context, sizeof(program->context), &result);
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &end) != 0) {
            perror("Reading CLOCK_MONOTONIC_RAW");
            return false;
        }
        program->elapsed_ns += elapsed_ns(start, end);

        if (status != 0 || result != 0) {
            fprintf(
                stderr, "%s VM failed on valid packet %" PRIu64 " (status %d, r0 %" PRIu64 ")\n",
                program->name, previous_packets + index + 1, status, result);
            return false;
        }
    }
    return true;
}

/* Independent native implementation of MurmurHash3 x86-32. The numeric
 * fields are the decoded little-endian words of the canonical 20-byte key. */
static uint32_t
reference_hash(const flow_key_t* key, uint32_t seed)
{
    const uint32_t words[] = {key->src_ip, key->dst_ip, key->src_port, key->dst_port, key->protocol};
    uint32_t hash = seed;
    for (size_t index = 0; index < sizeof(words) / sizeof(words[0]); index++) {
        uint32_t block = words[index] * 0xcc9e2d51u;
        block = (block << 15) | (block >> 17);
        hash ^= block * 0x1b873593u;
        hash = (hash << 13) | (hash >> 19);
        hash = hash * 5u + 0xe6546b64u;
    }
    hash ^= sizeof(words);
    hash = (hash ^ (hash >> 16)) * 0x85ebca6bu;
    hash = (hash ^ (hash >> 13)) * 0xc2b2ae35u;
    return hash ^ (hash >> 16);
}

static bool
update_reference(cms_counters_t counters, const flow_key_t* keys, size_t count)
{
    for (size_t index = 0; index < count; index++) {
        for (unsigned int row = 0; row < CMS_DEPTH; row++) {
            const uint32_t column = reference_hash(&keys[index], cms_seeds[row]) % CMS_WIDTH;
            if (counters[row][column] == UINT32_MAX) {
                fprintf(stderr, "CMS counter overflow at row %u, column %" PRIu32 "\n", row, column);
                return false;
            }
            counters[row][column]++;
        }
    }
    return true;
}

static bool
process_trace(trace_reader_t* reader, cms_benchmark_t* benchmark)
{
    flow_key_t* keys = malloc(CMS_BATCH_CAPACITY * sizeof(*keys));
    if (!keys) {
        perror("Allocating flow buffer");
        return false;
    }

    bool success = true;
    int status;
    do {
        char error[PCAP_ERRBUF_SIZE];
        size_t count;
        status = read_flow_batch(reader, keys, &count, &benchmark->packets_read, error);
        if (status < 0) {
            fprintf(stderr, "Trace read failed (%s)\n", error);
            success = false;
            break;
        }

        /* The matrices persist across blocks. Each pass sees the same keys
         * in capture order, and reference work stays outside the VM timings. */
        if (!run_vm_batch(&benchmark->scalar, keys, count, benchmark->valid_packets) ||
            !run_vm_batch(&benchmark->simd, keys, count, benchmark->valid_packets) ||
            !update_reference(benchmark->reference, keys, count)) {
            success = false;
            break;
        }
        benchmark->valid_packets += count;
    } while (status > 0);
    free(keys);
    return success;
}

static bool
compare_counters(const char* label, const cms_counters_t left, const cms_counters_t right)
{
    if (memcmp(left, right, sizeof(cms_counters_t)) == 0) {
        printf("  %-24s PASS\n", label);
        return true;
    }

    printf("  %-24s FAIL\n", label);
    for (unsigned int row = 0; row < CMS_DEPTH; row++) {
        for (unsigned int column = 0; column < CMS_WIDTH; column++) {
            if (left[row][column] != right[row][column]) {
                fprintf(
                    stderr, "%s differs at [%u][%u] (%" PRIu32 " vs %" PRIu32 ")\n",
                    label, row, column, left[row][column], right[row][column]);
                return false;
            }
        }
    }
    return false;
}

static bool
check_row_sums(const char* label, const cms_counters_t counters, uint64_t valid_packets)
{
    bool correct = true;
    for (unsigned int row = 0; row < CMS_DEPTH; row++) {
        uint64_t sum = 0;
        for (unsigned int column = 0; column < CMS_WIDTH; column++) {
            sum += counters[row][column];
        }
        if (sum != valid_packets) {
            fprintf(stderr, "%s row %u sums to %" PRIu64 ", expected %" PRIu64 "\n", label, row, sum, valid_packets);
            correct = false;
        }
    }
    printf("  %-24s %s\n", label, correct ? "PASS" : "FAIL");
    return correct;
}

static bool
report_correctness(const cms_benchmark_t* benchmark)
{
    puts("\nCorrectness:");
    const bool scalar_ref =
        compare_counters("Scalar vs reference:", benchmark->scalar.context.counters, benchmark->reference);
    const bool simd_ref =
        compare_counters("SIMD vs reference:", benchmark->simd.context.counters, benchmark->reference);
    const bool scalar_simd =
        compare_counters("Scalar vs SIMD:", benchmark->scalar.context.counters, benchmark->simd.context.counters);
    const bool scalar_sums =
        check_row_sums("Scalar row sums:", benchmark->scalar.context.counters, benchmark->valid_packets);
    const bool simd_sums =
        check_row_sums("SIMD row sums:", benchmark->simd.context.counters, benchmark->valid_packets);
    const bool reference_sums =
        check_row_sums("Reference row sums:", benchmark->reference, benchmark->valid_packets);
    return scalar_ref && simd_ref && scalar_simd && scalar_sums && simd_sums && reference_sums;
}

static void
report_performance(const cms_benchmark_t* benchmark)
{
    const double scalar_ns = (double)benchmark->scalar.elapsed_ns;
    const double simd_ns = (double)benchmark->simd.elapsed_ns;
    puts("\nPerformance:");
    printf("  Scalar total:            %.3f ms\n", scalar_ns / 1000000.0);
    printf("  SIMD total:              %.3f ms\n", simd_ns / 1000000.0);
    if (benchmark->valid_packets == 0) {
        puts("  Scalar ns/packet:        N/A\n  SIMD ns/packet:          N/A");
    } else {
        printf("  Scalar ns/packet:        %.2f\n", scalar_ns / (double)benchmark->valid_packets);
        printf("  SIMD ns/packet:          %.2f\n", simd_ns / (double)benchmark->valid_packets);
    }
    if (simd_ns == 0) {
        puts("  Speedup:                 N/A");
    } else {
        printf("  Speedup:                 %.3fx\n", scalar_ns / simd_ns);
    }
}

static bool
report_results(const char* trace_path, const cms_benchmark_t* benchmark)
{
    puts("Count-Min Sketch Benchmark\n--------------------------");
    printf("Trace:                 %s\n", trace_path);
    printf("CMS depth:             %d\n", CMS_DEPTH);
    printf("CMS width:             %d\n", CMS_WIDTH);
    printf("Packets read:          %" PRIu64 "\n", benchmark->packets_read);
    printf("Valid IPv4 TCP/UDP:    %" PRIu64 "\n", benchmark->valid_packets);
    printf("VM calls (per VM):     %" PRIu64 "\n", benchmark->valid_packets);

    const bool correct = report_correctness(benchmark);
    report_performance(benchmark);
    return correct;
}

static bool
open_ethernet_trace(trace_reader_t* reader, const char* path)
{
    char error[PCAP_ERRBUF_SIZE];
    if (trace_reader_open(reader, path, error) < 0) {
        fprintf(stderr, "Could not open %s (%s)\n", path, error);
        return false;
    }
    const int datalink = trace_reader_datalink(reader);
    if (datalink != DLT_EN10MB) {
        fprintf(stderr, "Unsupported datalink %d in %s (expected Ethernet)\n", datalink, path);
        return false;
    }
    return true;
}

int
main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /path/to/trace.pcap\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Zero all three sketches before loading the programs. */
    cms_benchmark_t* benchmark = calloc(1, sizeof(*benchmark));
    if (!benchmark) {
        perror("Allocating benchmark state");
        return EXIT_FAILURE;
    }
    benchmark->scalar.name = "Scalar";
    benchmark->simd.name = "SIMD";

    trace_reader_t reader = {0};
    const bool success = open_ethernet_trace(&reader, argv[1]) &&
                         init_vm(&benchmark->scalar, CMS_BYTECODE_DIR "/cms_serial.bin") &&
                         init_vm(&benchmark->simd, CMS_BYTECODE_DIR "/cms_simd.bin") &&
                         process_trace(&reader, benchmark) && report_results(argv[1], benchmark);

    trace_reader_close(&reader);
    if (benchmark->simd.vm) {
        ubpf_destroy(benchmark->simd.vm);
    }
    if (benchmark->scalar.vm) {
        ubpf_destroy(benchmark->scalar.vm);
    }
    free(benchmark);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
