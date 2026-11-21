// SPDX-License-Identifier: Apache-2.0

#ifndef UBPF_DEMOS_ACL_SIMD_CONTEXT_H
#define UBPF_DEMOS_ACL_SIMD_CONTEXT_H

#include "acl_context.h"

#include <stddef.h>

#define ACL_LANES 8u
#define ACL_GROUPS ((ACL_MAX_RULES + ACL_LANES - 1u) / ACL_LANES)

enum
{
    ACL_SRC_IP,
    ACL_DST_IP,
    ACL_PROTOCOL,
    ACL_SRC_PORT,
    ACL_DST_PORT,
    ACL_FIELDS,
};

typedef struct
{
    uint32_t masks[ACL_LANES];
    uint32_t values[ACL_LANES];
} acl_simd_field_t;

typedef struct
{
    acl_simd_field_t fields[ACL_FIELDS];
    uint32_t results[ACL_LANES];
    uint32_t active_lanes;
} acl_simd_group_t;

typedef struct
{
    acl_key_t key;
    uint32_t rule_count;
    acl_simd_group_t groups[ACL_GROUPS];
} acl_simd_context_t;

/* acl_simd.asm uses these sizes and offsets directly. */
_Static_assert(ACL_LANES == 8, "group width");
_Static_assert(offsetof(acl_key_t, src_ip) == 0, "source IPv4 offset");
_Static_assert(offsetof(acl_key_t, dst_ip) == 4, "destination IPv4 offset");
_Static_assert(offsetof(acl_key_t, protocol) == 8, "protocol offset");
_Static_assert(offsetof(acl_key_t, src_port) == 12, "source port offset");
_Static_assert(offsetof(acl_key_t, dst_port) == 16, "destination port offset");
_Static_assert(offsetof(acl_simd_context_t, key) == 0, "key offset");
_Static_assert(offsetof(acl_simd_context_t, rule_count) == 20, "rule count offset");
_Static_assert(offsetof(acl_simd_context_t, groups) == 24, "first group offset");
_Static_assert(offsetof(acl_simd_field_t, masks) == 0, "field masks offset");
_Static_assert(offsetof(acl_simd_field_t, values) == 32, "field values offset");
_Static_assert(sizeof(acl_simd_field_t) == 64, "field stride");
_Static_assert(offsetof(acl_simd_group_t, fields) == 0, "first field offset");
_Static_assert(ACL_SRC_IP == 0 && ACL_DST_IP == 1 && ACL_PROTOCOL == 2 && ACL_SRC_PORT == 3 && ACL_DST_PORT == 4,
               "field order");
_Static_assert(offsetof(acl_simd_group_t, results) == 320, "rule results offset");
_Static_assert(offsetof(acl_simd_group_t, active_lanes) == 352, "active lanes offset");
_Static_assert(sizeof(acl_simd_group_t) == 356, "group stride");

#endif
