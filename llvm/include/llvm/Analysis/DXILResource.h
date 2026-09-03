//===- DXILResource.h - Representations of DXIL resources -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DXILRESOURCE_H
#define LLVM_ANALYSIS_DXILRESOURCE_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Frontend/HLSL/HLSLBinding.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DXILABI.h"
#include <cstdint>
#include <variant>

namespace llvm {
class CallInst;
class DataLayout;
class LLVMContext;
class MDTuple;
class Value;

class DXILResourceTypeMap;

namespace dxil {

/// Return the resource name from a handle-from-binding call.
///
/// Accepts either \c dx_resource_handlefrombinding or
/// \c dx_resource_handlefromimplicitbinding.
/// \param CI The binding call instruction.
/// \return The resource name operand as a string reference.
LLVM_ABI StringRef getResourceNameFromBindingCall(CallInst *CI);

/// The dx.RawBuffer target extension type
///
/// `target("dx.RawBuffer", Type, IsWriteable, IsROV)`
class RawBufferExtType : public TargetExtType {
public:
  /// Deleted default constructor.
  RawBufferExtType() = delete;
  /// Deleted copy constructor; target extension types are uniqued.
  /// \param Other Unused; copy construction is deleted.
  RawBufferExtType(const RawBufferExtType &Other) = delete;
  /// Deleted copy assignment; target extension types are uniqued.
  /// \param Other Unused; copy assignment is deleted.
  RawBufferExtType &operator=(const RawBufferExtType &Other) = delete;

  /// Return true if this is a structured buffer rather than a byte-address
  /// buffer.
  /// \return True if this is a structured buffer rather than a byte-address buffer.
  bool isStructured() const {
    // TODO: We need to be more prescriptive here, but since there's some debate
    // over whether byte address buffer should have a void type or an i8 type,
    // accept either for now.
    Type *Ty = getTypeParameter(0);
    return !Ty->isVoidTy() && !Ty->isIntegerTy(8);
  }

  /// Return the structured element type, or null for a byte-address buffer.
  /// \return The structured element type, or null for a byte-address buffer.
  Type *getResourceType() const {
    return isStructured() ? getTypeParameter(0) : nullptr;
  }
  /// Return true if this buffer is writeable (UAV).
  /// \return True if this buffer is writeable (UAV).
  bool isWriteable() const { return getIntParameter(0); }
  /// Return true if this is a rasterizer-ordered view.
  /// \return True if this is a rasterizer-ordered view.
  bool isROV() const { return getIntParameter(1); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Target extension type to test.
  /// \return True if \p T is a dx.RawBuffer target extension type.
  static bool classof(const TargetExtType *T) {
    return T->getName() == "dx.RawBuffer";
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return True if \p T is a dx.RawBuffer target extension type.
  static bool classof(const Type *T) {
    return isa<TargetExtType>(T) && classof(cast<TargetExtType>(T));
  }
};

/// The dx.TypedBuffer target extension type
///
/// `target("dx.TypedBuffer", Type, IsWriteable, IsROV, IsSigned)`
class TypedBufferExtType : public TargetExtType {
public:
  /// Deleted default constructor.
  TypedBufferExtType() = delete;
  /// Deleted copy constructor; target extension types are uniqued.
  /// \param Other Unused; copy construction is deleted.
  TypedBufferExtType(const TypedBufferExtType &Other) = delete;
  /// Deleted copy assignment; target extension types are uniqued.
  /// \param Other Unused; copy assignment is deleted.
  TypedBufferExtType &operator=(const TypedBufferExtType &Other) = delete;

  /// Return the element type of this typed buffer.
  /// \return The element type of this typed buffer.
  Type *getResourceType() const { return getTypeParameter(0); }
  /// Return true if this buffer is writeable (UAV).
  /// \return True if this buffer is writeable (UAV).
  bool isWriteable() const { return getIntParameter(0); }
  /// Return true if this is a rasterizer-ordered view.
  /// \return True if this is a rasterizer-ordered view.
  bool isROV() const { return getIntParameter(1); }
  /// Return true if the element type is signed.
  /// \return True if the element type is signed.
  bool isSigned() const { return getIntParameter(2); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Target extension type to test.
  /// \return True if \p T is a dx.TypedBuffer target extension type.
  static bool classof(const TargetExtType *T) {
    return T->getName() == "dx.TypedBuffer";
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return True if \p T is a dx.TypedBuffer target extension type.
  static bool classof(const Type *T) {
    return isa<TargetExtType>(T) && classof(cast<TargetExtType>(T));
  }
};

/// The dx.Texture target extension type
///
/// `target("dx.Texture", Type, IsWriteable, IsROV, IsSigned, Dimension)`
class TextureExtType : public TargetExtType {
public:
  /// Deleted default constructor.
  TextureExtType() = delete;
  /// Deleted copy constructor; target extension types are uniqued.
  /// \param Other Unused; copy construction is deleted.
  TextureExtType(const TextureExtType &Other) = delete;
  /// Deleted copy assignment; target extension types are uniqued.
  /// \param Other Unused; copy assignment is deleted.
  TextureExtType &operator=(const TextureExtType &Other) = delete;

  /// Return the element type of this texture.
  /// \return The element type of this texture.
  Type *getResourceType() const { return getTypeParameter(0); }
  /// Return true if this texture is writeable (UAV).
  /// \return True if this texture is writeable (UAV).
  bool isWriteable() const { return getIntParameter(0); }
  /// Return true if this is a rasterizer-ordered view.
  /// \return True if this is a rasterizer-ordered view.
  bool isROV() const { return getIntParameter(1); }
  /// Return true if the element type is signed.
  /// \return True if the element type is signed.
  bool isSigned() const { return getIntParameter(2); }
  /// Return the texture dimension as a DXIL resource kind.
  /// \return The texture dimension as a DXIL resource kind.
  dxil::ResourceKind getDimension() const {
    return static_cast<dxil::ResourceKind>(getIntParameter(3));
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Target extension type to test.
  /// \return True if \p T is a dx.Texture target extension type.
  static bool classof(const TargetExtType *T) {
    return T->getName() == "dx.Texture";
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return True if \p T is a dx.Texture target extension type.
  static bool classof(const Type *T) {
    return isa<TargetExtType>(T) && classof(cast<TargetExtType>(T));
  }
};

/// The dx.MSTexture target extension type
///
/// `target("dx.MSTexture", Type, IsWriteable, Samples, IsSigned, Dimension)`
class MSTextureExtType : public TargetExtType {
public:
  /// Deleted default constructor.
  MSTextureExtType() = delete;
  /// Deleted copy constructor; target extension types are uniqued.
  /// \param Other Unused; copy construction is deleted.
  MSTextureExtType(const MSTextureExtType &Other) = delete;
  /// Deleted copy assignment; target extension types are uniqued.
  /// \param Other Unused; copy assignment is deleted.
  MSTextureExtType &operator=(const MSTextureExtType &Other) = delete;

  /// Return the element type of this multisampled texture.
  /// \return The element type of this multisampled texture.
  Type *getResourceType() const { return getTypeParameter(0); }
  /// Return true if this texture is writeable (UAV).
  /// \return True if this texture is writeable (UAV).
  bool isWriteable() const { return getIntParameter(0); }
  /// Return the number of samples.
  /// \return The number of samples.
  uint32_t getSampleCount() const { return getIntParameter(1); }
  /// Return true if the element type is signed.
  /// \return True if the element type is signed.
  bool isSigned() const { return getIntParameter(2); }
  /// Return the texture dimension as a DXIL resource kind.
  /// \return The texture dimension as a DXIL resource kind.
  dxil::ResourceKind getDimension() const {
    return static_cast<dxil::ResourceKind>(getIntParameter(3));
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Target extension type to test.
  /// \return True if \p T is a dx.MSTexture target extension type.
  static bool classof(const TargetExtType *T) {
    return T->getName() == "dx.MSTexture";
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return True if \p T is a dx.MSTexture target extension type.
  static bool classof(const Type *T) {
    return isa<TargetExtType>(T) && classof(cast<TargetExtType>(T));
  }
};

/// The dx.FeedbackTexture target extension type
///
/// `target("dx.FeedbackTexture", FeedbackType, Dimension)`
class FeedbackTextureExtType : public TargetExtType {
public:
  /// Deleted default constructor.
  FeedbackTextureExtType() = delete;
  /// Deleted copy constructor; target extension types are uniqued.
  /// \param Other Unused; copy construction is deleted.
  FeedbackTextureExtType(const FeedbackTextureExtType &Other) = delete;
  /// Deleted copy assignment; target extension types are uniqued.
  /// \param Other Unused; copy assignment is deleted.
  FeedbackTextureExtType &operator=(const FeedbackTextureExtType &Other) = delete;

  /// Return the sampler feedback type.
  /// \return The sampler feedback type.
  dxil::SamplerFeedbackType getFeedbackType() const {
    return static_cast<dxil::SamplerFeedbackType>(getIntParameter(0));
  }
  /// Return the texture dimension as a DXIL resource kind.
  /// \return The texture dimension as a DXIL resource kind.
  dxil::ResourceKind getDimension() const {
    return static_cast<dxil::ResourceKind>(getIntParameter(1));
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Target extension type to test.
  /// \return True if \p T is a dx.FeedbackTexture target extension type.
  static bool classof(const TargetExtType *T) {
    return T->getName() == "dx.FeedbackTexture";
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return True if \p T is a dx.FeedbackTexture target extension type.
  static bool classof(const Type *T) {
    return isa<TargetExtType>(T) && classof(cast<TargetExtType>(T));
  }
};

/// The dx.CBuffer target extension type
///
/// `target("dx.CBuffer", <Type>, ...)`
class CBufferExtType : public TargetExtType {
public:
  /// Deleted default constructor.
  CBufferExtType() = delete;
  /// Deleted copy constructor; target extension types are uniqued.
  /// \param Other Unused; copy construction is deleted.
  CBufferExtType(const CBufferExtType &Other) = delete;
  /// Deleted copy assignment; target extension types are uniqued.
  /// \param Other Unused; copy assignment is deleted.
  CBufferExtType &operator=(const CBufferExtType &Other) = delete;

  /// Return the constant-buffer layout type.
  /// \return The constant-buffer layout type.
  Type *getResourceType() const { return getTypeParameter(0); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Target extension type to test.
  /// \return True if \p T is a dx.CBuffer target extension type.
  static bool classof(const TargetExtType *T) {
    return T->getName() == "dx.CBuffer";
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return True if \p T is a dx.CBuffer target extension type.
  static bool classof(const Type *T) {
    return isa<TargetExtType>(T) && classof(cast<TargetExtType>(T));
  }
};

/// The dx.Sampler target extension type
///
/// `target("dx.Sampler", SamplerType)`
class SamplerExtType : public TargetExtType {
public:
  /// Deleted default constructor.
  SamplerExtType() = delete;
  /// Deleted copy constructor; target extension types are uniqued.
  /// \param Other Unused; copy construction is deleted.
  SamplerExtType(const SamplerExtType &Other) = delete;
  /// Deleted copy assignment; target extension types are uniqued.
  /// \param Other Unused; copy assignment is deleted.
  SamplerExtType &operator=(const SamplerExtType &Other) = delete;

  /// Return the sampler type kind.
  /// \return The sampler type kind.
  dxil::SamplerType getSamplerType() const {
    return static_cast<dxil::SamplerType>(getIntParameter(0));
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Target extension type to test.
  /// \return True if \p T is a dx.Sampler target extension type.
  static bool classof(const TargetExtType *T) {
    return T->getName() == "dx.Sampler";
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return True if \p T is a dx.Sampler target extension type.
  static bool classof(const Type *T) {
    return isa<TargetExtType>(T) && classof(cast<TargetExtType>(T));
  }
};

/// A target extension type representing any DXIL resource kind.
class AnyResourceExtType : public TargetExtType {
public:
  /// Deleted default constructor.
  AnyResourceExtType() = delete;
  /// Deleted copy constructor; target extension types are uniqued.
  /// \param Other Unused; copy construction is deleted.
  AnyResourceExtType(const AnyResourceExtType &Other) = delete;
  /// Deleted copy assignment; target extension types are uniqued.
  /// \param Other Unused; copy assignment is deleted.
  AnyResourceExtType &operator=(const AnyResourceExtType &Other) = delete;

  /// Return the underlying resource element type, or null if none.
  ///
  /// Sampler and feedback resources do not have an underlying type.
  /// \return The underlying resource element type, or null if none.
  Type *getResourceType() const {
    // Sampler and feedback resources do not have an underlying type.
    if (isa<SamplerExtType>(this) || isa<FeedbackTextureExtType>(this))
      return nullptr;
    // All other resources store the type in a parameter.
    return getTypeParameter(0);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Target extension type to test.
  /// \return True if \p T is a DXIL resource target extension type.
  static bool classof(const TargetExtType *T) {
    return isa<RawBufferExtType>(T) || isa<TypedBufferExtType>(T) ||
           isa<TextureExtType>(T) || isa<MSTextureExtType>(T) ||
           isa<FeedbackTextureExtType>(T) || isa<CBufferExtType>(T) ||
           isa<SamplerExtType>(T);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return True if \p T is a DXIL resource target extension type.
  static bool classof(const Type *T) {
    return isa<TargetExtType>(T) && classof(cast<TargetExtType>(T));
  }
};

/// The dx.Layout target extension type
///
/// `target("dx.Layout", <Type>, <size>, [offsets...])`
class LayoutExtType : public TargetExtType {
public:
  /// Deleted default constructor.
  LayoutExtType() = delete;
  /// Deleted copy constructor; target extension types are uniqued.
  /// \param Other Unused; copy construction is deleted.
  LayoutExtType(const LayoutExtType &Other) = delete;
  /// Deleted copy assignment; target extension types are uniqued.
  /// \param Other Unused; copy assignment is deleted.
  LayoutExtType &operator=(const LayoutExtType &Other) = delete;

  /// Return the wrapped layout type.
  /// \return The wrapped layout type.
  Type *getWrappedType() const { return getTypeParameter(0); }
  /// Return the total size of the layout in bytes.
  /// \return The total size of the layout in bytes.
  uint32_t getSize() const { return getIntParameter(0); }
  /// Return the byte offset of element \p I within the layout.
  /// \param I Zero-based element index.
  /// \return The byte offset of element \p I within the layout.
  uint32_t getOffsetOfElement(int I) const { return getIntParameter(I + 1); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Target extension type to test.
  /// \return True if \p T is a dx.Layout target extension type.
  static bool classof(const TargetExtType *T) {
    return T->getName() == "dx.Layout";
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return True if \p T is a dx.Layout target extension type.
  static bool classof(const Type *T) {
    return isa<TargetExtType>(T) && classof(cast<TargetExtType>(T));
  }
};

/// The dx.Padding target extension type
///
/// `target("dx.Padding", NumBytes)`
class PaddingExtType : public TargetExtType {
public:
  /// Deleted default constructor.
  PaddingExtType() = delete;
  /// Deleted copy constructor; target extension types are uniqued.
  /// \param Other Unused; copy construction is deleted.
  PaddingExtType(const PaddingExtType &Other) = delete;
  /// Deleted copy assignment; target extension types are uniqued.
  /// \param Other Unused; copy assignment is deleted.
  PaddingExtType &operator=(const PaddingExtType &Other) = delete;

  /// Return the number of padding bytes represented by this type.
  /// \return The number of padding bytes represented by this type.
  unsigned getNumBytes() const { return getIntParameter(0); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Target extension type to test.
  /// \return True if \p T is a dx.Padding target extension type.
  static bool classof(const TargetExtType *T) {
    return T->getName() == "dx.Padding";
  }
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param T Type to test.
  /// \return True if \p T is a dx.Padding target extension type.
  static bool classof(const Type *T) {
    return isa<TargetExtType>(T) && classof(cast<TargetExtType>(T));
  }
};

//===----------------------------------------------------------------------===//

/// Description of a DXIL resource's type properties.
class ResourceTypeInfo {
public:
  /// UAV-specific properties for a resource type.
  struct UAVInfo {
    /// Whether this UAV is a rasterizer-ordered view.
    bool IsROV;

    /// Return true if both UAV infos are equal.
    /// \param RHS Other UAV info to compare.
    /// \return True if both UAV infos are equal.
    bool operator==(const UAVInfo &RHS) const { return IsROV == RHS.IsROV; }
    /// Return true if the UAV infos differ.
    /// \param RHS Other UAV info to compare.
    /// \return True if the UAV infos differ.
    bool operator!=(const UAVInfo &RHS) const { return !(*this == RHS); }
    /// Order UAV infos by ROV flag.
    /// \param RHS Other UAV info to compare.
    /// \return True if this UAV info is ordered before \p RHS.
    bool operator<(const UAVInfo &RHS) const { return IsROV < RHS.IsROV; }
  };

  /// Structured-buffer layout properties.
  struct StructInfo {
    /// Byte stride between structured elements.
    uint32_t Stride;
    // Note: we store an integer here rather than using `MaybeAlign` because in
    // GCC 7 MaybeAlign isn't trivial so having one in this union would delete
    // our move constructor.
    // See https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0602r4.html
    /// Log2 of the required alignment, stored instead of \c MaybeAlign.
    uint32_t AlignLog2;

    /// Return true if both struct infos are equal.
    /// \param RHS Other struct info to compare.
    /// \return True if both struct infos are equal.
    bool operator==(const StructInfo &RHS) const {
      return std::tie(Stride, AlignLog2) == std::tie(RHS.Stride, RHS.AlignLog2);
    }
    /// Return true if the struct infos differ.
    /// \param RHS Other struct info to compare.
    /// \return True if the struct infos differ.
    bool operator!=(const StructInfo &RHS) const { return !(*this == RHS); }
    /// Order struct infos by stride then alignment.
    /// \param RHS Other struct info to compare.
    /// \return True if this struct info is ordered before \p RHS.
    bool operator<(const StructInfo &RHS) const {
      return std::tie(Stride, AlignLog2) < std::tie(RHS.Stride, RHS.AlignLog2);
    }
  };

  /// Typed-resource element properties.
  struct TypedInfo {
    /// Element type as declared in the frontend.
    dxil::ElementType ElementTy;
    /// Element type used for DXIL storage.
    dxil::ElementType DXILStorageTy;
    /// Number of elements in the typed resource.
    uint32_t ElementCount;

    /// Return true if both typed infos are equal.
    /// \param RHS Other typed info to compare.
    /// \return True if both typed infos are equal.
    bool operator==(const TypedInfo &RHS) const {
      return std::tie(ElementTy, ElementCount) ==
             std::tie(RHS.ElementTy, RHS.ElementCount);
    }
    /// Return true if the typed infos differ.
    /// \param RHS Other typed info to compare.
    /// \return True if the typed infos differ.
    bool operator!=(const TypedInfo &RHS) const { return !(*this == RHS); }
    /// Order typed infos by element type then count.
    /// \param RHS Other typed info to compare.
    /// \return True if this typed info is ordered before \p RHS.
    bool operator<(const TypedInfo &RHS) const {
      return std::tie(ElementTy, ElementCount) <
             std::tie(RHS.ElementTy, RHS.ElementCount);
    }
  };

private:
  TargetExtType *HandleTy;

  dxil::ResourceClass RC;
  dxil::ResourceKind Kind;

public:
  /// Construct type info from an explicit handle type, class, and kind.
  /// \param HandleTy Target extension type for the resource handle.
  /// \param RC DXIL resource class.
  /// \param Kind DXIL resource kind.
  LLVM_ABI ResourceTypeInfo(TargetExtType *HandleTy,
                            const dxil::ResourceClass RC,
                            const dxil::ResourceKind Kind);
  /// Construct type info by deriving class and kind from \p HandleTy.
  /// \param HandleTy Target extension type for the resource handle.
  ResourceTypeInfo(TargetExtType *HandleTy)
      : ResourceTypeInfo(HandleTy, {}, dxil::ResourceKind::Invalid) {}

  /// Return the target extension type of the resource handle.
  /// \return The target extension type of the resource handle.
  TargetExtType *getHandleTy() const { return HandleTy; }
  /// Create an LLVM struct type representing the resource's elements.
  /// \param CBufferName Optional name used when creating a CBuffer struct.
  /// \return An LLVM struct type representing the resource's elements.
  LLVM_ABI StructType *createElementStruct(StringRef CBufferName = "");

  // Conditions to check before accessing specific views.
  /// Return true if this resource is a UAV.
  /// \return True if this resource is a UAV.
  LLVM_ABI bool isUAV() const;
  /// Return true if this resource is a constant buffer.
  /// \return True if this resource is a constant buffer.
  LLVM_ABI bool isCBuffer() const;
  /// Return true if this resource is a sampler.
  /// \return True if this resource is a sampler.
  LLVM_ABI bool isSampler() const;
  /// Return true if this resource has structured layout info.
  /// \return True if this resource has structured layout info.
  LLVM_ABI bool isStruct() const;
  /// Return true if this resource is typed.
  /// \return True if this resource is typed.
  LLVM_ABI bool isTyped() const;
  /// Return true if this resource is a sampler-feedback texture.
  /// \return True if this resource is a sampler-feedback texture.
  LLVM_ABI bool isFeedback() const;
  /// Return true if this resource is multisampled.
  /// \return True if this resource is multisampled.
  LLVM_ABI bool isMultiSample() const;

  // Views into the type.
  /// Return UAV-specific properties; requires \c isUAV().
  /// \return UAV-specific properties.
  LLVM_ABI UAVInfo getUAV() const;
  /// Return the constant-buffer size in bytes; requires \c isCBuffer().
  /// \param DL Data layout used to compute the size.
  /// \return The constant-buffer size in bytes.
  LLVM_ABI uint32_t getCBufferSize(const DataLayout &DL) const;
  /// Return the sampler type; requires \c isSampler().
  /// \return The sampler type.
  LLVM_ABI dxil::SamplerType getSamplerType() const;
  /// Return structured layout info; requires \c isStruct().
  /// \param DL Data layout used to compute stride and alignment.
  /// \return Structured layout info.
  LLVM_ABI StructInfo getStruct(const DataLayout &DL) const;
  /// Return typed element info; requires \c isTyped().
  /// \return Typed element info.
  LLVM_ABI TypedInfo getTyped() const;
  /// Return the sampler feedback type; requires \c isFeedback().
  /// \return The sampler feedback type.
  LLVM_ABI dxil::SamplerFeedbackType getFeedbackType() const;
  /// Return the multisample count; requires \c isMultiSample().
  /// \return The multisample count.
  LLVM_ABI uint32_t getMultiSampleCount() const;

  /// Return the DXIL resource class.
  /// \return The DXIL resource class.
  dxil::ResourceClass getResourceClass() const { return RC; }
  /// Return the DXIL resource kind.
  /// \return The DXIL resource kind.
  dxil::ResourceKind getResourceKind() const { return Kind; }

  /// Return true if both resource type infos are equal.
  /// \param RHS Other resource type info to compare.
  /// \return True if both resource type infos are equal.
  LLVM_ABI bool operator==(const ResourceTypeInfo &RHS) const;
  /// Return true if the resource type infos differ.
  /// \param RHS Other resource type info to compare.
  /// \return True if the resource type infos differ.
  bool operator!=(const ResourceTypeInfo &RHS) const { return !(*this == RHS); }
  /// Order resource type infos for use as map keys.
  /// \param RHS Other resource type info to compare.
  /// \return True if this type info is ordered before \p RHS.
  LLVM_ABI bool operator<(const ResourceTypeInfo &RHS) const;

  /// Print a human-readable description of this type info.
  /// \param OS Stream to print to.
  /// \param DL Data layout used when formatting sizes.
  LLVM_ABI void print(raw_ostream &OS, const DataLayout &DL) const;
};

//===----------------------------------------------------------------------===//

/// Direction of atomic counter updates on a resource.
enum class ResourceCounterDirection {
  /// Counter is only incremented.
  Increment,
  /// Counter is only decremented.
  Decrement,
  /// Counter direction has not been determined.
  Unknown,
  /// Conflicting increment and decrement uses were observed.
  Invalid,
};

/// Binding and metadata for a single DXIL resource instance.
class ResourceInfo {
public:
  /// Explicit register binding for a resource.
  struct ResourceBinding {
    /// Assigned binding ID within the resource class.
    uint32_t BindingID = 0;
    /// Register space of the binding.
    uint32_t Space;
    /// Lower bound of the register range.
    uint32_t LowerBound;
    /// Number of registers; zero means unbounded.
    uint32_t Size;

    /// Construct an empty binding with all fields zero.
    ResourceBinding() : BindingID(0), Space(0), LowerBound(0), Size(0) {}
    /// Construct a binding with the given register range.
    /// \param BindingID Binding ID within the resource class.
    /// \param Space Register space.
    /// \param LowerBound Lower bound of the register range.
    /// \param Size Number of registers; zero means unbounded.
    ResourceBinding(uint32_t BindingID, uint32_t Space, uint32_t LowerBound,
                    uint32_t Size)
        : BindingID(BindingID), Space(Space), LowerBound(LowerBound),
          Size(Size) {}

    /// Return true if both bindings are equal.
    /// \param RHS Other binding to compare.
    /// \return True if both bindings are equal.
    bool operator==(const ResourceBinding &RHS) const {
      return std::tie(BindingID, Space, LowerBound, Size) ==
             std::tie(RHS.BindingID, RHS.Space, RHS.LowerBound, RHS.Size);
    }
    /// Return true if the bindings differ.
    /// \param RHS Other binding to compare.
    /// \return True if the bindings differ.
    bool operator!=(const ResourceBinding &RHS) const {
      return !(*this == RHS);
    }
    /// Order bindings by ID, space, lower bound, then size.
    ///
    /// A size of 0 indicates unbounded. Accounting for when the size is 0
    /// guarantees well-ordered results.
    /// \param RHS Other binding to compare.
    /// \return True if this binding is ordered before \p RHS.
    bool operator<(const ResourceBinding &RHS) const {
      // a size of 0 indicates unbounded. Accounting for when the size is 0
      // guarantees a well ordered results.
      const bool LHSIsUnbounded = Size == 0;
      const bool RHSIsUnbounded = RHS.Size == 0;
      return std::tie(BindingID, Space, LowerBound, LHSIsUnbounded, Size) <
             std::tie(RHS.BindingID, RHS.Space, RHS.LowerBound, RHSIsUnbounded,
                      RHS.Size);
    }
    /// Return true if this binding's register range overlaps \p RHS.
    /// \param RHS Other binding to test for overlap.
    /// \return True if this binding's register range overlaps \p RHS.
    bool overlapsWith(const ResourceBinding &RHS) const {
      if (Space != RHS.Space)
        return false;
      if (Size == 0)
        return LowerBound < RHS.LowerBound;
      return LowerBound + Size - 1 >= RHS.LowerBound;
    }
  };

private:
  std::variant<ResourceBinding, uint32_t> BindingOrHeapID;
  TargetExtType *HandleTy;
  StringRef Name;
  GlobalVariable *Symbol = nullptr;

public:
  /// Whether accesses to this resource are globally coherent.
  bool GloballyCoherent = false;
  /// Observed direction of counter updates for this resource.
  ResourceCounterDirection CounterDirection = ResourceCounterDirection::Unknown;
  /// Whether this resource is used by 64-bit atomics.
  bool HasAtomic64Use = false;

  /// Construct a resource with an explicit register binding.
  /// \param Space Register space.
  /// \param LowerBound Lower bound of the register range.
  /// \param Size Number of registers; zero means unbounded.
  /// \param HandleTy Target extension type of the resource handle.
  /// \param Name Optional resource name.
  /// \param Symbol Optional global variable symbol for the resource.
  ResourceInfo(uint32_t Space, uint32_t LowerBound, uint32_t Size,
               TargetExtType *HandleTy, StringRef Name = "",
               GlobalVariable *Symbol = nullptr)
      : BindingOrHeapID{ResourceBinding{0, Space, LowerBound, Size}},
        HandleTy(HandleTy), Name(Name), Symbol(Symbol) {}

  /// Construct a resource identified by a heap resource ID.
  /// \param HeapResourceID Index of the resource within the descriptor heap.
  /// \param HandleTy Target extension type of the resource handle.
  ResourceInfo(uint32_t HeapResourceID, TargetExtType *HandleTy)
      : BindingOrHeapID{HeapResourceID}, HandleTy(HandleTy), Name(""),
        Symbol(nullptr) {}

  /// Return true if this resource has an explicit register binding.
  /// \return True if this resource has an explicit register binding.
  bool hasBinding() const {
    return std::holds_alternative<ResourceBinding>(BindingOrHeapID);
  }
  /// Set the binding ID for a resource with an explicit binding.
  /// \param ID Binding ID to assign.
  void setBindingID(unsigned ID) {
    assert(hasBinding() && "Resource does not have a binding");
    std::get<ResourceBinding>(BindingOrHeapID).BindingID = ID;
  }

  /// Return true if a counter direction has been observed.
  /// \return True if a counter direction has been observed.
  bool hasCounter() const {
    return CounterDirection != ResourceCounterDirection::Unknown;
  }

  /// Return the explicit register binding; requires \c hasBinding().
  /// \return The explicit register binding.
  const ResourceBinding &getBinding() const {
    assert(hasBinding() && "Resource does not have a binding");
    return std::get<ResourceBinding>(BindingOrHeapID);
  }

  /// Return the descriptor-heap ID; requires that there is no binding.
  /// \return The descriptor-heap ID.
  uint32_t getHeapID() const {
    assert(!hasBinding() && "Resource does not have a heap ID");
    return std::get<uint32_t>(BindingOrHeapID);
  }

  /// Return the binding size, or 1 for heap-identified resources.
  /// \return The binding size, or 1 for heap-identified resources.
  uint32_t getSize() const {
    return hasBinding() ? std::get<ResourceBinding>(BindingOrHeapID).Size : 1;
  }

  /// Return the target extension type of the resource handle.
  /// \return The target extension type of the resource handle.
  TargetExtType *getHandleTy() const { return HandleTy; }
  /// Return the resource name.
  /// \return The resource name.
  StringRef getName() const { return Name; }

  /// Return true if a global symbol has been created for this resource.
  /// \return True if a global symbol has been created for this resource.
  bool hasSymbol() const { return Symbol; }
  /// Create and attach a global variable symbol for this resource.
  /// \param M Module in which to create the symbol.
  /// \param Ty Struct type of the symbol.
  /// \return The created global variable symbol.
  LLVM_ABI GlobalVariable *createSymbol(Module &M, StructType *Ty);
  /// Build DXIL resource metadata for this resource.
  /// \param M Module owning the metadata.
  /// \param RTI Type info for this resource.
  /// \return DXIL resource metadata for this resource.
  LLVM_ABI MDTuple *getAsMetadata(Module &M, dxil::ResourceTypeInfo &RTI) const;

  /// Return the annotate-handle property pair for this resource.
  /// \param M Module used when computing properties.
  /// \param RTI Type info for this resource.
  /// \return The annotate-handle property pair for this resource.
  LLVM_ABI std::pair<uint32_t, uint32_t>
  getAnnotateProps(Module &M, dxil::ResourceTypeInfo &RTI) const;

  /// Return true if both resource infos are equal.
  /// \param RHS Other resource info to compare.
  /// \return True if both resource infos are equal.
  bool operator==(const ResourceInfo &RHS) const {
    return std::tie(BindingOrHeapID, HandleTy, Symbol, Name) ==
           std::tie(RHS.BindingOrHeapID, RHS.HandleTy, RHS.Symbol, RHS.Name);
  }
  /// Return true if the resource infos differ.
  /// \param RHS Other resource info to compare.
  /// \return True if the resource infos differ.
  bool operator!=(const ResourceInfo &RHS) const { return !(*this == RHS); }
  /// Order resource infos by binding or heap ID.
  /// \param RHS Other resource info to compare.
  /// \return True if this resource info is ordered before \p RHS.
  bool operator<(const ResourceInfo &RHS) const {
    return BindingOrHeapID < RHS.BindingOrHeapID;
  }

  /// Print a human-readable description of this resource.
  /// \param OS Stream to print to.
  /// \param RTI Type info for this resource.
  /// \param DL Data layout used when formatting sizes.
  LLVM_ABI void print(raw_ostream &OS, dxil::ResourceTypeInfo &RTI,
                      const DataLayout &DL) const;
};

} // namespace dxil

//===----------------------------------------------------------------------===//

/// Lazy map from resource handle types to \c ResourceTypeInfo.
class DXILResourceTypeMap {
  DenseMap<TargetExtType *, dxil::ResourceTypeInfo> Infos;

public:
  /// Invalidate cached type infos when the module analysis is cleared.
  /// \param M Module whose analysis may be invalidated.
  /// \param PA Set of preserved analyses.
  /// \param Inv Invalidator for dependent analyses.
  /// \return True if this analysis should be invalidated.
  LLVM_ABI bool invalidate(Module &M, const PreservedAnalyses &PA,
                           ModuleAnalysisManager::Invalidator &Inv);

  /// Return the type info for \p Ty, creating it on first access.
  /// \param Ty Resource handle target extension type.
  /// \return The type info for \p Ty.
  dxil::ResourceTypeInfo &operator[](TargetExtType *Ty) {
    auto It = Infos.find(Ty);
    if (It != Infos.end())
      return It->second;
    auto [NewIt, Inserted] = Infos.try_emplace(Ty, Ty);
    return NewIt->second;
  }
};

/// Analysis that provides a \c DXILResourceTypeMap for a module.
class DXILResourceTypeAnalysis
    : public AnalysisInfoMixin<DXILResourceTypeAnalysis> {
  friend AnalysisInfoMixin<DXILResourceTypeAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Result type of this analysis.
  using Result = DXILResourceTypeMap;

  /// Run the analysis over module \p M.
  ///
  /// Running the pass just generates an empty map, which will be filled when
  /// users of the pass query the results.
  /// \param M Module to analyze.
  /// \param AM Module analysis manager (unused).
  /// \return An empty resource type map to be filled on query.
  DXILResourceTypeMap run(Module &M, ModuleAnalysisManager &AM) {
    // Running the pass just generates an empty map, which will be filled when
    // users of the pass query the results.
    return Result();
  }
};

/// Legacy immutable pass wrapping a \c DXILResourceTypeMap.
class LLVM_ABI DXILResourceTypeWrapperPass : public ImmutablePass {
  DXILResourceTypeMap DRTM;

  virtual void anchor();

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the DXIL resource type wrapper pass.
  DXILResourceTypeWrapperPass();

  /// Return the mutable resource type map.
  /// \return The mutable resource type map.
  DXILResourceTypeMap &getResourceTypeMap() { return DRTM; }
  /// Return the resource type map.
  /// \return The resource type map.
  const DXILResourceTypeMap &getResourceTypeMap() const { return DRTM; }
};

/// Create a legacy pass that provides \c DXILResourceTypeMap.
/// \return The newly created module pass.
LLVM_ABI ModulePass *createDXILResourceTypeWrapperPassPass();

//===----------------------------------------------------------------------===//

/// Map of DXIL resources discovered in a module.
class DXILResourceMap {
  using CallMapTy = DenseMap<CallInst *, unsigned>;

  SmallVector<dxil::ResourceInfo> Infos;
  CallMapTy CallMap;
  unsigned FirstUAV = 0;
  unsigned FirstCBuffer = 0;
  unsigned FirstSampler = 0;
  bool HasInvalidDirection = false;

  /// Populate all the resource instance data.
  void populate(Module &M, DXILResourceTypeMap &DRTM);
  /// Populate the map given the resource binding calls in the given module.
  void populateResourceInfos(Module &M, DXILResourceTypeMap &DRTM);
  /// Analyze uses to fill in per-resource dynamic state — counter directions
  /// and 64-bit atomic use.
  void populateFromInstructions(Module &M);
  void populateAtomicUses(Instruction &I);
  void populateRecordCounterDirection(Instruction &I);

  /// Resolves a resource handle into a vector of ResourceInfos that
  /// represent the possible unique creations of the handle. Certain cases are
  /// ambiguous so multiple creation instructions may be returned. The resulting
  /// ResourceInfo can be used to depuplicate unique handles that
  /// reference the same resource
  SmallVector<dxil::ResourceInfo *> findByUse(const Value *Key);

public:
  /// Iterator over resource infos.
  using iterator = SmallVector<dxil::ResourceInfo>::iterator;
  /// Const iterator over resource infos.
  using const_iterator = SmallVector<dxil::ResourceInfo>::const_iterator;

  /// Return an iterator to the first resource.
  /// \return An iterator to the first resource.
  iterator begin() { return Infos.begin(); }
  /// Return a const iterator to the first resource.
  /// \return A const iterator to the first resource.
  const_iterator begin() const { return Infos.begin(); }
  /// Return an iterator past the last resource.
  /// \return An iterator past the last resource.
  iterator end() { return Infos.end(); }
  /// Return a const iterator past the last resource.
  /// \return A const iterator past the last resource.
  const_iterator end() const { return Infos.end(); }

  /// Return true if no resources are present.
  /// \return True if no resources are present.
  bool empty() const { return Infos.empty(); }

  /// Find the resource created by binding call \p Key.
  /// \param Key Handle-from-binding call to look up.
  /// \return Iterator to the resource, or \c end() if not found.
  iterator find(const CallInst *Key) {
    auto Pos = CallMap.find(Key);
    return Pos == CallMap.end() ? Infos.end() : (Infos.begin() + Pos->second);
  }

  /// Find the resource created by binding call \p Key.
  /// \param Key Handle-from-binding call to look up.
  /// \return Const iterator to the resource, or \c end() if not found.
  const_iterator find(const CallInst *Key) const {
    auto Pos = CallMap.find(Key);
    return Pos == CallMap.end() ? Infos.end() : (Infos.begin() + Pos->second);
  }

  /// Return an iterator to the first SRV resource.
  /// \return An iterator to the first SRV resource.
  iterator srv_begin() { return begin(); }
  /// Return a const iterator to the first SRV resource.
  /// \return A const iterator to the first SRV resource.
  const_iterator srv_begin() const { return begin(); }
  /// Return an iterator past the last SRV resource.
  /// \return An iterator past the last SRV resource.
  iterator srv_end() { return begin() + FirstUAV; }
  /// Return a const iterator past the last SRV resource.
  /// \return A const iterator past the last SRV resource.
  const_iterator srv_end() const { return begin() + FirstUAV; }
  /// Return the range of SRV resources.
  /// \return The range of SRV resources.
  iterator_range<iterator> srvs() { return make_range(srv_begin(), srv_end()); }
  /// Return the const range of SRV resources.
  /// \return The const range of SRV resources.
  iterator_range<const_iterator> srvs() const {
    return make_range(srv_begin(), srv_end());
  }

  /// Return an iterator to the first UAV resource.
  /// \return An iterator to the first UAV resource.
  iterator uav_begin() { return begin() + FirstUAV; }
  /// Return a const iterator to the first UAV resource.
  /// \return A const iterator to the first UAV resource.
  const_iterator uav_begin() const { return begin() + FirstUAV; }
  /// Return an iterator past the last UAV resource.
  /// \return An iterator past the last UAV resource.
  iterator uav_end() { return begin() + FirstCBuffer; }
  /// Return a const iterator past the last UAV resource.
  /// \return A const iterator past the last UAV resource.
  const_iterator uav_end() const { return begin() + FirstCBuffer; }
  /// Return the range of UAV resources.
  /// \return The range of UAV resources.
  iterator_range<iterator> uavs() { return make_range(uav_begin(), uav_end()); }
  /// Return the const range of UAV resources.
  /// \return The const range of UAV resources.
  iterator_range<const_iterator> uavs() const {
    return make_range(uav_begin(), uav_end());
  }

  /// Return an iterator to the first CBuffer resource.
  /// \return An iterator to the first CBuffer resource.
  iterator cbuffer_begin() { return begin() + FirstCBuffer; }
  /// Return a const iterator to the first CBuffer resource.
  /// \return A const iterator to the first CBuffer resource.
  const_iterator cbuffer_begin() const { return begin() + FirstCBuffer; }
  /// Return an iterator past the last CBuffer resource.
  /// \return An iterator past the last CBuffer resource.
  iterator cbuffer_end() { return begin() + FirstSampler; }
  /// Return a const iterator past the last CBuffer resource.
  /// \return A const iterator past the last CBuffer resource.
  const_iterator cbuffer_end() const { return begin() + FirstSampler; }
  /// Return the range of CBuffer resources.
  /// \return The range of CBuffer resources.
  iterator_range<iterator> cbuffers() {
    return make_range(cbuffer_begin(), cbuffer_end());
  }
  /// Return the const range of CBuffer resources.
  /// \return The const range of CBuffer resources.
  iterator_range<const_iterator> cbuffers() const {
    return make_range(cbuffer_begin(), cbuffer_end());
  }

  /// Return an iterator to the first sampler resource.
  /// \return An iterator to the first sampler resource.
  iterator sampler_begin() { return begin() + FirstSampler; }
  /// Return a const iterator to the first sampler resource.
  /// \return A const iterator to the first sampler resource.
  const_iterator sampler_begin() const { return begin() + FirstSampler; }
  /// Return an iterator past the last sampler resource.
  /// \return An iterator past the last sampler resource.
  iterator sampler_end() { return end(); }
  /// Return a const iterator past the last sampler resource.
  /// \return A const iterator past the last sampler resource.
  const_iterator sampler_end() const { return end(); }
  /// Return the range of sampler resources.
  /// \return The range of sampler resources.
  iterator_range<iterator> samplers() {
    return make_range(sampler_begin(), sampler_end());
  }
  /// Return the const range of sampler resources.
  /// \return The const range of sampler resources.
  iterator_range<const_iterator> samplers() const {
    return make_range(sampler_begin(), sampler_end());
  }

  /// Iterator over binding-call instructions that create resources.
  struct call_iterator
      : iterator_adaptor_base<call_iterator, CallMapTy::iterator> {
    /// Construct an end/default call iterator.
    call_iterator() = default;
    /// Construct a call iterator from an underlying map iterator.
    /// \param Iter Underlying call-map iterator.
    call_iterator(CallMapTy::iterator Iter)
        : call_iterator::iterator_adaptor_base(std::move(Iter)) {}

    /// Return the binding call at this iterator position.
    /// \return The binding call at this iterator position.
    CallInst *operator*() const { return I->first; }
  };

  /// Return an iterator to the first binding call.
  /// \return An iterator to the first binding call.
  call_iterator call_begin() { return call_iterator(CallMap.begin()); }
  /// Return an iterator past the last binding call.
  /// \return An iterator past the last binding call.
  call_iterator call_end() { return call_iterator(CallMap.end()); }
  /// Return the range of binding-call instructions.
  /// \return The range of binding-call instructions.
  iterator_range<call_iterator> calls() {
    return make_range(call_begin(), call_end());
  }

  /// Return true if any resource has a conflicting counter direction.
  /// \return True if any resource has a conflicting counter direction.
  bool hasInvalidCounterDirection() const { return HasInvalidDirection; }

  /// Print all resources in this map.
  /// \param OS Stream to print to.
  /// \param DRTM Type map used to format resource types.
  /// \param DL Data layout used when formatting sizes.
  LLVM_ABI void print(raw_ostream &OS, DXILResourceTypeMap &DRTM,
                      const DataLayout &DL) const;

  friend class DXILResourceAnalysis;
  friend class DXILResourceWrapperPass;
};

/// Analysis that builds a \c DXILResourceMap for a module.
class DXILResourceAnalysis : public AnalysisInfoMixin<DXILResourceAnalysis> {
  friend AnalysisInfoMixin<DXILResourceAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Result type of this analysis.
  using Result = DXILResourceMap;

  /// Gather resource info for the module \c M.
  /// \param M Module to analyze.
  /// \param AM Module analysis manager providing type-map results.
  /// \return The DXIL resource map for the module.
  LLVM_ABI DXILResourceMap run(Module &M, ModuleAnalysisManager &AM);
};

/// Printer pass for the \c DXILResourceAnalysis results.
class DXILResourcePrinterPass
    : public RequiredPassInfoMixin<DXILResourcePrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// \param OS Stream that receives the printed resource map.
  explicit DXILResourcePrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print the DXIL resource map for module \p M.
  /// \param M Module whose resources are printed.
  /// \param AM Module analysis manager providing the resource map.
  /// \return All analyses are preserved.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Legacy module pass wrapping a \c DXILResourceMap.
class LLVM_ABI DXILResourceWrapperPass : public ModulePass {
  std::unique_ptr<DXILResourceMap> Map;
  DXILResourceTypeMap *DRTM;

public:
  /// Pass identification, replacement for typeid.
  static char ID; // Class identification, replacement for typeinfo

  /// Construct the DXIL resource wrapper pass.
  DXILResourceWrapperPass();
  /// Destroy the DXIL resource wrapper pass.
  ~DXILResourceWrapperPass() override;

  /// Return the computed resource map.
  /// \return The computed resource map.
  const DXILResourceMap &getResourceMap() const { return *Map; }
  /// Return the mutable computed resource map.
  /// \return The mutable computed resource map.
  DXILResourceMap &getResourceMap() { return *Map; }

  /// Declare required and preserved analyses for this pass.
  /// \param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Build the resource map for module \p M.
  /// \param M Module to analyze.
  /// \return False; this analysis does not modify the module.
  bool runOnModule(Module &M) override;
  /// Release the computed resource map.
  void releaseMemory() override;

  /// Print the resource map for module \p M.
  /// \param OS Stream to print to.
  /// \param M Optional module providing the data layout.
  void print(raw_ostream &OS, const Module *M) const override;
  /// Dump the resource map to stderr.
  void dump() const;
};

/// Create a legacy pass that computes \c DXILResourceMap.
/// \return The newly created module pass.
LLVM_ABI ModulePass *createDXILResourceWrapperPassPass();

//===----------------------------------------------------------------------===//

/// Binding-slot occupancy for DXIL resources in a module.
///
/// Stores the results of \c DXILResourceBindingAnalysis, which analyses all
/// \c llvm.dx.resource.handlefrombinding calls in the module and puts together
/// lists of used virtual register spaces and available virtual register slot
/// ranges for each binding type. It also stores additional information found
/// during the analysis such as whether the module uses implicit bindings or if
/// any of the bindings overlap.
///
/// This information will be used in DXILResourceImplicitBindings pass to
/// assign register slots to resources with implicit bindings, and in a
/// post-optimization validation pass that will raise diagnostic about
/// overlapping bindings.
class DXILResourceBindingInfo {
  hlsl::BindingInfo Bindings;
  bool HasImplicitBinding = false;
  bool HasOverlappingBinding = false;

  // Populate the resource binding info given explicit resource binding calls
  // in the module.
  void populate(Module &M, DXILResourceTypeMap &DRTM);

public:
  /// Return true if the module uses implicit bindings.
  /// \return True if the module uses implicit bindings.
  bool hasImplicitBinding() const { return HasImplicitBinding; }
  /// Record whether the module uses implicit bindings.
  /// \param Value True if an implicit binding was observed.
  void setHasImplicitBinding(bool Value) { HasImplicitBinding = Value; }
  /// Return true if any explicit bindings overlap.
  /// \return True if any explicit bindings overlap.
  bool hasOverlappingBinding() const { return HasOverlappingBinding; }
  /// Record whether any explicit bindings overlap.
  /// \param Value True if an overlapping binding was observed.
  void setHasOverlappingBinding(bool Value) { HasOverlappingBinding = Value; }

  /// Find an unused register slot range for a resource binding.
  /// \param RC Resource class to allocate in.
  /// \param Space Register space to search.
  /// \param Size Number of registers needed; negative means unbounded.
  /// \return Lower bound of an available range, or \c std::nullopt.
  std::optional<uint32_t> findAvailableBinding(dxil::ResourceClass RC,
                                               uint32_t Space, int32_t Size) {
    return Bindings.findAvailableBinding(RC, Space, Size);
  }

  friend class DXILResourceBindingAnalysis;
  friend class DXILResourceBindingWrapperPass;
};

/// Analysis that builds a \c DXILResourceBindingInfo for a module.
class DXILResourceBindingAnalysis
    : public AnalysisInfoMixin<DXILResourceBindingAnalysis> {
  friend AnalysisInfoMixin<DXILResourceBindingAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Result type of this analysis.
  using Result = DXILResourceBindingInfo;

  /// Analyze explicit resource bindings in module \p M.
  /// \param M Module to analyze.
  /// \param AM Module analysis manager providing type-map results.
  /// \return The DXIL resource binding info for the module.
  LLVM_ABI DXILResourceBindingInfo run(Module &M, ModuleAnalysisManager &AM);
};

/// Legacy module pass wrapping a \c DXILResourceBindingInfo.
class LLVM_ABI DXILResourceBindingWrapperPass : public ModulePass {
  std::unique_ptr<DXILResourceBindingInfo> BindingInfo;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the DXIL resource binding wrapper pass.
  DXILResourceBindingWrapperPass();
  /// Destroy the DXIL resource binding wrapper pass.
  ~DXILResourceBindingWrapperPass() override;

  /// Return the mutable binding info.
  /// \return The mutable binding info.
  DXILResourceBindingInfo &getBindingInfo() { return *BindingInfo; }
  /// Return the binding info.
  /// \return The binding info.
  const DXILResourceBindingInfo &getBindingInfo() const { return *BindingInfo; }

  /// Declare required and preserved analyses for this pass.
  /// \param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Build the binding info for module \p M.
  /// \param M Module to analyze.
  /// \return False; this analysis does not modify the module.
  bool runOnModule(Module &M) override;
  /// Release the computed binding info.
  void releaseMemory() override;
};

/// Create a legacy pass that computes \c DXILResourceBindingInfo.
/// \return The newly created module pass.
LLVM_ABI ModulePass *createDXILResourceBindingWrapperPassPass();

} // namespace llvm

#endif // LLVM_ANALYSIS_DXILRESOURCE_H
