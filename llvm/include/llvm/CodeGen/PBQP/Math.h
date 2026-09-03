//===- Math.h - PBQP Vector and Matrix classes ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PBQP_MATH_H
#define LLVM_CODEGEN_PBQP_MATH_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/InterleavedRange.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>

namespace llvm {
namespace PBQP {

/// Floating-point scalar type used by PBQP vectors and matrices.
using PBQPNum = float;

/// PBQP Vector class.
class Vector {
public:
  /// Construct a PBQP vector of the given size.
  /// \param Length Number of elements in the vector.
  explicit Vector(unsigned Length) : Data(Length) {}

  /// Construct a PBQP vector with initializer.
  /// \param Length Number of elements in the vector.
  /// \param InitVal Value used to initialize every element.
  Vector(unsigned Length, PBQPNum InitVal) : Data(Length) {
    std::fill(begin(), end(), InitVal);
  }

  /// Copy construct a PBQP vector.
  /// \param V Vector to copy.
  Vector(const Vector &V) : Data(ArrayRef<PBQPNum>(V.Data)) {}

  /// Move construct a PBQP vector.
  /// \param V Vector to move from.
  Vector(Vector &&V) : Data(std::move(V.Data)) {}

  /// Return a const iterator to the first element.
  /// \return Const pointer to the first element.
  const PBQPNum *begin() const { return Data.data(); }
  /// Return a const iterator past the last element.
  /// \return Const pointer past the last element.
  const PBQPNum *end() const { return Data.data() + Data.size(); }
  /// Return an iterator to the first element.
  /// \return Pointer to the first element.
  PBQPNum *begin() { return Data.data(); }
  /// Return an iterator past the last element.
  /// \return Pointer past the last element.
  PBQPNum *end() { return Data.data() + Data.size(); }

  /// Comparison operator.
  /// \param V Vector to compare against.
  /// \return True if the vectors are equal.
  bool operator==(const Vector &V) const {
    assert(!Data.empty() && "Invalid vector");
    return llvm::equal(*this, V);
  }

  /// Return the length of the vector
  /// \return Number of elements in the vector.
  unsigned getLength() const {
    assert(!Data.empty() && "Invalid vector");
    return Data.size();
  }

  /// Element access.
  /// \param Index Zero-based element index.
  /// \return Reference to the element at \p Index.
  PBQPNum& operator[](unsigned Index) {
    assert(!Data.empty() && "Invalid vector");
    assert(Index < Data.size() && "Vector element access out of bounds.");
    return Data[Index];
  }

  /// Const element access.
  /// \param Index Zero-based element index.
  /// \return Const reference to the element at \p Index.
  const PBQPNum& operator[](unsigned Index) const {
    assert(!Data.empty() && "Invalid vector");
    assert(Index < Data.size() && "Vector element access out of bounds.");
    return Data[Index];
  }

  /// Add another vector to this one.
  /// \param V Vector to add; must have the same length.
  /// \return Reference to this vector after addition.
  Vector& operator+=(const Vector &V) {
    assert(!Data.empty() && "Invalid vector");
    assert(Data.size() == V.Data.size() && "Vector length mismatch.");
    std::transform(begin(), end(), V.begin(), begin(), std::plus<PBQPNum>());
    return *this;
  }

  /// Returns the index of the minimum value in this vector
  /// \return Zero-based index of the minimum element.
  unsigned minIndex() const {
    assert(!Data.empty() && "Invalid vector");
    return llvm::min_element(*this) - begin();
  }

private:
  llvm::SmallVector<PBQPNum, 0> Data;
};

/// Return a hash_value for the given vector.
/// \param V Vector to hash.
/// \return Hash code for \p V.
inline hash_code hash_value(const Vector &V) {
  const unsigned *VBegin = reinterpret_cast<const unsigned *>(V.begin());
  const unsigned *VEnd = reinterpret_cast<const unsigned *>(V.end());
  return hash_combine(V.getLength(), hash_combine_range(VBegin, VEnd));
}

/// Output a textual representation of the given vector on the given
///        output stream.
/// \param OS Output stream to write to.
/// \param V Vector to print.
/// \return The output stream \p OS.
template <typename OStream>
OStream& operator<<(OStream &OS, const Vector &V) {
  assert((V.getLength() != 0) && "Zero-length vector badness.");
  OS << "[ " << llvm::interleaved(V) << " ]";
  return OS;
}

/// PBQP Matrix class
class Matrix {
private:
  friend hash_code hash_value(const Matrix &);

public:
  /// Construct a PBQP Matrix with the given dimensions.
  /// \param Rows Number of rows.
  /// \param Cols Number of columns.
  Matrix(unsigned Rows, unsigned Cols) :
    Rows(Rows), Cols(Cols), Data(std::make_unique<PBQPNum []>(Rows * Cols)) {
  }

  /// Construct a PBQP Matrix with the given dimensions and initial
  /// value.
  /// \param Rows Number of rows.
  /// \param Cols Number of columns.
  /// \param InitVal Value used to initialize every element.
  Matrix(unsigned Rows, unsigned Cols, PBQPNum InitVal)
    : Rows(Rows), Cols(Cols),
      Data(std::make_unique<PBQPNum []>(Rows * Cols)) {
    std::fill(Data.get(), Data.get() + (Rows * Cols), InitVal);
  }

  /// Copy construct a PBQP matrix.
  /// \param M Matrix to copy.
  Matrix(const Matrix &M)
    : Rows(M.Rows), Cols(M.Cols),
      Data(std::make_unique<PBQPNum []>(Rows * Cols)) {
    std::copy(M.Data.get(), M.Data.get() + (Rows * Cols), Data.get());
  }

  /// Move construct a PBQP matrix.
  /// \param M Matrix to move from.
  Matrix(Matrix &&M)
    : Rows(M.Rows), Cols(M.Cols), Data(std::move(M.Data)) {
    M.Rows = M.Cols = 0;
  }

  /// Comparison operator.
  /// \param M Matrix to compare against.
  /// \return True if the matrices are equal.
  bool operator==(const Matrix &M) const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    if (Rows != M.Rows || Cols != M.Cols)
      return false;
    return std::equal(Data.get(), Data.get() + (Rows * Cols), M.Data.get());
  }

  /// Return the number of rows in this matrix.
  /// \return Number of rows.
  unsigned getRows() const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    return Rows;
  }

  /// Return the number of cols in this matrix.
  /// \return Number of columns.
  unsigned getCols() const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    return Cols;
  }

  /// Matrix element access.
  /// \param R Zero-based row index.
  /// \return Pointer to the first element of row \p R.
  PBQPNum* operator[](unsigned R) {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    assert(R < Rows && "Row out of bounds.");
    return Data.get() + (R * Cols);
  }

  /// Matrix element access.
  /// \param R Zero-based row index.
  /// \return Const pointer to the first element of row \p R.
  const PBQPNum* operator[](unsigned R) const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    assert(R < Rows && "Row out of bounds.");
    return Data.get() + (R * Cols);
  }

  /// Returns the given row as a vector.
  /// \param R Zero-based row index.
  /// \return Copy of row \p R as a vector.
  Vector getRowAsVector(unsigned R) const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    Vector V(Cols);
    for (unsigned C = 0; C < Cols; ++C)
      V[C] = (*this)[R][C];
    return V;
  }

  /// Returns the given column as a vector.
  /// \param C Zero-based column index.
  /// \return Copy of column \p C as a vector.
  Vector getColAsVector(unsigned C) const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    Vector V(Rows);
    for (unsigned R = 0; R < Rows; ++R)
      V[R] = (*this)[R][C];
    return V;
  }

  /// Matrix transpose.
  /// \return Transposed copy of this matrix.
  Matrix transpose() const {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    Matrix M(Cols, Rows);
    for (unsigned r = 0; r < Rows; ++r)
      for (unsigned c = 0; c < Cols; ++c)
        M[c][r] = (*this)[r][c];
    return M;
  }

  /// Add the given matrix to this one.
  /// \param M Matrix to add; must have matching dimensions.
  /// \return Reference to this matrix after addition.
  Matrix& operator+=(const Matrix &M) {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    assert(Rows == M.Rows && Cols == M.Cols &&
           "Matrix dimensions mismatch.");
    std::transform(Data.get(), Data.get() + (Rows * Cols), M.Data.get(),
                   Data.get(), std::plus<PBQPNum>());
    return *this;
  }

  /// Return the sum of this matrix and another.
  /// \param M Matrix to add; must have matching dimensions.
  /// \return New matrix equal to the element-wise sum.
  Matrix operator+(const Matrix &M) {
    assert(Rows != 0 && Cols != 0 && Data && "Invalid matrix");
    Matrix Tmp(*this);
    Tmp += M;
    return Tmp;
  }

private:
  unsigned Rows, Cols;
  std::unique_ptr<PBQPNum []> Data;
};

/// Return a hash_code for the given matrix.
/// \param M Matrix to hash.
/// \return Hash code for \p M.
inline hash_code hash_value(const Matrix &M) {
  unsigned *MBegin = reinterpret_cast<unsigned*>(M.Data.get());
  unsigned *MEnd =
    reinterpret_cast<unsigned*>(M.Data.get() + (M.Rows * M.Cols));
  return hash_combine(M.Rows, M.Cols, hash_combine_range(MBegin, MEnd));
}

/// Output a textual representation of the given matrix on the given
///        output stream.
/// \param OS Output stream to write to.
/// \param M Matrix to print.
/// \return The output stream \p OS.
template <typename OStream>
OStream& operator<<(OStream &OS, const Matrix &M) {
  assert((M.getRows() != 0) && "Zero-row matrix badness.");
  for (unsigned i = 0; i < M.getRows(); ++i)
    OS << M.getRowAsVector(i) << "\n";
  return OS;
}

/// PBQP vector that carries solver-specific metadata.
template <typename Metadata>
class MDVector : public Vector {
public:
  /// Construct a metadata vector by copying a vector.
  /// \param v Vector whose elements are copied.
  MDVector(const Vector &v) : Vector(v), md(*this) {}
  /// Construct a metadata vector by moving a vector.
  /// \param v Vector whose elements are moved.
  MDVector(Vector &&v) : Vector(std::move(v)), md(*this) { }

  /// Return the metadata associated with this vector.
  /// \return Const reference to the vector metadata.
  const Metadata& getMetadata() const { return md; }

private:
  Metadata md;
};

/// Return a hash_code for the given metadata vector.
/// \param V Metadata vector to hash.
/// \return Hash code for \p V.
template <typename Metadata>
inline hash_code hash_value(const MDVector<Metadata> &V) {
  return hash_value(static_cast<const Vector&>(V));
}

/// PBQP matrix that carries solver-specific metadata.
template <typename Metadata>
class MDMatrix : public Matrix {
public:
  /// Construct a metadata matrix by copying a matrix.
  /// \param m Matrix whose elements are copied.
  MDMatrix(const Matrix &m) : Matrix(m), md(*this) {}
  /// Construct a metadata matrix by moving a matrix.
  /// \param m Matrix whose elements are moved.
  MDMatrix(Matrix &&m) : Matrix(std::move(m)), md(*this) { }

  /// Return the metadata associated with this matrix.
  /// \return Const reference to the matrix metadata.
  const Metadata& getMetadata() const { return md; }

private:
  Metadata md;
};

/// Return a hash_code for the given metadata matrix.
/// \param M Metadata matrix to hash.
/// \return Hash code for \p M.
template <typename Metadata>
inline hash_code hash_value(const MDMatrix<Metadata> &M) {
  return hash_value(static_cast<const Matrix&>(M));
}

} // end namespace PBQP
} // end namespace llvm

#endif // LLVM_CODEGEN_PBQP_MATH_H
