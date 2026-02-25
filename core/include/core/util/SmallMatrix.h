#pragma once

// =============================================================================
// SmallMatrix — Stack-allocated small matrix for WDF scattering operations.
//
// Designed for the hot path: no heap allocation, no Eigen dependency.
// Supports matrices up to ~12x12 (covers 9-port transformer junction).
// Operations: multiply, transpose, inverse (Gauss-Jordan for small N),
// rank-1 update (Sherman-Morrison).
//
// Reference: chowdsp_wdf RTypeAdaptorBase.h — S matrix pre-computed,
//            updated via Sherman-Morrison on port resistance change.
// =============================================================================

#include <array>
#include <cmath>
#include <cstring>
#include <cassert>

namespace transfo {

template <typename T, int Rows, int Cols>
class SmallMatrix
{
public:
    SmallMatrix() { zero(); }

    // ─── Element Access ─────────────────────────────────────────────────────
    T& operator()(int r, int c)             { return data_[r * Cols + c]; }
    const T& operator()(int r, int c) const { return data_[r * Cols + c]; }

    static constexpr int rows() { return Rows; }
    static constexpr int cols() { return Cols; }

    // ─── Initialization ─────────────────────────────────────────────────────
    void zero()
    {
        std::memset(data_.data(), 0, sizeof(T) * Rows * Cols);
    }

    void identity()
    {
        static_assert(Rows == Cols, "Identity requires square matrix");
        zero();
        for (int i = 0; i < Rows; ++i)
            (*this)(i, i) = T(1);
    }

    // ─── Raw Data Access ────────────────────────────────────────────────────
    T*       data()       { return data_.data(); }
    const T* data() const { return data_.data(); }

    // ─── Matrix-Vector Multiply: y = A * x ──────────────────────────────────
    // Output into pre-allocated array. No allocation.
    void mulVec(const T* x, T* y) const
    {
        for (int r = 0; r < Rows; ++r)
        {
            T sum = T(0);
            for (int c = 0; c < Cols; ++c)
                sum += (*this)(r, c) * x[c];
            y[r] = sum;
        }
    }

    // ─── Matrix-Matrix Multiply: C = A * B ──────────────────────────────────
    template <int ColsB>
    SmallMatrix<T, Rows, ColsB> operator*(const SmallMatrix<T, Cols, ColsB>& B) const
    {
        SmallMatrix<T, Rows, ColsB> C;
        for (int r = 0; r < Rows; ++r)
            for (int cb = 0; cb < ColsB; ++cb)
            {
                T sum = T(0);
                for (int k = 0; k < Cols; ++k)
                    sum += (*this)(r, k) * B(k, cb);
                C(r, cb) = sum;
            }
        return C;
    }

    // ─── Transpose ──────────────────────────────────────────────────────────
    SmallMatrix<T, Cols, Rows> transpose() const
    {
        SmallMatrix<T, Cols, Rows> result;
        for (int r = 0; r < Rows; ++r)
            for (int c = 0; c < Cols; ++c)
                result(c, r) = (*this)(r, c);
        return result;
    }

    // ─── In-Place Inverse (Gauss-Jordan) — square matrices only ─────────────
    // Returns false if singular. Modifies *this in place.
    bool invert()
    {
        static_assert(Rows == Cols, "Invert requires square matrix");
        constexpr int N = Rows;

        // Augmented matrix [A | I]
        SmallMatrix<T, N, N> inv;
        inv.identity();

        SmallMatrix<T, N, N> work = *this;

        for (int col = 0; col < N; ++col)
        {
            // Partial pivoting
            int pivotRow = col;
            T maxVal = std::abs(work(col, col));
            for (int r = col + 1; r < N; ++r)
            {
                T val = std::abs(work(r, col));
                if (val > maxVal)
                {
                    maxVal = val;
                    pivotRow = r;
                }
            }

            if (maxVal < T(1e-12))
                return false; // Singular

            // Swap rows
            if (pivotRow != col)
            {
                for (int c = 0; c < N; ++c)
                {
                    std::swap(work(col, c), work(pivotRow, c));
                    std::swap(inv(col, c), inv(pivotRow, c));
                }
            }

            // Scale pivot row
            T pivot = work(col, col);
            for (int c = 0; c < N; ++c)
            {
                work(col, c) /= pivot;
                inv(col, c) /= pivot;
            }

            // Eliminate column
            for (int r = 0; r < N; ++r)
            {
                if (r == col) continue;
                T factor = work(r, col);
                for (int c = 0; c < N; ++c)
                {
                    work(r, c) -= factor * work(col, c);
                    inv(r, c) -= factor * inv(col, c);
                }
            }
        }

        *this = inv;
        return true;
    }

    // ─── Rank-1 Update: A' = A + u * v^T (Sherman-Morrison helper) ──────────
    void rank1Update(const T* u, const T* v, T scale = T(1))
    {
        static_assert(Rows == Cols, "Rank-1 update requires square matrix");
        for (int r = 0; r < Rows; ++r)
            for (int c = 0; c < Cols; ++c)
                (*this)(r, c) += scale * u[r] * v[c];
    }

    // ─── Scalar Operations ──────────────────────────────────────────────────
    SmallMatrix& operator*=(T s)
    {
        for (int i = 0; i < Rows * Cols; ++i)
            data_[i] *= s;
        return *this;
    }

    SmallMatrix& operator+=(const SmallMatrix& other)
    {
        for (int i = 0; i < Rows * Cols; ++i)
            data_[i] += other.data_[i];
        return *this;
    }

    SmallMatrix& operator-=(const SmallMatrix& other)
    {
        for (int i = 0; i < Rows * Cols; ++i)
            data_[i] -= other.data_[i];
        return *this;
    }

private:
    std::array<T, Rows * Cols> data_{};
};

// ─── Convenience Aliases ────────────────────────────────────────────────────
using Mat2f = SmallMatrix<float, 2, 2>;
using Mat3f = SmallMatrix<float, 3, 3>;
using Mat4f = SmallMatrix<float, 4, 4>;
using Mat9f = SmallMatrix<float, 9, 9>;

} // namespace transfo
