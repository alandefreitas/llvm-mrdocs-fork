//===- llvm/Bitcode/BitcodeReader.h - Bitcode reader ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines interfaces to read LLVM bitcode files/streams.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITCODEREADER_H
#define LLVM_BITCODE_BITCODEREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitstream/BitCodeEnums.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>
namespace llvm {

class LLVMContext;
class Module;
class MemoryBuffer;
class Metadata;
class ModuleSummaryIndex;
class Type;
class Value;
struct ValueInfo;

/// Callback that may override the data layout of an imported bitcode module.
///
/// The first argument is the target triple. The second is the data layout
/// string from the input, or a default. That input layout is used if the
/// callback returns std::nullopt.
typedef std::function<std::optional<std::string>(StringRef, StringRef)>
    DataLayoutCallbackFuncTy;

/// Function that looks up a Type by its bitcode type ID.
typedef std::function<Type *(unsigned)> GetTypeByIDTy;

/// Function that returns the type ID of a contained type by index.
typedef std::function<unsigned(unsigned, unsigned)> GetContainedTypeIDTy;

/// Callback invoked for each Value with its type ID and type-lookup helpers.
typedef std::function<void(Value *, unsigned, GetTypeByIDTy,
                           GetContainedTypeIDTy)>
    ValueTypeCallbackTy;

/// Callback invoked for each Metadata value with its type ID and helpers.
typedef std::function<void(Metadata **, unsigned, GetTypeByIDTy,
                           GetContainedTypeIDTy)>
    MDTypeCallbackTy;

/// Convert \p Err to an error_code and emit diagnostics into \p Ctx.
///
/// Compatibility helper for legacy clients that expect std::error_code.
/// FIXME: Remove these functions once no longer needed by the C and libLTO
/// APIs.
///
/// \param Ctx Context that receives diagnostic messages.
/// \param Err Error to convert and consume.
/// \returns The corresponding std::error_code after emitting diagnostics.
LLVM_ABI std::error_code errorToErrorCodeAndEmitErrors(LLVMContext &Ctx,
                                                       Error Err);

/// Convert \p Val to ErrorOr, emitting diagnostics into \p Ctx on failure.
///
/// Compatibility helper for legacy clients that expect ErrorOr.
///
/// \param Ctx Context that receives diagnostic messages.
/// \param Val Expected value to convert.
/// \returns The value wrapped in ErrorOr, or an error_code after emitting
///          diagnostics.
template <typename T>
ErrorOr<T> expectedToErrorOrAndEmitErrors(LLVMContext &Ctx, Expected<T> Val) {
  if (!Val)
    return errorToErrorCodeAndEmitErrors(Ctx, Val.takeError());
  return std::move(*Val);
}

/// Optional callbacks that customize bitcode parsing behavior.
struct ParserCallbacks {
  /// Optional callback that overrides the module data layout string.
  std::optional<DataLayoutCallbackFuncTy> DataLayout;
  /// Optional callback invoked for each function definition or declaration.
  ///
  /// Allows accessing type information, including behind pointers. This can
  /// be useful when the opaque pointer upgrade clears type information behind
  /// pointers. The second argument to ValueTypeCallback is the type ID of the
  /// function; the two passed functions can be used to extract type
  /// information.
  std::optional<ValueTypeCallbackTy> ValueType;
  /// The MDType callback is called for every value in metadata.
  std::optional<MDTypeCallbackTy> MDType;

  /// When true, skip upgrading debug intrinsics to debug records.
  ///
  /// If true, do not auto-upgrade debug intrinsic calls (llvm.dbg.*) to
  /// non-instruction debug records during bitcode read. This flag allows
  /// direct manipulation of the old intrinsic-form debug info; beware that
  /// LLVM does not support using these intrinsics any more. The caller is
  /// responsible for performing the upgrade manually (e.g. via
  /// Module::convertToNewDbgValues()).
  bool SkipDebugIntrinsicUpgrade = false;

  /// Construct callbacks with no overrides enabled.
  ParserCallbacks() = default;
  /// Construct callbacks that override the data layout.
  ///
  /// \param DataLayout Callback used to override the module data layout.
  explicit ParserCallbacks(DataLayoutCallbackFuncTy DataLayout)
      : DataLayout(DataLayout) {}
};

  struct BitcodeFileContents;

  /// Basic information extracted from a bitcode module to be used for LTO.
  struct BitcodeLTOInfo {
    /// True if the module should be compiled with ThinLTO.
    bool IsThinLTO;
    /// True if the module contains a summary index.
    bool HasSummary;
    /// True if split LTO units are enabled for this module.
    bool EnableSplitLTOUnit;
    /// True if unified LTO is enabled for this module.
    bool UnifiedLTO;
  };

  /// Represents a module in a bitcode file.
  class BitcodeModule {
    // This covers the identification (if present) and module blocks.
    ArrayRef<uint8_t> Buffer;
    StringRef ModuleIdentifier;

    // The string table used to interpret this module.
    StringRef Strtab;

    // The bitstream location of the IDENTIFICATION_BLOCK.
    uint64_t IdentificationBit;

    // The bitstream location of this module's MODULE_BLOCK.
    uint64_t ModuleBit;

    BitcodeModule(ArrayRef<uint8_t> Buffer, StringRef ModuleIdentifier,
                  uint64_t IdentificationBit, uint64_t ModuleBit)
        : Buffer(Buffer), ModuleIdentifier(ModuleIdentifier),
          IdentificationBit(IdentificationBit), ModuleBit(ModuleBit) {}

    // Calls the ctor.
    LLVM_ABI friend Expected<BitcodeFileContents>
    getBitcodeFileContents(MemoryBufferRef Buffer);

    Expected<std::unique_ptr<Module>>
    getModuleImpl(LLVMContext &Context, bool MaterializeAll,
                  bool ShouldLazyLoadMetadata, bool IsImporting,
                  ParserCallbacks Callbacks = {});

  public:
    /// Return the raw bitcode bytes for this module.
    ///
    /// \returns A StringRef over this module's bitcode bytes.
    StringRef getBuffer() const {
      return StringRef((const char *)Buffer.begin(), Buffer.size());
    }

    /// Return the string table used to interpret this module.
    ///
    /// \returns The string table for this bitcode module.
    StringRef getStrtab() const { return Strtab; }

    /// Return the module identifier string for this bitcode module.
    ///
    /// \returns The module identifier associated with this bitcode module.
    StringRef getModuleIdentifier() const { return ModuleIdentifier; }

    /// Assign a new module identifier to this bitcode module.
    ///
    /// \param ModuleId New identifier string to associate with this module.
    void setModuleIdentifier(llvm::StringRef ModuleId) {
      ModuleIdentifier = ModuleId;
    }

    /// Read the bitcode module and prepare for lazy function deserialization.
    ///
    /// If ShouldLazyLoadMetadata is true, lazily load metadata as well.
    /// If IsImporting is true, this module is being parsed for ThinLTO
    /// importing into another module.
    ///
    /// \param Context LLVM context that owns the created module.
    /// \param ShouldLazyLoadMetadata If true, defer loading metadata.
    /// \param IsImporting If true, parse for ThinLTO importing.
    /// \param Callbacks Optional parser customization callbacks.
    /// \returns A Module prepared for lazy deserialization, or an Error on
    ///          failure.
    LLVM_ABI Expected<std::unique_ptr<Module>>
    getLazyModule(LLVMContext &Context, bool ShouldLazyLoadMetadata,
                  bool IsImporting, ParserCallbacks Callbacks = {});

    /// Read the entire bitcode module and return it.
    ///
    /// \param Context LLVM context that owns the created module.
    /// \param Callbacks Optional parser customization callbacks.
    /// \returns The fully materialized Module, or an Error on failure.
    LLVM_ABI Expected<std::unique_ptr<Module>>
    parseModule(LLVMContext &Context, ParserCallbacks Callbacks = {});

    /// Returns information about the module to be used for LTO: whether to
    /// compile with ThinLTO, and whether it has a summary.
    ///
    /// \returns LTO flags for this bitcode module, or an Error on failure.
    LLVM_ABI Expected<BitcodeLTOInfo> getLTOInfo();

    /// Parse the specified bitcode buffer, returning the module summary index.
    ///
    /// \returns The parsed module summary index, or an Error on failure.
    LLVM_ABI Expected<std::unique_ptr<ModuleSummaryIndex>> getSummary();

    /// Parse the specified bitcode buffer and merge its module summary index
    /// into CombinedIndex.
    ///
    /// \param CombinedIndex Summary index that receives merged entries.
    /// \param ModulePath Path used to identify this module in the index.
    /// \param IsPrevailing Optional predicate for prevailing definitions.
    /// \param OnValueInfo Optional callback invoked for each ValueInfo.
    /// \returns Error::success() on success, or an Error describing the failure.
    LLVM_ABI Error
    readSummary(ModuleSummaryIndex &CombinedIndex, StringRef ModulePath,
                std::function<bool(StringRef)> IsPrevailing = nullptr,
                std::function<void(ValueInfo)> OnValueInfo = nullptr);
  };

  /// Contents of a bitcode file, including modules and symbol table data.
  struct BitcodeFileContents {
    /// Modules found in the bitcode file.
    std::vector<BitcodeModule> Mods;
    /// Raw symbol table blob embedded in the bitcode file.
    StringRef Symtab;
    /// String table used to interpret \c Symtab.
    StringRef StrtabForSymtab;
  };

  /// Return the modules and raw symbol table from a bitcode buffer.
  ///
  /// This includes the raw contents of the symbol table embedded in the
  /// bitcode file. Clients which require a symbol table should prefer to use
  /// irsymtab::read instead of this function because it creates a reader for
  /// the irsymtab and handles upgrading bitcode files without a symbol table
  /// or with an old symbol table.
  ///
  /// \param Buffer Memory buffer containing the bitcode file.
  /// \returns The modules and raw symbol-table contents of the bitcode file.
  LLVM_ABI Expected<BitcodeFileContents>
  getBitcodeFileContents(MemoryBufferRef Buffer);

  /// Returns a list of modules in the specified bitcode buffer.
  ///
  /// \param Buffer Memory buffer containing the bitcode file.
  /// \returns The BitcodeModule entries found in \p Buffer, or an Error on
  ///          failure.
  LLVM_ABI Expected<std::vector<BitcodeModule>>
  getBitcodeModuleList(MemoryBufferRef Buffer);

  /// Read bitcode and prepare a module for lazy function deserialization.
  ///
  /// If ShouldLazyLoadMetadata is true, lazily load metadata as well. If
  /// IsImporting is true, this module is being parsed for ThinLTO importing
  /// into another module.
  ///
  /// \param Buffer Memory buffer containing the bitcode.
  /// \param Context LLVM context that owns the created module.
  /// \param ShouldLazyLoadMetadata If true, defer loading metadata.
  /// \param IsImporting If true, parse for ThinLTO importing.
  /// \param Callbacks Optional parser customization callbacks.
  /// \returns A Module prepared for lazy deserialization, or an Error on
  ///          failure.
  LLVM_ABI Expected<std::unique_ptr<Module>>
  getLazyBitcodeModule(MemoryBufferRef Buffer, LLVMContext &Context,
                       bool ShouldLazyLoadMetadata = false,
                       bool IsImporting = false,
                       ParserCallbacks Callbacks = {});

  /// Like getLazyBitcodeModule, but the module owns the memory buffer.
  ///
  /// If successful, this moves Buffer. On error, this does not move Buffer.
  /// If IsImporting is true, this module is being parsed for ThinLTO
  /// importing into another module.
  ///
  /// \param Buffer Memory buffer to take ownership of on success.
  /// \param Context LLVM context that owns the created module.
  /// \param ShouldLazyLoadMetadata If true, defer loading metadata.
  /// \param IsImporting If true, parse for ThinLTO importing.
  /// \param Callbacks Optional parser customization callbacks.
  /// \returns A Module that owns \p Buffer, or an Error on failure.
  LLVM_ABI Expected<std::unique_ptr<Module>> getOwningLazyBitcodeModule(
      std::unique_ptr<MemoryBuffer> &&Buffer, LLVMContext &Context,
      bool ShouldLazyLoadMetadata = false, bool IsImporting = false,
      ParserCallbacks Callbacks = {});

  /// Read the header of the specified bitcode buffer and extract just the
  /// triple information. If successful, this returns a string. On error, this
  /// returns "".
  ///
  /// \param Buffer Memory buffer containing the bitcode.
  /// \returns The target triple string, or an empty string on error.
  LLVM_ABI Expected<std::string> getBitcodeTargetTriple(MemoryBufferRef Buffer);

  /// Return true if \p Buffer contains a bitcode file with ObjC code (category
  /// or class) in it.
  ///
  /// \param Buffer Memory buffer containing the bitcode.
  /// \returns True if the bitcode contains ObjC category or class definitions.
  LLVM_ABI Expected<bool>
  isBitcodeContainingObjCCategory(MemoryBufferRef Buffer);

  /// Read the producer string from the header of a bitcode buffer.
  ///
  /// If successful, this returns a string. On error, this returns "".
  ///
  /// \param Buffer Memory buffer containing the bitcode.
  /// \returns The producer string from the identification block, or an empty
  ///          string on error.
  LLVM_ABI Expected<std::string>
  getBitcodeProducerString(MemoryBufferRef Buffer);

  /// Read the specified bitcode file, returning the module.
  ///
  /// \param Buffer Memory buffer containing the bitcode.
  /// \param Context LLVM context that owns the created module.
  /// \param Callbacks Optional parser customization callbacks.
  /// \returns The fully materialized Module, or an Error on failure.
  LLVM_ABI Expected<std::unique_ptr<Module>>
  parseBitcodeFile(MemoryBufferRef Buffer, LLVMContext &Context,
                   ParserCallbacks Callbacks = {});

  /// Returns LTO information for the specified bitcode file.
  ///
  /// \param Buffer Memory buffer containing the bitcode.
  /// \returns LTO flags for the bitcode module, or an Error on failure.
  LLVM_ABI Expected<BitcodeLTOInfo> getBitcodeLTOInfo(MemoryBufferRef Buffer);

  /// Parse the specified bitcode buffer, returning the module summary index.
  ///
  /// \param Buffer Memory buffer containing the bitcode.
  /// \returns The parsed module summary index, or an Error on failure.
  LLVM_ABI Expected<std::unique_ptr<ModuleSummaryIndex>>
  getModuleSummaryIndex(MemoryBufferRef Buffer);

  /// Parse the specified bitcode buffer and merge the index into CombinedIndex.
  ///
  /// \param Buffer Memory buffer containing the bitcode.
  /// \param CombinedIndex Summary index that receives merged entries.
  /// \returns Error::success() on success, or an Error describing the failure.
  LLVM_ABI Error readModuleSummaryIndex(MemoryBufferRef Buffer,
                                        ModuleSummaryIndex &CombinedIndex);

  /// Parse the module summary index out of an IR file.
  ///
  /// Returns the module summary index object if found, or an empty summary if
  /// not. If Path refers to an empty file and IgnoreEmptyThinLTOIndexFile is
  /// true, then this function will return nullptr.
  ///
  /// \param Path Path to the IR or bitcode file to read.
  /// \param IgnoreEmptyThinLTOIndexFile If true, treat an empty file as no
  ///        index.
  /// \returns The module summary index, an empty index if none is present, or
  ///          nullptr when an empty file is ignored.
  LLVM_ABI Expected<std::unique_ptr<ModuleSummaryIndex>>
  getModuleSummaryIndexForFile(StringRef Path,
                               bool IgnoreEmptyThinLTOIndexFile = false);

  /// isBitcodeWrapper - Return true if the given bytes are the magic bytes
  /// for an LLVM IR bitcode wrapper.
  ///
  /// \param BufPtr Pointer to the start of the buffer.
  /// \param BufEnd Pointer one past the end of the buffer.
  /// \returns True if the buffer starts with the bitcode wrapper magic.
  inline bool isBitcodeWrapper(const unsigned char *BufPtr,
                               const unsigned char *BufEnd) {
    // See if you can find the hidden message in the magic bytes :-).
    // (Hint: it's a little-endian encoding.)
    return BufPtr != BufEnd &&
           BufPtr[0] == 0xDE &&
           BufPtr[1] == 0xC0 &&
           BufPtr[2] == 0x17 &&
           BufPtr[3] == 0x0B;
  }

  /// isRawBitcode - Return true if the given bytes are the magic bytes for
  /// raw LLVM IR bitcode (without a wrapper).
  ///
  /// \param BufPtr Pointer to the start of the buffer.
  /// \param BufEnd Pointer one past the end of the buffer.
  /// \returns True if the buffer starts with the raw bitcode magic bytes.
  inline bool isRawBitcode(const unsigned char *BufPtr,
                           const unsigned char *BufEnd) {
    // These bytes sort of have a hidden message, but it's not in
    // little-endian this time, and it's a little redundant.
    return BufPtr != BufEnd &&
           BufPtr[0] == 'B' &&
           BufPtr[1] == 'C' &&
           BufPtr[2] == 0xc0 &&
           BufPtr[3] == 0xde;
  }

  /// isBitcode - Return true if the given bytes are the magic bytes for
  /// LLVM IR bitcode, either with or without a wrapper.
  ///
  /// \param BufPtr Pointer to the start of the buffer.
  /// \param BufEnd Pointer one past the end of the buffer.
  /// \returns True if the buffer starts with wrapped or raw bitcode magic.
  inline bool isBitcode(const unsigned char *BufPtr,
                        const unsigned char *BufEnd) {
    return isBitcodeWrapper(BufPtr, BufEnd) ||
           isRawBitcode(BufPtr, BufEnd);
  }

  /// SkipBitcodeWrapperHeader - Some systems wrap bc files with a special
  /// header for padding or other reasons.  The format of this header is:
  ///
  /// struct bc_header {
  ///   uint32_t Magic;         // 0x0B17C0DE
  ///   uint32_t Version;       // Version, currently always 0.
  ///   uint32_t BitcodeOffset; // Offset to traditional bitcode file.
  ///   uint32_t BitcodeSize;   // Size of traditional bitcode file.
  ///   ... potentially other gunk ...
  /// };
  ///
  /// This function is called when we find a file with a matching magic number.
  /// In this case, skip down to the subsection of the file that is actually a
  /// BC file.
  /// If 'VerifyBufferSize' is true, check that the buffer is large enough to
  /// contain the whole bitcode file.
  ///
  /// \param BufPtr On entry, start of the wrapper; on success, advanced to the
  ///        bitcode payload.
  /// \param BufEnd On entry, end of the buffer; on success, set to the end of
  ///        the bitcode payload.
  /// \param VerifyBufferSize If true, check that the buffer is large enough to
  ///        contain the whole bitcode file.
  /// \returns True if the wrapper is invalid or the buffer is too small; false
  ///          on success after advancing \p BufPtr and \p BufEnd.
  inline bool SkipBitcodeWrapperHeader(const unsigned char *&BufPtr,
                                       const unsigned char *&BufEnd,
                                       bool VerifyBufferSize) {
    // Must contain the offset and size field!
    if (unsigned(BufEnd - BufPtr) < BWH_SizeField + 4)
      return true;

    unsigned Offset = support::endian::read32le(&BufPtr[BWH_OffsetField]);
    unsigned Size = support::endian::read32le(&BufPtr[BWH_SizeField]);
    uint64_t BitcodeOffsetEnd = (uint64_t)Offset + (uint64_t)Size;

    // Verify that Offset+Size fits in the file.
    if (VerifyBufferSize && BitcodeOffsetEnd > uint64_t(BufEnd-BufPtr))
      return true;
    BufPtr += Offset;
    BufEnd = BufPtr+Size;
    return false;
  }

  /// Decode a wide APInt from a sequence of 64-bit words.
  ///
  /// \param Vals Little-endian limbs of the integer value.
  /// \param TypeBits Bit width of the resulting APInt.
  /// \returns An APInt with width \p TypeBits built from \p Vals.
  LLVM_ABI APInt readWideAPInt(ArrayRef<uint64_t> Vals, unsigned TypeBits);

  /// Return the error category for BitcodeError values.
  ///
  /// \returns The std::error_category used by BitcodeError.
  LLVM_ABI const std::error_category &BitcodeErrorCategory();
  /// Error codes that can be produced while reading bitcode.
  enum class BitcodeError {
    /// The bitcode stream is corrupted or otherwise invalid.
    CorruptedBitcode = 1
  };
  /// Convert a BitcodeError into a std::error_code.
  ///
  /// \param E Bitcode-specific error value to convert.
  /// \returns An error_code in the BitcodeErrorCategory.
  inline std::error_code make_error_code(BitcodeError E) {
    return std::error_code(static_cast<int>(E), BitcodeErrorCategory());
  }

} // end namespace llvm

namespace std {

template <> struct is_error_code_enum<llvm::BitcodeError> : std::true_type {};

} // end namespace std

#endif // LLVM_BITCODE_BITCODEREADER_H
