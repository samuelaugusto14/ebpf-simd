// SPDX-License-Identifier: Apache-2.0

#ifndef UBPF_DEMOS_CMS_CONTEXT_H
#define UBPF_DEMOS_CMS_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#define CMS_DEPTH 8
#define CMS_WIDTH 1024

typedef struct
{
    uint32_t src_ip;
    uint32_t dst_ip;
    uint32_t src_port;
    uint32_t dst_port;
    uint32_t protocol;
} flow_key_t;

typedef uint32_t cms_counters_t[CMS_DEPTH][CMS_WIDTH];

typedef struct
{
    flow_key_t flow;
    cms_counters_t counters;
    uint32_t seeds[CMS_DEPTH];
} cms_context_t;

/* cms_simd.asm addresses this layout directly from r1. */
_Static_assert(CMS_DEPTH == 8 && CMS_WIDTH == 1024, "SIMD sketch dimensions");
_Static_assert(offsetof(flow_key_t, src_ip) == 0, "source IPv4 offset");
_Static_assert(offsetof(flow_key_t, dst_ip) == 4, "destination IPv4 offset");
_Static_assert(offsetof(flow_key_t, src_port) == 8, "source port offset");
_Static_assert(offsetof(flow_key_t, dst_port) == 12, "destination port offset");
_Static_assert(offsetof(flow_key_t, protocol) == 16, "protocol offset");
_Static_assert(sizeof(flow_key_t) == 20, "flow key size");
_Static_assert(offsetof(cms_context_t, flow) == 0, "flow key offset");
_Static_assert(offsetof(cms_context_t, counters) == 20, "first counter offset");
_Static_assert(offsetof(cms_context_t, seeds) == 32788, "seeds offset");

#endif
