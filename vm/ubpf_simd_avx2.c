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

void
ubpf_simd_and_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_and_si256(load_vector(dst), load_vector(src)));
}

void
ubpf_simd_and32_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_and_si256(load_vector(dst), _mm256_set1_epi32(imm)));
}

void
ubpf_simd_and64_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_and_si256(load_vector(dst), _mm256_set1_epi64x((int64_t)imm)));
}

void
ubpf_simd_or_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_or_si256(load_vector(dst), load_vector(src)));
}

void
ubpf_simd_or32_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_or_si256(load_vector(dst), _mm256_set1_epi32(imm)));
}

void
ubpf_simd_or64_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_or_si256(load_vector(dst), _mm256_set1_epi64x((int64_t)imm)));
}

void
ubpf_simd_xor_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_xor_si256(load_vector(dst), load_vector(src)));
}

void
ubpf_simd_xor32_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_xor_si256(load_vector(dst), _mm256_set1_epi32(imm)));
}

void
ubpf_simd_xor64_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_xor_si256(load_vector(dst), _mm256_set1_epi64x((int64_t)imm)));
}

void
ubpf_simd_not(void* dst)
{
    store_vector(dst, _mm256_xor_si256(load_vector(dst), _mm256_set1_epi32(-1)));
}

enum
{
    SHIFT32_MASK = 31,
    SHIFT64_MASK = 63,
};

/* The SIMD ISA reduces each count modulo the lane width before shifting. */
static __m128i
shift_count(int32_t imm, uint32_t mask)
{
    return _mm_cvtsi32_si128((uint32_t)imm & mask);
}

static __m256i
shift_counts32(const void* src)
{
    return _mm256_and_si256(load_vector(src), _mm256_set1_epi32(SHIFT32_MASK));
}

static __m256i
shift_counts64(const void* src)
{
    return _mm256_and_si256(load_vector(src), _mm256_set1_epi64x(SHIFT64_MASK));
}

void
ubpf_simd_lsh32_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_sll_epi32(load_vector(dst), shift_count(imm, SHIFT32_MASK)));
}

void
ubpf_simd_lsh32_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_sllv_epi32(load_vector(dst), shift_counts32(src)));
}

void
ubpf_simd_lsh64_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_sll_epi64(load_vector(dst), shift_count(imm, SHIFT64_MASK)));
}

void
ubpf_simd_lsh64_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_sllv_epi64(load_vector(dst), shift_counts64(src)));
}

void
ubpf_simd_rsh32_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_srl_epi32(load_vector(dst), shift_count(imm, SHIFT32_MASK)));
}

void
ubpf_simd_rsh32_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_srlv_epi32(load_vector(dst), shift_counts32(src)));
}

void
ubpf_simd_rsh64_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_srl_epi64(load_vector(dst), shift_count(imm, SHIFT64_MASK)));
}

void
ubpf_simd_rsh64_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_srlv_epi64(load_vector(dst), shift_counts64(src)));
}

void
ubpf_simd_arsh32_imm(void* dst, int32_t imm)
{
    store_vector(dst, _mm256_sra_epi32(load_vector(dst), shift_count(imm, SHIFT32_MASK)));
}

void
ubpf_simd_arsh32_reg(void* dst, const void* src)
{
    store_vector(dst, _mm256_srav_epi32(load_vector(dst), shift_counts32(src)));
}

void
ubpf_simd_arsh64_imm(void* dst, int32_t imm)
{
    __m256i value = load_vector(dst);
    __m256i sign = _mm256_cmpgt_epi64(_mm256_setzero_si256(), value);
    // Complement negative lanes before the logical shift and again afterward.
    __m256i shifted = _mm256_srl_epi64(_mm256_xor_si256(value, sign), shift_count(imm, SHIFT64_MASK));
    store_vector(dst, _mm256_xor_si256(shifted, sign));
}

void
ubpf_simd_arsh64_reg(void* dst, const void* src)
{
    __m256i value = load_vector(dst);
    __m256i sign = _mm256_cmpgt_epi64(_mm256_setzero_si256(), value);
    __m256i shifted = _mm256_srlv_epi64(_mm256_xor_si256(value, sign), shift_counts64(src));
    store_vector(dst, _mm256_xor_si256(shifted, sign));
}

void
ubpf_simd_to_be32(void* dst)
{
    // VPSHUFB selects bytes independently within each 128-bit half.
    __m256i order = _mm256_setr_epi8(
        3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12,
        3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
    store_vector(dst, _mm256_shuffle_epi8(load_vector(dst), order));
}

void
ubpf_simd_to_be64(void* dst)
{
    __m256i order = _mm256_setr_epi8(
        7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8,
        7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8);
    store_vector(dst, _mm256_shuffle_epi8(load_vector(dst), order));
}

/* The x86-64 AVX2 backend already stores lanes in little-endian order. */
void
ubpf_simd_to_le32(void* dst)
{
    UNUSED_PARAMETER(dst);
}

void
ubpf_simd_to_le64(void* dst)
{
    UNUSED_PARAMETER(dst);
}

/* Flipping the sign bit maps unsigned order to signed order. */
static __m256i
compare_gt32(__m256i lhs, __m256i rhs, bool is_signed)
{
    if (!is_signed) {
        __m256i high_bit = _mm256_set1_epi32(INT32_MIN);
        lhs = _mm256_xor_si256(lhs, high_bit);
        rhs = _mm256_xor_si256(rhs, high_bit);
    }
    return _mm256_cmpgt_epi32(lhs, rhs);
}

static __m256i
compare_gt64(__m256i lhs, __m256i rhs, bool is_signed)
{
    if (!is_signed) {
        __m256i high_bit = _mm256_set1_epi64x(INT64_MIN);
        lhs = _mm256_xor_si256(lhs, high_bit);
        rhs = _mm256_xor_si256(rhs, high_bit);
    }
    return _mm256_cmpgt_epi64(lhs, rhs);
}

void
ubpf_simd_cmp_eq32_imm(void* mask, const void* lhs, int32_t imm)
{
    store_vector(mask, _mm256_cmpeq_epi32(load_vector(lhs), _mm256_set1_epi32(imm)));
}

void
ubpf_simd_cmp_eq32_reg(void* mask, const void* lhs, const void* rhs)
{
    store_vector(mask, _mm256_cmpeq_epi32(load_vector(lhs), load_vector(rhs)));
}

void
ubpf_simd_cmp_eq64_imm(void* mask, const void* lhs, int32_t imm)
{
    store_vector(mask, _mm256_cmpeq_epi64(load_vector(lhs), _mm256_set1_epi64x((int64_t)imm)));
}

void
ubpf_simd_cmp_eq64_reg(void* mask, const void* lhs, const void* rhs)
{
    store_vector(mask, _mm256_cmpeq_epi64(load_vector(lhs), load_vector(rhs)));
}

void
ubpf_simd_cmp_gt32_imm(void* mask, const void* lhs, int32_t imm, bool is_signed)
{
    store_vector(mask, compare_gt32(load_vector(lhs), _mm256_set1_epi32(imm), is_signed));
}

void
ubpf_simd_cmp_gt32_reg(void* mask, const void* lhs, const void* rhs, bool is_signed)
{
    store_vector(mask, compare_gt32(load_vector(lhs), load_vector(rhs), is_signed));
}

void
ubpf_simd_cmp_gt64_imm(void* mask, const void* lhs, int32_t imm, bool is_signed)
{
    store_vector(mask, compare_gt64(load_vector(lhs), _mm256_set1_epi64x((int64_t)imm), is_signed));
}

void
ubpf_simd_cmp_gt64_reg(void* mask, const void* lhs, const void* rhs, bool is_signed)
{
    store_vector(mask, compare_gt64(load_vector(lhs), load_vector(rhs), is_signed));
}

static __m256i
mask_bytes32(__m256i mask)
{
    // VPBLENDVB selects by byte. Expand each lane's sign bit to all of its bytes.
    return _mm256_srai_epi32(mask, SHIFT32_MASK);
}

static __m256i
mask_bytes64(__m256i mask)
{
    return _mm256_cmpgt_epi64(_mm256_setzero_si256(), mask);
}

void
ubpf_simd_move_masked32(void* dst, const void* src, const void* mask)
{
    store_vector(dst, _mm256_blendv_epi8(load_vector(dst), load_vector(src), mask_bytes32(load_vector(mask))));
}

void
ubpf_simd_move_masked64(void* dst, const void* src, const void* mask)
{
    store_vector(dst, _mm256_blendv_epi8(load_vector(dst), load_vector(src), mask_bytes64(load_vector(mask))));
}

uint32_t
ubpf_simd_mask_bits32(const void* mask)
{
    return (uint32_t)_mm256_movemask_ps(_mm256_castsi256_ps(load_vector(mask)));
}

uint32_t
ubpf_simd_mask_bits64(const void* mask)
{
    return (uint32_t)_mm256_movemask_pd(_mm256_castsi256_pd(load_vector(mask)));
}

static bool
can_use_masked_intrinsics(void* const* addresses, uint32_t active, size_t lane_size)
{
    // Active lane zero supplies a validated base aligned for the intrinsic's pointer type.
    return (active & 1) && (uintptr_t)addresses[0] % lane_size == 0;
}

static void
load_active_lanes(void* dst, void* const* addresses, uint32_t active, size_t lane_size)
{
    for (size_t lane = 0; lane < EBPF_SIMD_VECTOR_BYTES / lane_size; lane++) {
        if (active & (UINT32_C(1) << lane)) {
            memcpy((uint8_t*)dst + lane * lane_size, addresses[lane], lane_size);
        }
    }
}

static void
store_active_lanes(void* const* addresses, const void* src, uint32_t active, size_t lane_size)
{
    for (size_t lane = 0; lane < EBPF_SIMD_VECTOR_BYTES / lane_size; lane++) {
        if (active & (UINT32_C(1) << lane)) {
            memcpy(addresses[lane], (const uint8_t*)src + lane * lane_size, lane_size);
        }
    }
}

void
ubpf_simd_load_masked_contiguous(
    void* dst, void* const* addresses, const void* mask, uint32_t active, size_t lane_size)
{
    if (can_use_masked_intrinsics(addresses, active, lane_size)) {
        __m256i raw_mask = load_vector(mask);
        bool lanes32 = lane_size == sizeof(uint32_t);
        __m256i byte_mask = lanes32 ? mask_bytes32(raw_mask) : mask_bytes64(raw_mask);
        __m256i value = lanes32 ? _mm256_maskload_epi32((const int*)addresses[0], byte_mask)
                                : _mm256_maskload_epi64((const long long*)addresses[0], byte_mask);
        store_vector(dst, _mm256_blendv_epi8(load_vector(dst), value, byte_mask));
        return;
    }

    load_active_lanes(dst, addresses, active, lane_size);
}

void
ubpf_simd_store_masked_contiguous(
    void* const* addresses, const void* src, const void* mask, uint32_t active, size_t lane_size)
{
    if (can_use_masked_intrinsics(addresses, active, lane_size)) {
        __m256i raw_mask = load_vector(mask);
        __m256i value = load_vector(src);
        if (lane_size == sizeof(uint32_t)) {
            _mm256_maskstore_epi32((int*)addresses[0], raw_mask, value);
        } else {
            _mm256_maskstore_epi64((long long*)addresses[0], raw_mask, value);
        }
        return;
    }

    store_active_lanes(addresses, src, active, lane_size);
}

void
ubpf_simd_gather(
    void* dst, void* const* addresses, const void* base, const void* indices, const void* mask, uint32_t active,
    size_t lane_size)
{
    if (base && (uintptr_t)base % lane_size == 0) {
        __m256i index_vector = load_vector(indices);
        bool lanes32 = lane_size == sizeof(uint32_t);
        uint32_t high_bits = lanes32 ? (uint32_t)_mm256_movemask_ps(_mm256_castsi256_ps(index_vector))
                                     : (uint32_t)_mm256_movemask_pd(_mm256_castsi256_pd(index_vector));
        // AVX2 interprets indices as signed. Check the sign bit of active indices only.
        if ((active & high_bits) == 0) {
            __m256i previous = load_vector(dst);
            __m256i raw_mask = load_vector(mask);
            __m256i value =
                lanes32 ? _mm256_mask_i32gather_epi32(previous, (const int*)base, index_vector, raw_mask, 4)
                        : _mm256_mask_i64gather_epi64(previous, (const long long*)base, index_vector, raw_mask, 8);
            store_vector(dst, value);
            return;
        }
    }

    load_active_lanes(dst, addresses, active, lane_size);
}

void
ubpf_simd_scatter(void* const* addresses, const void* src, uint32_t active, size_t lane_size)
{
    uint8_t values[EBPF_SIMD_VECTOR_BYTES];
    store_vector(values, load_vector(src));
    store_active_lanes(addresses, values, active, lane_size);
}
