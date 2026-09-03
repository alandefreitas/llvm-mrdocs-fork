//==- NativeRawSymbol.h - Native implementation of IPDBRawSymbol -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVERAWSYMBOL_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVERAWSYMBOL_H

#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <memory>

namespace llvm {
namespace pdb {

class NativeSession;

/// Native PDB implementation of \c IPDBRawSymbol.
///
/// Provides default stub implementations for the full raw-symbol property
/// surface; concrete native symbol types override the accessors that apply
/// to their symbol kind.
class LLVM_ABI NativeRawSymbol : public IPDBRawSymbol {
  friend class SymbolCache;
  virtual void initialize() {}

public:
  /// Construct a native raw symbol bound to \p PDBSession.
  ///
  /// \param PDBSession The native PDB session that owns this symbol.
  /// \param Tag The CodeView/PDB symbol tag for this symbol.
  /// \param SymbolId Unique symbol index id within the session.
  NativeRawSymbol(NativeSession &PDBSession, PDB_SymType Tag,
                  SymIndexId SymbolId);

  /// Dump this symbol's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  /// Find child symbols of the given type.
  ///
  /// \param Type Symbol tag of children to enumerate.
  /// \returns An enumerator over matching child symbols.
  std::unique_ptr<IPDBEnumSymbols>
    findChildren(PDB_SymType Type) const override;
  /// Find child symbols of the given type whose name matches \p Name.
  ///
  /// \param Type Symbol tag of children to enumerate.
  /// \param Name Name pattern to match.
  /// \param Flags Name-search options controlling the match.
  /// \returns An enumerator over matching child symbols.
  std::unique_ptr<IPDBEnumSymbols>
    findChildren(PDB_SymType Type, StringRef Name,
      PDB_NameSearchFlags Flags) const override;
  /// Find child symbols by section and offset address.
  ///
  /// \param Type Symbol tag of children to enumerate.
  /// \param Name Name pattern to match.
  /// \param Flags Name-search options controlling the match.
  /// \param Section Section index of the address.
  /// \param Offset Offset within the section.
  /// \returns An enumerator over matching child symbols.
  std::unique_ptr<IPDBEnumSymbols>
    findChildrenByAddr(PDB_SymType Type, StringRef Name,
                       PDB_NameSearchFlags Flags,
                       uint32_t Section, uint32_t Offset) const override;
  /// Find child symbols by virtual address.
  ///
  /// \param Type Symbol tag of children to enumerate.
  /// \param Name Name pattern to match.
  /// \param Flags Name-search options controlling the match.
  /// \param VA Virtual address to search.
  /// \returns An enumerator over matching child symbols.
  std::unique_ptr<IPDBEnumSymbols>
    findChildrenByVA(PDB_SymType Type, StringRef Name, PDB_NameSearchFlags Flags,
                     uint64_t VA) const override;
  /// Find child symbols by relative virtual address.
  ///
  /// \param Type Symbol tag of children to enumerate.
  /// \param Name Name pattern to match.
  /// \param Flags Name-search options controlling the match.
  /// \param RVA Relative virtual address to search.
  /// \returns An enumerator over matching child symbols.
  std::unique_ptr<IPDBEnumSymbols>
    findChildrenByRVA(PDB_SymType Type, StringRef Name, PDB_NameSearchFlags Flags,
      uint32_t RVA) const override;

  /// Find inline frames at the given section and offset address.
  ///
  /// \param Section Section index of the address.
  /// \param Offset Offset within the section.
  /// \returns An enumerator over matching inline frame symbols.
  std::unique_ptr<IPDBEnumSymbols>
    findInlineFramesByAddr(uint32_t Section, uint32_t Offset) const override;
  /// Find inline frames at the given relative virtual address.
  ///
  /// \param RVA Relative virtual address to search.
  /// \returns An enumerator over matching inline frame symbols.
  std::unique_ptr<IPDBEnumSymbols>
    findInlineFramesByRVA(uint32_t RVA) const override;
  /// Find inline frames at the given virtual address.
  ///
  /// \param VA Virtual address to search.
  /// \returns An enumerator over matching inline frame symbols.
  std::unique_ptr<IPDBEnumSymbols>
    findInlineFramesByVA(uint64_t VA) const override;

  /// Find line numbers for all inlinees of this symbol.
  ///
  /// \returns An enumerator over matching line-number entries.
  std::unique_ptr<IPDBEnumLineNumbers> findInlineeLines() const override;
  /// Find inlinee line numbers covering the given section and offset range.
  ///
  /// \param Section Section index of the start address.
  /// \param Offset Offset within the section.
  /// \param Length Length in bytes of the address range.
  /// \returns An enumerator over matching line-number entries.
  std::unique_ptr<IPDBEnumLineNumbers>
    findInlineeLinesByAddr(uint32_t Section, uint32_t Offset,
                           uint32_t Length) const override;
  /// Find inlinee line numbers covering the given RVA range.
  ///
  /// \param RVA Relative virtual address of the start of the range.
  /// \param Length Length in bytes of the address range.
  /// \returns An enumerator over matching line-number entries.
  std::unique_ptr<IPDBEnumLineNumbers>
    findInlineeLinesByRVA(uint32_t RVA, uint32_t Length) const override;
  /// Find inlinee line numbers covering the given VA range.
  ///
  /// \param VA Virtual address of the start of the range.
  /// \param Length Length in bytes of the address range.
  /// \returns An enumerator over matching line-number entries.
  std::unique_ptr<IPDBEnumLineNumbers>
    findInlineeLinesByVA(uint64_t VA, uint32_t Length) const override;

  /// Append this symbol's raw data bytes to \p Bytes.
  ///
  /// \param Bytes Vector that receives the data bytes.
  void getDataBytes(SmallVector<uint8_t, 32> &Bytes) const override;
  /// Fill \p Version with the frontend compiler version for this symbol.
  ///
  /// \param Version Receives the frontend version components.
  void getFrontEndVersion(VersionInfo &Version) const override;
  /// Fill \p Version with the backend compiler version for this symbol.
  ///
  /// \param Version Receives the backend version components.
  void getBackEndVersion(VersionInfo &Version) const override;
  /// Return the access level of this member symbol.
  ///
  /// \returns The access level of this member symbol.
  PDB_MemberAccess getAccess() const override;
  /// Return the section-relative address offset of this symbol.
  ///
  /// \returns The section-relative address offset of this symbol.
  uint32_t getAddressOffset() const override;
  /// Return the section index of this symbol's address.
  ///
  /// \returns The section index of this symbol's address.
  uint32_t getAddressSection() const override;
  /// Return the PDB age associated with this symbol.
  ///
  /// \returns The PDB age associated with this symbol.
  uint32_t getAge() const override;
  /// Return the symbol id of this array's index type.
  ///
  /// \returns The symbol id of this array's index type.
  SymIndexId getArrayIndexTypeId() const override;
  /// Return the base data offset for this symbol.
  ///
  /// \returns The base data offset for this symbol.
  uint32_t getBaseDataOffset() const override;
  /// Return the base data slot for this symbol.
  ///
  /// \returns The base data slot for this symbol.
  uint32_t getBaseDataSlot() const override;
  /// Return the symbol id of this symbol's base symbol.
  ///
  /// \returns The symbol id of this symbol's base symbol.
  SymIndexId getBaseSymbolId() const override;
  /// Return the builtin type kind for this type symbol.
  ///
  /// \returns The builtin type kind for this type symbol.
  PDB_BuiltinType getBuiltinType() const override;
  /// Return the bit position of this bitfield member.
  ///
  /// \returns The bit position of this bitfield member.
  uint32_t getBitPosition() const override;
  /// Return the calling convention of this function type.
  ///
  /// \returns The calling convention of this function type.
  PDB_CallingConv getCallingConvention() const override;
  /// Return the symbol id of this symbol's class parent.
  ///
  /// \returns The symbol id of this symbol's class parent.
  SymIndexId getClassParentId() const override;
  /// Return the name of the compiler that produced this symbol.
  ///
  /// \returns The name of the compiler that produced this symbol.
  std::string getCompilerName() const override;
  /// Return the element or parameter count associated with this symbol.
  ///
  /// \returns The element or parameter count associated with this symbol.
  uint32_t getCount() const override;
  /// Return the number of live ranges associated with this symbol.
  ///
  /// \returns The number of live ranges associated with this symbol.
  uint32_t getCountLiveRanges() const override;
  /// Return the source language of this symbol.
  ///
  /// \returns The source language of this symbol.
  PDB_Lang getLanguage() const override;
  /// Return the symbol id of this symbol's lexical parent.
  ///
  /// \returns The symbol id of this symbol's lexical parent.
  SymIndexId getLexicalParentId() const override;
  /// Return the library or object name associated with this symbol.
  ///
  /// \returns The library or object name associated with this symbol.
  std::string getLibraryName() const override;
  /// Return the section-relative offset of this symbol's live-range start.
  ///
  /// \returns The section-relative offset of this symbol's live-range start.
  uint32_t getLiveRangeStartAddressOffset() const override;
  /// Return the section index of this symbol's live-range start address.
  ///
  /// \returns The section index of this symbol's live-range start address.
  uint32_t getLiveRangeStartAddressSection() const override;
  /// Return the RVA of this symbol's live-range start.
  ///
  /// \returns The RVA of this symbol's live-range start.
  uint32_t getLiveRangeStartRelativeVirtualAddress() const override;
  /// Return the register id used as the local base pointer.
  ///
  /// \returns The register id used as the local base pointer.
  codeview::RegisterId getLocalBasePointerRegisterId() const override;
  /// Return the symbol id of this dimension's lower bound.
  ///
  /// \returns The symbol id of this dimension's lower bound.
  SymIndexId getLowerBoundId() const override;
  /// Return the memory-space kind for this HLSL symbol.
  ///
  /// \returns The memory-space kind for this HLSL symbol.
  uint32_t getMemorySpaceKind() const override;
  /// Return the name of this symbol.
  ///
  /// \returns The name of this symbol.
  std::string getName() const override;
  /// Return the number of accelerator pointer tags on this symbol.
  ///
  /// \returns The number of accelerator pointer tags on this symbol.
  uint32_t getNumberOfAcceleratorPointerTags() const override;
  /// Return the number of columns in this matrix type.
  ///
  /// \returns The number of columns in this matrix type.
  uint32_t getNumberOfColumns() const override;
  /// Return the number of type modifiers on this type.
  ///
  /// \returns The number of type modifiers on this type.
  uint32_t getNumberOfModifiers() const override;
  /// Return the number of register indices on this symbol.
  ///
  /// \returns The number of register indices on this symbol.
  uint32_t getNumberOfRegisterIndices() const override;
  /// Return the number of rows in this matrix type.
  ///
  /// \returns The number of rows in this matrix type.
  uint32_t getNumberOfRows() const override;
  /// Return the object file name associated with this symbol.
  ///
  /// \returns The object file name associated with this symbol.
  std::string getObjectFileName() const override;
  /// Return the OEM id associated with this symbol.
  ///
  /// \returns The OEM id associated with this symbol.
  uint32_t getOemId() const override;
  /// Return the OEM symbol id associated with this symbol.
  ///
  /// \returns The OEM symbol id associated with this symbol.
  SymIndexId getOemSymbolId() const override;
  /// Return the offset of this member within its UDT.
  ///
  /// \returns The offset of this member within its UDT.
  uint32_t getOffsetInUdt() const override;
  /// Return the target CPU platform for this symbol.
  ///
  /// \returns The target CPU platform for this symbol.
  PDB_Cpu getPlatform() const override;
  /// Return the rank of this multi-dimensional array type.
  ///
  /// \returns The rank of this multi-dimensional array type.
  uint32_t getRank() const override;
  /// Return the register id where this symbol is located.
  ///
  /// \returns The register id where this symbol is located.
  codeview::RegisterId getRegisterId() const override;
  /// Return the register type associated with this symbol.
  ///
  /// \returns The register type associated with this symbol.
  uint32_t getRegisterType() const override;
  /// Return the relative virtual address of this symbol.
  ///
  /// \returns The relative virtual address of this symbol.
  uint32_t getRelativeVirtualAddress() const override;
  /// Return the sampler slot for this HLSL resource.
  ///
  /// \returns The sampler slot for this HLSL resource.
  uint32_t getSamplerSlot() const override;
  /// Return the signature value associated with this symbol.
  ///
  /// \returns The signature value associated with this symbol.
  uint32_t getSignature() const override;
  /// Return the size in bytes of this member within its UDT.
  ///
  /// \returns The size in bytes of this member within its UDT.
  uint32_t getSizeInUdt() const override;
  /// Return the slot index associated with this symbol.
  ///
  /// \returns The slot index associated with this symbol.
  uint32_t getSlot() const override;
  /// Return the source file name associated with this symbol.
  ///
  /// \returns The source file name associated with this symbol.
  std::string getSourceFileName() const override;
  /// Return the source line where this type is defined.
  ///
  /// \returns A line-number entry for the type definition, or null if none.
  std::unique_ptr<IPDBLineNumber> getSrcLineOnTypeDefn() const override;
  /// Return the stride between elements of this array or matrix.
  ///
  /// \returns The stride between elements of this array or matrix.
  uint32_t getStride() const override;
  /// Return the symbol id of this symbol's subtype.
  ///
  /// \returns The symbol id of this symbol's subtype.
  SymIndexId getSubTypeId() const override;
  /// Return the path of the PDB or symbols file for this symbol.
  ///
  /// \returns The path of the PDB or symbols file for this symbol.
  std::string getSymbolsFileName() const override;
  /// Return this symbol's unique SymIndexId within the session.
  ///
  /// \returns This symbol's unique SymIndexId within the session.
  SymIndexId getSymIndexId() const override;
  /// Return the target offset for this thunk or branch symbol.
  ///
  /// \returns The target offset for this thunk or branch symbol.
  uint32_t getTargetOffset() const override;
  /// Return the target relative virtual address for this thunk or branch.
  ///
  /// \returns The target relative virtual address for this thunk or branch.
  uint32_t getTargetRelativeVirtualAddress() const override;
  /// Return the target virtual address for this thunk or branch.
  ///
  /// \returns The target virtual address for this thunk or branch.
  uint64_t getTargetVirtualAddress() const override;
  /// Return the target section for this thunk or branch symbol.
  ///
  /// \returns The target section for this thunk or branch symbol.
  uint32_t getTargetSection() const override;
  /// Return the texture slot for this HLSL resource.
  ///
  /// \returns The texture slot for this HLSL resource.
  uint32_t getTextureSlot() const override;
  /// Return the timestamp associated with this symbol.
  ///
  /// \returns The timestamp associated with this symbol.
  uint32_t getTimeStamp() const override;
  /// Return the metadata token associated with this managed symbol.
  ///
  /// \returns The metadata token associated with this managed symbol.
  uint32_t getToken() const override;
  /// Return the symbol id of this symbol's type.
  ///
  /// \returns The symbol id of this symbol's type.
  SymIndexId getTypeId() const override;
  /// Return the UAV slot for this HLSL resource.
  ///
  /// \returns The UAV slot for this HLSL resource.
  uint32_t getUavSlot() const override;
  /// Return the undecorated (demangled) name of this symbol.
  ///
  /// \returns The undecorated (demangled) name of this symbol.
  std::string getUndecoratedName() const override;
  /// Return the undecorated name using the given undecoration flags.
  ///
  /// \param Flags Flags controlling which undecoration options to apply.
  /// \returns The undecorated name string.
  std::string getUndecoratedNameEx(PDB_UndnameFlags Flags) const override;
  /// Return the symbol id of this modified type's unmodified type.
  ///
  /// \returns The symbol id of this modified type's unmodified type.
  SymIndexId getUnmodifiedTypeId() const override;
  /// Return the symbol id of this dimension's upper bound.
  ///
  /// \returns The symbol id of this dimension's upper bound.
  SymIndexId getUpperBoundId() const override;
  /// Return the constant value associated with this symbol.
  ///
  /// \returns The constant value associated with this symbol.
  Variant getValue() const override;
  /// Return the virtual base displacement index for this base class.
  ///
  /// \returns The virtual base displacement index for this base class.
  uint32_t getVirtualBaseDispIndex() const override;
  /// Return the virtual base offset for this base class.
  ///
  /// \returns The virtual base offset for this base class.
  uint32_t getVirtualBaseOffset() const override;
  /// Return the symbol id of this UDT's virtual table shape.
  ///
  /// \returns The symbol id of this UDT's virtual table shape.
  SymIndexId getVirtualTableShapeId() const override;
  /// Return the builtin type describing this virtual base table.
  ///
  /// \returns The virtual base table type, or null if none.
  std::unique_ptr<PDBSymbolTypeBuiltin>
  getVirtualBaseTableType() const override;
  /// Return the data kind of this data symbol.
  ///
  /// \returns The data kind of this data symbol.
  PDB_DataKind getDataKind() const override;
  /// Return the symbol tag (kind) of this symbol.
  ///
  /// \returns The symbol tag (kind) of this symbol.
  PDB_SymType getSymTag() const override;
  /// Return the GUID associated with this symbol.
  ///
  /// \returns The GUID associated with this symbol.
  codeview::GUID getGuid() const override;
  /// Return the offset associated with this symbol.
  ///
  /// \returns The offset associated with this symbol.
  int32_t getOffset() const override;
  /// Return the this-adjust value for this member function.
  ///
  /// \returns The this-adjust value for this member function.
  int32_t getThisAdjust() const override;
  /// Return the virtual base pointer offset for this base class.
  ///
  /// \returns The virtual base pointer offset for this base class.
  int32_t getVirtualBasePointerOffset() const override;
  /// Return the location type of this symbol.
  ///
  /// \returns The location type of this symbol.
  PDB_LocType getLocationType() const override;
  /// Return the machine type associated with this symbol.
  ///
  /// \returns The machine type associated with this symbol.
  PDB_Machine getMachineType() const override;
  /// Return the thunk ordinal for this thunk symbol.
  ///
  /// \returns The thunk ordinal for this thunk symbol.
  codeview::ThunkOrdinal getThunkOrdinal() const override;
  /// Return the length in bytes of this symbol.
  ///
  /// \returns The length in bytes of this symbol.
  uint64_t getLength() const override;
  /// Return the length in bytes of this symbol's live range.
  ///
  /// \returns The length in bytes of this symbol's live range.
  uint64_t getLiveRangeLength() const override;
  /// Return the virtual address of this symbol.
  ///
  /// \returns The virtual address of this symbol.
  uint64_t getVirtualAddress() const override;
  /// Return the UDT kind (class, struct, union, or interface).
  ///
  /// \returns The UDT kind (class, struct, union, or interface).
  PDB_UdtType getUdtKind() const override;
  /// Return true if this UDT has a constructor.
  ///
  /// \returns True if this UDT has a constructor.
  bool hasConstructor() const override;
  /// Return true if this function uses a custom calling convention.
  ///
  /// \returns True if this function uses a custom calling convention.
  bool hasCustomCallingConvention() const override;
  /// Return true if this function uses a far return.
  ///
  /// \returns True if this function uses a far return.
  bool hasFarReturn() const override;
  /// Return true if this symbol represents code.
  ///
  /// \returns True if this symbol represents code.
  bool isCode() const override;
  /// Return true if this symbol was generated by the compiler.
  ///
  /// \returns True if this symbol was generated by the compiler.
  bool isCompilerGenerated() const override;
  /// Return true if this type is const-qualified.
  ///
  /// \returns True if this type is const-qualified.
  bool isConstType() const override;
  /// Return true if Edit and Continue is enabled for this symbol.
  ///
  /// \returns True if Edit and Continue is enabled for this symbol.
  bool isEditAndContinueEnabled() const override;
  /// Return true if this symbol is a function.
  ///
  /// \returns True if this symbol is a function.
  bool isFunction() const override;
  /// Return true if the address of this symbol has been taken.
  ///
  /// \returns True if the address of this symbol has been taken.
  bool getAddressTaken() const override;
  /// Return true if this symbol has no stack ordering.
  ///
  /// \returns True if this symbol has no stack ordering.
  bool getNoStackOrdering() const override;
  /// Return true if this function uses alloca.
  ///
  /// \returns True if this function uses alloca.
  bool hasAlloca() const override;
  /// Return true if this UDT has an assignment operator.
  ///
  /// \returns True if this UDT has an assignment operator.
  bool hasAssignmentOperator() const override;
  /// Return true if this symbol has C types.
  ///
  /// \returns True if this symbol has C types.
  bool hasCTypes() const override;
  /// Return true if this UDT has a cast operator.
  ///
  /// \returns True if this UDT has a cast operator.
  bool hasCastOperator() const override;
  /// Return true if this symbol has debug information.
  ///
  /// \returns True if this symbol has debug information.
  bool hasDebugInfo() const override;
  /// Return true if this function uses C++ exception handling.
  ///
  /// \returns True if this function uses C++ exception handling.
  bool hasEH() const override;
  /// Return true if this function uses asynchronous exception handling.
  ///
  /// \returns True if this function uses asynchronous exception handling.
  bool hasEHa() const override;
  /// Return true if this function contains inline assembly.
  ///
  /// \returns True if this function contains inline assembly.
  bool hasInlAsm() const override;
  /// Return true if this function has the inline attribute.
  ///
  /// \returns True if this function has the inline attribute.
  bool hasInlineAttribute() const override;
  /// Return true if this function uses an interrupt return.
  ///
  /// \returns True if this function uses an interrupt return.
  bool hasInterruptReturn() const override;
  /// Return true if this function has a frame pointer.
  ///
  /// \returns True if this function has a frame pointer.
  bool hasFramePointer() const override;
  /// Return true if this function uses longjmp.
  ///
  /// \returns True if this function uses longjmp.
  bool hasLongJump() const override;
  /// Return true if this symbol contains managed code.
  ///
  /// \returns True if this symbol contains managed code.
  bool hasManagedCode() const override;
  /// Return true if this UDT has nested types.
  ///
  /// \returns True if this UDT has nested types.
  bool hasNestedTypes() const override;
  /// Return true if this function has the noinline attribute.
  ///
  /// \returns True if this function has the noinline attribute.
  bool hasNoInlineAttribute() const override;
  /// Return true if this function has the noreturn attribute.
  ///
  /// \returns True if this function has the noreturn attribute.
  bool hasNoReturnAttribute() const override;
  /// Return true if this symbol has optimized-code debug info.
  ///
  /// \returns True if this symbol has optimized-code debug info.
  bool hasOptimizedCodeDebugInfo() const override;
  /// Return true if this UDT has an overloaded operator.
  ///
  /// \returns True if this UDT has an overloaded operator.
  bool hasOverloadedOperator() const override;
  /// Return true if this function uses structured exception handling.
  ///
  /// \returns True if this function uses structured exception handling.
  bool hasSEH() const override;
  /// Return true if this function has security checks.
  ///
  /// \returns True if this function has security checks.
  bool hasSecurityChecks() const override;
  /// Return true if this function uses setjmp.
  ///
  /// \returns True if this function uses setjmp.
  bool hasSetJump() const override;
  /// Return true if this function has strict GS checks.
  ///
  /// \returns True if this function has strict GS checks.
  bool hasStrictGSCheck() const override;
  /// Return true if this is an accelerator group shared local.
  ///
  /// \returns True if this is an accelerator group shared local.
  bool isAcceleratorGroupSharedLocal() const override;
  /// Return true if this is an accelerator pointer-tag live range.
  ///
  /// \returns True if this is an accelerator pointer-tag live range.
  bool isAcceleratorPointerTagLiveRange() const override;
  /// Return true if this is an accelerator stub function.
  ///
  /// \returns True if this is an accelerator stub function.
  bool isAcceleratorStubFunction() const override;
  /// Return true if this symbol is aggregated.
  ///
  /// \returns True if this symbol is aggregated.
  bool isAggregated() const override;
  /// Return true if this is an introducing virtual function.
  ///
  /// \returns True if this is an introducing virtual function.
  bool isIntroVirtualFunction() const override;
  /// Return true if this symbol was converted from CIL.
  ///
  /// \returns True if this symbol was converted from CIL.
  bool isCVTCIL() const override;
  /// Return true if this constructor initializes a virtual base.
  ///
  /// \returns True if this constructor initializes a virtual base.
  bool isConstructorVirtualBase() const override;
  /// Return true if this function returns a C++ UDT.
  ///
  /// \returns True if this function returns a C++ UDT.
  bool isCxxReturnUdt() const override;
  /// Return true if this data is aligned.
  ///
  /// \returns True if this data is aligned.
  bool isDataAligned() const override;
  /// Return true if this is HLSL data.
  ///
  /// \returns True if this is HLSL data.
  bool isHLSLData() const override;
  /// Return true if this code is hotpatchable.
  ///
  /// \returns True if this code is hotpatchable.
  bool isHotpatchable() const override;
  /// Return true if this is an indirect virtual base class.
  ///
  /// \returns True if this is an indirect virtual base class.
  bool isIndirectVirtualBaseClass() const override;
  /// Return true if this UDT is an interface.
  ///
  /// \returns True if this UDT is an interface.
  bool isInterfaceUdt() const override;
  /// Return true if this is an intrinsic symbol.
  ///
  /// \returns True if this is an intrinsic symbol.
  bool isIntrinsic() const override;
  /// Return true if this symbol was compiled with link-time code generation.
  ///
  /// \returns True if this symbol was compiled with link-time code generation.
  bool isLTCG() const override;
  /// Return true if this symbol's location depends on control flow.
  ///
  /// \returns True if this symbol's location depends on control flow.
  bool isLocationControlFlowDependent() const override;
  /// Return true if this is an MSIL netmodule.
  ///
  /// \returns True if this is an MSIL netmodule.
  bool isMSILNetmodule() const override;
  /// Return true if this matrix type is stored in row-major order.
  ///
  /// \returns True if this matrix type is stored in row-major order.
  bool isMatrixRowMajor() const override;
  /// Return true if this symbol is managed code.
  ///
  /// \returns True if this symbol is managed code.
  bool isManagedCode() const override;
  /// Return true if this symbol is MSIL code.
  ///
  /// \returns True if this symbol is MSIL code.
  bool isMSILCode() const override;
  /// Return true if this member pointer uses multiple inheritance.
  ///
  /// \returns True if this member pointer uses multiple inheritance.
  bool isMultipleInheritance() const override;
  /// Return true if this function is declared naked.
  ///
  /// \returns True if this function is declared naked.
  bool isNaked() const override;
  /// Return true if this symbol is nested within another type.
  ///
  /// \returns True if this symbol is nested within another type.
  bool isNested() const override;
  /// Return true if this local was optimized away.
  ///
  /// \returns True if this local was optimized away.
  bool isOptimizedAway() const override;
  /// Return true if this UDT is packed.
  ///
  /// \returns True if this UDT is packed.
  bool isPacked() const override;
  /// Return true if this pointer is based on a symbol value.
  ///
  /// \returns True if this pointer is based on a symbol value.
  bool isPointerBasedOnSymbolValue() const override;
  /// Return true if this is a pointer to a data member.
  ///
  /// \returns True if this is a pointer to a data member.
  bool isPointerToDataMember() const override;
  /// Return true if this is a pointer to a member function.
  ///
  /// \returns True if this is a pointer to a member function.
  bool isPointerToMemberFunction() const override;
  /// Return true if this function is pure virtual.
  ///
  /// \returns True if this function is pure virtual.
  bool isPureVirtual() const override;
  /// Return true if this is an rvalue reference type.
  ///
  /// \returns True if this is an rvalue reference type.
  bool isRValueReference() const override;
  /// Return true if this UDT is a ref UDT.
  ///
  /// \returns True if this UDT is a ref UDT.
  bool isRefUdt() const override;
  /// Return true if this is a reference type.
  ///
  /// \returns True if this is a reference type.
  bool isReference() const override;
  /// Return true if this is a restricted type.
  ///
  /// \returns True if this is a restricted type.
  bool isRestrictedType() const override;
  /// Return true if this symbol is a return value.
  ///
  /// \returns True if this symbol is a return value.
  bool isReturnValue() const override;
  /// Return true if this symbol uses safe buffers.
  ///
  /// \returns True if this symbol uses safe buffers.
  bool isSafeBuffers() const override;
  /// Return true if this symbol is scoped.
  ///
  /// \returns True if this symbol is scoped.
  bool isScoped() const override;
  /// Return true if this symbol was compiled with SDL checks.
  ///
  /// \returns True if this symbol was compiled with SDL checks.
  bool isSdl() const override;
  /// Return true if this member pointer uses single inheritance.
  ///
  /// \returns True if this member pointer uses single inheritance.
  bool isSingleInheritance() const override;
  /// Return true if this symbol is split (PGO cold/hot).
  ///
  /// \returns True if this symbol is split (PGO cold/hot).
  bool isSplitted() const override;
  /// Return true if this symbol is static.
  ///
  /// \returns True if this symbol is static.
  bool isStatic() const override;
  /// Return true if this module has private symbols.
  ///
  /// \returns True if this module has private symbols.
  bool hasPrivateSymbols() const override;
  /// Return true if this type is unaligned.
  ///
  /// \returns True if this type is unaligned.
  bool isUnalignedType() const override;
  /// Return true if this code is unreachable.
  ///
  /// \returns True if this code is unreachable.
  bool isUnreached() const override;
  /// Return true if this UDT is a value UDT.
  ///
  /// \returns True if this UDT is a value UDT.
  bool isValueUdt() const override;
  /// Return true if this function or base is virtual.
  ///
  /// \returns True if this function or base is virtual.
  bool isVirtual() const override;
  /// Return true if this is a virtual base class.
  ///
  /// \returns True if this is a virtual base class.
  bool isVirtualBaseClass() const override;
  /// Return true if this member pointer uses virtual inheritance.
  ///
  /// \returns True if this member pointer uses virtual inheritance.
  bool isVirtualInheritance() const override;
  /// Return true if this type is volatile-qualified.
  ///
  /// \returns True if this type is volatile-qualified.
  bool isVolatileType() const override;
  /// Return true if this function was inlined.
  ///
  /// \returns True if this function was inlined.
  bool wasInlined() const override;
  /// Return the unused string property for this symbol.
  ///
  /// \returns The unused string property for this symbol.
  std::string getUnused() const override;

protected:
  /// The native PDB session that owns this symbol.
  NativeSession &Session;
  /// The PDB symbol tag (kind) of this symbol.
  PDB_SymType Tag;
  /// Unique symbol index id of this symbol within the session.
  SymIndexId SymbolId;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVERAWSYMBOL_H
