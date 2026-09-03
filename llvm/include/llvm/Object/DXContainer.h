//===- DXContainer.h - DXContainer file implementation ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the DXContainerFile class, which implements the ObjectFile
// interface for DXContainer files.
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_DXCONTAINER_H
#define LLVM_OBJECT_DXCONTAINER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/MC/DXContainerInfo.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/TargetParser/Triple.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace llvm {
namespace object {

namespace detail {
template <typename T>
std::enable_if_t<std::is_arithmetic<T>::value, void> swapBytes(T &value) {
  sys::swapByteOrder(value);
}

template <typename T>
std::enable_if_t<std::is_class<T>::value, void> swapBytes(T &value) {
  value.swapBytes();
}
} // namespace detail

/// View over a little-endian array of \tparam T that may be misaligned.
///
/// The underlying resource data is little-endian encoded and may not be
/// properly aligned to read directly from. The dereference operator creates a
/// copy of the data and byte-swaps it as appropriate.
template <typename T> struct ViewArray {
  /// Contiguous bytes that hold the array elements.
  StringRef Data;
  /// Size in bytes of each element in the list.
  uint32_t Stride = sizeof(T);

  /// Construct an empty view.
  ViewArray() = default;
  /// Construct a view over \p D with element stride \p S.
  ///
  /// \param D Bytes that hold the array elements.
  /// \param S Size in bytes of each element.
  ViewArray(StringRef D, size_t S) : Data(D), Stride(S) {}

  /// Element type stored in this view.
  using value_type = T;
  /// Maximum stride that can be copied into a \c value_type.
  ///
  /// \returns The size in bytes of \c value_type.
  static constexpr uint32_t MaxStride() {
    return static_cast<uint32_t>(sizeof(value_type));
  }

  /// Iterator that copies and byte-swaps each element on dereference.
  struct iterator {
    /// Contiguous bytes that hold the array elements.
    StringRef Data;
    /// Size in bytes of each element in the list.
    uint32_t Stride;
    /// Pointer to the current element's first byte within \c Data.
    const char *Current;

    /// Construct an iterator into view \p A at byte position \p C.
    ///
    /// \param A View whose data and stride are copied into this iterator.
    /// \param C Byte position within \p A that this iterator refers to.
    iterator(const ViewArray &A, const char *C)
        : Data(A.Data), Stride(A.Stride), Current(C) {}
    /// Copy-construct an iterator from \p Other.
    ///
    /// \param Other Iterator to copy.
    iterator(const iterator &Other) = default;

    /// Copy of the element at the current position, byte-swapped if needed.
    ///
    /// Explicitly zeros the structure so unused fields are zero. It is up to
    /// the user to know if the fields are used by verifying the PSV version.
    ///
    /// \returns A byte-swapped copy of the current element, or a zeroed value
    ///          if the iterator is past the end.
    value_type operator*() {
      value_type Val;
      std::memset(&Val, 0, sizeof(value_type));
      if (Current >= Data.end())
        return Val;
      memcpy(static_cast<void *>(&Val), Current, std::min(Stride, MaxStride()));
      if (sys::IsBigEndianHost)
        detail::swapBytes(Val);
      return Val;
    }

    /// Advance this iterator to the next element and return it.
    ///
    /// \returns This iterator after advancing.
    iterator operator++() {
      if (Current < Data.end())
        Current += Stride;
      return *this;
    }

    /// Post-increment this iterator.
    ///
    /// \param Unused Dummy parameter distinguishing postfix from prefix.
    /// \returns A copy of the iterator before advancing.
    iterator operator++([[maybe_unused]] int Unused) {
      iterator Tmp = *this;
      ++*this;
      return Tmp;
    }

    /// Move this iterator to the previous element and return it.
    ///
    /// \returns This iterator after moving backward.
    iterator operator--() {
      if (Current > Data.begin())
        Current -= Stride;
      return *this;
    }

    /// Post-decrement this iterator.
    ///
    /// \param Unused Dummy parameter distinguishing postfix from prefix.
    /// \returns A copy of the iterator before moving backward.
    iterator operator--([[maybe_unused]] int Unused) {
      iterator Tmp = *this;
      --*this;
      return Tmp;
    }

    /// True if this iterator and \p I refer to the same position.
    ///
    /// \param I Other iterator to compare against.
    /// \returns True if both iterators refer to the same position.
    bool operator==(const iterator I) { return I.Current == Current; }
    /// True if this iterator and \p I refer to different positions.
    ///
    /// \param I Other iterator to compare against.
    /// \returns True if the iterators refer to different positions.
    bool operator!=(const iterator I) { return !(*this == I); }
  };

  /// Iterator to the first element in this view.
  ///
  /// \returns An iterator to the first element.
  iterator begin() const { return iterator(*this, Data.begin()); }

  /// Past-the-end iterator for this view.
  ///
  /// \returns An iterator one past the last element.
  iterator end() const { return iterator(*this, Data.end()); }

  /// Number of elements in this view.
  ///
  /// \returns The element count.
  size_t size() const { return Data.size() / Stride; }

  /// True if this view has no bytes.
  ///
  /// \returns True if the view is empty.
  bool isEmpty() const { return Data.empty(); }
};

/// DirectX container part parsers and views (root signature, PSV, signatures).
namespace DirectX {
/// View of a single root-signature parameter header and its payload bytes.
struct RootParameterView {
  /// Parameter header describing type, visibility, and payload offset.
  const dxbc::RTS0::v1::RootParameterHeader &Header;
  /// Raw little-endian payload bytes for this parameter.
  StringRef ParamData;

  /// Construct a view of header \p H with payload bytes \p P.
  ///
  /// \param H Root-parameter header to wrap.
  /// \param P Payload bytes for this parameter.
  RootParameterView(const dxbc::RTS0::v1::RootParameterHeader &H, StringRef P)
      : Header(H), ParamData(P) {}

  /// Read the parameter payload as structure type \tparam T.
  ///
  /// \returns The decoded structure, or an error if the payload size does not
  ///          match \c sizeof(T).
  template <typename T> Expected<T> readParameter() {
    T Struct;
    if (sizeof(T) != ParamData.size())
      return make_error<GenericBinaryError>(
          "Reading structure out of file bounds", object_error::parse_failed);

    memcpy(&Struct, ParamData.data(), sizeof(T));
    // DXContainer is always little endian
    if (sys::IsBigEndianHost)
      Struct.swapBytes();
    return Struct;
  }
};

/// Root-parameter view for 32-bit root constants.
struct RootConstantView : RootParameterView {
  /// True if \p V describes a Constants32Bit root parameter.
  ///
  /// \param V Root-parameter view to test.
  /// \returns True if \p V is a root-constants parameter.
  static bool classof(const RootParameterView *V) {
    return V->Header.ParameterType ==
           (uint32_t)dxbc::RootParameterType::Constants32Bit;
  }

  /// Decode the root-constants payload.
  ///
  /// \returns The decoded root constants, or an error on failure.
  llvm::Expected<dxbc::RTS0::v1::RootConstants> read() {
    return readParameter<dxbc::RTS0::v1::RootConstants>();
  }
};

/// Root-parameter view for a CBV, SRV, or UAV root descriptor.
struct RootDescriptorView : RootParameterView {
  /// True if \p V describes a CBV, SRV, or UAV root descriptor.
  ///
  /// \param V Root-parameter view to test.
  /// \returns True if \p V is a CBV, SRV, or UAV root descriptor.
  static bool classof(const RootParameterView *V) {
    return (V->Header.ParameterType ==
                llvm::to_underlying(dxbc::RootParameterType::CBV) ||
            V->Header.ParameterType ==
                llvm::to_underlying(dxbc::RootParameterType::SRV) ||
            V->Header.ParameterType ==
                llvm::to_underlying(dxbc::RootParameterType::UAV));
  }

  /// Decode the root-descriptor payload for root-signature \p Version.
  ///
  /// \param Version Root-signature version (1 or 2).
  /// \returns The decoded root descriptor, or an error on failure.
  llvm::Expected<dxbc::RTS0::v2::RootDescriptor> read(uint32_t Version) {
    if (Version == 1) {
      auto Descriptor = readParameter<dxbc::RTS0::v1::RootDescriptor>();
      if (Error E = Descriptor.takeError())
        return E;
      return dxbc::RTS0::v2::RootDescriptor(*Descriptor);
    }
    if (Version != 2)
      return make_error<GenericBinaryError>("Invalid Root Signature version: " +
                                                Twine(Version),
                                            object_error::parse_failed);
    return readParameter<dxbc::RTS0::v2::RootDescriptor>();
  }
};
/// Descriptor table holding a count, offset, and view of descriptor ranges.
template <typename T> struct DescriptorTable {
  /// Number of descriptor ranges in this table.
  uint32_t NumRanges;
  /// Byte offset of the ranges array within the root-signature part.
  uint32_t RangesOffset;
  /// View of the descriptor-range entries.
  ViewArray<T> Ranges;

  /// Iterator to the first descriptor range.
  ///
  /// \returns An iterator to the first range.
  typename ViewArray<T>::iterator begin() const { return Ranges.begin(); }

  /// Past-the-end iterator for descriptor ranges.
  ///
  /// \returns An iterator one past the last range.
  typename ViewArray<T>::iterator end() const { return Ranges.end(); }
};

/// Root-parameter view for a descriptor table.
struct DescriptorTableView : RootParameterView {
  /// True if \p V describes a descriptor-table root parameter.
  ///
  /// \param V Root-parameter view to test.
  /// \returns True if \p V is a descriptor-table parameter.
  static bool classof(const RootParameterView *V) {
    return (V->Header.ParameterType ==
            llvm::to_underlying(dxbc::RootParameterType::DescriptorTable));
  }

  /// Decode this parameter as a descriptor table of range type \tparam T.
  ///
  /// \returns The decoded descriptor table, or an error on failure.
  template <typename T> llvm::Expected<DescriptorTable<T>> read() {
    const char *Current = ParamData.begin();
    DescriptorTable<T> Table;

    Table.NumRanges =
        support::endian::read<uint32_t, llvm::endianness::little>(Current);
    Current += sizeof(uint32_t);

    Table.RangesOffset =
        support::endian::read<uint32_t, llvm::endianness::little>(Current);
    Current += sizeof(uint32_t);

    Table.Ranges.Data = ParamData.substr(2 * sizeof(uint32_t),
                                         Table.NumRanges * Table.Ranges.Stride);
    return Table;
  }
};

static Error parseFailed(const Twine &Msg) {
  return make_error<GenericBinaryError>(Msg.str(), object_error::parse_failed);
}

/// Parsed DirectX root-signature (RTS0) part.
class RootSignature {
private:
  uint32_t Version;
  uint32_t NumParameters;
  uint32_t RootParametersOffset;
  uint32_t NumStaticSamplers;
  uint32_t StaticSamplersOffset;
  uint32_t Flags;
  ViewArray<dxbc::RTS0::v1::RootParameterHeader> ParametersHeaders;
  StringRef PartData;
  ViewArray<dxbc::RTS0::v3::StaticSampler> StaticSamplers;

  using param_header_iterator =
      ViewArray<dxbc::RTS0::v1::RootParameterHeader>::iterator;
  using samplers_iterator = ViewArray<dxbc::RTS0::v3::StaticSampler>::iterator;

public:
  /// Construct a root signature over part bytes \p PD.
  ///
  /// \param PD Raw RTS0 part payload.
  RootSignature(StringRef PD) : PartData(PD) {}

  /// Parse the root-signature header, parameters, and static samplers.
  ///
  /// \returns Success, or an error if parsing failed.
  LLVM_ABI Error parse();
  /// Root-signature format version.
  ///
  /// \returns The root-signature version.
  uint32_t getVersion() const { return Version; }
  /// Number of root parameters declared in the header.
  ///
  /// \returns The root-parameter count from the header.
  uint32_t getNumParameters() const { return NumParameters; }
  /// Byte offset of the root-parameter headers within the part.
  ///
  /// \returns The root-parameter headers offset in bytes.
  uint32_t getRootParametersOffset() const { return RootParametersOffset; }
  /// Number of static samplers declared in the header.
  ///
  /// \returns The static-sampler count from the header.
  uint32_t getNumStaticSamplers() const { return NumStaticSamplers; }
  /// Byte offset of the static-sampler array within the part.
  ///
  /// \returns The static-sampler array offset in bytes.
  uint32_t getStaticSamplersOffset() const { return StaticSamplersOffset; }
  /// Number of parsed root-parameter headers.
  ///
  /// \returns The number of parsed root-parameter headers.
  uint32_t getNumRootParameters() const { return ParametersHeaders.size(); }
  /// Range over the root-parameter headers.
  ///
  /// \returns An iterator range over the root-parameter headers.
  llvm::iterator_range<param_header_iterator> param_headers() const {
    return ParametersHeaders;
  }
  /// Range over the static samplers.
  ///
  /// \returns An iterator range over the static samplers.
  llvm::iterator_range<samplers_iterator> samplers() const {
    return StaticSamplers;
  }
  /// Root-signature flags bitfield.
  ///
  /// \returns The root-signature flags.
  uint32_t getFlags() const { return Flags; }

  /// Build a parameter view for root-parameter header \p Header.
  ///
  /// \param Header Parameter header whose payload should be viewed.
  /// \returns A view of the parameter payload, or an error on failure.
  llvm::Expected<RootParameterView>
  getParameter(const dxbc::RTS0::v1::RootParameterHeader &Header) const {
    size_t DataSize;
    size_t EndOfSectionByte = getNumStaticSamplers() == 0
                                  ? PartData.size()
                                  : getStaticSamplersOffset();

    if (!dxbc::isValidParameterType(Header.ParameterType))
      return parseFailed("invalid parameter type");

    switch (static_cast<dxbc::RootParameterType>(Header.ParameterType)) {
    case dxbc::RootParameterType::Constants32Bit:
      DataSize = sizeof(dxbc::RTS0::v1::RootConstants);
      break;
    case dxbc::RootParameterType::CBV:
    case dxbc::RootParameterType::SRV:
    case dxbc::RootParameterType::UAV:
      if (Version == 1)
        DataSize = sizeof(dxbc::RTS0::v1::RootDescriptor);
      else
        DataSize = sizeof(dxbc::RTS0::v2::RootDescriptor);
      break;
    case dxbc::RootParameterType::DescriptorTable:
      if (Header.ParameterOffset + sizeof(uint32_t) > EndOfSectionByte)
        return parseFailed("Reading structure out of file bounds");

      uint32_t NumRanges =
          support::endian::read<uint32_t, llvm::endianness::little>(
              PartData.begin() + Header.ParameterOffset);
      if (Version == 1)
        DataSize = sizeof(dxbc::RTS0::v1::DescriptorRange) * NumRanges;
      else
        DataSize = sizeof(dxbc::RTS0::v2::DescriptorRange) * NumRanges;

      // 4 bytes for the number of ranges in table and
      // 4 bytes for the ranges offset
      DataSize += 2 * sizeof(uint32_t);
      break;
    }
    if (Header.ParameterOffset + DataSize > EndOfSectionByte)
      return parseFailed("Reading structure out of file bounds");

    StringRef Buff = PartData.substr(Header.ParameterOffset, DataSize);
    RootParameterView View = RootParameterView(Header, Buff);
    return View;
  }
};

/// Parsed pipeline-state validation (PSV) runtime info part.
class PSVRuntimeInfo {

  using ResourceArray = ViewArray<dxbc::PSV::v2::ResourceBindInfo>;
  using SigElementArray = ViewArray<dxbc::PSV::v0::SignatureElement>;

  StringRef Data;
  uint32_t Size;
  using InfoStruct =
      std::variant<std::monostate, dxbc::PSV::v0::RuntimeInfo,
                   dxbc::PSV::v1::RuntimeInfo, dxbc::PSV::v2::RuntimeInfo,
                   dxbc::PSV::v3::RuntimeInfo>;
  InfoStruct BasicInfo;
  ResourceArray Resources;
  StringRef StringTable;
  SmallVector<uint32_t> SemanticIndexTable;
  SigElementArray SigInputElements;
  SigElementArray SigOutputElements;
  SigElementArray SigPatchOrPrimElements;

  std::array<ViewArray<uint32_t>, 4> OutputVectorMasks;
  ViewArray<uint32_t> PatchOrPrimMasks;
  std::array<ViewArray<uint32_t>, 4> InputOutputMap;
  ViewArray<uint32_t> InputPatchMap;
  ViewArray<uint32_t> PatchOutputMap;

public:
  /// Construct PSV runtime info over part bytes \p D.
  ///
  /// \param D Raw PSV0 part payload.
  PSVRuntimeInfo(StringRef D) : Data(D), Size(0) {}

  /// Parse the PSV part for shader kind \p ShaderKind.
  ///
  /// Parsing depends on the shader kind.
  ///
  /// \param ShaderKind Shader stage that selects PSV layout details.
  /// \returns Success, or an error if parsing failed.
  LLVM_ABI Error parse(uint16_t ShaderKind);

  /// Size in bytes of the runtime-info structure stored in the part.
  ///
  /// \returns The runtime-info structure size in bytes.
  uint32_t getSize() const { return Size; }
  /// Number of resource bind-info entries.
  ///
  /// \returns The resource bind-info count.
  uint32_t getResourceCount() const { return Resources.size(); }
  /// View of the resource bind-info array.
  ///
  /// \returns A view of the resource bind-info entries.
  ResourceArray getResources() const { return Resources; }

  /// PSV runtime-info version inferred from \c Size.
  ///
  /// \returns The PSV version (0 through 3).
  uint32_t getVersion() const {
    return Size >= sizeof(dxbc::PSV::v3::RuntimeInfo)
               ? 3
               : (Size >= sizeof(dxbc::PSV::v2::RuntimeInfo)     ? 2
                  : (Size >= sizeof(dxbc::PSV::v1::RuntimeInfo)) ? 1
                                                                 : 0);
  }

  /// Stride in bytes of each resource bind-info entry.
  ///
  /// \returns The resource bind-info stride in bytes.
  uint32_t getResourceStride() const { return Resources.Stride; }

  /// Variant holding the versioned runtime-info structure.
  ///
  /// \returns The versioned runtime-info variant.
  const InfoStruct &getInfo() const { return BasicInfo; }

  /// Pointer to the runtime info cast as \tparam T, or nullptr if unavailable.
  ///
  /// \returns A pointer to the info as \tparam T, or nullptr if unavailable.
  template <typename T> const T *getInfoAs() const {
    if (const auto *P = std::get_if<dxbc::PSV::v3::RuntimeInfo>(&BasicInfo))
      return static_cast<const T *>(P);
    if (std::is_same<T, dxbc::PSV::v3::RuntimeInfo>::value)
      return nullptr;

    if (const auto *P = std::get_if<dxbc::PSV::v2::RuntimeInfo>(&BasicInfo))
      return static_cast<const T *>(P);
    if (std::is_same<T, dxbc::PSV::v2::RuntimeInfo>::value)
      return nullptr;

    if (const auto *P = std::get_if<dxbc::PSV::v1::RuntimeInfo>(&BasicInfo))
      return static_cast<const T *>(P);
    if (std::is_same<T, dxbc::PSV::v1::RuntimeInfo>::value)
      return nullptr;

    if (const auto *P = std::get_if<dxbc::PSV::v0::RuntimeInfo>(&BasicInfo))
      return static_cast<const T *>(P);
    return nullptr;
  }

  /// String table embedded in the PSV part.
  ///
  /// \returns The PSV string table bytes.
  StringRef getStringTable() const { return StringTable; }
  /// Semantic-index table embedded in the PSV part.
  ///
  /// \returns The semantic-index table.
  ArrayRef<uint32_t> getSemanticIndexTable() const {
    return SemanticIndexTable;
  }

  /// Number of signature input elements.
  ///
  /// \returns The signature input element count.
  LLVM_ABI uint8_t getSigInputCount() const;
  /// Number of signature output elements.
  ///
  /// \returns The signature output element count.
  LLVM_ABI uint8_t getSigOutputCount() const;
  /// Number of signature patch-or-primitive elements.
  ///
  /// \returns The signature patch-or-primitive element count.
  LLVM_ABI uint8_t getSigPatchOrPrimCount() const;

  /// View of the signature input elements.
  ///
  /// \returns A view of the signature input elements.
  SigElementArray getSigInputElements() const { return SigInputElements; }
  /// View of the signature output elements.
  ///
  /// \returns A view of the signature output elements.
  SigElementArray getSigOutputElements() const { return SigOutputElements; }
  /// View of the signature patch-or-primitive elements.
  ///
  /// \returns A view of the signature patch-or-primitive elements.
  SigElementArray getSigPatchOrPrimElements() const {
    return SigPatchOrPrimElements;
  }

  /// Output-vector dependency mask for stream index \p Idx.
  ///
  /// \param Idx Output stream index in \c [0, 4).
  /// \returns The output-vector dependency mask for \p Idx.
  ViewArray<uint32_t> getOutputVectorMasks(size_t Idx) const {
    assert(Idx < 4);
    return OutputVectorMasks[Idx];
  }

  /// Patch-or-primitive vector dependency masks.
  ///
  /// \returns The patch-or-primitive vector dependency masks.
  ViewArray<uint32_t> getPatchOrPrimMasks() const { return PatchOrPrimMasks; }

  /// Input-to-output dependency map for stream index \p Idx.
  ///
  /// \param Idx Output stream index in \c [0, 4).
  /// \returns The input-to-output dependency map for \p Idx.
  ViewArray<uint32_t> getInputOutputMap(size_t Idx) const {
    assert(Idx < 4);
    return InputOutputMap[Idx];
  }

  /// Input-to-patch dependency map.
  ///
  /// \returns The input-to-patch dependency map.
  ViewArray<uint32_t> getInputPatchMap() const { return InputPatchMap; }
  /// Patch-to-output dependency map.
  ///
  /// \returns The patch-to-output dependency map.
  ViewArray<uint32_t> getPatchOutputMap() const { return PatchOutputMap; }

  /// Stride in bytes of each signature element entry.
  ///
  /// \returns The signature element stride in bytes.
  uint32_t getSigElementStride() const { return SigInputElements.Stride; }

  /// True if the shader uses ViewID (PSV v1+).
  ///
  /// \returns True if ViewID is used, or false if unavailable.
  bool usesViewID() const {
    if (const auto *P = getInfoAs<dxbc::PSV::v1::RuntimeInfo>())
      return P->UsesViewID != 0;
    return false;
  }

  /// Number of input vectors (PSV v1+), or zero if unavailable.
  ///
  /// \returns The input vector count, or zero if unavailable.
  uint8_t getInputVectorCount() const {
    if (const auto *P = getInfoAs<dxbc::PSV::v1::RuntimeInfo>())
      return P->SigInputVectors;
    return 0;
  }

  /// Per-stream output vector counts (PSV v1+), or empty if unavailable.
  ///
  /// \returns The per-stream output vector counts, or empty if unavailable.
  ArrayRef<uint8_t> getOutputVectorCounts() const {
    if (const auto *P = getInfoAs<dxbc::PSV::v1::RuntimeInfo>())
      return ArrayRef<uint8_t>(P->SigOutputVectors);
    return ArrayRef<uint8_t>();
  }

  /// Patch-constant or primitive vector count (PSV v1+), or zero if unavailable.
  ///
  /// \returns The patch-constant or primitive vector count, or zero if unavailable.
  uint8_t getPatchConstOrPrimVectorCount() const {
    if (const auto *P = getInfoAs<dxbc::PSV::v1::RuntimeInfo>())
      return P->GeomData.SigPatchConstOrPrimVectors;
    return 0;
  }
};

/// Parsed program signature (ISG1/OSG1/PSG1) part.
class Signature {
  ViewArray<dxbc::ProgramSignatureElement> Parameters;
  uint32_t StringTableOffset;
  StringRef StringTable;

public:
  /// Iterator to the first signature parameter.
  ///
  /// \returns An iterator to the first parameter.
  ViewArray<dxbc::ProgramSignatureElement>::iterator begin() const {
    return Parameters.begin();
  }

  /// Past-the-end iterator for signature parameters.
  ///
  /// \returns An iterator one past the last parameter.
  ViewArray<dxbc::ProgramSignatureElement>::iterator end() const {
    return Parameters.end();
  }

  /// Semantic name at absolute signature offset \p Offset.
  ///
  /// Name offsets are from the start of the signature data, not from the start
  /// of the string table. The header encodes the start offset of the string
  /// table, so this converts the offset before slicing.
  ///
  /// \param Offset Absolute offset of the name within the signature part.
  /// \returns The null-terminated semantic name at \p Offset.
  StringRef getName(uint32_t Offset) const {
    assert(Offset >= StringTableOffset &&
           Offset < StringTableOffset + StringTable.size() &&
           "Offset out of range.");
    uint32_t TableOffset = Offset - StringTableOffset;
    return StringTable.slice(TableOffset, StringTable.find('\0', TableOffset));
  }

  /// True if this signature has no parameters.
  ///
  /// \returns True if the signature has no parameters.
  bool isEmpty() const { return Parameters.isEmpty(); }

  /// Initialize this signature from part payload \p Part.
  ///
  /// \param Part Raw signature part bytes.
  /// \returns Success, or an error if initialization failed.
  LLVM_ABI Error initialize(StringRef Part);
};

} // namespace DirectX

/// Parsed DirectX container (DXBC) object.
class DXContainer {
public:
  /// Program header paired with a pointer to the DXIL bitcode bytes.
  using DXILData = std::pair<dxbc::ProgramHeader, const char *>;

private:
  DXContainer(MemoryBufferRef O);

  MemoryBufferRef Data;
  dxbc::Header Header;
  SmallVector<uint32_t, 4> PartOffsets;
  std::optional<DXILData> DXIL;
  std::optional<DXILData> DebugDXIL;
  std::optional<uint64_t> ShaderFeatureFlags;
  std::optional<dxbc::ShaderHash> Hash;
  std::optional<DirectX::PSVRuntimeInfo> PSVInfo;
  std::optional<DirectX::RootSignature> RootSignature;
  DirectX::Signature InputSignature;
  DirectX::Signature OutputSignature;
  DirectX::Signature PatchConstantSignature;
  std::optional<mcdxbc::DebugName> DebugName;
  std::optional<mcdxbc::CompilerVersion> VersionInfo;
  std::optional<mcdxbc::SourceInfo> SourceInfo;
  std::optional<StringRef> PrivateData;

  Error parseHeader();
  Error parsePartOffsets();
  Error parseDXILHeader(dxbc::PartType PT, StringRef Part);
  Error parseDebugName(StringRef Part);
  Error parseShaderFeatureFlags(StringRef Part);
  Error parseHash(StringRef Part);
  Error parseRootSignature(StringRef Part);
  Error parsePSVInfo(StringRef Part);
  Error parseSignature(StringRef Part, DirectX::Signature &Array);
  Error parseCompilerVersionInfo(StringRef Part);
  Error parseSourceInfo(StringRef Part);
  Error parsePrivateData(StringRef Part);
  /// Iterator over DXContainer parts with a parsed part header and payload.
  friend class PartIterator;

public:
  /// Iterator over DXContainer parts with a parsed part header and payload.
  ///
  /// Wraps the iterator for the PartOffsets member of the DXContainer. It
  /// contains a reference to the container, the current iterator value, and
  /// storage for a parsed part header.
  class PartIterator {
    const DXContainer &Container;
    SmallVectorImpl<uint32_t>::const_iterator OffsetIt;
    struct PartData {
      dxbc::PartHeader Part;
      uint32_t Offset;
      StringRef Data;
    } IteratorState;

    friend class DXContainer;
    friend class DXContainerObjectFile;

    PartIterator(const DXContainer &C,
                 SmallVectorImpl<uint32_t>::const_iterator It)
        : Container(C), OffsetIt(It) {
      if (OffsetIt == Container.PartOffsets.end())
        updateIteratorImpl(Container.PartOffsets.back());
      else
        updateIterator();
    }

    // Updates the iterator's state data. This results in copying the part
    // header into the iterator and handling any required byte swapping. This is
    // called when incrementing or decrementing the iterator.
    void updateIterator() {
      if (OffsetIt != Container.PartOffsets.end())
        updateIteratorImpl(*OffsetIt);
    }

    // Implementation for updating the iterator state based on a specified
    // offest.
    LLVM_ABI void updateIteratorImpl(const uint32_t Offset);

  public:
    /// Advance this iterator to the next part and return it.
    ///
    /// \returns This iterator after advancing.
    PartIterator &operator++() {
      if (OffsetIt == Container.PartOffsets.end())
        return *this;
      ++OffsetIt;
      updateIterator();
      return *this;
    }

    /// Post-increment this iterator.
    ///
    /// \param Unused Dummy parameter distinguishing postfix from prefix.
    /// \returns A copy of the iterator before advancing.
    PartIterator operator++([[maybe_unused]] int Unused) {
      PartIterator Tmp = *this;
      ++(*this);
      return Tmp;
    }

    /// True if this iterator and \p RHS refer to the same part offset.
    ///
    /// \param RHS Other part iterator to compare against.
    /// \returns True if both iterators refer to the same part offset.
    bool operator==(const PartIterator &RHS) const {
      return OffsetIt == RHS.OffsetIt;
    }

    /// True if this iterator and \p RHS refer to different part offsets.
    ///
    /// \param RHS Other part iterator to compare against.
    /// \returns True if the iterators refer to different part offsets.
    bool operator!=(const PartIterator &RHS) const {
      return OffsetIt != RHS.OffsetIt;
    }

    /// Parsed part header, offset, and payload at the current position.
    ///
    /// \returns The current part data.
    const PartData &operator*() { return IteratorState; }
    /// Pointer to the parsed part data at the current position.
    ///
    /// \returns A pointer to the current part data.
    const PartData *operator->() { return &IteratorState; }
  };

  /// Iterator to the first part in this container.
  ///
  /// \returns An iterator to the first part.
  PartIterator begin() const {
    return PartIterator(*this, PartOffsets.begin());
  }

  /// Past-the-end iterator for parts in this container.
  ///
  /// \returns An iterator one past the last part.
  PartIterator end() const { return PartIterator(*this, PartOffsets.end()); }

  /// Raw container bytes.
  ///
  /// \returns The raw DXBC container bytes.
  StringRef getData() const { return Data.getBuffer(); }
  /// Parse a DXContainer from memory buffer \p Object.
  ///
  /// \param Object Memory buffer holding the DXBC binary.
  /// \returns The parsed container, or an error if parsing failed.
  LLVM_ABI static Expected<DXContainer> create(MemoryBufferRef Object);

  /// DXBC container header.
  ///
  /// \returns The DXBC container header.
  const dxbc::Header &getHeader() const { return Header; }

  /// DXIL or debug-DXIL program data, selected by \p Debug.
  ///
  /// \param Debug If true, return the debug DXIL part; otherwise the DXIL part.
  /// \returns The selected DXIL program data, if present.
  const std::optional<DXILData> &getDXIL(bool Debug) const {
    return Debug ? DebugDXIL : DXIL;
  }

  /// Shader kind from the DXIL or debug-DXIL program header, if present.
  ///
  /// \returns The shader kind, or nullopt if no program part is present.
  std::optional<uint16_t> getShaderKind() const {
    const auto &ProgramPart = DXIL ? DXIL : DebugDXIL;
    if (!ProgramPart)
      return std::nullopt;
    return ProgramPart->first.ShaderKind;
  }

  /// Debug name part contents, if present.
  ///
  /// \returns The debug name, if the part is present.
  const std::optional<mcdxbc::DebugName> getDebugName() const {
    return DebugName;
  }

  /// Shader feature flags, if the SFI0 part is present.
  ///
  /// \returns The shader feature flags, if present.
  std::optional<uint64_t> getShaderFeatureFlags() const {
    return ShaderFeatureFlags;
  }

  /// Shader hash, if the HASH part is present.
  ///
  /// \returns The shader hash, if present.
  std::optional<dxbc::ShaderHash> getShaderHash() const { return Hash; }

  /// Parsed root signature, if the RTS0 part is present.
  ///
  /// \returns The parsed root signature, if present.
  std::optional<DirectX::RootSignature> getRootSignature() const {
    return RootSignature;
  }

  /// Parsed PSV runtime info, if the PSV0 part is present.
  ///
  /// \returns The parsed PSV runtime info, if present.
  const std::optional<DirectX::PSVRuntimeInfo> &getPSVInfo() const {
    return PSVInfo;
  };

  /// Input signature (ISG1) part.
  ///
  /// \returns The input signature.
  const DirectX::Signature &getInputSignature() const { return InputSignature; }
  /// Output signature (OSG1) part.
  ///
  /// \returns The output signature.
  const DirectX::Signature &getOutputSignature() const {
    return OutputSignature;
  }
  /// Patch-constant signature (PSG1) part.
  ///
  /// \returns The patch-constant signature.
  const DirectX::Signature &getPatchConstantSignature() const {
    return PatchConstantSignature;
  }

  /// Compiler version info, if the VERS part is present.
  ///
  /// \returns The compiler version info, if present.
  const std::optional<mcdxbc::CompilerVersion> &getCompilerVersionInfo() const {
    return VersionInfo;
  }

  /// Source info, if the SRCI part is present.
  ///
  /// \returns The source info, if present.
  const std::optional<mcdxbc::SourceInfo> &getSourceInfo() const {
    return SourceInfo;
  }

  /// Private data part payload, if present.
  ///
  /// \returns The private data payload, if present.
  const std::optional<StringRef> &getPrivateData() const { return PrivateData; }
};

/// ObjectFile wrapper that exposes DXContainer parts as sections.
class LLVM_ABI DXContainerObjectFile : public ObjectFile {
private:
  friend class ObjectFile;
  DXContainer Container;

  using PartData = DXContainer::PartIterator::PartData;
  llvm::SmallVector<PartData> Parts;
  using PartIterator = llvm::SmallVector<PartData>::iterator;

  DXContainerObjectFile(DXContainer C)
      : ObjectFile(ID_DXContainer, MemoryBufferRef(C.getData(), "")),
        Container(C) {
    for (auto &P : C)
      Parts.push_back(P);
  }

public:
  /// Underlying parsed DXContainer.
  ///
  /// \returns The underlying parsed DXContainer.
  const DXContainer &getDXContainer() const { return Container; }

  /// True if \p v is a DXContainerObjectFile.
  ///
  /// \param v Binary to test.
  /// \returns True if \p v is a DXContainerObjectFile.
  static bool classof(const Binary *v) { return v->isDXContainer(); }

  /// DXBC container header.
  ///
  /// \returns The DXBC container header.
  const dxbc::Header &getHeader() const { return Container.getHeader(); }

  /// Name of symbol \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \returns The symbol name, or an error on failure.
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;
  /// Virtual address of symbol \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \returns The symbol address, or an error on failure.
  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  /// Format-specific symbol value for \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \returns The format-specific symbol value.
  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;
  /// Size of common symbol \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \returns The common symbol size in bytes.
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;

  /// Classify symbol \p Symb (data, function, etc.).
  ///
  /// \param Symb Opaque symbol handle.
  /// \returns The symbol type, or an error on failure.
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const override;
  /// Section defining symbol \p Symb, or section_end() if undefined.
  ///
  /// \param Symb Opaque symbol handle.
  /// \returns An iterator to the defining section, or an error on failure.
  Expected<section_iterator> getSymbolSection(DataRefImpl Symb) const override;
  /// Advance \p Sec to the next section.
  ///
  /// \param Sec Opaque section handle to advance.
  void moveSectionNext(DataRefImpl &Sec) const override;
  /// Name of section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \returns The section name, or an error on failure.
  Expected<StringRef> getSectionName(DataRefImpl Sec) const override;
  /// Virtual load address of section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \returns The section virtual address.
  uint64_t getSectionAddress(DataRefImpl Sec) const override;
  /// Index of section \p Sec within this object.
  ///
  /// \param Sec Opaque section handle.
  /// \returns The section index.
  uint64_t getSectionIndex(DataRefImpl Sec) const override;
  /// Size in bytes of section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \returns The section size in bytes.
  uint64_t getSectionSize(DataRefImpl Sec) const override;
  /// Raw contents of section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \returns The section contents, or an error on failure.
  Expected<ArrayRef<uint8_t>>
  getSectionContents(DataRefImpl Sec) const override;

  /// Alignment of section \p Sec in bytes.
  ///
  /// \param Sec Opaque section handle.
  /// \returns The section alignment in bytes.
  uint64_t getSectionAlignment(DataRefImpl Sec) const override;
  /// True if section \p Sec is compressed.
  ///
  /// \param Sec Opaque section handle.
  /// \returns True if the section is compressed.
  bool isSectionCompressed(DataRefImpl Sec) const override;
  /// True if section \p Sec contains executable code.
  ///
  /// \param Sec Opaque section handle.
  /// \returns True if the section contains executable code.
  bool isSectionText(DataRefImpl Sec) const override;
  /// True if section \p Sec contains initialized data (not text).
  ///
  /// \param Sec Opaque section handle.
  /// \returns True if the section contains initialized data.
  bool isSectionData(DataRefImpl Sec) const override;
  /// True if section \p Sec is BSS (zero-initialized, no file contents).
  ///
  /// \param Sec Opaque section handle.
  /// \returns True if the section is BSS.
  bool isSectionBSS(DataRefImpl Sec) const override;
  /// True if section \p Sec's contents are absent from the object image.
  ///
  /// \param Sec Opaque section handle.
  /// \returns True if the section is virtual.
  bool isSectionVirtual(DataRefImpl Sec) const override;

  /// Iterator to the first relocation in section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \returns An iterator to the first relocation in \p Sec.
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override;
  /// Past-the-end iterator for relocations in section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \returns An iterator one past the last relocation in \p Sec.
  relocation_iterator section_rel_end(DataRefImpl Sec) const override;

  /// Advance \p Rel to the next relocation.
  ///
  /// \param Rel Opaque relocation handle to advance.
  void moveRelocationNext(DataRefImpl &Rel) const override;
  /// Byte offset of relocation \p Rel within its section.
  ///
  /// \param Rel Opaque relocation handle.
  /// \returns The relocation offset within its section.
  uint64_t getRelocationOffset(DataRefImpl Rel) const override;
  /// Symbol referenced by relocation \p Rel.
  ///
  /// \param Rel Opaque relocation handle.
  /// \returns An iterator to the referenced symbol.
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override;
  /// Format-specific type encoding of relocation \p Rel.
  ///
  /// \param Rel Opaque relocation handle.
  /// \returns The relocation type encoding.
  uint64_t getRelocationType(DataRefImpl Rel) const override;
  /// Append a display name for relocation \p Rel's type to \p Result.
  ///
  /// \param Rel Opaque relocation handle.
  /// \param Result Buffer that receives the type name.
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override;

  /// Iterator to the first section in this object.
  ///
  /// \returns An iterator to the first section.
  section_iterator section_begin() const override;
  /// Past-the-end iterator for sections in this object.
  ///
  /// \returns An iterator one past the last section.
  section_iterator section_end() const override;

  /// Number of bytes used to represent an address in this format.
  ///
  /// \returns The address size in bytes.
  uint8_t getBytesInAddress() const override;
  /// Human-readable name of this object file format.
  ///
  /// \returns The object file format name.
  StringRef getFileFormatName() const override;
  /// Target architecture of this object file.
  ///
  /// \returns The target architecture.
  Triple::ArchType getArch() const override;
  /// Subtarget features described by this object file.
  ///
  /// \returns The subtarget features, or an error on failure.
  Expected<SubtargetFeatures> getFeatures() const override;

  /// Advance \p Symb to the next symbol.
  ///
  /// \param Symb Opaque symbol handle to advance.
  void moveSymbolNext(DataRefImpl &Symb) const override {}
  /// Print the name of symbol \p Symb to \p OS.
  ///
  /// \param OS Stream to write the symbol name to.
  /// \param Symb Opaque symbol handle.
  /// \returns Success, or an error if the name could not be printed.
  Error printSymbolName(raw_ostream &OS, DataRefImpl Symb) const override;
  /// Symbol flags for \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \returns The symbol flags, or an error on failure.
  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override;
  /// Iterator to the first symbol in this object.
  ///
  /// \returns An iterator to the first symbol.
  basic_symbol_iterator symbol_begin() const override {
    return basic_symbol_iterator(SymbolRef());
  }
  /// Past-the-end iterator for symbols in this object.
  ///
  /// \returns An iterator one past the last symbol.
  basic_symbol_iterator symbol_end() const override {
    return basic_symbol_iterator(SymbolRef());
  }
  /// Always false; DXContainerObjectFile is not a 64-bit address space.
  ///
  /// \returns Always false.
  bool is64Bit() const override { return false; }

  /// Always false; DX containers are not relocatable objects.
  ///
  /// \returns Always false.
  bool isRelocatableObject() const override { return false; }
};

} // namespace object
} // namespace llvm

#endif // LLVM_OBJECT_DXCONTAINER_H
