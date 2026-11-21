// SPDX-License-Identifier: Apache-2.0

#include "cms_context.h"

static inline uint32_t
rotate_left(uint32_t value, unsigned int count)
{
    return (value << count) | (value >> (32 - count));
}

static inline uint32_t
mix_word(uint32_t word)
{
    return rotate_left(word * 0xcc9e2d51u, 15) * 0x1b873593u;
}

static inline uint32_t
mix_block(uint32_t hash, uint32_t word)
{
    return rotate_left(hash ^ word, 13) * 5u + 0xe6546b64u;
}

static inline uint32_t
finalize_hash(uint32_t hash)
{
    hash ^= sizeof(flow_key_t);
    hash ^= hash >> 16;
    hash *= 0x85ebca6bu;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35u;
    return hash ^ (hash >> 16);
}

int
cms_update(cms_context_t* context)
{
    /* MurmurHash3 x86-32 of five little-endian words. These five mixes
     * depend only on the key and are shared by all eight rows. */
    const uint32_t src_ip = mix_word(context->flow.src_ip);
    const uint32_t dst_ip = mix_word(context->flow.dst_ip);
    const uint32_t src_port = mix_word(context->flow.src_port);
    const uint32_t dst_port = mix_word(context->flow.dst_port);
    const uint32_t protocol = mix_word(context->flow.protocol);

    for (unsigned int row = 0; row < CMS_DEPTH; row++) {
        uint32_t hash = mix_block(context->seeds[row], src_ip);
        hash = mix_block(hash, dst_ip);
        hash = mix_block(hash, src_port);
        hash = mix_block(hash, dst_port);
        hash = mix_block(hash, protocol);
        const uint32_t column = finalize_hash(hash) & (CMS_WIDTH - 1u);
        context->counters[row][column]++;
    }

    return 0;
}
