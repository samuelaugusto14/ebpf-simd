// SPDX-License-Identifier: Apache-2.0

#include "acl_context.h"

static inline int
matches(uint32_t value, acl_match_t condition)
{
    return (value & condition.mask) == condition.value;
}

uint32_t
acl_classify(const acl_serial_context_t* context)
{
    for (uint32_t i = 0; i < context->rule_count; ++i) {
        const acl_serial_rule_t* rule = &context->rules[i];
        if (matches(context->key.src_ip, rule->src_ip) &&
            matches(context->key.dst_ip, rule->dst_ip) &&
            matches(context->key.protocol, rule->protocol) &&
            matches(context->key.src_port, rule->src_port) &&
            matches(context->key.dst_port, rule->dst_port)) {
            return rule->result;
        }
    }
    return ACL_DEFAULT_RESULT;
}
