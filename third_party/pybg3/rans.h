// pybg3
//
// Copyright (C) 2024 Mackenzie Straight.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the “Software”), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

// References:
// https://github.com/eiz/libbg3/blob/main/docs/bitknit2.txt
// Jarek Duda, "Asymmetric numeral systems: entropy coding combining speed of Huffman
// coding with compression rate of arithmetic coding", https://arxiv.org/abs/1311.2540
// Fabian Giesen, "Interleaved entropy coders", https://arxiv.org/abs/1402.3392
// https://fgiesen.wordpress.com/2015/12/21/rans-in-practice/
// https://fgiesen.wordpress.com/2023/05/06/a-very-brief-bitknit-retrospective/
// https://fgiesen.wordpress.com/2016/03/07/repeated-match-offsets-in-bitknit/
// https://github.com/rygorous/ryg_rans

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#define LIBBG3_BITKNIT2_MAGIC 0x75B1

#ifdef NDEBUG
#define LIBBG3_FORCEINLINE __attribute__((always_inline))
#else
#define LIBBG3_FORCEINLINE
#endif

namespace rans {
template <int NumBytes>
struct unsigned_t;

template <>
struct unsigned_t<1> {
  using type = uint8_t;
};
template <>
struct unsigned_t<2> {
  using type = uint16_t;
};
template <>
struct unsigned_t<4> {
  using type = uint32_t;
};
template <>
struct unsigned_t<8> {
  using type = uint64_t;
};

// Represents a probability distribution as an array of quantized frequencies.
// The frequencies are stored as a prefix sum, where sums[i] contains the sum of
// the frequencies of symbols < i. sums[VocabSize] always equals 2^FrequencyBits
// and no symbol may have a frequency of 0.
//
// Additionally, reverse lookups from a FrequencyBits-bit code to a symbol are
// supported and may be accelerated by a lookup table. The lookup table is
// indexed by the high bits of the code value, and contains the symbol index at
// which to start a linear search. If the lookup table isn't used (LookupBits ==
// 0), reverse lookup is done by binary search.
template <typename T, size_t VocabSize, size_t FrequencyBits, size_t LookupBits>
struct frequency_table {
  static_assert(FrequencyBits > 0 && FrequencyBits < sizeof(T) * 8);
  static_assert(LookupBits <= FrequencyBits);
  static_assert(VocabSize > 0 && VocabSize < (1 << FrequencyBits));
  static constexpr size_t frequency_bits = FrequencyBits;
  static constexpr size_t vocab_size = VocabSize;
  static constexpr size_t lookup_shift = FrequencyBits - LookupBits;
  // Invariants:
  // - sums[VocabSize] == 2^FrequencyBits
  // - sums[i] < sums[i + 1]
  // - you must call finish_update after changing sums to recalculate lookup.
  std::array<T, VocabSize + 1> sums;
  std::array<T, LookupBits ? 1 << LookupBits : 0> lookup;
  // Given a code, find the symbol whose cumulative frequency range the code falls within.
  // If code is >= 2^FrequencyBits, the behavior is undefined.
  size_t LIBBG3_FORCEINLINE find_symbol(size_t code) const {
    if constexpr (LookupBits == 0) {
      return find_symbol_slow(code);
    } else {
      size_t sym = lookup[code >> lookup_shift];
      while (code >= sums[sym + 1]) {
        sym++;
      }
      return sym;
    }
  }
  // (very!) slow fallback binary search method.
  size_t LIBBG3_FORCEINLINE find_symbol_slow(size_t code) const {
    size_t lo = 0, hi = VocabSize - 1;
    while (lo <= hi) {
      size_t mid = lo + ((hi - lo) / 2);
      T upper_bound = sums[mid + 1] - 1;
      T lower_bound = sums[mid];
      if (code > upper_bound) {
        lo = mid + 1;
      } else if (code < lower_bound) {
        hi = mid - 1;
      } else {
        return mid;
      }
    }
    __builtin_unreachable();
  }
  void finish_update() {
    if constexpr (LookupBits == 0) {
      return;
    }
    size_t code = 0, sym = 0, next = sums[1];
    while (code < (T(1) << FrequencyBits)) {
      if (code < next) {
        lookup[code >> lookup_shift] = sym;
        code += T(1) << lookup_shift;
      } else {
        sym++;
        next = sums[sym + 1];
      }
    }
  }
  T LIBBG3_FORCEINLINE frequency(T symbol) const {
    return sums[symbol + 1] - sums[symbol];
  }
  T LIBBG3_FORCEINLINE sum_below(T symbol) const { return sums[symbol]; }
};

// A symbol probability model that periodically updates its distribution using recently
// seen symbols. The distribution is initialized with (VocabSize - NumMinProbableSymbols)
// symbols of (approximately) equal probability, followed by NumMinProbableSymbols symbols
// with the minimum probability. The distribution is stored in a frequency_table with the
// given parameters. Every AdaptationInterval symbols seen by observe_symbol will trigger
// an update to the distribution.
template <typename T,
          int AdaptationInterval,
          size_t VocabSize,
          size_t NumMinProbableSymbols,
          size_t FrequencyBits,
          size_t LookupBits>
struct deferred_adaptive_model {
  using cdf_t = frequency_table<T, VocabSize, FrequencyBits, LookupBits>;
  static constexpr size_t num_equiprobable_symbols = VocabSize - NumMinProbableSymbols;
  static constexpr size_t num_min_probable_symbols = NumMinProbableSymbols;
  static constexpr size_t adaptation_interval = AdaptationInterval;
  static constexpr T total_sum = (1 << FrequencyBits);
  static constexpr T frequency_incr = (total_sum - VocabSize) / AdaptationInterval;
  static constexpr T last_frequency_incr =
      1 + total_sum - VocabSize - frequency_incr * AdaptationInterval;
  deferred_adaptive_model() {
    static_assert(num_equiprobable_symbols > 0);
    for (size_t i = 0; i < num_equiprobable_symbols; ++i) {
      cdf.sums[i] = (total_sum - num_min_probable_symbols) * i / num_equiprobable_symbols;
    }
    for (size_t i = num_equiprobable_symbols; i <= VocabSize; ++i) {
      cdf.sums[i] = total_sum - VocabSize + i;
    }
    for (size_t i = 0; i < VocabSize; ++i) {
      frequency_accumulator[i] = 1;
    }
    cdf.finish_update();
  }
  bool LIBBG3_FORCEINLINE observe_symbol(T symbol) {
    frequency_accumulator[symbol] += frequency_incr;
    adaptation_counter = (adaptation_counter + 1) % AdaptationInterval;
    if (adaptation_counter == 0) {
      frequency_accumulator[symbol] += last_frequency_incr;
      // Sum in the next size up to avoid issues with unsigned division. -1 / 2 == -1 will
      // not hold if we're averaging uint16_ts.
      typename unsigned_t<sizeof(T) * 2>::type sum = 0;
      for (size_t i = 1; i <= VocabSize; ++i) {
        sum += frequency_accumulator[i - 1];
        // Trickier than it looks:
        // https://fgiesen.wordpress.com/2015/02/20/mixing-discrete-probability-distributions/
        cdf.sums[i] += (sum - cdf.sums[i]) / 2;
        frequency_accumulator[i - 1] = 1;
      }
      cdf.finish_update();
      return true;
    }
    return false;
  }
  cdf_t cdf;
  std::array<T, VocabSize> frequency_accumulator;
  int adaptation_counter{0};
};

// A stack that grows top to bottom. Initialize with (start, start, end) for
// reading and (start, end, end) for writing.
template <typename T>
struct bounded_stack {
  bounded_stack(T* begin, T* cur, T* end) : begin(begin), cur(cur), end(end) {}
  void LIBBG3_FORCEINLINE push(T word) {
    if (cur == begin) {
      throw std::out_of_range("bitstream overflow");
    }
    *--cur = word;
  }
  T LIBBG3_FORCEINLINE pop() {
    if (cur == end) {
      throw std::out_of_range("unexpected end of bitstream");
    }
    return *cur++;
  }
  T *begin, *cur, *end;
};

// Abstractly, an arbitrary precision natural number which is always >= 2^N, where N is
// half the number of bits in Bits. Information can be added and removed from the number
// like a stack using push and pop operations. To keep operations on the top of the
// stack fast, digits at the top of the stack are cached and the rest are offloaded to a
// bounded_stack.
template <typename Bits>
struct rans_state {
  using stream_bits_t = unsigned_t<sizeof(Bits) / 2>::type;
  static constexpr size_t refill_shift = sizeof(Bits) * 4;
  static constexpr Bits refill_threshold = Bits(1) << refill_shift;
  rans_state(Bits bits) : bits(bits) {}
  rans_state() : bits(refill_threshold) {}
  void LIBBG3_FORCEINLINE push_bits(bounded_stack<stream_bits_t>& stream,
                                    Bits sym,
                                    int nbits) {
    Bits mask = ~(~Bits(0) >> nbits);
    if (bits & mask) {
      offload(stream);
    }
    bits = (bits << nbits) | (sym & ((Bits(1) << nbits) - 1));
  }
  Bits LIBBG3_FORCEINLINE pop_bits(bounded_stack<stream_bits_t>& stream, int nbits) {
    assert(nbits < refill_shift);
    Bits sym = bits & ((Bits(1) << nbits) - 1);
    bits >>= nbits;
    maybe_refill(stream);
    return sym;
  }
  template <typename CDF>
  void LIBBG3_FORCEINLINE push_cdf(bounded_stack<stream_bits_t>& stream,
                                   Bits sym,
                                   CDF const& cdf) {
    Bits mask = ~(~Bits(0) >> CDF::frequency_bits);
    Bits freq = cdf.frequency(sym);
    if ((bits / freq) & mask) {
      offload(stream);
    }
    bits = ((bits / freq) << CDF::frequency_bits) + (bits % freq) + cdf.sum_below(sym);
  }
  template <typename CDF>
  Bits LIBBG3_FORCEINLINE pop_cdf(bounded_stack<stream_bits_t>& stream, CDF const& cdf) {
    static_assert(CDF::frequency_bits < refill_shift);
    size_t code = bits & ((Bits(1) << CDF::frequency_bits) - 1);
    size_t sym = cdf.find_symbol(code);
    size_t freq = cdf.frequency(sym);
    bits = (bits >> CDF::frequency_bits) * freq + code - cdf.sum_below(sym);
    maybe_refill(stream);
    return sym;
  }
  void LIBBG3_FORCEINLINE maybe_refill(bounded_stack<stream_bits_t>& stream) {
    if (bits < refill_threshold) {
      bits = (bits << refill_shift) | stream.pop();
    }
  }
  void LIBBG3_FORCEINLINE offload(bounded_stack<stream_bits_t>& stream) {
    stream.push(bits & (refill_threshold - 1));
    bits >>= refill_shift;
  }
  Bits bits;
};

// The idea for this way of managing the offset cache is described here:
// https://fgiesen.wordpress.com/2016/03/07/repeated-match-offsets-in-bitknit/
// tldr the items don't move on a cache hit, the indices just have a swizzle
// table stored in 4bit fields in a register. we bump items by doing bitwise
// rotations on a subset of the bits.
template <typename T>
struct register_lru_cache {
  std::array<T, 8> entries;
  uint32_t entry_order = 0x76543210;
  register_lru_cache() {
    for (size_t i = 0; i < 8; ++i) {
      entries[i] = 1;
    }
  }
  void LIBBG3_FORCEINLINE insert(T value) {
    entries[entry_order >> 28] = entries[(entry_order >> 24) & 15];
    entries[(entry_order >> 24) & 15] = value;
  }
  uint32_t entry(uint32_t index) {
    uint32_t slot = (entry_order >> (index * 4)) & 15;
    return entries[slot];
  }
  uint32_t LIBBG3_FORCEINLINE hit(uint32_t index) {
    uint32_t slot = (entry_order >> (index * 4)) & 15;
    uint32_t rotate_mask = (16 << (index * 4)) - 1;
    uint32_t rotated_order = ((entry_order << 4) | slot) & rotate_mask;
    entry_order = (entry_order & ~rotate_mask) | rotated_order;
    return entries[slot];
  }
};

struct bitknit2_state {
  bitknit2_state(uint8_t* dst, size_t dst_len)
      : dst(dst), dst_end(dst + dst_len), src(0, 0, 0) {}
  bool decode(uint16_t* data, size_t data_len_bytes) {
    src = bounded_stack<uint16_t>(data, data, data + data_len_bytes / 2);
    if (src.cur == src.end || *src.cur != LIBBG3_BITKNIT2_MAGIC) {
      return false;
    }
    src.pop();
    uint8_t* dst_cur = dst;
    while (dst_cur < dst_end) {
      if (src.cur == src.end) {
        return false;
      }
      decode_quantum(dst_cur);
    }
    return true;
  }

 private:
  void LIBBG3_FORCEINLINE decode_quantum(uint8_t*& dst_cur) {
    size_t offset = dst_cur - dst;
    size_t boundary =
        std::min(size_t(dst_end - dst), (offset & ~size_t(0xFFFF)) + 0x10000);
    uint8_t* quantum_end = dst + boundary;
    // A NUL word at the beginning of the quantum == copy raw data.
    if (!*src.cur) {
      src.cur++;
      size_t copy_len =
          std::min(size_t(src.end - src.cur) * 2, size_t(quantum_end - dst_cur));
      memcpy(dst_cur, src.cur, copy_len);
      dst_cur += copy_len;
      src.cur += copy_len / 2;
      return;
    }
    // This sucks, but clang generates much better code if these are all in locals instead
    // of struct members.
    rans_state<uint32_t> state1, state2;
    decode_initial_state(state1, state2);
    if (dst_cur == dst) {
      *dst_cur++ = pop_bits(8, state1, state2);
    }
    uint8_t* cur_tmp = dst_cur;  // clang does slightly better this way
    while (cur_tmp < quantum_end) {
      size_t model_index = size_t(cur_tmp) % 4;
      uint32_t command = pop_model(command_word_models[model_index], state1, state2);
      if (command >= 256) {
        decode_copy(command, state1, state2, cur_tmp);
      } else {
        *cur_tmp = command + *(cur_tmp - delta_offset);
        cur_tmp++;
      }
    }
    dst_cur = cur_tmp;
    if (state1.bits != 0x10000 || state2.bits != 0x10000) {
      throw std::runtime_error("rANS stream corrupted");
    }
  }
  void LIBBG3_FORCEINLINE decode_copy(uint32_t command,
                                      rans_state<uint32_t>& state1,
                                      rans_state<uint32_t>& state2,
                                      uint8_t*& dst_cur) {
    size_t model_index = size_t(dst_cur) % 4;
    uint32_t copy_length;
    if (command < 288) {
      // Min copy length is 2, giving this variant a max copy length of 33.
      copy_length = command - 254;
    } else {
      uint32_t copy_length_length = command - 287;
      uint32_t copy_length_bits = pop_bits(copy_length_length, state1, state2);
      // Min extension length is 1, giving this a min copy length of 34: (1 << 1) + 32
      copy_length = (1 << copy_length_length) + copy_length_bits + 32;
    }
    uint32_t copy_offset;
    uint32_t cache_ref = pop_model(cache_reference_models[model_index], state1, state2);
    if (cache_ref < 8) {
      copy_offset = copy_offset_cache.hit(cache_ref);
    } else {
      uint32_t copy_offset_length = pop_model(copy_offset_model, state1, state2);
      uint32_t copy_offset_bits = pop_bits(copy_offset_length % 16, state1, state2);
      if (copy_offset_length >= 16) {
        copy_offset_bits = (copy_offset_bits << 16) | src.pop();
      }
      // The weirdness is bc 32 << 0 == 32, so to support copy offsets <32, it's reduced
      // by 32. This way a cache_ref of 8 and a copy_offset_length of 0 gives a copy
      // offset of 1.
      copy_offset =
          (32 << copy_offset_length) + (copy_offset_bits << 5) - 32 + (cache_ref - 7);
      copy_offset_cache.insert(copy_offset);
    }
    if (copy_offset > dst_cur - dst) {
      throw std::out_of_range("invalid copy offset");
    }
    if (copy_length > dst_end - dst_cur) {
      throw std::out_of_range("invalid copy length");
    }
    delta_offset = copy_offset;
    for (size_t i = 0; i < copy_length; ++i) {
      *dst_cur = *(dst_cur - copy_offset);
      dst_cur++;
    }
  }
  uint32_t LIBBG3_FORCEINLINE pop_bits(int nbits,
                                       rans_state<uint32_t>& state1,
                                       rans_state<uint32_t>& state2) {
    uint32_t result = state1.pop_bits(src, nbits);
    std::swap(state1, state2);
    return result;
  }
  template <typename Model>
  uint32_t LIBBG3_FORCEINLINE pop_model(Model& model,
                                        rans_state<uint32_t>& state1,
                                        rans_state<uint32_t>& state2) {
    uint32_t result = state1.pop_cdf(src, model.cdf);
    model.observe_symbol(result);
    std::swap(state1, state2);
    return result;
  }
  // I hope sometimes saving those 2 bytes per quantum was worth it.
  // See "tying the knot" https://fgiesen.wordpress.com/2015/12/21/rans-in-practice/
  void LIBBG3_FORCEINLINE decode_initial_state(rans_state<uint32_t>& state1,
                                               rans_state<uint32_t>& state2) {
    uint16_t init_0 = src.pop(), init_1 = src.pop();
    rans_state<uint32_t> merged_state((init_0 << 16) | init_1);
    // The index of the highest set bit in state2.
    uint32_t split_point = merged_state.pop_bits(src, 4);
    state1.bits = merged_state.bits >> split_point;
    state1.maybe_refill(src);
    // High bits from merged_state, low bits from stream
    state2.bits = (merged_state.bits << 16) | src.pop();
    // Mask off high bits that went to state1
    state2.bits = state2.bits & ((1 << (16 + split_point)) - 1);
    // Set high order bit
    state2.bits |= 1 << (16 + split_point);
  }
  bounded_stack<uint16_t> src;
  uint8_t *dst, *dst_end;
  std::array<deferred_adaptive_model<uint16_t, 1024, 300, 36, 15, 10>, 4>
      command_word_models;
  std::array<deferred_adaptive_model<uint16_t, 1024, 40, 0, 15, 10>, 4>
      cache_reference_models;
  deferred_adaptive_model<uint16_t, 1024, 21, 0, 15, 10> copy_offset_model;
  register_lru_cache<uint32_t> copy_offset_cache;
  size_t delta_offset{1};
};
}  // namespace rans
