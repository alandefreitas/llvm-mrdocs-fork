//===---- RuntimeDyldChecker.h - RuntimeDyld tester framework -----*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_RUNTIMEDYLDCHECKER_H
#define LLVM_EXECUTIONENGINE_RUNTIMEDYLDCHECKER_H

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/SymbolStringPool.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace llvm {

class StringRef;
class MCDisassembler;
class MemoryBuffer;
class MCInstPrinter;
class RuntimeDyld;
/// Internal implementation class behind RuntimeDyldChecker.
class RuntimeDyldCheckerImpl;
class raw_ostream;

/// Holds target-specific properties for a symbol.
using TargetFlagsType = uint8_t;

/// RuntimeDyld invariant checker for verifying that RuntimeDyld has
///        correctly applied relocations.
///
/// The RuntimeDyldChecker class evaluates expressions against an attached
/// RuntimeDyld instance to verify that relocations have been applied
/// correctly.
///
/// The expression language supports basic pointer arithmetic and bit-masking,
/// and has limited disassembler integration for accessing instruction
/// operands and the next PC (program counter) address for each instruction.
///
/// The language syntax is:
///
/// check = expr '=' expr
///
/// expr = binary_expr
///      | sliceable_expr
///
/// sliceable_expr = '*{' number '}' load_addr_expr [slice]
///                | '(' expr ')' [slice]
///                | ident_expr [slice]
///                | number [slice]
///
/// slice = '[' high-bit-index ':' low-bit-index ']'
///
/// load_addr_expr = symbol
///                | '(' symbol '+' number ')'
///                | '(' symbol '-' number ')'
///
/// ident_expr = 'decode_operand' '(' symbol ',' operand-index ')'
///            | 'next_pc'        '(' symbol ')'
///            | 'stub_addr' '(' stub-container-name ',' symbol ')'
///            | 'got_addr' '(' stub-container-name ',' symbol ')'
///            | 'section_addr' '(' stub-container-name ',' symbol ')'
///            | symbol
///
/// binary_expr = expr '+' expr
///             | expr '-' expr
///             | expr '&' expr
///             | expr '|' expr
///             | expr '<<' expr
///             | expr '>>' expr
///
class RuntimeDyldChecker {
public:
  /// Content and target address for a symbol or section region.
  class MemoryRegionInfo {
  public:
    /// Construct an uninitialized memory region.
    MemoryRegionInfo() : Size(0), Initialized(false) {}

    /// Constructor for symbols/sections with content and TargetFlag.
    /// \param Content Bytes that make up the region contents.
    /// \param TargetAddress Address of the region in the target process.
    /// \param TargetFlags Target-specific flags associated with the region.
    MemoryRegionInfo(ArrayRef<char> Content, JITTargetAddress TargetAddress,
                     TargetFlagsType TargetFlags)
        : ContentPtr(Content.data()), Size(Content.size()),
          TargetAddress(TargetAddress), TargetFlags(TargetFlags) {
      Initialized = true;
    }

    /// Constructor for zero-fill symbols/sections.
    /// \param Size Zero-fill length of the region in bytes.
    /// \param TargetAddress Address of the region in the target process.
    MemoryRegionInfo(uint64_t Size, JITTargetAddress TargetAddress)
        : Size(Size), TargetAddress(TargetAddress) {
      Initialized = true;
    }

    /// Returns true if this is a zero-fill symbol/section.
    /// @return True if this is a zero-fill symbol/section.
    bool isZeroFill() const {
      assert(Initialized && "setZeroFill / setContent not called");
      return !ContentPtr;
    }

    /// Set the content for this memory region.
    /// \param Content Bytes that make up the region contents.
    void setContent(ArrayRef<char> Content) {
      assert(!Initialized && "Content/zero-fill already set");
      ContentPtr = Content.data();
      Size = Content.size();
      Initialized = true;
    }

    /// Set a zero-fill length for this memory region.
    /// \param Size Zero-fill length of the region in bytes.
    void setZeroFill(uint64_t Size) {
      assert(!Initialized && "Content/zero-fill already set");
      this->Size = Size;
      Initialized = true;
    }

    /// Returns the content for this section if there is any.
    /// @return Content bytes for this region.
    ArrayRef<char> getContent() const {
      assert(!isZeroFill() && "Can't get content for a zero-fill section");
      return {ContentPtr, static_cast<size_t>(Size)};
    }

    /// Returns the zero-fill length for this section.
    /// @return Zero-fill length of this region in bytes.
    uint64_t getZeroFillLength() const {
      assert(isZeroFill() && "Can't get zero-fill length for content section");
      return Size;
    }

    /// Set the target address for this region.
    /// \param TargetAddress Address of the region in the target process.
    void setTargetAddress(JITTargetAddress TargetAddress) {
      assert(!this->TargetAddress && "TargetAddress already set");
      this->TargetAddress = TargetAddress;
    }

    /// Return the target address for this region.
    /// @return Target address of this region in the target process.
    JITTargetAddress getTargetAddress() const { return TargetAddress; }

    /// Get the target flags for this Symbol.
    /// @return Target-specific flags associated with this region.
    TargetFlagsType getTargetFlags() const { return TargetFlags; }

    /// Set the target flags for this Symbol.
    /// \param Flags Target-specific flags to associate with this region.
    void setTargetFlags(TargetFlagsType Flags) {
      assert(Flags <= 1 && "Add more bits to store more than one flag");
      TargetFlags = Flags;
    }

  private:
    const char *ContentPtr = nullptr;
    uint64_t Size : 63;
    uint64_t Initialized : 1;
    JITTargetAddress TargetAddress = 0;
    TargetFlagsType TargetFlags = 0;
  };

  /// Callback that reports whether a named symbol is valid.
  using IsSymbolValidFunction = std::function<bool(StringRef Symbol)>;
  /// Callback that returns memory-region info for a named symbol.
  using GetSymbolInfoFunction =
      std::function<Expected<MemoryRegionInfo>(StringRef SymbolName)>;
  /// Callback that returns memory-region info for a named section.
  using GetSectionInfoFunction = std::function<Expected<MemoryRegionInfo>(
      StringRef FileName, StringRef SectionName)>;
  /// Callback that returns memory-region info for a stub.
  using GetStubInfoFunction = std::function<Expected<MemoryRegionInfo>(
      StringRef StubContainer, StringRef TargetName, StringRef StubKindFilter)>;
  /// Callback that returns memory-region info for a GOT entry.
  using GetGOTInfoFunction = std::function<Expected<MemoryRegionInfo>(
      StringRef GOTContainer, StringRef TargetName)>;

  /// Construct a RuntimeDyldChecker with the given lookup callbacks.
  /// \param IsSymbolValid Callback that reports whether a symbol is valid.
  /// \param GetSymbolInfo Callback that returns info for a named symbol.
  /// \param GetSectionInfo Callback that returns info for a named section.
  /// \param GetStubInfo Callback that returns info for a stub.
  /// \param GetGOTInfo Callback that returns info for a GOT entry.
  /// \param Endianness Endianness of the target being checked.
  /// \param TT Target triple for the code under test.
  /// \param CPU Target CPU name used for disassembly.
  /// \param TF Subtarget features used for disassembly.
  /// \param ErrStream Stream that receives checker error messages.
  LLVM_ABI RuntimeDyldChecker(
      IsSymbolValidFunction IsSymbolValid, GetSymbolInfoFunction GetSymbolInfo,
      GetSectionInfoFunction GetSectionInfo, GetStubInfoFunction GetStubInfo,
      GetGOTInfoFunction GetGOTInfo, llvm::endianness Endianness, Triple TT,
      StringRef CPU, SubtargetFeatures TF, raw_ostream &ErrStream);
  /// Destroy the RuntimeDyldChecker and its implementation.
  LLVM_ABI ~RuntimeDyldChecker();

    /// Check a single expression against the attached RuntimeDyld
  ///        instance.
  /// \param CheckExpr Expression to evaluate as a checker equality.
  /// @return True if the expression evaluates successfully.
  LLVM_ABI bool check(StringRef CheckExpr) const;

    /// Evaluate all check-rule lines in a memory buffer.
  ///
  /// Scan the given memory buffer for lines beginning with the string in
  /// RulePrefix. The remainder of the line is passed to the check method to be
  /// evaluated as an expression.
  /// \param RulePrefix Prefix that marks check-rule lines in the buffer.
  /// \param MemBuf Memory buffer whose contents are scanned for check rules.
  /// @return True if every matching check rule evaluates successfully.
  LLVM_ABI bool checkAllRulesInBuffer(StringRef RulePrefix,
                                      MemoryBuffer *MemBuf) const;

    /// Returns the address of the requested section (or an error message
  ///        in the second element of the pair if the address cannot be found).
  ///
  /// if 'LocalAddress' is true, this returns the address of the section
  /// within the linker's memory. If 'LocalAddress' is false it returns the
  /// address within the target process (i.e. the load address).
  /// \param FileName Object file that contains the section.
  /// \param SectionName Name of the section whose address is requested.
  /// \param LocalAddress Whether to return the linker-local address.
  /// @return Pair of section address and empty string on success, or zero and
  ///         an error message if the address cannot be found.
  LLVM_ABI std::pair<uint64_t, std::string>
  getSectionAddr(StringRef FileName, StringRef SectionName, bool LocalAddress);

    /// If there is a section at the given local address, return its load
  /// address, otherwise return std::nullopt.
  /// \param LocalAddress Linker-local address of the section to look up.
  /// @return Load address of the section, or std::nullopt if none exists.
  LLVM_ABI std::optional<uint64_t>
  getSectionLoadAddress(void *LocalAddress) const;

private:
  std::unique_ptr<RuntimeDyldCheckerImpl> Impl;
};

} // end namespace llvm

#endif
