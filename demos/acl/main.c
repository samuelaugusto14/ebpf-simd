// SPDX-License-Identifier: Apache-2.0

#include "acl_simd_context.h"
#include "trace_reader.h"

#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ubpf.h>

#define ACL_BATCH_CAPACITY 2000000u
#define PREFIX_MASK UINT32_C(0xffffff00)
#define ANY UINT32_MAX
#define NO_PORT UINT32_MAX
#define PREFIX(a, b, c) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8))

#define S1 PREFIX(163, 234, 176)
#define S2 PREFIX(208, 108, 253)
#define S3 PREFIX(208, 108, 242)
#define S4 PREFIX(163, 234, 102)
#define D1 PREFIX(208, 108, 253)
#define D2 PREFIX(163, 234, 176)
#define D3 PREFIX(163, 234, 102)
#define D4 PREFIX(199, 221, 50)

enum
{
    PASS,
    DROP,
};

enum
{
    ETHERNET_HEADER_SIZE = 14,
    ETHERTYPE_IPV4 = 0x0800,
    ETHERTYPE_VLAN = 0x8100,
    ETHERTYPE_QINQ = 0x88a8,
    VLAN_TAG_SIZE = 4,
    IPV4_MIN_HEADER_SIZE = 20,
    IPV4_FRAGMENT_MASK = 0x3fff,
    TCP_MIN_HEADER_SIZE = 20,
    UDP_HEADER_SIZE = 8,
    ICMP_HEADER_SIZE = 8,
    BPF_INSTRUCTION_SIZE = 8,
};

typedef struct
{
    uint32_t src_prefix;
    uint32_t dst_prefix;
    uint32_t protocol;
    uint32_t src_port;
    uint32_t dst_port;
    uint32_t action;
} acl_rule_t;

/* Array indices are the rule IDs, even when priorities are inverted. */
static const acl_rule_t rules[] = {
    { S1,  ANY, IPPROTO_TCP,  ANY,  443,  PASS }, /* R00 */
    { S1,  ANY, IPPROTO_TCP,  ANY,  ANY,  DROP },
    { ANY, D1,  IPPROTO_TCP,  ANY,  22,   DROP },
    { ANY, D1,  IPPROTO_TCP,  ANY,  443,  PASS },
    { S2,  ANY, IPPROTO_UDP,  ANY,  53,   DROP },
    { ANY, D2,  IPPROTO_UDP,  ANY,  53,   PASS },
    { S3,  D2,  IPPROTO_UDP,  ANY,  123,  DROP },
    { ANY, D3,  IPPROTO_TCP,  ANY,  80,   DROP },
    { S4,  ANY, IPPROTO_TCP,  ANY,  25,   DROP }, /* R08 */
    { ANY, D4,  IPPROTO_UDP,  ANY,  443,  DROP },
    { S2,  ANY, IPPROTO_TCP,  ANY,  443,  PASS },
    { S2,  ANY, IPPROTO_TCP,  ANY,  80,   DROP },
    { ANY, D1,  IPPROTO_UDP,  ANY,  123,  DROP },
    { S3,  ANY, IPPROTO_TCP,  ANY,  22,   DROP },
    { S4,  ANY, IPPROTO_UDP,  ANY,  53,   PASS },
    { ANY, D2,  IPPROTO_TCP,  ANY,  445,  DROP },
    { S1,  ANY, IPPROTO_UDP,  ANY,  1900, DROP }, /* R16 */
    { S3,  ANY, IPPROTO_TCP,  ANY,  3389, DROP },
    { ANY, D3,  IPPROTO_TCP,  ANY,  8080, DROP },
    { S4,  D4,  IPPROTO_TCP,  ANY,  443,  PASS },
    { ANY, D4,  IPPROTO_TCP,  ANY,  443,  DROP },
    { S2,  ANY, IPPROTO_UDP,  53,   ANY,  PASS },
    { ANY, D1,  IPPROTO_UDP,  53,   ANY,  DROP },
    { S3,  ANY, IPPROTO_TCP,  443,  ANY,  PASS },
    { S4,  ANY, IPPROTO_TCP,  80,   ANY,  DROP }, /* R24 */
    { S1,  D3,  IPPROTO_UDP,  ANY,  500,  DROP },
    { ANY, D2,  IPPROTO_ICMP, ANY,  ANY,  DROP },
    { S2,  ANY, IPPROTO_ICMP, ANY,  ANY,  PASS },
    { ANY, ANY, IPPROTO_TCP,  ANY,  23,   DROP },
    { ANY, ANY, IPPROTO_UDP,  ANY,  1900, DROP }, /* R29 */
};

_Static_assert(sizeof(rules) / sizeof(rules[0]) == ACL_MAX_RULES, "rule count");

typedef struct
{
    unsigned int rule_count;
    bool invert;
} acl_options_t;

typedef struct
{
    acl_key_t key;
    uint32_t expected;
} acl_packet_t;

typedef struct
{
    const char* name;
    struct ubpf_vm* vm;
    void* context;
    size_t context_size;
    uint64_t elapsed_ns;
} acl_program_t;

typedef struct
{
    acl_program_t scalar;
    acl_program_t simd;
    uint64_t packets_read;
    uint64_t packets_classified;
    uint64_t drops;
    uint64_t winners[ACL_MAX_RULES + 1];
} acl_benchmark_t;

static uint32_t
rule_result(unsigned int rule_id, uint32_t action)
{
    return ((rule_id + 1u) << 1) | action;
}

static acl_match_t
encode_match(uint32_t value, uint32_t mask)
{
    const uint32_t field_mask = value == ANY ? 0u : mask;
    return (acl_match_t){ .mask = field_mask, .value = value & field_mask };
}

static void
set_lane(acl_simd_field_t* field, unsigned int lane, acl_match_t match)
{
    field->masks[lane] = match.mask;
    field->values[lane] = match.value;
}

static void
prepare_policy(acl_serial_context_t* scalar, acl_simd_context_t* simd, acl_options_t options)
{
    scalar->rule_count = options.rule_count;
    simd->rule_count = options.rule_count;
    for (unsigned int priority = 0; priority < options.rule_count; ++priority) {
        const unsigned int id = options.invert ? options.rule_count - 1u - priority : priority;
        const acl_rule_t* rule = &rules[id];
        const acl_serial_rule_t encoded = {
            .src_ip = encode_match(rule->src_prefix, PREFIX_MASK),
            .dst_ip = encode_match(rule->dst_prefix, PREFIX_MASK),
            .protocol = encode_match(rule->protocol, UINT32_MAX),
            .src_port = encode_match(rule->src_port, UINT32_MAX),
            .dst_port = encode_match(rule->dst_port, UINT32_MAX),
            .result = rule_result(id, rule->action),
        };
        scalar->rules[priority] = encoded;

        acl_simd_group_t* group = &simd->groups[priority / ACL_LANES];
        const unsigned int lane = priority % ACL_LANES;
        set_lane(&group->fields[ACL_SRC_IP], lane, encoded.src_ip);
        set_lane(&group->fields[ACL_DST_IP], lane, encoded.dst_ip);
        set_lane(&group->fields[ACL_PROTOCOL], lane, encoded.protocol);
        set_lane(&group->fields[ACL_SRC_PORT], lane, encoded.src_port);
        set_lane(&group->fields[ACL_DST_PORT], lane, encoded.dst_port);
        group->results[lane] = encoded.result;
        group->active_lanes |= UINT32_C(1) << lane;
    }
}

/* Evaluate the original policy in C without using the VM's field masks. */
static uint32_t
classify_reference(const acl_key_t* key, acl_options_t options)
{
    for (unsigned int priority = 0; priority < options.rule_count; ++priority) {
        const unsigned int id = options.invert ? options.rule_count - 1u - priority : priority;
        const acl_rule_t* rule = &rules[id];
        if ((rule->src_prefix == ANY || (key->src_ip & PREFIX_MASK) == rule->src_prefix) &&
            (rule->dst_prefix == ANY || (key->dst_ip & PREFIX_MASK) == rule->dst_prefix) &&
            (rule->protocol == ANY || key->protocol == rule->protocol) &&
            (rule->src_port == ANY || key->src_port == rule->src_port) &&
            (rule->dst_port == ANY || key->dst_port == rule->dst_port)) {
            return rule_result(id, rule->action);
        }
    }
    return ACL_DEFAULT_RESULT;
}

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

static bool
find_ipv4_offset(const unsigned char* data, size_t captured, size_t* offset)
{
    if (captured < ETHERNET_HEADER_SIZE) {
        return false;
    }

    *offset = ETHERNET_HEADER_SIZE;
    uint16_t ethertype = read_be16(data + 12);
    while (ethertype == ETHERTYPE_VLAN || ethertype == ETHERTYPE_QINQ) {
        if (captured - *offset < VLAN_TAG_SIZE) {
            return false;
        }
        ethertype = read_be16(data + *offset + 2);
        *offset += VLAN_TAG_SIZE;
    }
    return ethertype == ETHERTYPE_IPV4;
}

static bool
parse_flow_key(const trace_packet_t* packet, acl_key_t* key)
{
    const size_t captured = packet->header->caplen;
    const size_t wire_size = packet->header->len;
    size_t offset;
    if (!find_ipv4_offset(packet->data, captured, &offset) || captured - offset < IPV4_MIN_HEADER_SIZE) {
        return false;
    }

    const unsigned char* ip = packet->data + offset;
    const size_t ip_header = (ip[0] & 0x0fu) * 4u;
    const size_t ip_size = read_be16(ip + 2);
    if (ip[0] >> 4 != 4 || ip_header < IPV4_MIN_HEADER_SIZE || ip_header > captured - offset ||
        ip_header > ip_size || offset > wire_size || ip_size > wire_size - offset ||
        (read_be16(ip + 6) & IPV4_FRAGMENT_MASK) != 0) {
        return false;
    }

    const size_t payload = ip_size - ip_header;
    const size_t captured_payload = captured - offset - ip_header;
    const size_t available = payload < captured_payload ? payload : captured_payload;
    const unsigned char* transport = ip + ip_header;
    const uint8_t protocol = ip[9];
    uint32_t src_port = NO_PORT;
    uint32_t dst_port = NO_PORT;
    switch (protocol) {
    case IPPROTO_TCP: {
        if (available < TCP_MIN_HEADER_SIZE) {
            return false;
        }
        const size_t tcp_header = (transport[12] >> 4) * 4u;
        if (tcp_header < TCP_MIN_HEADER_SIZE || tcp_header > available) {
            return false;
        }
        src_port = read_be16(transport);
        dst_port = read_be16(transport + 2);
        break;
    }
    case IPPROTO_UDP: {
        if (available < UDP_HEADER_SIZE) {
            return false;
        }
        const size_t udp_size = read_be16(transport + 4);
        if (udp_size < UDP_HEADER_SIZE || udp_size > payload) {
            return false;
        }
        src_port = read_be16(transport);
        dst_port = read_be16(transport + 2);
        break;
    }
    case IPPROTO_ICMP:
        if (available < ICMP_HEADER_SIZE) {
            return false;
        }
        break;
    }

    *key = (acl_key_t){
        .src_ip = read_be32(ip + 12),
        .dst_ip = read_be32(ip + 16),
        .protocol = protocol,
        .src_port = src_port,
        .dst_port = dst_port,
    };
    return true;
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
    free(bytecode);
    if (status != 0) {
        fprintf(stderr, "Could not load %s (%s)\n", path, error ? error : "unknown error");
    }
    free(error);
    return status == 0;
}

static bool
init_vm(acl_program_t* program, const char* path)
{
    program->vm = ubpf_create();
    if (!program->vm) {
        fprintf(stderr, "Could not create the %s VM\n", program->name);
        return false;
    }
    return load_bytecode(program->vm, path);
}

static int
read_batch(trace_reader_t* reader, acl_packet_t* packets, size_t* count, uint64_t* packets_read, acl_options_t options)
{
    *count = 0;
    while (*count < ACL_BATCH_CAPACITY) {
        trace_packet_t packet;
        char error[PCAP_ERRBUF_SIZE];
        const int status = trace_reader_next(reader, &packet, error);
        if (status < 0) {
            fprintf(stderr, "Trace read failed (%s)\n", error);
        }
        if (status <= 0) {
            return status;
        }
        ++*packets_read;

        acl_packet_t* entry = &packets[*count];
        if (parse_flow_key(&packet, &entry->key)) {
            entry->expected = classify_reference(&entry->key, options);
            ++*count;
        }
    }
    return 1;
}

static uint64_t
elapsed_ns(struct timespec start, struct timespec end)
{
    const int64_t seconds = (int64_t)end.tv_sec - (int64_t)start.tv_sec;
    return (uint64_t)(seconds * INT64_C(1000000000) + end.tv_nsec - start.tv_nsec);
}

static bool
run_vm_batch(acl_program_t* program, const acl_packet_t* packets, size_t count, uint64_t classified)
{
    /* Both contexts start with the packet key. */
    acl_key_t* key = program->context;
    for (size_t index = 0; index < count; ++index) {
        *key = packets[index].key;
        uint64_t result = 0;
        struct timespec start, end;

        if (clock_gettime(CLOCK_MONOTONIC_RAW, &start) != 0) {
            perror("Reading CLOCK_MONOTONIC_RAW");
            return false;
        }
        const int status = ubpf_exec(program->vm, program->context, program->context_size, &result);
        if (clock_gettime(CLOCK_MONOTONIC_RAW, &end) != 0) {
            perror("Reading CLOCK_MONOTONIC_RAW");
            return false;
        }
        program->elapsed_ns += elapsed_ns(start, end);

        if (status != 0 || result != packets[index].expected) {
            fprintf(stderr, "%s VM failed on packet %" PRIu64 " (status %d, r0 %" PRIu64 ", expected %" PRIu32 ")\n",
                    program->name, classified + index + 1, status, result, packets[index].expected);
            return false;
        }
    }
    return true;
}

static bool
process_trace(trace_reader_t* reader, acl_benchmark_t* benchmark, acl_options_t options)
{
    acl_packet_t* packets = malloc(ACL_BATCH_CAPACITY * sizeof(*packets));
    if (!packets) {
        perror("Allocating packet buffer");
        return false;
    }

    int status;
    bool success = true;
    do {
        size_t count;
        status = read_batch(reader, packets, &count, &benchmark->packets_read, options);
        if (status < 0 ||
            !run_vm_batch(&benchmark->scalar, packets, count, benchmark->packets_classified) ||
            !run_vm_batch(&benchmark->simd, packets, count, benchmark->packets_classified)) {
            success = false;
            break;
        }
        for (size_t index = 0; index < count; ++index) {
            const uint32_t result = packets[index].expected;
            ++benchmark->winners[result >> 1];
            benchmark->drops += result & DROP;
        }
        benchmark->packets_classified += count;
    } while (status > 0);
    free(packets);
    return success;
}

static void
report_results(const char* trace, const acl_benchmark_t* benchmark, acl_options_t options)
{
    printf("IPv4 ACL Benchmark\nTrace:                 %s\n", trace);
    printf("Rules enabled:         %u\nRule order:            %s\n",
           options.rule_count, options.invert ? "inverted" : "normal");
    printf("Packets read:          %" PRIu64 "\nPackets classified:    %" PRIu64 "\nVM calls (per VM):     %" PRIu64 "\n",
           benchmark->packets_read, benchmark->packets_classified, benchmark->packets_classified);
    printf("PASS:                  %" PRIu64 "\nDROP:                  %" PRIu64 "\n",
           benchmark->packets_classified - benchmark->drops, benchmark->drops);
    puts("Winning rules:");
    for (unsigned int id = 0; id < options.rule_count; ++id) {
        printf("  R%02u %-4s %" PRIu64 "\n", id, rules[id].action == DROP ? "DROP" : "PASS", benchmark->winners[id + 1]);
    }
    printf("  Default PASS %" PRIu64 "\n", benchmark->winners[0]);
    puts("Correctness:           PASS (scalar and SIMD matched the native reference per packet)");

    const double scalar_ns = (double)benchmark->scalar.elapsed_ns;
    const double simd_ns = (double)benchmark->simd.elapsed_ns;
    printf("Scalar total:          %.3f ms\nSIMD total:            %.3f ms\n",
           scalar_ns / 1000000.0, simd_ns / 1000000.0);
    if (benchmark->packets_classified > 0) {
        printf("Scalar ns/packet:      %.2f\nSIMD ns/packet:        %.2f\n",
               scalar_ns / benchmark->packets_classified, simd_ns / benchmark->packets_classified);
    } else {
        puts("Scalar ns/packet:      N/A\nSIMD ns/packet:        N/A");
    }
    if (simd_ns > 0) {
        printf("Speedup:               %.3fx\n", scalar_ns / simd_ns);
    } else {
        puts("Speedup:               N/A");
    }
}

static bool
parse_options(int argc, char* argv[], acl_options_t* options)
{
    if (argc < 2 || argc > 4) {
        return false;
    }
    options->rule_count = ACL_MAX_RULES;
    bool have_count = false;
    for (int arg = 2; arg < argc; ++arg) {
        if (!options->invert && strcmp(argv[arg], "--invert") == 0) {
            options->invert = true;
        } else if (!have_count && argv[arg][0] != '\0' && argv[arg][strspn(argv[arg], "0123456789")] == '\0') {
            const unsigned long count = strtoul(argv[arg], NULL, 10);
            if (count == 0 || count > ACL_MAX_RULES) {
                return false;
            }
            options->rule_count = (unsigned int)count;
            have_count = true;
        } else {
            return false;
        }
    }
    return true;
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
    acl_options_t options = {0};
    if (!parse_options(argc, argv, &options)) {
        fprintf(stderr, "Usage: %s <pcap> [num_rules (1-%u)] [--invert]\n", argv[0], ACL_MAX_RULES);
        return EXIT_FAILURE;
    }

    acl_serial_context_t scalar_context = {0};
    acl_simd_context_t simd_context = {0};
    prepare_policy(&scalar_context, &simd_context, options);
    acl_benchmark_t benchmark = {
        .scalar = { .name = "Scalar", .context = &scalar_context, .context_size = sizeof(scalar_context) },
        .simd = { .name = "SIMD", .context = &simd_context, .context_size = sizeof(simd_context) },
    };
    trace_reader_t reader = {0};
    const bool success = open_ethernet_trace(&reader, argv[1]) &&
                         init_vm(&benchmark.scalar, ACL_BYTECODE_DIR "/acl_serial.bin") &&
                         init_vm(&benchmark.simd, ACL_BYTECODE_DIR "/acl_simd.bin") &&
                         process_trace(&reader, &benchmark, options);
    if (success) {
        report_results(argv[1], &benchmark, options);
    }

    trace_reader_close(&reader);
    if (benchmark.simd.vm) {
        ubpf_destroy(benchmark.simd.vm);
    }
    if (benchmark.scalar.vm) {
        ubpf_destroy(benchmark.scalar.vm);
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
