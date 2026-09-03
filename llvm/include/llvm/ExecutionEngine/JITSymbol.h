//===- JITSymbol.h - JIT symbol abstraction ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Abstraction for target process addresses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITSYMBOL_H
#define LLVM_EXECUTIONENGINE_JITSYMBOL_H

#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {

class GlobalValue;
class GlobalValueSummary;

namespace object {

class SymbolRef;

} // end namespace object

/// Represents an address in the target process's address space.
using JITTargetAddress = uint64_t;

/// Convert a JITTargetAddress to a pointer.
///
/// Note: This is a raw cast of the address bit pattern to the given pointer
/// type. When casting to a function pointer in order to execute JIT'd code
/// jitTargetAddressToFunction should be preferred, as it will also perform
/// pointer signing on targets that require it.
/// @param Addr Target address to cast to a pointer.
/// @return \p Addr cast to pointer type \c T.
template <typename T> T jitTargetAddressToPointer(JITTargetAddress Addr) {
  static_assert(std::is_pointer<T>::value, "T must be a pointer type");
  uintptr_t IntPtr = static_cast<uintptr_t>(Addr);
  assert(IntPtr == Addr && "JITTargetAddress value out of range for uintptr_t");
  return reinterpret_cast<T>(IntPtr);
}

/// Convert a JITTargetAddress to a callable function pointer.
///
/// Casts the given address to a callable function pointer. This operation
/// will perform pointer signing for platforms that require it (e.g. arm64e).
/// @param Addr Target address to cast to a function pointer.
/// @return \p Addr cast to a callable function pointer of type \c T.
template <typename T> T jitTargetAddressToFunction(JITTargetAddress Addr) {
  static_assert(std::is_pointer<T>::value &&
                    std::is_function<std::remove_pointer_t<T>>::value,
                "T must be a function pointer type");
  return jitTargetAddressToPointer<T>(Addr);
}

/// Convert a pointer to a JITTargetAddress.
/// @param Ptr Pointer whose address bits are returned as a target address.
/// @return The address bits of \p Ptr as a JITTargetAddress.
template <typename T> JITTargetAddress pointerToJITTargetAddress(T *Ptr) {
  return static_cast<JITTargetAddress>(reinterpret_cast<uintptr_t>(Ptr));
}

/// Flags for symbols in the JIT.
class JITSymbolFlags {
public:
  /// Integer type used to store the common JIT symbol flags.
  using UnderlyingType = uint8_t;
  /// Integer type used to store target-specific JIT symbol flags.
  using TargetFlagsType = uint8_t;

  /// Named bit flags that describe a JIT symbol.
  enum FlagNames : UnderlyingType {
    None = 0,                            ///< No flags set.
    HasError = 1U << 0,                  ///< Symbol lookup ended in an error.
    Weak = 1U << 1,                      ///< Weak definition.
    Common = 1U << 2,                    ///< Common definition.
    Absolute = 1U << 3,                  ///< Absolute (non-relocatable) symbol.
    Exported = 1U << 4,                  ///< Symbol is exported.
    Callable = 1U << 5,                  ///< Symbol is known to be callable.
    MaterializationSideEffectsOnly = 1U << 6, ///< Sync-only; no real address.
    /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
    LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */
        MaterializationSideEffectsOnly)
  };

  /// Default-construct a JITSymbolFlags instance.
  JITSymbolFlags() = default;

  /// Construct a JITSymbolFlags instance from the given flags.
  /// @param Flags Common JIT symbol flags to store.
  JITSymbolFlags(FlagNames Flags) : Flags(Flags) {}

  /// Construct a JITSymbolFlags instance from the given flags and target
  ///        flags.
  /// @param Flags Common JIT symbol flags to store.
  /// @param TargetFlags Target-specific flags to store.
  JITSymbolFlags(FlagNames Flags, TargetFlagsType TargetFlags)
      : TargetFlags(TargetFlags), Flags(Flags) {}

  /// Implicitly convert to bool. Returns true if any flag is set.
  /// @return True if any flag is set.
  explicit operator bool() const { return Flags != None || TargetFlags != 0; }

  /// Compare for equality.
  /// @param RHS Flags value to compare against.
  /// @return True if both flag sets are equal.
  bool operator==(const JITSymbolFlags &RHS) const {
    return Flags == RHS.Flags && TargetFlags == RHS.TargetFlags;
  }

  /// Bitwise AND-assignment for FlagNames.
  /// @param RHS Flag bits to AND into this value.
  /// @return A reference to this flags value after the AND.
  JITSymbolFlags &operator&=(const FlagNames &RHS) {
    Flags &= RHS;
    return *this;
  }

  /// Bitwise OR-assignment for FlagNames.
  /// @param RHS Flag bits to OR into this value.
  /// @return A reference to this flags value after the OR.
  JITSymbolFlags &operator|=(const FlagNames &RHS) {
    Flags |= RHS;
    return *this;
  }

  /// Return true if there was an error retrieving this symbol.
  /// @return True if there was an error retrieving this symbol.
  bool hasError() const {
    return (Flags & HasError) == HasError;
  }

  /// Returns true if the Weak flag is set.
  /// @return True if the Weak flag is set.
  bool isWeak() const {
    return (Flags & Weak) == Weak;
  }

  /// Returns true if the Common flag is set.
  /// @return True if the Common flag is set.
  bool isCommon() const {
    return (Flags & Common) == Common;
  }

  /// Returns true if the symbol isn't weak or common.
  /// @return True if the symbol isn't weak or common.
  bool isStrong() const {
    return !isWeak() && !isCommon();
  }

  /// Returns true if the Exported flag is set.
  /// @return True if the Exported flag is set.
  bool isExported() const {
    return (Flags & Exported) == Exported;
  }

  /// Returns true if the given symbol is known to be callable.
  /// @return True if the given symbol is known to be callable.
  bool isCallable() const { return (Flags & Callable) == Callable; }

  /// Returns true if this is a materialization-side-effects-only symbol.
  ///
  /// Such symbols do not have a real address. They exist to trigger and
  /// support synchronization of materialization side effects, e.g. for
  /// collecting initialization information. These symbols will vanish from
  /// the symbol table immediately upon reaching the ready state, and will
  /// appear to queries as if they were never defined (except that query
  /// callback execution will be delayed until they reach the ready state).
  /// MaterializationSideEffectOnly symbols should only be queried using the
  /// SymbolLookupFlags::WeaklyReferencedSymbol flag (see
  /// llvm/include/llvm/ExecutionEngine/Orc/Core.h).
  /// @return True if this is a materialization-side-effects-only symbol.
  bool hasMaterializationSideEffectsOnly() const {
    return (Flags & MaterializationSideEffectsOnly) ==
           MaterializationSideEffectsOnly;
  }

  /// Get the underlying flags value as an integer.
  /// @return The underlying flags value as an integer.
  UnderlyingType getRawFlagsValue() const {
    return static_cast<UnderlyingType>(Flags);
  }

  /// Return a reference to the target-specific flags.
  /// @return A mutable reference to the target-specific flags.
  TargetFlagsType& getTargetFlags() { return TargetFlags; }

  /// Return a reference to the target-specific flags.
  /// @return A const reference to the target-specific flags.
  const TargetFlagsType& getTargetFlags() const { return TargetFlags; }

  /// Construct a JITSymbolFlags value based on the flags of the given global
  /// value.
  /// @param GV Global value whose linkage and attributes are mapped to flags.
  /// @return Flags derived from the global value.
  LLVM_ABI static JITSymbolFlags fromGlobalValue(const GlobalValue &GV);

  /// Construct a JITSymbolFlags value based on the flags of the given global
  /// value summary.
  /// @param S Global-value summary whose flags are mapped to JITSymbolFlags.
  /// @return Flags derived from the global-value summary.
  LLVM_ABI static JITSymbolFlags fromSummary(GlobalValueSummary *S);

  /// Construct a JITSymbolFlags value based on the flags of the given libobject
  /// symbol.
  /// @param Symbol Object-file symbol whose attributes are mapped to flags.
  /// @return Flags derived from the object symbol, or an error.
  LLVM_ABI static Expected<JITSymbolFlags>
  fromObjectSymbol(const object::SymbolRef &Symbol);

private:
  TargetFlagsType TargetFlags = 0;
  FlagNames Flags = None;
};

/// Bitwise AND of JITSymbolFlags with a FlagNames mask.
/// @param LHS Flags value to mask.
/// @param RHS Flag bits to AND with \p LHS.
/// @return A new JITSymbolFlags equal to \p LHS masked by \p RHS.
inline JITSymbolFlags operator&(const JITSymbolFlags &LHS,
                                const JITSymbolFlags::FlagNames &RHS) {
  JITSymbolFlags Tmp = LHS;
  Tmp &= RHS;
  return Tmp;
}

/// Bitwise OR of JITSymbolFlags with a FlagNames mask.
/// @param LHS Flags value to update.
/// @param RHS Flag bits to OR with \p LHS.
/// @return A new JITSymbolFlags equal to \p LHS with \p RHS bits set.
inline JITSymbolFlags operator|(const JITSymbolFlags &LHS,
                                const JITSymbolFlags::FlagNames &RHS) {
  JITSymbolFlags Tmp = LHS;
  Tmp |= RHS;
  return Tmp;
}

/// ARM-specific JIT symbol flags.
/// FIXME: This should be moved into a target-specific header.
class ARMJITSymbolFlags {
public:
  /// Default-construct ARM-specific JIT symbol flags with no bits set.
  ARMJITSymbolFlags() = default;

  /// Named ARM target-specific JIT symbol flag bits.
  enum FlagNames {
    None = 0,     ///< No ARM-specific flags set.
    Thumb = 1 << 0 ///< Symbol refers to Thumb code.
  };

  /// Convert to a mutable reference to the underlying target-flags integer.
  /// @return A mutable reference to the underlying target-flags integer.
  operator JITSymbolFlags::TargetFlagsType&() { return Flags; }

  /// Build ARMJITSymbolFlags from the given object-file symbol.
  /// @param Symbol Object-file symbol whose ARM attributes are inspected.
  /// @return ARM-specific flags derived from the object symbol.
  LLVM_ABI static ARMJITSymbolFlags
  fromObjectSymbol(const object::SymbolRef &Symbol);

private:
  JITSymbolFlags::TargetFlagsType Flags = 0;
};

/// Represents a symbol that has been evaluated to an address already.
class JITEvaluatedSymbol {
public:
  /// Default-construct a null evaluated symbol with address zero.
  JITEvaluatedSymbol() = default;

  /// Create a 'null' symbol.
  /// @param Unused Ignored; selects the null-symbol overload.
  JITEvaluatedSymbol(std::nullptr_t Unused) {}

  /// Create a symbol for the given address and flags.
  /// @param Address Resolved address in the target process.
  /// @param Flags Symbol flags associated with the address.
  JITEvaluatedSymbol(JITTargetAddress Address, JITSymbolFlags Flags)
      : Address(Address), Flags(Flags) {}

  /// Create a symbol from the given pointer with the given flags.
  /// @param P Pointer whose address becomes the symbol address.
  /// @param Flags Flags to attach; defaults to Exported.
  /// @return An evaluated symbol for \p P with the given \p Flags.
  template <typename T>
  static JITEvaluatedSymbol
  fromPointer(T *P, JITSymbolFlags Flags = JITSymbolFlags::Exported) {
    return JITEvaluatedSymbol(pointerToJITTargetAddress(P), Flags);
  }

  /// An evaluated symbol converts to 'true' if its address is non-zero.
  /// @return True if the address is non-zero.
  explicit operator bool() const { return Address != 0; }

  /// Return the address of this symbol.
  /// @return The address of this symbol.
  JITTargetAddress getAddress() const { return Address; }

  /// Return the flags for this symbol.
  /// @return The flags for this symbol.
  JITSymbolFlags getFlags() const { return Flags; }

  /// Set the flags for this symbol.
  /// @param Flags New flags to store on this symbol.
  void setFlags(JITSymbolFlags Flags) { this->Flags = std::move(Flags); }

private:
  JITTargetAddress Address = 0;
  JITSymbolFlags Flags;
};

/// Represents a symbol in the JIT.
class JITSymbol {
public:
  /// Functor that materializes a symbol address on demand.
  using GetAddressFtor = unique_function<Expected<JITTargetAddress>()>;

  /// Create a 'null' symbol, used to represent a "symbol not found"
  ///        result from a successful (non-erroneous) lookup.
  /// @param Unused Ignored; selects the null-symbol overload.
  JITSymbol(std::nullptr_t Unused)
      : CachedAddr(0) {}

  /// Create a JITSymbol representing an error in the symbol lookup
  ///        process (e.g. a network failure during a remote lookup).
  /// @param Err Error to store in this symbol.
  JITSymbol(Error Err)
    : Err(std::move(Err)), Flags(JITSymbolFlags::HasError) {}

  /// Create a symbol for a definition with a known address.
  /// @param Addr Resolved address in the target process.
  /// @param Flags Symbol flags associated with the address.
  JITSymbol(JITTargetAddress Addr, JITSymbolFlags Flags)
      : CachedAddr(Addr), Flags(Flags) {}

  /// Construct a JITSymbol from a JITEvaluatedSymbol.
  /// @param Sym Evaluated symbol whose address and flags are copied.
  JITSymbol(JITEvaluatedSymbol Sym)
      : CachedAddr(Sym.getAddress()), Flags(Sym.getFlags()) {}

  /// Create a symbol for a definition that doesn't have a known address
  ///        yet.
  /// @param GetAddress A functor to materialize a definition (fixing the
  ///        address) on demand.
  /// @param Flags Symbol flags associated with the lazy definition.
  ///
  ///   This constructor allows a JIT layer to provide a reference to a symbol
  /// definition without actually materializing the definition up front. The
  /// user can materialize the definition at any time by calling the getAddress
  /// method.
  JITSymbol(GetAddressFtor GetAddress, JITSymbolFlags Flags)
      : GetAddress(std::move(GetAddress)), CachedAddr(0), Flags(Flags) {}

  /// Deleted copy constructor; JITSymbol is move-only.
  /// @param Other Unused; copy construction is deleted.
  JITSymbol(const JITSymbol &Other) = delete;
  /// Deleted copy assignment; JITSymbol is move-only.
  /// @param Other Unused; copy assignment is deleted.
  JITSymbol& operator=(const JITSymbol &Other) = delete;

  /// Move-construct a JITSymbol, transferring ownership of state.
  /// @param Other Symbol to move from.
  JITSymbol(JITSymbol &&Other)
    : GetAddress(std::move(Other.GetAddress)), Flags(std::move(Other.Flags)) {
    if (Flags.hasError())
      Err = std::move(Other.Err);
    else
      CachedAddr = std::move(Other.CachedAddr);
  }

  /// Move-assign a JITSymbol, transferring ownership of state.
  /// @param Other Symbol to move from.
  /// @return A reference to this symbol after the move.
  JITSymbol& operator=(JITSymbol &&Other) {
    GetAddress = std::move(Other.GetAddress);
    Flags = std::move(Other.Flags);
    if (Flags.hasError())
      Err = std::move(Other.Err);
    else
      CachedAddr = std::move(Other.CachedAddr);
    return *this;
  }

  /// Destroy this JITSymbol, cleaning up the active union member.
  ~JITSymbol() {
    if (Flags.hasError())
      Err.~Error();
    else
      CachedAddr.~JITTargetAddress();
  }

  /// Returns true if the symbol exists, false otherwise.
  /// @return True if the symbol exists, false otherwise.
  explicit operator bool() const {
    return !Flags.hasError() && (CachedAddr || GetAddress);
  }

  /// Move the error field value out of this JITSymbol.
  /// @return The stored error, or success if none.
  Error takeError() {
    if (Flags.hasError())
      return std::move(Err);
    return Error::success();
  }

  /// Get the address of the symbol in the target address space. Returns
  ///        '0' if the symbol does not exist.
  /// @return The symbol address, or zero if it does not exist.
  Expected<JITTargetAddress> getAddress() {
    assert(!Flags.hasError() && "getAddress called on error value");
    if (GetAddress) {
      if (auto CachedAddrOrErr = GetAddress()) {
        GetAddress = nullptr;
        CachedAddr = *CachedAddrOrErr;
        assert(CachedAddr && "Symbol could not be materialized.");
      } else
        return CachedAddrOrErr.takeError();
    }
    return CachedAddr;
  }

  /// Return the flags for this symbol.
  /// @return The flags for this symbol.
  JITSymbolFlags getFlags() const { return Flags; }

private:
  GetAddressFtor GetAddress;
  union {
    /// Cached resolved address when the symbol is not in an error state.
    JITTargetAddress CachedAddr;
    /// Lookup error when the HasError flag is set.
    Error Err;
  };
  JITSymbolFlags Flags;
};

/// Symbol resolution interface.
///
/// Allows symbol flags and addresses to be looked up by name.
/// Symbol queries are done in bulk (i.e. you request resolution of a set of
/// symbols, rather than a single one) to reduce IPC overhead in the case of
/// remote JITing, and expose opportunities for parallel compilation.
class LLVM_ABI JITSymbolResolver {
public:
  /// Set of symbol names to look up together.
  using LookupSet = std::set<StringRef>;
  /// Map from symbol name to fully resolved evaluated symbol.
  using LookupResult = std::map<StringRef, JITEvaluatedSymbol>;
  /// Callback invoked with the result of an asynchronous symbol lookup.
  using OnResolvedFunction = unique_function<void(Expected<LookupResult>)>;

  /// Destroy a JITSymbolResolver.
  virtual ~JITSymbolResolver() = default;

  /// Returns the fully resolved address and flags for each of the given
  ///        symbols.
  ///
  /// This method will return an error if any of the given symbols can not be
  /// resolved, or if the resolution process itself triggers an error.
  /// @param Symbols Set of symbol names to resolve.
  /// @param OnResolved Callback invoked with the lookup result or an error.
  virtual void lookup(const LookupSet &Symbols,
                      OnResolvedFunction OnResolved) = 0;

  /// Returns the subset of symbols the caller must materialize.
  ///
  /// Only weak/common symbols should be looked up, as strong definitions are
  /// implicitly always part of the caller's responsibility.
  /// @param Symbols Candidate symbols whose responsibility is queried.
  /// @return The subset of \p Symbols the caller must materialize.
  virtual Expected<LookupSet>
  getResponsibilitySet(const LookupSet &Symbols) = 0;

  /// Specify if this resolver can return valid symbols with zero value.
  /// @return True if this resolver can return valid symbols with zero value.
  virtual bool allowsZeroSymbols() { return false; }

private:
  virtual void anchor();
};

/// Legacy symbol resolution interface.
class LLVM_ABI LegacyJITSymbolResolver : public JITSymbolResolver {
public:
  /// Performs lookup by, for each symbol, first calling
  ///        findSymbolInLogicalDylib and if that fails calling
  ///        findSymbol.
  /// @param Symbols Set of symbol names to resolve.
  /// @param OnResolved Callback invoked with the lookup result or an error.
  void lookup(const LookupSet &Symbols, OnResolvedFunction OnResolved) final;

  /// Performs flags lookup by calling findSymbolInLogicalDylib and
  ///        returning the flags value for that symbol.
  /// @param Symbols Candidate symbols whose responsibility is queried.
  /// @return The subset of \p Symbols the caller must materialize.
  Expected<LookupSet> getResponsibilitySet(const LookupSet &Symbols) final;

  /// Return the address of a symbol in this resolver's logical dylib.
  ///
  /// Unlike findSymbol, queries through this interface should return addresses
  /// for hidden symbols.
  ///
  /// This is of particular importance for the Orc JIT APIs, which support lazy
  /// compilation by breaking up modules: Each of those broken out modules
  /// must be able to resolve hidden symbols provided by the others. Clients
  /// writing memory managers for MCJIT can usually ignore this method.
  ///
  /// This method will be queried by RuntimeDyld when checking for previous
  /// definitions of common symbols.
  /// @param Name Symbol name to look up in the logical dynamic library.
  /// @return The symbol for \p Name in this resolver's logical dylib.
  virtual JITSymbol findSymbolInLogicalDylib(const std::string &Name) = 0;

  /// This method returns the address of the specified function or variable.
  /// It is used to resolve symbols during module linking.
  ///
  /// If the returned symbol's address is equal to ~0ULL then RuntimeDyld will
  /// skip all relocations for that symbol, and the client will be responsible
  /// for handling them manually.
  /// @param Name Symbol name to look up.
  /// @return The symbol for the specified function or variable.
  virtual JITSymbol findSymbol(const std::string &Name) = 0;

private:
  void anchor() override;
};

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITSYMBOL_H
