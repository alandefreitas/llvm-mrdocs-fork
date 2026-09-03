//===- Utility.h - Collection of geneirc offloading utilities -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_OFFLOADING_UTILITY_H
#define LLVM_FRONTEND_OFFLOADING_UTILITY_H

#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <memory>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"

namespace llvm {
namespace offloading {

/// This is the record of an object that just be registered with the offloading
/// runtime.
struct EntryTy {
  /// Reserved bytes used to detect an older version of the struct, always zero.
  uint64_t Reserved = 0x0;
  /// The current version of the struct for runtime forward compatibility.
  uint16_t Version = 0x1;
  /// The expected consumer of this entry, e.g. CUDA or OpenMP.
  uint16_t Kind;
  /// Flags associated with the global.
  uint32_t Flags;
  /// The address of the global to be registered by the runtime.
  void *Address;
  /// The name of the symbol in the device image.
  char *SymbolName;
  /// The number of bytes the symbol takes.
  uint64_t Size;
  /// Extra generic data used to register this entry.
  uint64_t Data;
  /// An extra pointer, usually null.
  void *AuxAddr;
};

/// Offloading entry flags for CUDA / HIP. The first three bits indicate the
/// type of entry while the others are a bit field for additional information.
enum OffloadEntryKindFlag : uint32_t {
  /// Mark the entry as a global entry. This indicates the presense of a
  /// kernel if the size size field is zero and a variable otherwise.
  OffloadGlobalEntry = 0x0,
  /// Mark the entry as a managed global variable.
  OffloadGlobalManagedEntry = 0x1,
  /// Mark the entry as a surface variable.
  OffloadGlobalSurfaceEntry = 0x2,
  /// Mark the entry as a texture variable.
  OffloadGlobalTextureEntry = 0x3,
  /// Mark the entry as being extern.
  OffloadGlobalExtern = 0x1 << 3,
  /// Mark the entry as being constant.
  OffloadGlobalConstant = 0x1 << 4,
  /// Mark the entry as being a normalized surface.
  OffloadGlobalNormalized = 0x1 << 5,
};

/// Returns the type of the offloading entry we use to store kernels and
/// globals that will be registered with the offloading runtime.
/// \param M The module that owns the context used to create the type.
/// \return The struct type used for offloading entries in \p M.
LLVM_ABI StructType *getEntryTy(Module &M);

/// Returns the section name for offloading entries based on the target triple.
///
/// ELF: "llvm_offload_entries", COFF: "llvm_offload_entries",
/// Mach-O: "__LLVM,offload_entries".
/// \param M The module whose target triple selects the section name.
/// \return The offloading entry section name for the module's object format.
LLVM_ABI StringRef getOffloadEntrySection(Module &M);

/// Create an offloading entry used to register this global at runtime.
///
/// \param M The module to be used.
/// \param Kind The offloading language expected to consume this.
/// \param Addr The pointer to the global being registered.
/// \param Name The symbol name associated with the global.
/// \param Size The size in bytes of the global (0 for functions).
/// \param Flags Flags associated with the entry.
/// \param Data Extra data storage associated with the entry.
/// \param AuxAddr An extra pointer if needed.
/// \return The emitted global variable containing the offloading entry.
LLVM_ABI GlobalVariable *
emitOffloadingEntry(Module &M, object::OffloadKind Kind, Constant *Addr,
                    StringRef Name, uint64_t Size, uint32_t Flags,
                    uint64_t Data, Constant *AuxAddr = nullptr);

/// Create a constant struct initializer used to register this global at
/// runtime.
/// \param M The module to be used.
/// \param Kind The offloading language expected to consume this.
/// \param Addr The pointer to the global being registered.
/// \param Name The symbol name associated with the global.
/// \param Size The size in bytes of the global (0 for functions).
/// \param Flags Flags associated with the entry.
/// \param Data Extra data storage associated with the entry.
/// \param AuxAddr An extra pointer if needed.
/// \return The constant struct and the global variable holding the symbol name.
LLVM_ABI std::pair<Constant *, GlobalVariable *>
getOffloadingEntryInitializer(Module &M, object::OffloadKind Kind,
                              Constant *Addr, StringRef Name, uint64_t Size,
                              uint32_t Flags, uint64_t Data, Constant *AuxAddr);

/// Creates a pair of constants used to iterate the array of offloading entries
/// by accessing the section variables provided by the linker.
/// \param M The module providing the offload entry section.
/// \return A pair of constants pointing to the start and end of the entry array.
LLVM_ABI std::pair<Constant *, Constant *> getOffloadEntryArray(Module &M);

/// AMDGPU-specific offloading helpers for image compatibility and metadata.
namespace amdgpu {
/// Check if an image is compatible with current system's environment. The
/// system environment is given as a 'target-id' which has the form:
///
/// <target-id> := <processor> ( ":" <target-feature> ( "+" | "-" ) )*
///
/// If a feature is not specific as '+' or '-' it is assumed to be in an 'any'
/// and is compatible with either '+' or '-'. The HSA runtime returns this
/// information using the target-id, while we use the ELF header to determine
/// these features.
/// \param ImageArch Processor architecture of the image.
/// \param ImageFlags ELF feature flags of the image.
/// \param EnvTargetID Target-id describing the system's environment.
/// \return True if the image is compatible with the given environment.
LLVM_ABI bool isImageCompatibleWithEnv(StringRef ImageArch, uint32_t ImageFlags,
                                       StringRef EnvTargetID);

/// Metadata describing an AMDGPU kernel from code object metadata.
///
/// For more information about the metadata and its meaning see:
/// https://llvm.org/docs/AMDGPUUsage.html#code-object-v3
struct AMDGPUKernelMetaData {
  /// Constant indicating that a value is invalid.
  static constexpr uint32_t KInvalidValue =
      std::numeric_limits<uint32_t>::max();
  /// The amount of group segment memory required by a work-group in bytes.
  uint32_t GroupSegmentList = KInvalidValue;
  /// The amount of fixed private address space memory required for a work-item
  /// in bytes.
  uint32_t PrivateSegmentSize = KInvalidValue;
  /// Number of scalar registers required by a wavefront.
  uint32_t SGPRCount = KInvalidValue;
  /// Number of vector registers required by each work-item.
  uint32_t VGPRCount = KInvalidValue;
  /// Number of stores from a scalar register to a register allocator created
  /// spill location.
  uint32_t SGPRSpillCount = KInvalidValue;
  /// Number of stores from a vector register to a register allocator created
  /// spill location.
  uint32_t VGPRSpillCount = KInvalidValue;
  /// Number of accumulator registers required by each work-item.
  uint32_t AGPRCount = KInvalidValue;
  /// Corresponds to the OpenCL reqd_work_group_size attribute.
  uint32_t RequestedWorkgroupSize[3] = {KInvalidValue, KInvalidValue,
                                        KInvalidValue};
  /// Corresponds to the OpenCL work_group_size_hint attribute.
  uint32_t WorkgroupSizeHint[3] = {KInvalidValue, KInvalidValue, KInvalidValue};
  /// Wavefront size.
  uint32_t WavefrontSize = KInvalidValue;
  /// Maximum flat work-group size supported by the kernel in work-items.
  uint32_t MaxFlatWorkgroupSize = KInvalidValue;
  /// Per-argument {offset, size} in bytes, read from the ".args" array in code
  /// object metadata. Explicit user arguments are first, followed by
  /// hidden arguments.
  SmallVector<std::pair<uint32_t, uint32_t>, 8> ArgMDs;
};

/// Reads AMDGPU specific metadata from the ELF file and propagates the
/// KernelInfoMap.
/// \param MemBuffer Memory buffer containing the AMDGPU ELF image.
/// \param KernelInfoMap Map filled with per-kernel metadata extracted from
/// the image.
/// \param ELFABIVersion Set to the ELF ABI version found in the image.
/// \return Success, or an error if the metadata cannot be read.
LLVM_ABI Error getAMDGPUMetaDataFromImage(
    MemoryBufferRef MemBuffer, StringMap<AMDGPUKernelMetaData> &KernelInfoMap,
    uint16_t &ELFABIVersion);
} // namespace amdgpu

/// Containerizes an image within an OffloadBinary image.
///
/// Creates a nested OffloadBinary structure where the inner binary contains
/// the raw image and associated metadata (version, format, triple, etc.).
/// \param Binary The image to containerize.
/// \param Triple The target triple to be associated with the image.
/// \param ImageKind The format of the image, e.g. SPIR-V or CUBIN.
/// \param OffloadKind The expected consuming runtime of the image, e.g. CUDA or
/// OpenMP.
/// \param ImageFlags Flags associated with the image, e.g. for AMDGPU the
/// features.
/// \param MetaData The key-value map of metadata to be associated with the
/// image.
/// \return Success, or an error if containerization fails.
LLVM_ABI Error containerizeImage(std::unique_ptr<MemoryBuffer> &Binary,
                                 llvm::Triple Triple,
                                 object::ImageKind ImageKind,
                                 object::OffloadKind OffloadKind,
                                 int32_t ImageFlags,
                                 MapVector<StringRef, StringRef> &MetaData);

/// SYCL OffloadBinary symbol-table serialization helpers.
namespace sycl {

/// Header of a serialized SYCL OffloadBinary symbol table.
///
/// Serialized symbol table stored in the "symbols" entry of a SYCL
/// OffloadBinary. The in-memory layout of the blob is:
///   [ SymbolTableHeader               ]
///   [ SymbolTableEntry  Entries[N]    ]  -- N == Header.Count
///   [ char              StringData[]  ]  -- packed null-terminated names
/// Use writeSymbolTable() to produce the blob and forEachSymbol() to consume
/// it; both encapsulate all pointer arithmetic.
struct SymbolTableHeader {
  uint32_t Count; ///< Number of symbol entries.
};
/// One entry in a serialized SYCL OffloadBinary symbol table.
struct SymbolTableEntry {
  uint32_t OffsetToSymbol; ///< Byte offset from blob start to the symbol name.
  uint32_t SymbolSize;     ///< Length of the symbol name in bytes, excluding
                           ///< the null terminator.
};

/// Serialize \p Names into \p Out.
/// \param Names Symbol names to serialize into the table.
/// \param Out Buffer that receives the serialized symbol-table blob.
LLVM_ABI void writeSymbolTable(ArrayRef<StringRef> Names, SmallString<0> &Out);

/// Invoke \p Callback with a \c StringRef for each symbol in \p Symbols,
/// the raw serialized symbol-table blob.
/// \param Symbols Raw serialized symbol-table blob to iterate.
/// \param Callback Invoked once per symbol with the symbol name as a
/// StringRef.
template <typename Fn> void forEachSymbol(StringRef Symbols, Fn &&Callback) {
  assert(Symbols.size() >= sizeof(SymbolTableHeader) &&
         "symbols blob smaller than header");
  const char *Base = Symbols.data();
  const auto &Header = *reinterpret_cast<const SymbolTableHeader *>(Base);
  const auto *Entries = reinterpret_cast<const SymbolTableEntry *>(&Header + 1);
  for (uint32_t I = 0; I < Header.Count; ++I)
    Callback(
        StringRef(Base + Entries[I].OffsetToSymbol, Entries[I].SymbolSize));
}

} // namespace sycl

/// Intel-specific helpers for containerizing OpenMP SPIR-V images.
namespace intel {
/// Containerizes an OpenMP SPIR-V image into an OffloadBinary image.
/// \param Binary The SPIR-V binary to containerize.
/// \param Triple The target triple to be associated with the image.
/// \param CompileOpts Optional compilation options.
/// \param LinkOpts Optional linking options.
/// \return Success, or an error if containerization fails.
LLVM_ABI Error containerizeOpenMPSPIRVImage(
    std::unique_ptr<MemoryBuffer> &Binary, llvm::Triple Triple,
    StringRef CompileOpts = "", StringRef LinkOpts = "");
} // namespace intel
} // namespace offloading
} // namespace llvm

#endif // LLVM_FRONTEND_OFFLOADING_UTILITY_H
