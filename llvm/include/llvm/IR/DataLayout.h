//===- llvm/DataLayout.h - Data size & alignment info -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines layout properties related to datatype size/offset/alignment
// information.  It uses lazy annotations to cache information about how
// structure types are laid out and used.
//
// This structure should be created once, filled in if the defaults are not
// correct and then passed around by const&.  None of the members functions
// require modification to the object.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DATALAYOUT_H
#define LLVM_IR_DATALAYOUT_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TrailingObjects.h"
#include "llvm/Support/TypeSize.h"
#include <cassert>
#include <cstdint>
#include <string>

// This needs to be outside of the namespace, to avoid conflict with llvm-c
// decl.
using LLVMTargetDataRef = struct LLVMOpaqueTargetData *;

namespace llvm {

class GlobalVariable;
class LLVMContext;
class StructLayout;
class Triple;
class Value;

// FIXME: Currently the DataLayout string carries a "preferred alignment"
// for types. As the DataLayout is module/global, this should likely be
// sunk down to an FTTI element that is queried rather than a global
// preference.

/// A parsed version of the target data layout string in and methods for
/// querying it.
///
/// The target data layout string is specified *by the target* - a frontend
/// generating LLVM IR is required to generate the right target data for the
/// target being codegen'd to.
class DataLayout {
public:
  /// Primitive type specification.
  struct PrimitiveSpec {
    /// Bit width of the primitive type.
    uint32_t BitWidth;
    /// ABI-required alignment for this type.
    Align ABIAlign;
    /// Preferred alignment for this type.
    Align PrefAlign;

    /// Returns true if this specification equals \p Other.
    ///
    /// \param Other Specification to compare against.
    /// @return True if this specification equals \p Other.
    LLVM_ABI bool operator==(const PrimitiveSpec &Other) const;
  };

  /// Pointer type specification.
  struct PointerSpec {
    /// Address space number for this pointer specification.
    uint32_t AddrSpace;
    /// Bit width of the full pointer representation.
    uint32_t BitWidth;
    /// ABI-required alignment for pointers in this address space.
    Align ABIAlign;
    /// Preferred alignment for pointers in this address space.
    Align PrefAlign;
    /// Index (and address) bit width used for GEP and addressing.
    ///
    /// The index bit width also defines the address size in this address space.
    /// If the index width is less than the representation bit width, the
    /// pointer is non-integral and bits beyond the index width could be used
    /// for additional metadata (e.g. AMDGPU buffer fat pointers with bounds
    /// and other flags or CHERI capabilities that contain bounds+permissions).
    uint32_t IndexBitWidth;
    /// True if pointers lack a well-defined bitwise representation.
    ///
    /// Pointers in this address space don't have a well-defined bitwise
    /// representation (e.g. they may be relocated by a copying garbage
    /// collector and thus have different addresses at different times).
    bool HasUnstableRepresentation;
    /// True if pointers carry additional out-of-band state when stored.
    ///
    /// Pointers in this address space have additional state bits that are
    /// located at a target-defined location when stored in memory. An example
    /// of this would be CHERI capabilities where the validity bit is stored
    /// separately from the pointer address+bounds information.
    bool HasExternalState;
    /// Symbolic name of the address space.
    std::string AddrSpaceName;
    /// The null pointer bit representation for this address space.
    APInt NullPtrValue;

    /// Returns true if this specification equals \p Other.
    ///
    /// \param Other Specification to compare against.
    /// @return True if this specification equals \p Other.
    LLVM_ABI bool operator==(const PointerSpec &Other) const;
  };

  /// How function-pointer alignment relates to function alignment.
  enum class FunctionPtrAlignType {
    /// The function pointer alignment is independent of the function alignment.
    Independent,
    /// The function pointer alignment is a multiple of the function alignment.
    MultipleOfFunctionAlign,
  };

private:
  bool BigEndian = false;
  bool VectorsAreElementAligned = false;

  unsigned AllocaAddrSpace = 0;
  unsigned ProgramAddrSpace = 0;
  unsigned DefaultGlobalsAddrSpace = 0;

  MaybeAlign StackNaturalAlign;
  MaybeAlign FunctionPtrAlign;
  FunctionPtrAlignType TheFunctionPtrAlignType =
      FunctionPtrAlignType::Independent;

  enum ManglingModeT {
    MM_None,
    MM_ELF,
    MM_MachO,
    MM_WinCOFF,
    MM_WinCOFFX86,
    MM_GOFF,
    MM_Mips,
    MM_XCOFF
  };
  ManglingModeT ManglingMode = MM_None;

  // FIXME: `unsigned char` truncates the value parsed by `parseSpecifier`.
  SmallVector<unsigned char, 8> LegalIntWidths;

  /// Primitive type specifications. Sorted and uniqued by type bit width.
  SmallVector<PrimitiveSpec, 6> IntSpecs;
  SmallVector<PrimitiveSpec, 4> FloatSpecs;
  SmallVector<PrimitiveSpec, 10> VectorSpecs;

  /// Pointer type specifications. Sorted and uniqued by address space number.
  SmallVector<PointerSpec, 8> PointerSpecs;

  /// The string representation used to create this DataLayout
  std::string StringRepresentation;

  /// Struct type ABI and preferred alignments. The default spec is "a:8:64".
  Align StructABIAlignment = Align::Constant<1>();
  Align StructPrefAlignment = Align::Constant<8>();

  // The StructType -> StructLayout map.
  mutable void *LayoutMap = nullptr;

  /// Sets or updates the specification for the given primitive type.
  void setPrimitiveSpec(char Specifier, uint32_t BitWidth, Align ABIAlign,
                        Align PrefAlign);

  /// Searches for a pointer specification that matches the given address space.
  /// Returns the default address space specification if not found.
  LLVM_ABI const PointerSpec &getPointerSpec(uint32_t AddrSpace) const;

  /// Sets or updates the specification for pointer in the given address space.
  void setPointerSpec(uint32_t AddrSpace, uint32_t BitWidth, Align ABIAlign,
                      Align PrefAlign, uint32_t IndexBitWidth,
                      bool HasUnstableRepr, bool HasExternalState,
                      StringRef AddrSpaceName, APInt NullPtrValue);

  /// Internal helper to get alignment for integer of given bitwidth.
  LLVM_ABI Align getIntegerAlignment(uint32_t BitWidth, bool abi_or_pref) const;

  /// Internal helper method that returns requested alignment for type.
  Align getAlignment(Type *Ty, bool abi_or_pref) const;

  /// Attempts to parse primitive specification ('i', 'f', or 'v').
  Error parsePrimitiveSpec(StringRef Spec);

  /// Attempts to parse aggregate specification ('a').
  Error parseAggregateSpec(StringRef Spec);

  /// Attempts to parse pointer specification ('p').
  Error parsePointerSpec(StringRef Spec,
                         SmallDenseSet<StringRef, 8> &AddrSpaceNames);

  /// Attempts to parse a single specification.
  Error parseSpecification(StringRef Spec,
                           SmallVectorImpl<unsigned> &NonIntegralAddressSpaces,
                           SmallDenseSet<StringRef, 8> &AddrSpaceNames);

  /// Attempts to parse a data layout string.
  Error parseLayoutString(StringRef LayoutString);

public:
  /// Constructs a DataLayout with default values.
  LLVM_ABI DataLayout();

  /// Constructs a DataLayout from a specification string.
  /// WARNING: Aborts execution if the string is malformed. Use parse() instead.
  ///
  /// \param LayoutString Target data-layout specification string.
  LLVM_ABI explicit DataLayout(StringRef LayoutString);

  /// Copy-constructs a DataLayout.
  ///
  /// \param DL DataLayout to copy.
  DataLayout(const DataLayout &DL) { *this = DL; }

  /// Destroys this DataLayout and its cached structure layouts.
  LLVM_ABI ~DataLayout(); // Not virtual, do not subclass this class

  /// Copy-assigns another DataLayout into this one.
  ///
  /// \param Other DataLayout to copy from.
  /// @return A reference to this DataLayout after assignment.
  LLVM_ABI DataLayout &operator=(const DataLayout &Other);

  /// Returns true if this DataLayout is equal to \p Other.
  ///
  /// \param Other DataLayout to compare against.
  /// @return True if this DataLayout equals \p Other.
  LLVM_ABI bool operator==(const DataLayout &Other) const;
  /// Returns true if this DataLayout is not equal to \p Other.
  ///
  /// \param Other DataLayout to compare against.
  /// @return True if this DataLayout differs from \p Other.
  bool operator!=(const DataLayout &Other) const { return !(*this == Other); }

  /// Parse a data layout string and return the layout. Return an error
  /// description on failure.
  ///
  /// \param LayoutString Target data-layout specification string.
  /// @return The parsed DataLayout, or an error description on failure.
  LLVM_ABI static Expected<DataLayout> parse(StringRef LayoutString);

  /// Layout endianness...
  /// @return True if this layout is little-endian.
  bool isLittleEndian() const { return !BigEndian; }
  /// Returns true if this layout is big-endian.
  /// @return True if this layout is big-endian.
  bool isBigEndian() const { return BigEndian; }

  /// Whether vectors are element aligned, rather than naturally aligned.
  /// @return True if vectors are element aligned rather than naturally aligned.
  bool vectorsAreElementAligned() const { return VectorsAreElementAligned; }

  /// Returns the string representation of the DataLayout.
  ///
  /// This representation is in the same format accepted by the string
  /// constructor above. This should not be used to compare two DataLayout as
  /// different string can represent the same layout.
  /// @return The string representation of this DataLayout.
  const std::string &getStringRepresentation() const {
    return StringRepresentation;
  }

  /// Test if the DataLayout was constructed from an empty string.
  /// @return True if this DataLayout was constructed from an empty string.
  bool isDefault() const { return StringRepresentation.empty(); }

  /// Returns true if the specified type is known to be a native integer
  /// type supported by the CPU.
  ///
  /// For example, i64 is not native on most 32-bit CPUs and i37 is not native
  /// on any known one. This returns false if the integer width is not legal.
  ///
  /// The width is specified in bits.
  ///
  /// \param Width Integer bit width to test.
  /// @return True if \p Width is a legal native integer width.
  bool isLegalInteger(uint64_t Width) const {
    return llvm::is_contained(LegalIntWidths, Width);
  }

  /// Returns true if the specified integer width is not a legal native width.
  ///
  /// \param Width Integer bit width to test.
  /// @return True if \p Width is not a legal native integer width.
  bool isIllegalInteger(uint64_t Width) const { return !isLegalInteger(Width); }

  /// Returns the natural stack alignment, or MaybeAlign() if one wasn't
  /// specified.
  /// @return The natural stack alignment, or MaybeAlign() if none was specified.
  MaybeAlign getStackAlignment() const { return StackNaturalAlign; }

  /// Returns the address space used for alloca.
  /// @return The address space used for alloca.
  unsigned getAllocaAddrSpace() const { return AllocaAddrSpace; }

  /// Returns a pointer type suitable for alloca in \p Ctx.
  ///
  /// \param Ctx Context in which to create the pointer type.
  /// @return A pointer type suitable for alloca in \p Ctx.
  PointerType *getAllocaPtrType(LLVMContext &Ctx) const {
    return PointerType::get(Ctx, AllocaAddrSpace);
  }

  /// Returns the alignment of function pointers, which may or may not be
  /// related to the alignment of functions.
  /// \see getFunctionPtrAlignType
  /// @return The alignment of function pointers.
  MaybeAlign getFunctionPtrAlign() const { return FunctionPtrAlign; }

  /// Return the type of function pointer alignment.
  /// \see getFunctionPtrAlign
  /// @return The type of function pointer alignment.
  FunctionPtrAlignType getFunctionPtrAlignType() const {
    return TheFunctionPtrAlignType;
  }

  /// Returns the address space used for program code.
  /// @return The address space used for program code.
  unsigned getProgramAddressSpace() const { return ProgramAddrSpace; }
  /// Returns the default address space used for global variables.
  /// @return The default address space used for global variables.
  unsigned getDefaultGlobalsAddressSpace() const {
    return DefaultGlobalsAddrSpace;
  }

  /// Returns true if this layout uses Microsoft fastcall/stdcall mangling.
  /// @return True if this layout uses Microsoft fastcall/stdcall mangling.
  bool hasMicrosoftFastStdCallMangling() const {
    return ManglingMode == MM_WinCOFFX86;
  }

  /// Returns true if symbols with leading question marks should not receive IR
  /// mangling. True for Windows mangling modes.
  /// @return True if symbols with leading question marks should not receive IR mangling.
  bool doNotMangleLeadingQuestionMark() const {
    return ManglingMode == MM_WinCOFF || ManglingMode == MM_WinCOFFX86;
  }

  /// Returns true if this layout has a linker-private global prefix.
  /// @return True if this layout has a linker-private global prefix.
  bool hasLinkerPrivateGlobalPrefix() const { return ManglingMode == MM_MachO; }

  /// Returns the linker-private global prefix, or empty if none.
  /// @return The linker-private global prefix, or empty if none.
  StringRef getLinkerPrivateGlobalPrefix() const {
    if (ManglingMode == MM_MachO)
      return "l";
    return "";
  }

  /// Returns the global symbol prefix character for this layout.
  /// @return The global symbol prefix character for this layout.
  char getGlobalPrefix() const {
    switch (ManglingMode) {
    case MM_None:
    case MM_ELF:
    case MM_GOFF:
    case MM_Mips:
    case MM_WinCOFF:
    case MM_XCOFF:
      return '\0';
    case MM_MachO:
    case MM_WinCOFFX86:
      return '_';
    }
    llvm_unreachable("invalid mangling mode");
  }

  /// Returns the prefix used for internal (local) symbols.
  /// @return The prefix used for internal \(local\) symbols.
  StringRef getInternalSymbolPrefix() const {
    switch (ManglingMode) {
    case MM_None:
      return "";
    case MM_ELF:
    case MM_WinCOFF:
      return ".L";
    case MM_GOFF:
      return "L#";
    case MM_Mips:
      return "$";
    case MM_MachO:
    case MM_WinCOFFX86:
      return "L";
    case MM_XCOFF:
      return "L..";
    }
    llvm_unreachable("invalid mangling mode");
  }

  /// Returns true if the specified type fits in a native integer type
  /// supported by the CPU.
  ///
  /// For example, if the CPU only supports i32 as a native integer type, then
  /// i27 fits in a legal integer type but i45 does not.
  ///
  /// \param Width Integer bit width to test.
  /// @return True if \p Width fits in a legal native integer type.
  bool fitsInLegalInteger(unsigned Width) const {
    for (unsigned LegalIntWidth : LegalIntWidths)
      if (Width <= LegalIntWidth)
        return true;
    return false;
  }

  /// Layout pointer alignment.
  ///
  /// \param AS Address space of the pointer.
  /// @return The ABI alignment for pointers in address space \p AS.
  LLVM_ABI Align getPointerABIAlignment(unsigned AS) const;

  /// Returns the symbolic name of address space \p AS, if any.
  ///
  /// \param AS Address space to look up.
  /// @return The symbolic name of address space \p AS, or empty if none.
  LLVM_ABI StringRef getAddressSpaceName(unsigned AS) const;

  /// Returns the address space number for the given symbolic name, if any.
  ///
  /// \param Name Symbolic address-space name to look up.
  /// @return The address space number for \p Name, or std::nullopt if none.
  LLVM_ABI std::optional<unsigned> getNamedAddressSpace(StringRef Name) const;

  /// Return target's alignment for stack-based pointers
  /// FIXME: The defaults need to be removed once all of
  /// the backends/clients are updated.
  ///
  /// \param AS Address space of the pointer.
  /// @return The preferred alignment for stack-based pointers in address space \p AS.
  LLVM_ABI Align getPointerPrefAlignment(unsigned AS = 0) const;

  /// Returns the pointer representation size in bytes for address space \p AS.
  ///
  /// The difference between this function and getAddressSize() is that this one
  /// returns the size of the entire pointer representation (including metadata
  /// bits for fat pointers) and the latter only returns the number of address
  /// bits.
  /// \sa DataLayout::getAddressSizeInBits
  /// FIXME: The defaults need to be removed once all of
  /// the backends/clients are updated.
  ///
  /// \param AS Address space of the pointer.
  /// @return The pointer representation size in bytes for address space \p AS.
  LLVM_ABI unsigned getPointerSize(unsigned AS = 0) const;

  /// Returns the index size in bytes used for address calculation in \p AS.
  ///
  /// This not only defines the size used in getelementptr operations, but also
  /// the size of addresses in this \p AS. For example, a 64-bit CHERI-enabled
  /// target has 128-bit pointers of which only 64 are used to represent the
  /// address and the remaining ones are used for metadata such as bounds and
  /// access permissions. In this case getPointerSize() returns 16, but
  /// getIndexSize() returns 8. To help with code understanding, the alias
  /// getAddressSize() can be used instead of getIndexSize() to clarify that an
  /// address width is needed.
  ///
  /// \param AS Address space whose index size is requested.
  /// @return The index size in bytes used for address calculation in \p AS.
  LLVM_ABI unsigned getIndexSize(unsigned AS) const;

  /// Returns the integral address size in bytes for address space \p AS.
  ///
  /// This is defined to be the same as getIndexSize(). This exists as a
  /// separate function to make it clearer when reading code that the size of an
  /// address is being requested. While targets exist where index size and the
  /// underlying address width are not identical (e.g. AMDGPU fat pointers with
  /// 48-bit addresses and 32-bit offsets indexing), there is currently no need
  /// to differentiate these properties in LLVM.
  /// \sa DataLayout::getIndexSize
  /// \sa DataLayout::getAddressSizeInBits
  ///
  /// \param AS Address space whose address size is requested.
  /// @return The integral address size in bytes for address space \p AS.
  unsigned getAddressSize(unsigned AS) const { return getIndexSize(AS); }

  /// Return the address spaces with special pointer semantics (such as being
  /// unstable or non-integral).
  /// @return The address spaces with special pointer semantics.
  SmallVector<unsigned, 8> getNonStandardAddressSpaces() const {
    SmallVector<unsigned, 8> AddrSpaces;
    for (const PointerSpec &PS : PointerSpecs) {
      if (PS.HasUnstableRepresentation || PS.HasExternalState ||
          PS.BitWidth != PS.IndexBitWidth)
        AddrSpaces.push_back(PS.AddrSpace);
    }
    return AddrSpaces;
  }

  /// Returns whether address space \p AddrSpace has a non-integral pointer.
  ///
  /// A non-integral pointer is not just an integer address but some other
  /// bitwise representation. When true, passes cannot assume that all bits of
  /// the representation map directly to the allocation address. NOTE: This also
  /// returns true for "unstable" pointers where the representation may be just
  /// an address, but this value can change at any given time (e.g. due to
  /// copying garbage collection). Examples include AMDGPU buffer descriptors
  /// with a 128-bit fat pointer and a 32-bit offset or CHERI capabilities that
  /// contain bounds, permissions and an out-of-band validity bit.
  ///
  /// In general, more specialized functions such as mustNotIntroduceIntToPtr(),
  /// mustNotIntroducePtrToInt(), or hasExternalState() should be preferred over
  /// this one when reasoning about the behavior of IR analysis/transforms.
  /// TODO: should remove/deprecate this once all uses have migrated.
  ///
  /// \param AddrSpace Address space to query.
  /// @return True if address space \p AddrSpace has a non-integral pointer.
  bool isNonIntegralAddressSpace(unsigned AddrSpace) const {
    const auto &PS = getPointerSpec(AddrSpace);
    return PS.BitWidth != PS.IndexBitWidth || PS.HasUnstableRepresentation ||
           PS.HasExternalState;
  }

  /// Returns whether address space \p AddrSpace has an unstable pointer.
  ///
  /// The bitwise pattern of such pointers is allowed to change in a
  /// target-specific way. For example, this could be used for copying garbage
  /// collection where the garbage collector could update the pointer value as
  /// part of the collection sweep.
  ///
  /// \param AddrSpace Address space to query.
  /// @return True if address space \p AddrSpace has an unstable pointer representation.
  bool hasUnstableRepresentation(unsigned AddrSpace) const {
    return getPointerSpec(AddrSpace).HasUnstableRepresentation;
  }
  /// Returns whether \p Ty is a pointer (or vector of pointers) with an
  /// unstable representation.
  ///
  /// \param Ty Type to query.
  /// @return True if \p Ty is a pointer \(or vector of pointers\) with an unstable representation.
  bool hasUnstableRepresentation(Type *Ty) const {
    auto *PTy = dyn_cast<PointerType>(Ty->getScalarType());
    return PTy && hasUnstableRepresentation(PTy->getPointerAddressSpace());
  }

  /// Returns whether address space \p AddrSpace has external pointer state.
  ///
  /// Having external state implies a non-integral pointer representation.
  /// These pointer types must be loaded and stored using appropriate
  /// instructions and cannot use integer loads/stores as this would not
  /// propagate the out-of-band state. An example of such a pointer type is a
  /// CHERI capability that contain bounds, permissions and an out-of-band
  /// validity bit that is invalidated whenever an integer/FP store is performed
  /// to the associated memory location.
  ///
  /// \param AddrSpace Address space to query.
  /// @return True if address space \p AddrSpace has external pointer state.
  bool hasExternalState(unsigned AddrSpace) const {
    return getPointerSpec(AddrSpace).HasExternalState;
  }
  /// Returns whether \p Ty is a pointer (or vector of pointers) with external
  /// state.
  ///
  /// \param Ty Type to query.
  /// @return True if \p Ty is a pointer \(or vector of pointers\) with external state.
  bool hasExternalState(Type *Ty) const {
    auto *PTy = dyn_cast<PointerType>(Ty->getScalarType());
    return PTy && hasExternalState(PTy->getPointerAddressSpace());
  }

  /// Returns the null pointer bit pattern for the given address space.
  ///
  /// \param AS Address space whose null-pointer value is requested.
  /// @return The null pointer bit pattern for address space \p AS.
  APInt getNullPtrValue(unsigned AS) const {
    return getPointerSpec(AS).NullPtrValue;
  }

  /// Returns whether passes must avoid introducing `inttoptr` instructions
  /// for this address space (unless they have target-specific knowledge).
  ///
  /// This is currently the case for non-integral pointer representations with
  /// external state (hasExternalState()) since `inttoptr` cannot recreate the
  /// external state bits.
  /// New `inttoptr` instructions should also be avoided for "unstable" bitwise
  /// representations (hasUnstableRepresentation()) unless the pass knows it is
  /// within a critical section that retains the current representation.
  ///
  /// \param AddrSpace Address space to query.
  /// @return True if passes must avoid introducing `inttoptr` for \p AddrSpace.
  bool mustNotIntroduceIntToPtr(unsigned AddrSpace) const {
    return hasUnstableRepresentation(AddrSpace) || hasExternalState(AddrSpace);
  }

  /// Returns whether passes must avoid introducing `ptrtoint` instructions
  /// for this address space (unless they have target-specific knowledge).
  ///
  /// This is currently the case for pointer address spaces that have an
  /// "unstable" representation (hasUnstableRepresentation()) since the
  /// bitwise pattern of such pointers could change unless the pass knows it is
  /// within a critical section that retains the current representation.
  ///
  /// \param AddrSpace Address space to query.
  /// @return True if passes must avoid introducing `ptrtoint` for \p AddrSpace.
  bool mustNotIntroducePtrToInt(unsigned AddrSpace) const {
    return hasUnstableRepresentation(AddrSpace);
  }

  /// Returns whether \p PT is a non-integral pointer type.
  ///
  /// \param PT Pointer type to query.
  /// @return True if \p PT is a non-integral pointer type.
  bool isNonIntegralPointerType(PointerType *PT) const {
    return isNonIntegralAddressSpace(PT->getAddressSpace());
  }

  /// Returns whether \p Ty is a non-integral pointer (or vector of pointers).
  ///
  /// \param Ty Type to query; must be a pointer or vector of pointers scalar.
  /// @return True if \p Ty is a non-integral pointer \(or vector of pointers\).
  bool isNonIntegralPointerType(Type *Ty) const {
    auto *PTy = dyn_cast<PointerType>(Ty->getScalarType());
    return PTy && isNonIntegralPointerType(PTy);
  }

  /// Returns whether passes must avoid introducing `ptrtoint` for \p Ty.
  ///
  /// \param Ty Type to query; must be a pointer or vector of pointers scalar.
  /// @return True if passes must avoid introducing `ptrtoint` for \p Ty.
  bool mustNotIntroducePtrToInt(Type *Ty) const {
    auto *PTy = dyn_cast<PointerType>(Ty->getScalarType());
    return PTy && mustNotIntroducePtrToInt(PTy->getPointerAddressSpace());
  }

  /// Returns whether passes must avoid introducing `inttoptr` for \p Ty.
  ///
  /// \param Ty Type to query; must be a pointer or vector of pointers scalar.
  /// @return True if passes must avoid introducing `inttoptr` for \p Ty.
  bool mustNotIntroduceIntToPtr(Type *Ty) const {
    auto *PTy = dyn_cast<PointerType>(Ty->getScalarType());
    return PTy && mustNotIntroduceIntToPtr(PTy->getPointerAddressSpace());
  }

  /// Returns the pointer representation size in bits for address space \p AS.
  ///
  /// This is not necessarily the same as the integer address of a pointer (e.g.
  /// for fat pointers).
  /// \sa DataLayout::getAddressSizeInBits()
  /// FIXME: The defaults need to be removed once all of
  /// the backends/clients are updated.
  ///
  /// \param AS Address space of the pointer.
  /// @return The pointer representation size in bits for address space \p AS.
  unsigned getPointerSizeInBits(unsigned AS = 0) const {
    return getPointerSpec(AS).BitWidth;
  }

  /// The size in bits of indices used for address calculation in getelementptr
  /// and for addresses in the given AS. See getIndexSize() for more
  /// information.
  /// \sa DataLayout::getAddressSizeInBits()
  ///
  /// \param AS Address space whose index size is requested.
  /// @return The index size in bits for address space \p AS.
  unsigned getIndexSizeInBits(unsigned AS) const {
    return getPointerSpec(AS).IndexBitWidth;
  }

  /// Returns the size in bits of an address in address space \p AS.
  ///
  /// This is defined to return the same value as getIndexSizeInBits() since
  /// there is currently no target that requires these two properties to have
  /// different values. See getIndexSize() for more information.
  /// \sa DataLayout::getIndexSizeInBits()
  ///
  /// \param AS Address space whose address size is requested.
  /// @return The size in bits of an address in address space \p AS.
  unsigned getAddressSizeInBits(unsigned AS) const {
    return getIndexSizeInBits(AS);
  }

  /// Returns the pointer representation size in bits for \p Ty.
  ///
  /// If this function is called with a pointer type, then the type size of the
  /// pointer is returned. If this function is called with a vector of pointers,
  /// then the type size of the pointer is returned. This should only be called
  /// with a pointer or vector of pointers.
  ///
  /// \param Ty Pointer or vector-of-pointer type.
  /// @return The pointer representation size in bits for \p Ty.
  LLVM_ABI unsigned getPointerTypeSizeInBits(Type *Ty) const;

  /// Returns the size in bits of the index used in GEP calculation for \p Ty.
  ///
  /// The function should be called with pointer or vector of pointers type.
  /// This is defined to return the same value as getAddressSizeInBits(), but
  /// separate functions exist for code clarity.
  ///
  /// \param Ty Pointer or vector-of-pointer type.
  /// @return The index size in bits used in GEP calculation for \p Ty.
  LLVM_ABI unsigned getIndexTypeSizeInBits(Type *Ty) const;

  /// Returns the size in bits of an address for \p Ty.
  ///
  /// This is defined to return the same value as getIndexTypeSizeInBits(), but
  /// separate functions exist for code clarity.
  ///
  /// \param Ty Pointer or vector-of-pointer type.
  /// @return The size in bits of an address for \p Ty.
  unsigned getAddressSizeInBits(Type *Ty) const {
    return getIndexTypeSizeInBits(Ty);
  }

  /// Returns the pointer representation size in bytes for \p Ty.
  ///
  /// \param Ty Pointer or vector-of-pointer type.
  /// @return The pointer representation size in bytes for \p Ty.
  unsigned getPointerTypeSize(Type *Ty) const {
    return getPointerTypeSizeInBits(Ty) / 8;
  }

  /// Size examples:
  ///
  /// Type        SizeInBits  StoreSizeInBits  AllocSizeInBits[*]
  /// ----        ----------  ---------------  ---------------
  ///  i1            1           8                8
  ///  i8            8           8                8
  ///  i19          19          24               32
  ///  i32          32          32               32
  ///  i100        100         104              128
  ///  i128        128         128              128
  ///  Float        32          32               32
  ///  Double       64          64               64
  ///  X86_FP80     80          80               96
  ///
  /// [*] The alloc size depends on the alignment, and thus on the target.
  ///     These values are for x86-32 linux.

  /// Returns the number of bits necessary to hold the specified type.
  ///
  /// If Ty is a scalable vector type, the scalable property will be set and
  /// the runtime size will be a positive integer multiple of the base size.
  ///
  /// For example, returns 36 for i36 and 80 for x86_fp80. The type passed must
  /// have a size (Type::isSized() must return true).
  ///
  /// \param Ty Type whose size in bits is requested.
  /// @return The number of bits necessary to hold \p Ty.
  TypeSize getTypeSizeInBits(Type *Ty) const;

  /// Returns the maximum number of bytes that may be overwritten by
  /// storing the specified type.
  ///
  /// If Ty is a scalable vector type, the scalable property will be set and
  /// the runtime size will be a positive integer multiple of the base size.
  ///
  /// For example, returns 5 for i36 and 10 for x86_fp80.
  ///
  /// \param Ty Type whose store size in bytes is requested.
  /// @return The maximum number of bytes that may be overwritten by storing \p Ty.
  TypeSize getTypeStoreSize(Type *Ty) const {
    TypeSize StoreSizeInBits = getTypeStoreSizeInBits(Ty);
    return {StoreSizeInBits.getKnownMinValue() / 8,
            StoreSizeInBits.isScalable()};
  }

  /// Returns the maximum number of bits that may be overwritten by
  /// storing the specified type; always a multiple of 8.
  ///
  /// If Ty is a scalable vector type, the scalable property will be set and
  /// the runtime size will be a positive integer multiple of the base size.
  ///
  /// For example, returns 40 for i36 and 80 for x86_fp80.
  ///
  /// \param Ty Type whose store size in bits is requested.
  /// @return The maximum number of bits that may be overwritten by storing \p Ty.
  TypeSize getTypeStoreSizeInBits(Type *Ty) const {
    TypeSize BaseSize = getTypeSizeInBits(Ty);
    uint64_t AlignedSizeInBits =
        alignToPowerOf2(BaseSize.getKnownMinValue(), 8);
    return {AlignedSizeInBits, BaseSize.isScalable()};
  }

  /// Returns true if no extra padding bits are needed when storing the
  /// specified type.
  ///
  /// For example, returns false for i19 that has a 24-bit store size.
  ///
  /// \param Ty Type to compare type size against store size.
  /// @return True if no extra padding bits are needed when storing \p Ty.
  bool typeSizeEqualsStoreSize(Type *Ty) const {
    return getTypeSizeInBits(Ty) == getTypeStoreSizeInBits(Ty);
  }

  /// Returns the offset in bytes between successive objects of the
  /// specified type, including alignment padding.
  ///
  /// If Ty is a scalable vector type, the scalable property will be set and
  /// the runtime size will be a positive integer multiple of the base size.
  ///
  /// This is the amount that alloca reserves for this type. For example,
  /// returns 12 or 16 for x86_fp80, depending on alignment.
  ///
  /// \param Ty Type whose allocation size in bytes is requested.
  /// @return The offset in bytes between successive objects of \p Ty, including alignment padding.
  LLVM_ABI TypeSize getTypeAllocSize(Type *Ty) const;

  /// Returns the offset in bits between successive objects of the
  /// specified type, including alignment padding; always a multiple of 8.
  ///
  /// If Ty is a scalable vector type, the scalable property will be set and
  /// the runtime size will be a positive integer multiple of the base size.
  ///
  /// This is the amount that alloca reserves for this type. For example,
  /// returns 96 or 128 for x86_fp80, depending on alignment.
  ///
  /// \param Ty Type whose allocation size in bits is requested.
  /// @return The offset in bits between successive objects of \p Ty, including alignment padding.
  TypeSize getTypeAllocSizeInBits(Type *Ty) const {
    return 8 * getTypeAllocSize(Ty);
  }

  /// Returns the minimum ABI-required alignment for the specified type.
  ///
  /// \param Ty Type whose ABI alignment is requested.
  /// @return The minimum ABI-required alignment for \p Ty.
  LLVM_ABI Align getABITypeAlign(Type *Ty) const;

  /// Helper function to return `Alignment` if it's set or the result of
  /// `getABITypeAlign(Ty)`, in any case the result is a valid alignment.
  ///
  /// \param Alignment Optional alignment override.
  /// \param Ty Type whose ABI alignment is used when \p Alignment is unset.
  /// @return \p Alignment if set; otherwise the ABI alignment of \p Ty.
  inline Align getValueOrABITypeAlignment(MaybeAlign Alignment,
                                          Type *Ty) const {
    return Alignment ? *Alignment : getABITypeAlign(Ty);
  }

  /// Returns the minimum ABI-required alignment for an integer type of
  /// the specified bitwidth.
  ///
  /// \param BitWidth Bit width of the integer type.
  /// @return The minimum ABI-required alignment for an integer of \p BitWidth bits.
  Align getABIIntegerTypeAlignment(unsigned BitWidth) const {
    return getIntegerAlignment(BitWidth, /* abi_or_pref */ true);
  }

  /// Returns the preferred stack/global alignment for the specified
  /// type.
  ///
  /// This is always at least as good as the ABI alignment.
  ///
  /// \param Ty Type whose preferred alignment is requested.
  /// @return The preferred stack/global alignment for \p Ty.
  LLVM_ABI Align getPrefTypeAlign(Type *Ty) const;

  /// Returns a byte type with the same size of a pointer in the given address
  /// space.
  ///
  /// \param C Context in which to create the type.
  /// \param AddressSpace Address space of the pointer.
  /// @return A byte type with the same size as a pointer in \p AddressSpace.
  LLVM_ABI ByteType *getBytePtrType(LLVMContext &C,
                                    unsigned AddressSpace = 0) const;

  /// Returns an integer type with size at least as big as that of a
  /// pointer in the given address space.
  ///
  /// \param C Context in which to create the type.
  /// \param AddressSpace Address space of the pointer.
  /// @return An integer type with size at least as big as a pointer in \p AddressSpace.
  LLVM_ABI IntegerType *getIntPtrType(LLVMContext &C,
                                      unsigned AddressSpace = 0) const;

  /// Returns an integer (vector of integer) type with size at least as
  /// big as that of a pointer of the given pointer (vector of pointer) type.
  ///
  /// \param Ty Pointer or vector-of-pointer type.
  /// @return An integer \(or vector of integer\) type with size at least as big as \p Ty.
  LLVM_ABI Type *getIntPtrType(Type *Ty) const;

  /// Returns a byte (vector of byte) type with the same size of a pointer of
  /// the given pointer (vector of pointer) type.
  ///
  /// \param Ty Pointer or vector-of-pointer type.
  /// @return A byte \(or vector of byte\) type with the same size as \p Ty.
  LLVM_ABI Type *getBytePtrType(Type *Ty) const;

  /// Returns the smallest integer type with size at least as big as
  /// Width bits.
  ///
  /// \param C Context in which to create the type.
  /// \param Width Minimum bit width of the integer type.
  /// @return The smallest legal integer type with size at least \p Width bits.
  LLVM_ABI Type *getSmallestLegalIntType(LLVMContext &C,
                                         unsigned Width = 0) const;

  /// Returns the largest legal integer type, or null if none are set.
  ///
  /// \param C Context in which to create the type.
  /// @return The largest legal integer type, or null if none are set.
  Type *getLargestLegalIntType(LLVMContext &C) const {
    unsigned LargestSize = getLargestLegalIntTypeSizeInBits();
    return (LargestSize == 0) ? nullptr : Type::getIntNTy(C, LargestSize);
  }

  /// Returns the size of largest legal integer type size, or 0 if none
  /// are set.
  /// @return The size in bits of the largest legal integer type, or 0 if none are set.
  LLVM_ABI unsigned getLargestLegalIntTypeSizeInBits() const;

  /// Returns the type of a GEP index in \p AddressSpace.
  /// If it was not specified explicitly, it will be the integer type of the
  /// pointer width - IntPtrType.
  ///
  /// \param C Context in which to create the type.
  /// \param AddressSpace Address space whose index type is requested.
  /// @return The integer type of a GEP index in \p AddressSpace.
  LLVM_ABI IntegerType *getIndexType(LLVMContext &C,
                                     unsigned AddressSpace) const;
  /// Returns the type of an address in \p AddressSpace.
  ///
  /// \param C Context in which to create the type.
  /// \param AddressSpace Address space whose address type is requested.
  /// @return The integer type of an address in \p AddressSpace.
  IntegerType *getAddressType(LLVMContext &C, unsigned AddressSpace) const {
    return getIndexType(C, AddressSpace);
  }

  /// Returns the type of a GEP index.
  /// If it was not specified explicitly, it will be the integer type of the
  /// pointer width - IntPtrType.
  ///
  /// \param PtrTy Pointer or vector-of-pointer type.
  /// @return The type of a GEP index for \p PtrTy.
  LLVM_ABI Type *getIndexType(Type *PtrTy) const;
  /// Returns the type of an address for the given pointer type.
  ///
  /// \param PtrTy Pointer or vector-of-pointer type.
  /// @return The type of an address for \p PtrTy.
  Type *getAddressType(Type *PtrTy) const { return getIndexType(PtrTy); }

  /// Returns the offset from the beginning of the type for the specified
  /// indices.
  ///
  /// Note that this takes the element type, not the pointer type.
  /// This is used to implement getelementptr.
  ///
  /// \param ElemTy Element type being indexed into.
  /// \param Indices Sequence of GEP index values.
  /// @return The offset in bytes from the beginning of the type for \p Indices.
  LLVM_ABI int64_t getIndexedOffsetInType(Type *ElemTy,
                                          ArrayRef<Value *> Indices) const;

  /// Get GEP indices to access \p Offset inside \p ElemTy.
  ///
  /// ElemTy is updated to be the result element type and Offset to be the
  /// residual offset.
  ///
  /// \param ElemTy Element type being indexed into; updated to the result type.
  /// \param Offset Byte offset into \p ElemTy; updated to any residual offset.
  /// @return GEP indices to access \p Offset inside \p ElemTy.
  LLVM_ABI SmallVector<APInt> getGEPIndicesForOffset(Type *&ElemTy,
                                                     APInt &Offset) const;

  /// Get a single GEP index to access \p Offset inside \p ElemTy.
  ///
  /// Returns std::nullopt if an index cannot be computed, e.g. because the type
  /// is not an aggregate. ElemTy is updated to be the result element type and
  /// Offset to be the residual offset.
  ///
  /// \param ElemTy Element type being indexed into; updated to the result type.
  /// \param Offset Byte offset into \p ElemTy; updated to any residual offset.
  /// @return A single GEP index to access \p Offset inside \p ElemTy, or std::nullopt if none.
  LLVM_ABI std::optional<APInt> getGEPIndexForOffset(Type *&ElemTy,
                                                     APInt &Offset) const;

  /// Returns a StructLayout object, indicating the alignment of the
  /// struct, its size, and the offsets of its fields.
  ///
  /// Note that this information is lazily cached.
  ///
  /// \param Ty Structure type to lay out.
  /// @return A StructLayout describing the alignment, size, and field offsets of \p Ty.
  LLVM_ABI const StructLayout *getStructLayout(StructType *Ty) const;

  /// Returns the preferred alignment of the specified global.
  ///
  /// This includes an explicitly requested alignment (if the global has one).
  ///
  /// \param GV Global variable whose preferred alignment is requested.
  /// @return The preferred alignment of \p GV.
  LLVM_ABI Align getPreferredAlign(const GlobalVariable *GV) const;
};

/// Convert an opaque \c LLVMTargetDataRef to a \c DataLayout pointer.
///
/// \param P Opaque C API target-data reference to unwrap.
/// @return The DataLayout pointer corresponding to \p P.
inline DataLayout *unwrap(LLVMTargetDataRef P) {
  return reinterpret_cast<DataLayout *>(P);
}

/// Convert a \c DataLayout pointer to an opaque \c LLVMTargetDataRef.
///
/// \param P DataLayout to wrap for the C API.
/// @return An opaque LLVMTargetDataRef for \p P.
inline LLVMTargetDataRef wrap(const DataLayout *P) {
  return reinterpret_cast<LLVMTargetDataRef>(const_cast<DataLayout *>(P));
}

/// Used to lazily calculate structure layout information for a target machine,
/// based on the DataLayout structure.
class StructLayout final : private TrailingObjects<StructLayout, TypeSize> {
  friend TrailingObjects;

  TypeSize StructSize;
  Align StructAlignment;
  unsigned IsPadded : 1;
  unsigned NumElements : 31;

public:
  /// Returns the total size of the structure in bytes.
  /// @return The total size of the structure in bytes.
  TypeSize getSizeInBytes() const { return StructSize; }

  /// Returns the total size of the structure in bits.
  /// @return The total size of the structure in bits.
  TypeSize getSizeInBits() const { return 8 * StructSize; }

  /// Returns the ABI alignment of the structure.
  /// @return The ABI alignment of the structure.
  Align getAlignment() const { return StructAlignment; }

  /// Returns whether the struct has padding or not between its fields.
  /// NB: Padding in nested element is not taken into account.
  /// @return True if the struct has padding between its fields.
  bool hasPadding() const { return IsPadded; }

  /// Given a valid byte offset into the structure, returns the structure
  /// index that contains it.
  ///
  /// \param FixedOffset Byte offset into the structure.
  /// @return The structure member index that contains \p FixedOffset.
  LLVM_ABI unsigned getElementContainingOffset(uint64_t FixedOffset) const;

  /// Returns a mutable view of the byte offsets of each structure member.
  /// @return A mutable view of the byte offsets of each structure member.
  MutableArrayRef<TypeSize> getMemberOffsets() {
    return getTrailingObjects(NumElements);
  }

  /// Returns the byte offsets of each structure member.
  /// @return The byte offsets of each structure member.
  ArrayRef<TypeSize> getMemberOffsets() const {
    return getTrailingObjects(NumElements);
  }

  /// Returns the byte offset of the structure member at index \p Idx.
  ///
  /// \param Idx Zero-based index of the structure member.
  /// @return The byte offset of the structure member at index \p Idx.
  TypeSize getElementOffset(unsigned Idx) const {
    assert(Idx < NumElements && "Invalid element idx!");
    return getMemberOffsets()[Idx];
  }

  /// Returns the bit offset of the structure member at index \p Idx.
  ///
  /// \param Idx Zero-based index of the structure member.
  /// @return The bit offset of the structure member at index \p Idx.
  TypeSize getElementOffsetInBits(unsigned Idx) const {
    return getElementOffset(Idx) * 8;
  }

private:
  friend class DataLayout; // Only DataLayout can create this class

  StructLayout(StructType *ST, const DataLayout &DL);
};

// The implementation of this method is provided inline as it is particularly
// well suited to constant folding when called on a specific Type subclass.
inline TypeSize DataLayout::getTypeSizeInBits(Type *Ty) const {
  assert(Ty->isSized() && "Cannot getTypeInfo() on a type that is unsized!");
  switch (Ty->getTypeID()) {
  case Type::LabelTyID:
    return TypeSize::getFixed(getPointerSizeInBits(0));
  case Type::PointerTyID:
    return TypeSize::getFixed(
        getPointerSizeInBits(Ty->getPointerAddressSpace()));
  case Type::ArrayTyID: {
    ArrayType *ATy = cast<ArrayType>(Ty);
    return ATy->getNumElements() *
           getTypeAllocSizeInBits(ATy->getElementType());
  }
  case Type::StructTyID:
    // Get the layout annotation... which is lazily created on demand.
    return getStructLayout(cast<StructType>(Ty))->getSizeInBits();
  case Type::ByteTyID:
    return TypeSize::getFixed(Ty->getByteBitWidth());
  case Type::IntegerTyID:
    return TypeSize::getFixed(Ty->getIntegerBitWidth());
  case Type::HalfTyID:
  case Type::BFloatTyID:
    return TypeSize::getFixed(16);
  case Type::FloatTyID:
    return TypeSize::getFixed(32);
  case Type::DoubleTyID:
    return TypeSize::getFixed(64);
  case Type::PPC_FP128TyID:
  case Type::FP128TyID:
    return TypeSize::getFixed(128);
  case Type::X86_AMXTyID:
    return TypeSize::getFixed(8192);
  // In memory objects this is always aligned to a higher boundary, but
  // only 80 bits contain information.
  case Type::X86_FP80TyID:
    return TypeSize::getFixed(80);
  case Type::FixedVectorTyID:
  case Type::ScalableVectorTyID: {
    VectorType *VTy = cast<VectorType>(Ty);
    auto EltCnt = VTy->getElementCount();
    uint64_t MinBits = EltCnt.getKnownMinValue() *
                       getTypeSizeInBits(VTy->getElementType()).getFixedValue();
    return TypeSize(MinBits, EltCnt.isScalable());
  }
  case Type::TargetExtTyID: {
    Type *LayoutTy = cast<TargetExtType>(Ty)->getLayoutType();
    return getTypeSizeInBits(LayoutTy);
  }
  default:
    llvm_unreachable("DataLayout::getTypeSizeInBits(): Unsupported type");
  }
}

} // end namespace llvm

#endif // LLVM_IR_DATALAYOUT_H
