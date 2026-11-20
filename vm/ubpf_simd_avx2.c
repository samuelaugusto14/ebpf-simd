// SPDX-License-Identifier: Apache-2.0

#include "ubpf_int.h"
#include <immintrin.h>
#include <string.h>

static __m256i
load_vector(const void* src)
{
    return _mm256_loadu_si256((const __m256i_u*)src);
}

static void
store_vector(void* dst, __m256i value)
{
    _mm256_storeu_si256((__m256i_u*)dst, value);
}

/* The intrinsics take signed scalars. Preserve the input bit pattern. */
static int32_t
signed_bits32(uint32_t value)
{
    int32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int64_t
signed_bits64(uint64_t value)
{
    int64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static __m256i
multiply_lo64(__m256i left, __m256i right)
{
    // VPMULUDQ multiplies the low dword of each qword. Move the high dwords
    // down for the cross products. The high-by-high term lies above bit 63.
    __m256i left_high = _mm256_srli_epi64(left, 32);
    __m256i right_high = _mm256_srli_epi64(right, 32);
    __m256i low_product = _mm256_mul_epu32(left, right);
    __m256i cross_products = _mm256_add_epi64(_mm256_mul_epu32(left, right_high), _mm256_mul_epu32(left_high, right));
    return _mm256_add_epi64(low_product, _mm256_slli_epi64(cross_products, 32));
}

void
ubpf_simd_copy_vector(void* dst, const void* src)
{
    store_vector(dst, load_vector(src));
}

void
ubpf_simd_add32_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_add_epi32(load_vector(dst), _mm256_set1_epi32(imm)));
}

void
ubpf_simd_add32_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_add_epi32(load_vector(dst), load_vector(src)));
}

void
ubpf_simd_add64_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_add_epi64(load_vector(dst), _mm256_set1_epi64x((int64_t)imm)));
}

void
ubpf_simd_add64_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_add_epi64(load_vector(dst), load_vector(src)));
}

void
ubpf_simd_sub32_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_sub_epi32(load_vector(dst), _mm256_set1_epi32(imm)));
}

void
ubpf_simd_sub32_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_sub_epi32(load_vector(dst), load_vector(src)));
}

void
ubpf_simd_sub64_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_sub_epi64(load_vector(dst), _mm256_set1_epi64x((int64_t)imm)));
}

void
ubpf_simd_sub64_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_sub_epi64(load_vector(dst), load_vector(src)));
}

void
ubpf_simd_mul32_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_mullo_epi32(load_vector(dst), _mm256_set1_epi32(imm)));
}

void
ubpf_simd_mul32_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_mullo_epi32(load_vector(dst), load_vector(src)));
}

void
ubpf_simd_mul64_imm(void* dst, int32_t imm)
{
    store_vector(dst, multiply_lo64(load_vector(dst), _mm256_set1_epi64x((int64_t)imm)));
}

void
ubpf_simd_mul64_reg(void* dst, const void* src)
{
    store_vector(dst, multiply_lo64(load_vector(dst), load_vector(src)));
}

void
ubpf_simd_broadcast32(void* dst, uint32_t value)
{
    store_vector(dst, _mm256_set1_epi32(signed_bits32(value)));
}

void
ubpf_simd_broadcast64(void* dst, uint64_t value)
{
    store_vector(dst, _mm256_set1_epi64x(signed_bits64(value)));
}

uint32_t
ubpf_simd_extract32(const void* src, unsigned int lane)
{
    __m256i vector = load_vector(src);
    switch (lane) {
    case 0:
        return (uint32_t)_mm256_extract_epi32(vector, 0);
    case 1:
        return (uint32_t)_mm256_extract_epi32(vector, 1);
    case 2:
        return (uint32_t)_mm256_extract_epi32(vector, 2);
    case 3:
        return (uint32_t)_mm256_extract_epi32(vector, 3);
    case 4:
        return (uint32_t)_mm256_extract_epi32(vector, 4);
    case 5:
        return (uint32_t)_mm256_extract_epi32(vector, 5);
    case 6:
        return (uint32_t)_mm256_extract_epi32(vector, 6);
    case 7:
        return (uint32_t)_mm256_extract_epi32(vector, 7);
    default:
        return 0; // The bytecode validator rejects this selector.
    }
}

void
ubpf_simd_insert32(void* dst, unsigned int lane, uint32_t value)
{
    __m256i vector = load_vector(dst);
    int32_t bits = signed_bits32(value);
    switch (lane) {
    case 0:
        vector = _mm256_insert_epi32(vector, bits, 0);
        break;
    case 1:
        vector = _mm256_insert_epi32(vector, bits, 1);
        break;
    case 2:
        vector = _mm256_insert_epi32(vector, bits, 2);
        break;
    case 3:
        vector = _mm256_insert_epi32(vector, bits, 3);
        break;
    case 4:
        vector = _mm256_insert_epi32(vector, bits, 4);
        break;
    case 5:
        vector = _mm256_insert_epi32(vector, bits, 5);
        break;
    case 6:
        vector = _mm256_insert_epi32(vector, bits, 6);
        break;
    case 7:
        vector = _mm256_insert_epi32(vector, bits, 7);
        break;
    default:
        return; // The bytecode validator rejects this selector.
    }
    store_vector(dst, vector);
}

uint64_t
ubpf_simd_extract64(const void* src, unsigned int lane)
{
    __m256i vector = load_vector(src);
    switch (lane) {
    case 0:
        return (uint64_t)_mm256_extract_epi64(vector, 0);
    case 1:
        return (uint64_t)_mm256_extract_epi64(vector, 1);
    case 2:
        return (uint64_t)_mm256_extract_epi64(vector, 2);
    case 3:
        return (uint64_t)_mm256_extract_epi64(vector, 3);
    default:
        return 0; // The bytecode validator rejects this selector.
    }
}

void
ubpf_simd_insert64(void* dst, unsigned int lane, uint64_t value)
{
    __m256i vector = load_vector(dst);
    int64_t bits = signed_bits64(value);
    switch (lane) {
    case 0:
        vector = _mm256_insert_epi64(vector, bits, 0);
        break;
    case 1:
        vector = _mm256_insert_epi64(vector, bits, 1);
        break;
    case 2:
        vector = _mm256_insert_epi64(vector, bits, 2);
        break;
    case 3:
        vector = _mm256_insert_epi64(vector, bits, 3);
        break;
    default:
        return; // The bytecode validator rejects this selector.
    }
    store_vector(dst, vector);
}

uint32_t
ubpf_simd_reduce_add32(const void* src)
{
    __m256i vector = load_vector(src);
    __m128i sum = _mm_add_epi32(_mm256_castsi256_si128(vector), _mm256_extracti128_si256(vector, 1));
    sum = _mm_add_epi32(sum, _mm_srli_si128(sum, 8));
    sum = _mm_add_epi32(sum, _mm_srli_si128(sum, 4));
    return (uint32_t)_mm_cvtsi128_si32(sum);
}

uint64_t
ubpf_simd_reduce_add64(const void* src)
{
    __m256i vector = load_vector(src);
    __m128i sum = _mm_add_epi64(_mm256_castsi256_si128(vector), _mm256_extracti128_si256(vector, 1));
    sum = _mm_add_epi64(sum, _mm_srli_si128(sum, 8));
    return (uint64_t)_mm_cvtsi128_si64(sum);
}
