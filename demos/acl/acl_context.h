// SPDX-License-Identifier: Apache-2.0

#ifndef UBPF_DEMOS_ACL_CONTEXT_H
#define UBPF_DEMOS_ACL_CONTEXT_H

#include <stdint.h>

#define ACL_MAX_RULES 30u
#define ACL_DEFAULT_RESULT 0u

typedef struct
{
    uint32_t src_ip;
    uint32_t dst_ip;
    uint32_t protocol;
    uint32_t src_port;
    uint32_t dst_port;
} acl_key_t;

typedef struct
{
    uint32_t mask;
    uint32_t value;
} acl_match_t;

typedef struct
{
    acl_match_t src_ip;
    acl_match_t dst_ip;
    acl_match_t protocol;
    acl_match_t src_port;
    acl_match_t dst_port;
    uint32_t result;
} acl_serial_rule_t;

typedef struct
{
    acl_key_t key;
    uint32_t rule_count;
    acl_serial_rule_t rules[ACL_MAX_RULES];
} acl_serial_context_t;

#endif
