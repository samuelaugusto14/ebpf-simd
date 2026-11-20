// SPDX-License-Identifier: Apache-2.0

#include "ubpf_int.h"
#include <immintrin.h>

void
ubpf_simd_copy_vector(void* dst, const void* src)
{
    __m256i value = _mm256_loadu_si256((const __m256i_u*)src);
    _mm256_storeu_si256((__m256i_u*)dst, value);
}

void
ubpf_simd_add32_imm(void* dst, int32_t imm)
{
    __m256i value = _mm256_loadu_si256((const __m256i_u*)dst);
    __m256i addend = _mm256_set1_epi32(imm);
    _mm256_storeu_si256((__m256i_u*)dst, _mm256_add_epi32(value, addend));
}
