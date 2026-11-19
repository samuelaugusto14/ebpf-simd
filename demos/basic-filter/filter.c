#include "filter.h"

uint64_t
filter(struct filter_batch* batch)
{
    const uint32_t tcp_protocol = 6;
    const uint32_t https_port = 443;
    uint32_t accepted = 0;

    for (uint32_t i = 0; i < batch->count && i < BASIC_FILTER_BATCH_SIZE; i++) {
        uint32_t allow = batch->protocol[i] == tcp_protocol && batch->dst_port[i] == https_port;
        batch->results[i] = allow;
        accepted += allow;
    }

    return accepted;
}
