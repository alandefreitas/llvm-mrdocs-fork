//===- TensorSpec.h - type descriptor for a tensor --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_ANALYSIS_TENSORSPEC_H
#define LLVM_ANALYSIS_TENSORSPEC_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/LLVMContext.h"

#include <optional>
#include <vector>

namespace llvm {
/// Forward declarations of LLVM JSON types used by TensorSpec serialization.
namespace json {
class OStream;
class Value;
} // namespace json

/// X-macro listing C element types paired with TensorType enumerator names.
///
/// Known tensor types. The left part is the C type, the right is a name we can
/// use to identify the type (to implement TensorSpec equality checks), and to
/// use, if needed, when mapping to an underlying evaluator's type system. The
/// main requirement is that the C type we use has the same size and encoding
/// (e.g. endian-ness) as the one used by the evaluator.
/// @param M Macro taking a C element type and a TensorType enumerator name.
#define SUPPORTED_TENSOR_TYPES(M)                                              \
  M(float, Float)                                                              \
  M(double, Double)                                                            \
  M(int8_t, Int8)                                                              \
  M(uint8_t, UInt8)                                                            \
  M(int16_t, Int16)                                                            \
  M(uint16_t, UInt16)                                                          \
  M(int32_t, Int32)                                                            \
  M(uint32_t, UInt32)                                                          \
  M(int64_t, Int64)                                                            \
  M(uint64_t, UInt64)

/// Element type of a tensor buffer.
enum class TensorType {
  Invalid, ///< Unspecified or invalid element type.
  Float,   ///< 32-bit floating-point element.
  Double,  ///< 64-bit floating-point element.
  Int8,    ///< Signed 8-bit integer element.
  UInt8,   ///< Unsigned 8-bit integer element.
  Int16,   ///< Signed 16-bit integer element.
  UInt16,  ///< Unsigned 16-bit integer element.
  Int32,   ///< Signed 32-bit integer element.
  UInt32,  ///< Unsigned 32-bit integer element.
  Int64,   ///< Signed 64-bit integer element.
  UInt64,  ///< Unsigned 64-bit integer element.
  Total    ///< Sentinel counting supported types; not a valid element type.
};

/// Specification of a named tensor: shape, element type, name, and port.
///
/// TensorSpec encapsulates the specification of a tensor: its dimensions, or
/// "shape" (row-major), its type (see TensorSpec::getDataType specializations
/// for supported types), its name and port (see "TensorFlow: Large-Scale
/// Machine Learning on Heterogeneous Distributed Systems", section 4.2, para 2:
/// https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/45166.pdf)
///
/// Note that the design is motivated by Tensorflow, but it is not intended to
/// be Tensorflow-specific.
class TensorSpec final {
public:
  /// Create a TensorSpec for element type \p T with the given name and shape.
  /// @tparam T C element type; must match a TensorSpec::getDataType specialization.
  /// @param Name Tensor name.
  /// @param Shape Dimensions of the tensor in row-major order.
  /// @param Port Optional port index (default 0).
  /// @return A TensorSpec describing a tensor of element type \p T.
  template <typename T>
  static TensorSpec createSpec(const std::string &Name,
                               const std::vector<int64_t> &Shape,
                               int Port = 0) {
    return TensorSpec(Name, Port, getDataType<T>(), sizeof(T), Shape);
  }

  /// Return the tensor name.
  /// @return Name identifying this tensor.
  const std::string &name() const { return Name; }
  /// Return the tensor port index.
  /// @return Port index associated with this tensor.
  int port() const { return Port; }
  /// Return the element type of the tensor.
  /// @return Element type enumerator for this tensor.
  TensorType type() const { return Type; }
  /// Return the tensor dimensions in row-major order.
  /// @return Dimensions of the tensor in row-major order.
  const std::vector<int64_t> &shape() const { return Shape; }

  /// Return true if this specification equals \p Other.
  /// @param Other Specification to compare against.
  /// @return True when name, port, type, and shape all match.
  bool operator==(const TensorSpec &Other) const {
    return Name == Other.Name && Port == Other.Port && Type == Other.Type &&
           Shape == Other.Shape;
  }

  /// Return true if this specification differs from \p Other.
  /// @param Other Specification to compare against.
  /// @return True when this specification is not equal to \p Other.
  bool operator!=(const TensorSpec &Other) const { return !(*this == Other); }

  /// Get the number of elements in a tensor with this shape.
  /// @return Product of the dimensions in the shape.
  size_t getElementCount() const { return ElementCount; }
  /// Get the size, in bytes, of one element.
  /// @return Byte size of a single element of this tensor's type.
  size_t getElementByteSize() const { return ElementSize; }
  /// Get the total size of a memory buffer needed to store the whole tensor.
  /// @return Element count times element byte size.
  size_t getTotalTensorBufferSize() const { return ElementCount * ElementSize; }

  /// Return true if the element type is \p T.
  /// @tparam T C element type to test against this specification.
  /// @return True when \p T matches this specification's element type.
  template <typename T> bool isElementType() const {
    return getDataType<T>() == Type;
  }

  /// Construct a TensorSpec that copies \p Other but uses \p NewName.
  /// @param NewName Name for the new specification.
  /// @param Other Specification whose port, type, and shape are copied.
  TensorSpec(const std::string &NewName, const TensorSpec &Other)
      : TensorSpec(NewName, Other.Port, Other.Type, Other.ElementSize,
                   Other.Shape) {}

  /// Write this specification as a JSON object to \p OS.
  /// @param OS JSON output stream to receive the object.
  LLVM_ABI void toJSON(json::OStream &OS) const;

private:
  LLVM_ABI TensorSpec(const std::string &Name, int Port, TensorType Type,
                      size_t ElementSize, const std::vector<int64_t> &Shape);

  template <typename T> static TensorType getDataType();

  std::string Name;
  int Port = 0;
  TensorType Type = TensorType::Invalid;
  std::vector<int64_t> Shape;
  size_t ElementCount = 0;
  size_t ElementSize = 0;
};

/// Format a tensor buffer as a string for debugging.
/// @param Buffer Contiguous bytes holding the tensor values.
/// @param Spec Specification describing the layout and element type of \p Buffer.
/// @return Human-readable string representation of the tensor contents.
LLVM_ABI std::string tensorValueToString(const char *Buffer,
                                         const TensorSpec &Spec);

/// Construct a TensorSpec from a JSON dictionary describing the tensor.
///
/// The dictionary has the form:
/// { "name": <string>,
///   "port": <int>,
///   "type": <string. Use LLVM's types, e.g. float, double, int64_t>,
///   "shape": <array of ints> }
/// For the "type" field, see the C++ primitive types used in
/// TFUTILS_SUPPORTED_TYPES.
/// @param Ctx LLVM context used for diagnostics if the JSON is invalid.
/// @param Value JSON value expected to be a dictionary with name, port, type,
///        and shape fields.
/// @return A TensorSpec on success, or \c std::nullopt if \p Value is invalid.
LLVM_ABI std::optional<TensorSpec>
getTensorSpecFromJSON(LLVMContext &Ctx, const json::Value &Value);

#define TFUTILS_GETDATATYPE_DEF(T, Name)                                       \
  template <> LLVM_ABI TensorType TensorSpec::getDataType<T>();
SUPPORTED_TENSOR_TYPES(TFUTILS_GETDATATYPE_DEF)

#undef TFUTILS_GETDATATYPE_DEF
} // namespace llvm

#endif // LLVM_ANALYSIS_TENSORSPEC_H
