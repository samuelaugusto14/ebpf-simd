#ifndef BASIC_FILTER_H
#define BASIC_FILTER_H

#include <stdint.h>

enum { BASIC_FILTER_BATCH_SIZE = 8 };

struct filter_batch
{
    uint32_t count;
    uint32_t protocol[BASIC_FILTER_BATCH_SIZE];
    uint32_t dst_port[BASIC_FILTER_BATCH_SIZE];
    uint32_t results[BASIC_FILTER_BATCH_SIZE];
};

#endif
