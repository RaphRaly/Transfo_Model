#pragma once

// =============================================================================
// AlignedBuffer — SIMD-aligned buffer for audio and WDF processing.
//
// Stack-allocated for small sizes (via std::array), heap-allocated for
// dynamic sizes. All buffers aligned to 32 bytes (AVX alignment).
// No JUCE dependency.
// =============================================================================

#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace transfo {

// ─── Fixed-size aligned buffer (stack, for hot path) ────────────────────────
template <typename T, int N> struct alignas(32) AlignedArray {
  std::array<T, N> data_{};

  T &operator[](int i) { return data_[i]; }
  const T &operator[](int i) const { return data_[i]; }

  T *begin() { return data_.data(); }
  const T *begin() const { return data_.data(); }
  T *end() { return data_.data() + N; }
  const T *end() const { return data_.data() + N; }

  T *data() { return data_.data(); }
  const T *data() const { return data_.data(); }

  constexpr int size() const { return N; }

  void clear() { std::memset(data_.data(), 0, sizeof(T) * N); }
};

// ─── Dynamic-size aligned buffer (heap, for variable block sizes) ───────────
template <typename T> class AlignedBuffer {
public:
  AlignedBuffer() = default;

  explicit AlignedBuffer(int size) { resize(size); }

  void resize(int newSize) {
    size_ = newSize;
    if (newSize > 0) {
      // Allocate with 32-byte alignment
      const size_t bytes = static_cast<size_t>(newSize) * sizeof(T);
#if defined(_MSC_VER)
      data_ = static_cast<T *>(_aligned_malloc(bytes, 32));
#else
      data_ = static_cast<T *>(std::aligned_alloc(32, (bytes + 31) & ~31));
#endif
      clear();
    }
  }

  ~AlignedBuffer() {
    if (data_) {
#if defined(_MSC_VER)
      _aligned_free(data_);
#else
      std::free(data_);
#endif
    }
  }

  // Move semantics
  AlignedBuffer(AlignedBuffer &&other) noexcept
      : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
  }

  AlignedBuffer &operator=(AlignedBuffer &&other) noexcept {
    if (this != &other) {
      if (data_) {
#if defined(_MSC_VER)
        _aligned_free(data_);
#else
        std::free(data_);
#endif
      }
      data_ = other.data_;
      size_ = other.size_;
      other.data_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }

  // No copy
  AlignedBuffer(const AlignedBuffer &) = delete;
  AlignedBuffer &operator=(const AlignedBuffer &) = delete;

  T &operator[](int i) { return data_[i]; }
  const T &operator[](int i) const { return data_[i]; }

  T *data() { return data_; }
  const T *data() const { return data_; }
  int size() const { return size_; }

  void clear() {
    if (data_ && size_ > 0)
      std::memset(data_, 0, static_cast<size_t>(size_) * sizeof(T));
  }

private:
  T *data_ = nullptr;
  int size_ = 0;
};

} // namespace transfo
