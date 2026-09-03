//===- llvm/TextAPI/Symbol.h - TAPI Symbol ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_SYMBOL_H
#define LLVM_TEXTAPI_SYMBOL_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/TextAPI/Target.h"

namespace llvm {
namespace MachO {

// clang-format off

/// Symbol flags.
enum class SymbolFlags : uint8_t {
  /// No flags
  None             = 0,

  /// Thread-local value symbol
  ThreadLocalValue = 1U << 0,

  /// Weak defined symbol
  WeakDefined      = 1U << 1,

  /// Weak referenced symbol
  WeakReferenced   = 1U << 2,

  /// Undefined
  Undefined        = 1U << 3,

  /// Rexported
  Rexported        = 1U << 4,

  /// Data Segment  
  Data             = 1U << 5,

  /// Text Segment
  Text             = 1U << 6,
  
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/Text),
};

// clang-format on

/// Mapping of entry types in TextStubs.
enum class EncodeKind : uint8_t {
  /// Ordinary global symbol.
  GlobalSymbol,
  /// Objective-C class symbol.
  ObjectiveCClass,
  /// Objective-C class exception-handling type symbol.
  ObjectiveCClassEHType,
  /// Objective-C instance variable symbol.
  ObjectiveCInstanceVariable,
};

/// Prefix for Objective-C 1 class name symbols.
constexpr StringLiteral ObjC1ClassNamePrefix = ".objc_class_name_";
/// Prefix for Objective-C 2 class name symbols.
constexpr StringLiteral ObjC2ClassNamePrefix = "_OBJC_CLASS_$_";
/// Prefix for Objective-C 2 metaclass name symbols.
constexpr StringLiteral ObjC2MetaClassNamePrefix = "_OBJC_METACLASS_$_";
/// Prefix for Objective-C 2 exception-handling type symbols.
constexpr StringLiteral ObjC2EHTypePrefix = "_OBJC_EHTYPE_$_";
/// Prefix for Objective-C 2 instance variable symbols.
constexpr StringLiteral ObjC2IVarPrefix = "_OBJC_IVAR_$_";

/// ObjC Interface symbol mappings.
enum class ObjCIFSymbolKind : uint8_t {
  /// No Objective-C interface symbol kind.
  None = 0,
  /// Is OBJC_CLASS* symbol.
  Class = 1U << 0,
  /// Is OBJC_METACLASS* symbol.
  MetaClass = 1U << 1,
  /// Is OBJC_EHTYPE* symbol.
  EHType = 1U << 2,

  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/EHType),
};

/// Small list of TextAPI targets associated with a symbol.
using TargetList = SmallVector<Target, 5>;

/// Insert \p Targ into \p Container in sorted order if not already present.
///
/// Keep containers that hold Targets in sorted order and uniqued.
///
/// \param Container The sorted container of targets to update.
/// \param Targ The target to insert.
/// \return Iterator to the existing or newly inserted target.
template <typename C>
typename C::iterator addEntry(C &Container, const Target &Targ) {
  auto Iter =
      lower_bound(Container, Targ, [](const Target &LHS, const Target &RHS) {
        return LHS < RHS;
      });
  if ((Iter != std::end(Container)) && !(Targ < *Iter))
    return Iter;

  return Container.insert(Iter, Targ);
}

/// TextAPI symbol describing a named entry and its targets.
class Symbol {
public:
  /// Construct a symbol from its kind, name, targets, and flags.
  ///
  /// \param Kind The encode kind of the symbol.
  /// \param Name The symbol name.
  /// \param Targets The targets associated with the symbol.
  /// \param Flags The flags that describe attributes of the symbol.
  Symbol(EncodeKind Kind, StringRef Name, TargetList Targets, SymbolFlags Flags)
      : Name(Name), Targets(std::move(Targets)), Kind(Kind), Flags(Flags) {}

  /// Add a target to this symbol, keeping the list sorted and unique.
  ///
  /// \param InputTarget The target to associate with the symbol.
  void addTarget(Target InputTarget) { addEntry(Targets, InputTarget); }

  /// Return the encode kind of this symbol.
  ///
  /// \return The symbol's encode kind.
  EncodeKind getKind() const { return Kind; }

  /// Return the name of this symbol.
  ///
  /// \return The symbol name.
  StringRef getName() const { return Name; }

  /// Return the set of architectures covered by this symbol's targets.
  ///
  /// \return Architecture set derived from the symbol's targets.
  ArchitectureSet getArchitectures() const {
    return mapToArchitectureSet(Targets);
  }

  /// Return the flags associated with this symbol.
  ///
  /// \return The symbol flags.
  SymbolFlags getFlags() const { return Flags; }

  /// Return whether this symbol is weakly defined.
  ///
  /// \return True if the WeakDefined flag is set.
  bool isWeakDefined() const {
    return (Flags & SymbolFlags::WeakDefined) == SymbolFlags::WeakDefined;
  }

  /// Return whether this symbol is weakly referenced.
  ///
  /// \return True if the WeakReferenced flag is set.
  bool isWeakReferenced() const {
    return (Flags & SymbolFlags::WeakReferenced) == SymbolFlags::WeakReferenced;
  }

  /// Return whether this symbol is a thread-local value.
  ///
  /// \return True if the ThreadLocalValue flag is set.
  bool isThreadLocalValue() const {
    return (Flags & SymbolFlags::ThreadLocalValue) ==
           SymbolFlags::ThreadLocalValue;
  }

  /// Return whether this symbol is undefined.
  ///
  /// \return True if the Undefined flag is set.
  bool isUndefined() const {
    return (Flags & SymbolFlags::Undefined) == SymbolFlags::Undefined;
  }

  /// Return whether this symbol is reexported.
  ///
  /// \return True if the Rexported flag is set.
  bool isReexported() const {
    return (Flags & SymbolFlags::Rexported) == SymbolFlags::Rexported;
  }

  /// Return whether this symbol lives in the data segment.
  ///
  /// \return True if the Data flag is set.
  bool isData() const {
    return (Flags & SymbolFlags::Data) == SymbolFlags::Data;
  }

  /// Return whether this symbol lives in the text segment.
  ///
  /// \return True if the Text flag is set.
  bool isText() const {
    return (Flags & SymbolFlags::Text) == SymbolFlags::Text;
  }

  /// Return whether this symbol is present for the given architecture.
  ///
  /// \param Arch The architecture to query.
  /// \return True if any target uses \p Arch.
  bool hasArchitecture(Architecture Arch) const {
    return mapToArchitectureSet(Targets).contains(Arch);
  }

  /// Return whether this symbol is associated with the given target.
  ///
  /// \param Targ The target to query.
  /// \return True if \p Targ is in the symbol's target list.
  bool hasTarget(const Target &Targ) const {
    return llvm::is_contained(Targets, Targ);
  }

  /// Const iterator over this symbol's targets.
  using const_target_iterator = TargetList::const_iterator;
  /// Range of this symbol's targets.
  using const_target_range = llvm::iterator_range<const_target_iterator>;

  /// Return a range over all targets associated with this symbol.
  ///
  /// \return A range covering every target.
  const_target_range targets() const { return {Targets}; }

  /// Const filtered iterator over this symbol's targets.
  using const_filtered_target_iterator =
      llvm::filter_iterator<const_target_iterator,
                            std::function<bool(const Target &)>>;
  /// Range of filtered targets associated with this symbol.
  using const_filtered_target_range =
      llvm::iterator_range<const_filtered_target_iterator>;

  /// Return a range of targets whose architectures intersect \p architectures.
  ///
  /// \param architectures The architecture set used to filter targets.
  /// \return A filtered range of matching targets.
  LLVM_ABI const_filtered_target_range
  targets(ArchitectureSet architectures) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this symbol to the given stream.
  ///
  /// \param OS The output stream to write to.
  void dump(raw_ostream &OS) const;

  /// Dump this symbol to the standard error stream.
  void dump() const { dump(llvm::errs()); }
#endif

  /// Compare this symbol with another for equality.
  ///
  /// \param O The other symbol.
  /// \return True if both symbols compare equal.
  LLVM_ABI bool operator==(const Symbol &O) const;

  /// Compare this symbol with another for inequality.
  ///
  /// \param O The other symbol.
  /// \return True if the symbols are not equal.
  bool operator!=(const Symbol &O) const { return !(*this == O); }

  /// Compare this symbol with another for ordering by kind and name.
  ///
  /// \param O The other symbol.
  /// \return True if this symbol orders before \p O.
  bool operator<(const Symbol &O) const {
    return std::tie(Kind, Name) < std::tie(O.Kind, O.Name);
  }

private:
  StringRef Name;
  TargetList Targets;
  EncodeKind Kind;
  SymbolFlags Flags;
};

/// Lightweight struct for passing around symbol information.
struct SimpleSymbol {
  /// Symbol name.
  StringRef Name;
  /// Encode kind of the symbol.
  EncodeKind Kind;
  /// Objective-C interface classification for the symbol.
  ObjCIFSymbolKind ObjCInterfaceType;

  /// Compare this simple symbol with another for ordering.
  ///
  /// \param O The other simple symbol.
  /// \return True if this symbol orders before \p O.
  bool operator<(const SimpleSymbol &O) const {
    return std::tie(Name, Kind, ObjCInterfaceType) <
           std::tie(O.Name, O.Kind, O.ObjCInterfaceType);
  }
};

/// Get symbol classification by parsing the name of a symbol.
///
/// \param SymName The name of symbol.
/// \return A lightweight symbol with the parsed name, kind, and ObjC type.
LLVM_ABI SimpleSymbol parseSymbol(StringRef SymName);

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_SYMBOL_H
