//===----- FunctionInfo.h - ABI Function Information --------- C++ --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines FunctionInfo and associated types used in representing the
// ABI-coerced types for function arguments and return values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ABI_FUNCTIONINFO_H
#define LLVM_ABI_FUNCTIONINFO_H

#include "llvm/ABI/Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/TrailingObjects.h"
#include <optional>

namespace llvm {
/// Types and helpers for classifying function arguments and returns under an
/// ABI.
namespace abi {

/// Helper class to encapsulate information about how a specific type should be
/// passed to or returned from a function.
class ArgInfo {
public:
  /// How an argument or return value is passed under the ABI.
  enum Kind {
    /// Pass the argument directly using the normal converted LLVM type, or by
    /// coercing to another specified type stored in 'CoerceToType'.
    Direct,
    /// Valid only for integer argument types. Same as 'direct' but also emit a
    /// zero/sign extension attribute.
    Extend,
    /// Pass the argument indirectly via a hidden pointer with the specified
    /// alignment and address space.
    Indirect,
    /// Ignore the argument (treat as void). Useful for void and empty structs.
    Ignore,
  };

private:
  const Type *CoercionType = nullptr;
  // Alignment is optional for direct arguments, but required for indirect
  // arguments. This invariant is enforced by the methods of this class.
  //
  // The field is not part of DirectAttrInfo/IndirectAttrInfo because it would
  // make the union non-trivial, disabling implicit copy/move constructors and
  // assignment operators for the entire class.
  MaybeAlign Alignment;

  struct DirectAttrInfo {
    unsigned Offset;
  };

  struct IndirectAttrInfo {
    unsigned AddrSpace;
  };

  union {
    /// Attributes used when the kind is Direct or Extend.
    DirectAttrInfo DirectAttr;
    /// Attributes used when the kind is Indirect.
    IndirectAttrInfo IndirectAttr;
  };

  Kind TheKind;
  bool SignExt : 1;
  bool ZeroExt : 1;
  bool IndirectByVal : 1;
  bool IndirectRealign : 1;

  ArgInfo(Kind K = Direct)
      : TheKind(K), SignExt(false), ZeroExt(false), IndirectByVal(false),
        IndirectRealign(false) {}

public:
  /// Return ArgInfo for an argument passed directly, optionally coerced.
  ///
  /// \param T The type to coerce to. If null, the argument's original type is
  ///          used directly.
  /// \param Offset Byte offset into the memory representation at which the
  ///               coerced type begins. Used when only part of a larger value
  ///               is passed directly (e.g. the high word of a multi-eightbyte
  ///               return value on x86-64).
  /// \param Align  Override for the argument's alignment. If absent, the
  ///               default alignment for \p T is used.
  /// \return ArgInfo describing a Direct argument.
  static ArgInfo getDirect(const Type *T = nullptr, unsigned Offset = 0,
                           MaybeAlign Align = std::nullopt) {
    ArgInfo AI(Direct);
    AI.CoercionType = T;
    AI.Alignment = Align;
    AI.DirectAttr.Offset = Offset;
    return AI;
  }

  /// Return ArgInfo for an integer argument that must be sign- or zero-extended.
  ///
  /// \param T The integer type to extend; must be non-null.
  /// \return ArgInfo describing an Extend argument with sign or zero extension
  ///         set from the signedness of \p T.
  static ArgInfo getExtend(const Type *T) {
    assert(T && "Type cannot be null");
    assert(T->isInteger() && "Unexpected type - only integers can be extended");

    ArgInfo AI(Extend);
    AI.CoercionType = T;
    AI.Alignment = std::nullopt;
    AI.DirectAttr.Offset = 0;

    const IntegerType *IntTy = cast<IntegerType>(T);
    if (IntTy->isSigned())
      AI.setSignExt();
    else
      AI.setZeroExt();

    return AI;
  }

  /// Return ArgInfo for an argument passed indirectly via a hidden pointer.
  ///
  /// \param Align     Required alignment of the indirect argument.
  /// \param ByVal     True if the argument is passed by value through the
  ///                  pointer (the callee receives a copy).
  /// \param AddrSpace Address space of the hidden pointer.
  /// \param Realign   True when the caller could not guarantee sufficient
  ///                  alignment and the callee must copy the argument to a
  ///                  properly aligned temporary before use.
  /// \return ArgInfo describing an Indirect argument.
  static ArgInfo getIndirect(Align Align, bool ByVal, unsigned AddrSpace = 0,
                             bool Realign = false) {
    ArgInfo AI(Indirect);
    AI.Alignment = Align;
    AI.IndirectAttr.AddrSpace = AddrSpace;
    AI.IndirectByVal = ByVal;
    AI.IndirectRealign = Realign;
    return AI;
  }

  /// Return ArgInfo that ignores the argument (treats it as void).
  ///
  /// \return ArgInfo with kind Ignore.
  static ArgInfo getIgnore() { return ArgInfo(Ignore); }

  /// Set whether this Extend argument should be sign-extended.
  ///
  /// \param SignExtend True to request sign extension; clears zero-ext.
  /// \return A reference to this ArgInfo for chaining.
  ArgInfo &setSignExt(bool SignExtend = true) {
    this->SignExt = SignExtend;
    if (SignExtend)
      this->ZeroExt = false;
    return *this;
  }

  /// Set whether this Extend argument should be zero-extended.
  ///
  /// \param ZeroExtend True to request zero extension; clears sign-ext.
  /// \return A reference to this ArgInfo for chaining.
  ArgInfo &setZeroExt(bool ZeroExtend = true) {
    this->ZeroExt = ZeroExtend;
    if (ZeroExtend)
      this->SignExt = false;
    return *this;
  }

  /// Return how this argument or return value is passed.
  ///
  /// \return The ABI passing kind for this argument or return value.
  Kind getKind() const { return TheKind; }
  /// Return true if the kind is Direct.
  ///
  /// \return True if the kind is Direct.
  bool isDirect() const { return TheKind == Direct; }
  /// Return true if the kind is Indirect.
  ///
  /// \return True if the kind is Indirect.
  bool isIndirect() const { return TheKind == Indirect; }
  /// Return true if the kind is Ignore.
  ///
  /// \return True if the kind is Ignore.
  bool isIgnore() const { return TheKind == Ignore; }
  /// Return true if the kind is Extend.
  ///
  /// \return True if the kind is Extend.
  bool isExtend() const { return TheKind == Extend; }

  /// Return the byte offset used for a Direct or Extend coercion.
  ///
  /// \return The byte offset into the memory representation for the coercion.
  unsigned getDirectOffset() const {
    assert((isDirect() || isExtend()) && "Not a direct or extend kind");
    return DirectAttr.Offset;
  }

  /// Return the optional alignment override for a Direct or Extend argument.
  ///
  /// \return The alignment override, or an empty MaybeAlign if unset.
  MaybeAlign getDirectAlign() const {
    assert((isDirect() || isExtend()) && "Not a direct or extend kind");
    return Alignment;
  }

  /// Return the required alignment of an Indirect argument.
  ///
  /// \return The required alignment of the indirect argument.
  Align getIndirectAlign() const {
    assert(isIndirect() && "Invalid Kind!");
    assert(Alignment.has_value() &&
           "Indirect arguments must have an alignment");
    return *Alignment;
  }

  /// Return the address space of the hidden pointer for an Indirect argument.
  ///
  /// \return The address space of the hidden pointer.
  unsigned getIndirectAddrSpace() const {
    assert(isIndirect() && "Invalid Kind!");
    return IndirectAttr.AddrSpace;
  }

  /// Return true if an Indirect argument is passed by value through the pointer.
  ///
  /// \return True if the Indirect argument is passed by value.
  bool getIndirectByVal() const {
    assert(isIndirect() && "Invalid Kind!");
    return IndirectByVal;
  }

  /// Return true if an Indirect argument must be realigned by the callee.
  ///
  /// \return True if the callee must realign the Indirect argument.
  bool getIndirectRealign() const {
    assert(isIndirect() && "Invalid Kind!");
    return IndirectRealign;
  }

  /// Return true if this Extend argument requests sign extension.
  ///
  /// \return True if sign extension is requested.
  bool isSignExt() const {
    assert(isExtend() && "Invalid Kind!");
    return SignExt;
  }

  /// Return true if this Extend argument requests zero extension.
  ///
  /// \return True if zero extension is requested.
  bool isZeroExt() const {
    assert(isExtend() && "Invalid Kind!");
    return ZeroExt;
  }

  /// Return true if this Extend argument requests neither sign nor zero ext.
  ///
  /// \return True if neither sign nor zero extension is requested.
  bool isNoExt() const {
    assert(isExtend() && "Invalid Kind!");
    return !SignExt && !ZeroExt;
  }

  /// Return the type to coerce to for a Direct or Extend argument, or null.
  ///
  /// \return The coercion type, or null if the original type is used.
  const Type *getCoerceToType() const {
    assert((isDirect() || isExtend()) && "Invalid Kind!");
    return CoercionType;
  }
};

/// A function argument paired with its ABI passing information.
struct ArgEntry {
  /// The ABI type of the argument.
  const Type *ABIType;
  /// How the argument is passed under the ABI.
  ArgInfo Info;

  /// Construct an entry with type \p T and default Direct passing info.
  ///
  /// \param T The ABI type of the argument.
  ArgEntry(const Type *T) : ABIType(T), Info(ArgInfo::getDirect()) {}
  /// Construct an entry with type \p T and explicit passing info \p A.
  ///
  /// \param T The ABI type of the argument.
  /// \param A How the argument is passed under the ABI.
  ArgEntry(const Type *T, ArgInfo A) : ABIType(T), Info(A) {}
};

/// Describes which arguments of a function signature are required.
///
/// Whether a signature accepts arguments beyond its declared parameters, and
/// where the declared ones end when it does. An argument past an ellipsis is
/// unnamed, and some ABI rules pass an unnamed type differently.
///
/// A count and All are not interchangeable when the count equals the number of
/// arguments. FunctionInfo::isVariadic() is true whenever a count was given,
/// so a signature with no ellipsis has to be spelled All.
class RequiredArgs {
  /// The number of leading arguments that are declared parameters, or ~0U if
  /// the signature accepts no optional arguments.
  unsigned NumRequired;

public:
  /// Tag type for constructing a RequiredArgs with no optional arguments.
  enum All_t {
    /// Sentinel selecting the form with no ellipsis.
    All
  };

  /// A signature with no ellipsis, where every argument is declared.
  ///
  /// \param Kind Tag selecting the all-arguments-required form.
  RequiredArgs(All_t Kind) : NumRequired(~0U) {}

  /// A signature whose leading \p N arguments are declared and whose remaining
  /// arguments pass through an ellipsis.
  ///
  /// \param N Number of leading declared (non-variadic) arguments.
  explicit RequiredArgs(unsigned N) : NumRequired(N) {
    assert(N != ~0U && "~0U is reserved for a signature with no ellipsis");
  }

  /// Return true if the signature accepts arguments past the declared ones.
  ///
  /// \return True if the signature accepts optional (variadic) arguments.
  bool allowsOptionalArgs() const { return NumRequired != ~0U; }

  /// Return the number of leading declared parameters for a variadic signature.
  ///
  /// \return The number of leading declared (non-variadic) arguments.
  unsigned getNumRequiredArgs() const {
    assert(allowsOptionalArgs() && "signature accepts no optional arguments");
    return NumRequired;
  }
};

/// ABI classification of a function's return value and arguments.
class FunctionInfo final : private TrailingObjects<FunctionInfo, ArgEntry> {
private:
  const Type *ReturnType;
  ArgInfo ReturnInfo;
  unsigned NumArgs;
  CallingConv::ID CC = CallingConv::C;
  RequiredArgs Required;

  FunctionInfo(CallingConv::ID CC, const Type *RetTy, unsigned NumArguments,
               RequiredArgs Required)
      : ReturnType(RetTy), ReturnInfo(ArgInfo::getDirect()),
        NumArgs(NumArguments), CC(CC), Required(Required) {}

  friend class TrailingObjects;

public:
  /// Const iterator over the trailing ArgEntry array.
  using const_arg_iterator = const ArgEntry *;
  /// Mutable iterator over the trailing ArgEntry array.
  using arg_iterator = ArgEntry *;

  /// Deallocate storage for a FunctionInfo allocated with trailing objects.
  ///
  /// \param p Pointer returned by the matching sized allocation.
  void operator delete(void *p) { ::operator delete(p); }
  /// Return a const iterator to the first argument entry.
  ///
  /// \return A const iterator to the first ArgEntry.
  const_arg_iterator arg_begin() const { return getTrailingObjects(); }
  /// Return a const iterator past the last argument entry.
  ///
  /// \return A const iterator past the last ArgEntry.
  const_arg_iterator arg_end() const { return getTrailingObjects() + NumArgs; }
  /// Return a mutable iterator to the first argument entry.
  ///
  /// \return A mutable iterator to the first ArgEntry.
  arg_iterator arg_begin() { return getTrailingObjects(); }
  /// Return a mutable iterator past the last argument entry.
  ///
  /// \return A mutable iterator past the last ArgEntry.
  arg_iterator arg_end() { return getTrailingObjects() + NumArgs; }

  /// Return the number of argument entries.
  ///
  /// \return The number of argument entries.
  unsigned arg_size() const { return NumArgs; }

  /// Allocate and initialize a FunctionInfo for the given signature.
  ///
  /// \param CC         Calling convention of the function.
  /// \param ReturnType ABI type of the return value.
  /// \param ArgTypes   ABI types of the arguments, in order.
  /// \param Required   Which leading arguments are declared parameters.
  /// \return An owning pointer to the newly allocated FunctionInfo.
  LLVM_ABI static std::unique_ptr<FunctionInfo>
  create(CallingConv::ID CC, const Type *ReturnType,
         ArrayRef<const Type *> ArgTypes,
         RequiredArgs Required = RequiredArgs::All);

  /// Return the ABI type of the return value.
  ///
  /// \return The ABI type of the return value.
  const Type *getReturnType() const { return ReturnType; }
  /// Return a mutable reference to how the return value is passed.
  ///
  /// \return A mutable reference to the return-value ArgInfo.
  ArgInfo &getReturnInfo() { return ReturnInfo; }
  /// Return a const reference to how the return value is passed.
  ///
  /// \return A const reference to the return-value ArgInfo.
  const ArgInfo &getReturnInfo() const { return ReturnInfo; }

  /// Return the calling convention of this function.
  ///
  /// \return The calling convention identifier.
  CallingConv::ID getCallingConvention() const { return CC; }

  /// Return true if the signature was constructed with optional arguments.
  ///
  /// \return True if the signature accepts optional (variadic) arguments.
  bool isVariadic() const { return Required.allowsOptionalArgs(); }

  /// Return the number of leading declared parameters.
  ///
  /// \return The number of leading declared parameters, or arg_size() when
  ///         the signature is not variadic.
  unsigned getNumRequiredArgs() const {
    return isVariadic() ? Required.getNumRequiredArgs() : arg_size();
  }

  /// Return a const view of the argument entries.
  ///
  /// \return A const ArrayRef over the trailing ArgEntry array.
  ArrayRef<ArgEntry> arguments() const {
    return {getTrailingObjects(), NumArgs};
  }

  /// Return a mutable view of the argument entries.
  ///
  /// \return A MutableArrayRef over the trailing ArgEntry array.
  MutableArrayRef<ArgEntry> arguments() {
    return {getTrailingObjects(), NumArgs};
  }

  /// Return a mutable reference to the argument entry at \p Index.
  ///
  /// \param Index Zero-based argument index; must be less than arg_size().
  /// \return A mutable reference to the ArgEntry at \p Index.
  ArgEntry &getArgInfo(unsigned Index) {
    assert(Index < NumArgs && "Invalid argument index");
    return arguments()[Index];
  }

  /// Return a const reference to the argument entry at \p Index.
  ///
  /// \param Index Zero-based argument index; must be less than arg_size().
  /// \return A const reference to the ArgEntry at \p Index.
  const ArgEntry &getArgInfo(unsigned Index) const {
    assert(Index < NumArgs && "Invalid argument index");
    return arguments()[Index];
  }
};

} // namespace abi
} // namespace llvm

#endif // LLVM_ABI_FUNCTIONINFO_H
