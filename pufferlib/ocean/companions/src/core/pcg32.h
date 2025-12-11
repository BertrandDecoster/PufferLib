// PCG32 - Portable, deterministic PRNG
// Based on PCG (Permuted Congruential Generator) by Melissa O'Neill
// This implementation produces identical sequences on all platforms.

#ifndef COMPANIONS_PCG32_H_
#define COMPANIONS_PCG32_H_

#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>

namespace companions {

// PCG32 random number generator - compatible with C++ standard library
// Usage: works with std::shuffle, std::uniform_int_distribution, etc.
class pcg32 {
 public:
  using result_type = uint32_t;

  // Default constructor with default seed
  pcg32() : state_(0x853c49e6748fea9bULL), inc_(0xda3e39cb94b95bdbULL) {}

  // Constructor with seed
  explicit pcg32(uint64_t seed) { seed_impl(seed); }

  // Seed the generator
  void seed(uint64_t s) { seed_impl(s); }

  // Generate random number
  result_type operator()() {
    uint64_t oldstate = state_;
    // Advance internal state
    state_ = oldstate * 6364136223846793005ULL + inc_;
    // Calculate output function (XSH RR)
    uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
  }

  // Required for UniformRandomBitGenerator concept
  static constexpr result_type min() { return 0; }
  static constexpr result_type max() { return std::numeric_limits<uint32_t>::max(); }

  // Discard n values (for compatibility)
  void discard(unsigned long long n) {
    for (unsigned long long i = 0; i < n; ++i) {
      (*this)();
    }
  }

 private:
  void seed_impl(uint64_t s) {
    state_ = 0;
    inc_ = (s << 1u) | 1u;  // inc must be odd
    (*this)();
    state_ += s;
    (*this)();
  }

  uint64_t state_;
  uint64_t inc_;
};

// Portable Fisher-Yates shuffle - identical results on all platforms
// Unlike std::shuffle, this implementation is guaranteed to produce the same
// permutation given the same RNG state, regardless of C++ standard library.
template<typename RandomIt, typename URBG>
void portable_shuffle(RandomIt first, RandomIt last, URBG&& g) {
    using diff_t = typename std::iterator_traits<RandomIt>::difference_type;
    diff_t n = last - first;
    for (diff_t i = n - 1; i > 0; --i) {
        // Generate uniform random index in [0, i]
        diff_t j = static_cast<diff_t>(g() % static_cast<uint32_t>(i + 1));
        if (i != j) {
            std::iter_swap(first + i, first + j);
        }
    }
}

}  // namespace companions

#endif  // COMPANIONS_PCG32_H_
