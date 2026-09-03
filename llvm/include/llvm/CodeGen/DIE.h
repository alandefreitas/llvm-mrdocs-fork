//===- lib/CodeGen/DIE.h - DWARF Info Entries -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Data structures for DWARF info entries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DIE_H
#define LLVM_CODEGEN_DIE_H

#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/DwarfStringPoolEntry.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace llvm {

class AsmPrinter;
class DIE;
class DIEUnit;
/// DWARF compile unit that owns type units and related debug metadata.
class DwarfCompileUnit;
class MCExpr;
class MCSection;
class MCSymbol;
class raw_ostream;

//===--------------------------------------------------------------------===//
/// Dwarf abbreviation data, describes one attribute of a Dwarf abbreviation.
class DIEAbbrevData {
  /// Dwarf attribute code.
  dwarf::Attribute Attribute;

  /// Dwarf form code.
  dwarf::Form Form;

  /// Dwarf attribute value for DW_FORM_implicit_const
  int64_t Value = 0;

public:
  /// Construct abbreviation data for attribute \p A with form \p F.
  ///
  /// \param A DWARF attribute code.
  /// \param F DWARF form code.
  DIEAbbrevData(dwarf::Attribute A, dwarf::Form F)
      : Attribute(A), Form(F) {}
  /// Construct abbreviation data for an implicit-const attribute.
  ///
  /// \param A DWARF attribute code.
  /// \param V Implicit constant value.
  DIEAbbrevData(dwarf::Attribute A, int64_t V)
      : Attribute(A), Form(dwarf::DW_FORM_implicit_const), Value(V) {}

  /// Accessors.
  /// @{
  /// Return the DWARF attribute code.
  ///
  /// \returns The DWARF attribute code.
  dwarf::Attribute getAttribute() const { return Attribute; }
  /// Return the DWARF form code.
  ///
  /// \returns The DWARF form code.
  dwarf::Form getForm() const { return Form; }
  /// Return the implicit-const attribute value.
  ///
  /// \returns The implicit-const attribute value.
  int64_t getValue() const { return Value; }
  /// @}

  /// Used to gather unique data for the abbreviation folding set.
  ///
  /// \param ID Folding set ID to profile into.
  LLVM_ABI void Profile(FoldingSetNodeID &ID) const;
};

//===--------------------------------------------------------------------===//
/// Dwarf abbreviation, describes the organization of a debug information
/// object.
class DIEAbbrev : public FoldingSetNode {
  /// Unique number for node.
  unsigned Number = 0;

  /// Dwarf tag code.
  dwarf::Tag Tag;

  /// Whether or not this node has children.
  ///
  /// This cheats a bit in all of the uses since the values in the standard
  /// are 0 and 1 for no children and children respectively.
  bool Children;

  /// Raw data bytes for abbreviation.
  SmallVector<DIEAbbrevData, 12> Data;

public:
  /// Construct an abbreviation for DWARF tag \p T with children flag \p C.
  ///
  /// \param T DWARF tag for this abbreviation.
  /// \param C Whether DIEs using this abbreviation have children.
  DIEAbbrev(dwarf::Tag T, bool C) : Tag(T), Children(C) {}

  /// Accessors.
  /// @{
  /// Return the DWARF tag for this abbreviation.
  ///
  /// \returns The DWARF tag.
  dwarf::Tag getTag() const { return Tag; }
  /// Return the unique abbreviation number.
  ///
  /// \returns The unique abbreviation number.
  unsigned getNumber() const { return Number; }
  /// Return true if DIEs using this abbreviation have children.
  ///
  /// \returns True if DIEs using this abbreviation have children.
  bool hasChildren() const { return Children; }
  /// Return the attribute/form pairs for this abbreviation.
  ///
  /// \returns The attribute/form pairs for this abbreviation.
  const SmallVectorImpl<DIEAbbrevData> &getData() const { return Data; }
  /// Set whether DIEs using this abbreviation have children.
  ///
  /// \param hasChild New children flag.
  void setChildrenFlag(bool hasChild) { Children = hasChild; }
  /// Set the unique abbreviation number.
  ///
  /// \param N Abbreviation number to assign.
  void setNumber(unsigned N) { Number = N; }
  /// @}

  /// Adds another set of attribute information to the abbreviation.
  ///
  /// \param Attribute DWARF attribute to append.
  /// \param Form DWARF form to append.
  void AddAttribute(dwarf::Attribute Attribute, dwarf::Form Form) {
    Data.push_back(DIEAbbrevData(Attribute, Form));
  }

  /// Adds attribute with DW_FORM_implicit_const value
  ///
  /// \param Attribute DWARF attribute to append.
  /// \param Value Implicit constant value for the attribute.
  void AddImplicitConstAttribute(dwarf::Attribute Attribute, int64_t Value) {
    Data.push_back(DIEAbbrevData(Attribute, Value));
  }

  /// Adds another set of attribute information to the abbreviation.
  ///
  /// \param AbbrevData Attribute/form pair to append.
  void AddAttribute(const DIEAbbrevData &AbbrevData) {
    Data.push_back(AbbrevData);
  }

  /// Used to gather unique data for the abbreviation folding set.
  ///
  /// \param ID Folding set ID to profile into.
  LLVM_ABI void Profile(FoldingSetNodeID &ID) const;

  /// Print the abbreviation using the specified asm printer.
  ///
  /// \param AP Asm printer used to emit the abbreviation.
  LLVM_ABI void Emit(const AsmPrinter *AP) const;

  /// Print this abbreviation to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
  /// Dump this abbreviation to standard error for debugging.
  LLVM_ABI void dump() const;
};

//===--------------------------------------------------------------------===//
/// Helps unique DIEAbbrev objects and assigns abbreviation numbers.
///
/// This class will unique the DIE abbreviations for a llvm::DIE object and
/// assign a unique abbreviation number to each unique DIEAbbrev object it
/// finds. The resulting collection of DIEAbbrev objects can then be emitted
/// into the .debug_abbrev section.
class DIEAbbrevSet {
  /// The bump allocator to use when creating DIEAbbrev objects in the uniqued
  /// storage container.
  BumpPtrAllocator &Alloc;
  /// FoldingSet that uniques the abbreviations.
  FoldingSet<DIEAbbrev> AbbreviationsSet;
  /// A list of all the unique abbreviations in use.
  std::vector<DIEAbbrev *> Abbreviations;

public:
  /// Construct an abbreviation set that allocates uniqued abbreviations from
  /// \p A.
  ///
  /// \param A Bump allocator used for uniqued DIEAbbrev objects.
  DIEAbbrevSet(BumpPtrAllocator &A) : Alloc(A) {}
  /// Destroy uniqued abbreviations owned by this set.
  LLVM_ABI ~DIEAbbrevSet();

  /// Generate the abbreviation declaration for a DIE and return a pointer to
  /// the generated abbreviation.
  ///
  /// \param Die the debug info entry to generate the abbreviation for.
  /// \returns A reference to the uniqued abbreviation declaration that is
  /// owned by this class.
  LLVM_ABI DIEAbbrev &uniqueAbbreviation(DIE &Die);

  /// Print all abbreviations using the specified asm printer.
  ///
  /// \param AP Asm printer used to emit abbreviations.
  /// \param Section Section that receives the abbreviation declarations.
  LLVM_ABI void Emit(const AsmPrinter *AP, MCSection *Section) const;
};

//===--------------------------------------------------------------------===//
/// An integer value DIE.
///
class DIEInteger {
  uint64_t Integer;

public:
  /// Construct from integer value \p I.
  ///
  /// \param I Integer payload to store.
  explicit DIEInteger(uint64_t I) : Integer(I) {}

  /// Choose the best form for integer.
  ///
  /// \param IsSigned Whether \p Int should be interpreted as signed.
  /// \param Int Integer value used to select the narrowest fitting form.
  /// \returns The narrowest DWARF form that can encode \p Int.
  static dwarf::Form BestForm(bool IsSigned, uint64_t Int) {
    if (IsSigned) {
      const int64_t SignedInt = Int;
      if ((int8_t)Int == SignedInt)
        return dwarf::DW_FORM_data1;
      if ((int16_t)Int == SignedInt)
        return dwarf::DW_FORM_data2;
      if ((int32_t)Int == SignedInt)
        return dwarf::DW_FORM_data4;
    } else {
      if ((uint8_t)Int == Int)
        return dwarf::DW_FORM_data1;
      if ((uint16_t)Int == Int)
        return dwarf::DW_FORM_data2;
      if ((uint32_t)Int == Int)
        return dwarf::DW_FORM_data4;
    }
    return dwarf::DW_FORM_data8;
  }

  /// Return the stored integer value.
  ///
  /// \returns The stored integer value.
  uint64_t getValue() const { return Integer; }
  /// Set the stored integer value to \p Val.
  ///
  /// \param Val New integer payload.
  void setValue(uint64_t Val) { Integer = Val; }

  /// Emit this integer using \p Form via the asm printer.
  ///
  /// \param Asm Asm printer used to emit the value.
  /// \param Form DWARF form to use when emitting.
  LLVM_ABI void emitValue(const AsmPrinter *Asm, dwarf::Form Form) const;
  /// Return the size in bytes of this integer when encoded with \p Form.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \param Form DWARF form to measure.
  /// \returns Size in bytes of the encoded integer.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const;

  /// Print this integer to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// An expression DIE.
class DIEExpr {
  const MCExpr *Expr;

public:
  /// Construct from MCExpr \p E.
  ///
  /// \param E Expression to store.
  explicit DIEExpr(const MCExpr *E) : Expr(E) {}

  /// Get MCExpr.
  ///
  /// \returns The stored MCExpr.
  const MCExpr *getValue() const { return Expr; }

  /// Emit this expression using \p Form via the asm printer.
  ///
  /// \param AP Asm printer used to emit the value.
  /// \param Form DWARF form to use when emitting.
  LLVM_ABI void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  /// Return the size in bytes of this expression when encoded with \p Form.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \param Form DWARF form to measure.
  /// \returns Size in bytes of the encoded expression.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const;

  /// Print this expression to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// A label DIE.
class DIELabel {
  const MCSymbol *Label;

public:
  /// Construct from MCSymbol \p L.
  ///
  /// \param L Label symbol to store.
  explicit DIELabel(const MCSymbol *L) : Label(L) {}

  /// Get MCSymbol.
  ///
  /// \returns The stored MCSymbol.
  const MCSymbol *getValue() const { return Label; }

  /// Emit this label using \p Form via the asm printer.
  ///
  /// \param AP Asm printer used to emit the value.
  /// \param Form DWARF form to use when emitting.
  LLVM_ABI void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  /// Return the size in bytes of this label when encoded with \p Form.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \param Form DWARF form to measure.
  /// \returns Size in bytes of the encoded label.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const;

  /// Print this label to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// A BaseTypeRef DIE.
class DIEBaseTypeRef {
  const DwarfCompileUnit *CU;
  const uint64_t Index;
  static constexpr unsigned ULEB128PadSize = 4;

public:
  /// Construct a base-type reference for compile unit \p TheCU at index \p Idx.
  ///
  /// \param TheCU Compile unit that owns the base-type list.
  /// \param Idx Index of the referenced base type.
  explicit DIEBaseTypeRef(const DwarfCompileUnit *TheCU, uint64_t Idx)
    : CU(TheCU), Index(Idx) {}

  /// Emit this base type reference.
  ///
  /// \param AP Asm printer used to emit the value.
  /// \param Form DWARF form to use when emitting.
  LLVM_ABI void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  /// Return the size of the base type reference in bytes.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \param Form DWARF form to measure.
  /// \returns Size in bytes of the encoded base type reference.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const;

  /// Print this base type reference to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
  /// Return the base-type list index.
  ///
  /// \returns The base-type list index.
  uint64_t getIndex() const { return Index; }
};

//===--------------------------------------------------------------------===//
/// A simple label difference DIE.
///
class DIEDelta {
  const MCSymbol *LabelHi;
  const MCSymbol *LabelLo;

public:
  /// Construct a label difference from \p Hi minus \p Lo.
  ///
  /// \param Hi High label of the difference.
  /// \param Lo Low label of the difference.
  DIEDelta(const MCSymbol *Hi, const MCSymbol *Lo) : LabelHi(Hi), LabelLo(Lo) {}

  /// Emit this label difference using \p Form via the asm printer.
  ///
  /// \param AP Asm printer used to emit the value.
  /// \param Form DWARF form to use when emitting.
  LLVM_ABI void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  /// Return the size in bytes of this label difference when encoded with \p Form.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \param Form DWARF form to measure.
  /// \returns Size in bytes of the encoded label difference.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const;

  /// Print this label difference to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// A container for string pool string values.
///
/// This class is used with the DW_FORM_strp and DW_FORM_GNU_str_index forms.
class DIEString {
  DwarfStringPoolEntryRef S;

public:
  /// Construct from string-pool entry \p S.
  ///
  /// \param S Reference to a pooled string entry.
  DIEString(DwarfStringPoolEntryRef S) : S(S) {}

  /// Grab the string out of the object.
  ///
  /// \returns The string contents from the string pool entry.
  StringRef getString() const { return S.getString(); }

  /// Emit this string-pool reference using \p Form via the asm printer.
  ///
  /// \param AP Asm printer used to emit the value.
  /// \param Form DWARF form to use when emitting.
  LLVM_ABI void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  /// Return the size in bytes of this string reference when encoded with \p Form.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \param Form DWARF form to measure.
  /// \returns Size in bytes of the encoded string reference.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const;

  /// Print this string-pool reference to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// A container for inline string values.
///
/// This class is used with the DW_FORM_string form.
class DIEInlineString {
  StringRef S;

public:
  /// Construct by copying \p Str into allocator \p A.
  ///
  /// \param Str String contents to store.
  /// \param A Allocator used to own the copied characters.
  template <typename Allocator>
  explicit DIEInlineString(StringRef Str, Allocator &A) : S(Str.copy(A)) {}

  /// Destroy this inline string container.
  ~DIEInlineString() = default;

  /// Grab the string out of the object.
  ///
  /// \returns The inline string contents.
  StringRef getString() const { return S; }

  /// Emit this inline string using \p Form via the asm printer.
  ///
  /// \param AP Asm printer used to emit the value.
  /// \param Form DWARF form to use when emitting.
  LLVM_ABI void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  /// Return the size in bytes of this inline string when encoded with \p Form.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \param Form DWARF form to measure.
  /// \returns Size in bytes of the encoded inline string.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const;

  /// Print this inline string to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// A pointer to another debug information entry.  An instance of this class can
/// also be used as a proxy for a debug information entry not yet defined
/// (ie. types.)
class DIEEntry {
  DIE *Entry;

public:
  /// Default construction is deleted; a DIE entry is required.
  DIEEntry() = delete;
  /// Construct a reference to DIE \p E.
  ///
  /// \param E DIE being referenced.
  explicit DIEEntry(DIE &E) : Entry(&E) {}

  /// Return the referenced DIE.
  ///
  /// \returns The referenced DIE.
  DIE &getEntry() const { return *Entry; }

  /// Emit this DIE reference using \p Form via the asm printer.
  ///
  /// \param AP Asm printer used to emit the value.
  /// \param Form DWARF form to use when emitting.
  LLVM_ABI void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  /// Return the size in bytes of this DIE reference when encoded with \p Form.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \param Form DWARF form to measure.
  /// \returns Size in bytes of the encoded DIE reference.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const;

  /// Print this DIE reference to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// Represents a pointer to a location list in the debug_loc
/// section.
class DIELocList {
  /// Index into the .debug_loc vector.
  size_t Index;

public:
  /// Construct from location-list index \p I.
  ///
  /// \param I Index into the .debug_loc vector.
  DIELocList(size_t I) : Index(I) {}

  /// Grab the current index out.
  ///
  /// \returns The index into the .debug_loc vector.
  size_t getValue() const { return Index; }

  /// Emit this location-list reference using \p Form via the asm printer.
  ///
  /// \param AP Asm printer used to emit the value.
  /// \param Form DWARF form to use when emitting.
  LLVM_ABI void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  /// Return the size in bytes of this location-list reference for \p Form.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \param Form DWARF form to measure.
  /// \returns Size in bytes of the encoded location-list reference.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const;

  /// Print this location-list reference to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// An address-index plus label-difference DIE value.
class DIEAddrOffset {
  DIEInteger Addr;
  DIEDelta Offset;

public:
  /// Construct from address index \p Idx and labels \p Hi / \p Lo.
  ///
  /// \param Idx Address pool index.
  /// \param Hi High label of the difference.
  /// \param Lo Low label of the difference.
  explicit DIEAddrOffset(uint64_t Idx, const MCSymbol *Hi, const MCSymbol *Lo)
      : Addr(Idx), Offset(Hi, Lo) {}

  /// Emit this address offset using \p Form via the asm printer.
  ///
  /// \param AP Asm printer used to emit the value.
  /// \param Form DWARF form to use when emitting.
  LLVM_ABI void emitValue(const AsmPrinter *AP, dwarf::Form Form) const;
  /// Return the size in bytes of this address offset when encoded with \p Form.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \param Form DWARF form to measure.
  /// \returns Size in bytes of the encoded address offset.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const;

  /// Print this address offset to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
class DIEBlock;
class DIELoc;
/// A debug information entry value. Some of these roughly correlate
/// to DWARF attribute classes.
class DIEValue {
public:
  /// Kind of data stored in a DIEValue.
  enum Type {
    /// Empty / invalid value.
    isNone,
    /// Expand to the DIEValue::Type enumerator for payload type \c T.
    ///
    /// \param T DIE value type name suffix (e.g. Integer, String).
#define HANDLE_DIEVALUE(T) is##T,
#include "llvm/CodeGen/DIEValue.def"
  };

private:
  /// Type of data stored in the value.
  Type Ty = isNone;
  dwarf::Attribute Attribute = (dwarf::Attribute)0;
  dwarf::Form Form = (dwarf::Form)0;

  /// Storage for the value.
  ///
  /// All values that aren't standard layout (or are larger than 8 bytes)
  /// should be stored by reference instead of by value.
  using ValTy =
      AlignedCharArrayUnion<DIEInteger, DIEString, DIEExpr, DIELabel,
                            DIEDelta *, DIEEntry, DIEBlock *, DIELoc *,
                            DIELocList, DIEBaseTypeRef *, DIEAddrOffset *>;

  static_assert(sizeof(ValTy) <= sizeof(uint64_t) ||
                    sizeof(ValTy) <= sizeof(void *),
                "Expected all large types to be stored via pointer");

  /// Underlying stored value.
  ValTy Val;

  template <class T> void construct(T V) {
    static_assert(std::is_standard_layout<T>::value ||
                      std::is_pointer<T>::value,
                  "Expected standard layout or pointer");
    new (reinterpret_cast<void *>(&Val)) T(V);
  }

  template <class T> T *get() { return reinterpret_cast<T *>(&Val); }
  template <class T> const T *get() const {
    return reinterpret_cast<const T *>(&Val);
  }
  template <class T> void destruct() { get<T>()->~T(); }

  /// Destroy the underlying value.
  ///
  /// This should get optimized down to a no-op.  We could skip it if we could
  /// add a static assert on \a std::is_trivially_copyable(), but we currently
  /// support versions of GCC that don't understand that.
  void destroyVal() {
    switch (Ty) {
    case isNone:
      return;
#define HANDLE_DIEVALUE_SMALL(T)                                               \
  case is##T:                                                                  \
    destruct<DIE##T>();                                                        \
    return;
#define HANDLE_DIEVALUE_LARGE(T)                                               \
  case is##T:                                                                  \
    destruct<const DIE##T *>();                                                \
    return;
#include "llvm/CodeGen/DIEValue.def"
    }
  }

  /// Copy the underlying value.
  ///
  /// This should get optimized down to a simple copy.  We need to actually
  /// construct the value, rather than calling memcpy, to satisfy strict
  /// aliasing rules.
  void copyVal(const DIEValue &X) {
    switch (Ty) {
    case isNone:
      return;
#define HANDLE_DIEVALUE_SMALL(T)                                               \
  case is##T:                                                                  \
    construct<DIE##T>(*X.get<DIE##T>());                                       \
    return;
#define HANDLE_DIEVALUE_LARGE(T)                                               \
  case is##T:                                                                  \
    construct<const DIE##T *>(*X.get<const DIE##T *>());                       \
    return;
#include "llvm/CodeGen/DIEValue.def"
    }
  }

public:
  /// Construct an empty DIE value.
  DIEValue() = default;

  /// Copy-construct a DIE value from \p X.
  ///
  /// \param X Value to copy.
  DIEValue(const DIEValue &X) : Ty(X.Ty), Attribute(X.Attribute), Form(X.Form) {
    copyVal(X);
  }

  /// Copy-assign from \p X, destroying any previous stored value.
  ///
  /// \param X Value to copy.
  /// \returns A reference to this value.
  DIEValue &operator=(const DIEValue &X) {
    if (this == &X)
      return *this;
    destroyVal();
    Ty = X.Ty;
    Attribute = X.Attribute;
    Form = X.Form;
    copyVal(X);
    return *this;
  }

  /// Destroy the stored value.
  ~DIEValue() { destroyVal(); }

#define HANDLE_DIEVALUE_SMALL(T)                                               \
  DIEValue(dwarf::Attribute Attribute, dwarf::Form Form, const DIE##T &V)      \
      : Ty(is##T), Attribute(Attribute), Form(Form) {                          \
    construct<DIE##T>(V);                                                      \
  }
#define HANDLE_DIEVALUE_LARGE(T)                                               \
  DIEValue(dwarf::Attribute Attribute, dwarf::Form Form, const DIE##T *V)      \
      : Ty(is##T), Attribute(Attribute), Form(Form) {                          \
    assert(V && "Expected valid value");                                       \
    construct<const DIE##T *>(V);                                              \
  }
#include "llvm/CodeGen/DIEValue.def"

  /// Accessors.
  /// @{
  /// Return the kind of data stored in this value.
  ///
  /// \returns The DIEValue::Type discriminator for the stored payload.
  Type getType() const { return Ty; }
  /// Return the DWARF attribute associated with this value.
  ///
  /// \returns The DWARF attribute associated with this value.
  dwarf::Attribute getAttribute() const { return Attribute; }
  /// Return the DWARF form associated with this value.
  ///
  /// \returns The DWARF form associated with this value.
  dwarf::Form getForm() const { return Form; }
  /// Return true if this value stores a non-empty typed payload.
  ///
  /// \returns True if this value stores a non-empty typed payload.
  explicit operator bool() const { return Ty; }
  /// @}

  /// Return the stored small DIE value of type \c DIE##T.
  ///
  /// \param T DIE value type name suffix for the accessor.
  /// \returns Const reference to the stored \c DIE##T value.
#define HANDLE_DIEVALUE_SMALL(T)                                               \
  const DIE##T &getDIE##T() const {                                            \
    assert(getType() == is##T && "Expected " #T);                              \
    return *get<DIE##T>();                                                     \
  }
#define HANDLE_DIEVALUE_LARGE(T)                                               \
  const DIE##T &getDIE##T() const {                                            \
    assert(getType() == is##T && "Expected " #T);                              \
    return **get<const DIE##T *>();                                            \
  }
#include "llvm/CodeGen/DIEValue.def"

  /// Emit value via the Dwarf writer.
  ///
  /// \param AP Asm printer used to emit the value.
  LLVM_ABI void emitValue(const AsmPrinter *AP) const;

  /// Return the size of a value in bytes.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \returns Size of the value in bytes.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams) const;

  /// Print this value to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
  /// Dump this value to standard error for debugging.
  LLVM_ABI void dump() const;
};

/// Node in an intrusive circular back-list.
struct IntrusiveBackListNode {
  /// Pointer to the next node, with a one-bit end-of-list flag.
  PointerIntPair<IntrusiveBackListNode *, 1> Next;

  /// Construct a node that points to itself (unlinked).
  IntrusiveBackListNode() : Next(this, true) {}

  /// Return the next node, or null if this is the last node.
  ///
  /// \returns The next node, or null if this is the last node.
  IntrusiveBackListNode *getNext() const {
    return Next.getInt() ? nullptr : Next.getPointer();
  }
};

/// Untyped intrusive circular back-list of \a IntrusiveBackListNode.
struct IntrusiveBackListBase {
  /// Node type stored in this list.
  using Node = IntrusiveBackListNode;

  /// Pointer to the last node, or null if the list is empty.
  Node *Last = nullptr;

  /// Return true if the list contains no nodes.
  ///
  /// \returns True if the list contains no nodes.
  bool empty() const { return !Last; }

  /// Append node \p N to the back of the list.
  ///
  /// \param N Unlinked node to append.
  void push_back(Node &N) {
    assert(N.Next.getPointer() == &N && "Expected unlinked node");
    assert(static_cast<bool>(N.Next.getInt()) == true &&
           "Expected unlinked node");

    if (Last) {
      N.Next = Last->Next;
      Last->Next.setPointerAndInt(&N, false);
    }
    Last = &N;
  }

  /// Insert node \p N at the front of the list.
  ///
  /// \param N Unlinked node to insert.
  void push_front(Node &N) {
    assert(N.Next.getPointer() == &N && "Expected unlinked node");
    assert(static_cast<bool>(N.Next.getInt()) == true &&
           "Expected unlinked node");

    if (Last) {
      N.Next.setPointerAndInt(Last->Next.getPointer(), false);
      Last->Next.setPointerAndInt(&N, true);
    } else {
      Last = &N;
    }
  }

  /// Remove node \p N from the list.
  ///
  /// Walks the list until \p N's predecessor is found, updates the
  /// predecessor's next pointer, and resets \p N's next pointer to itself.
  ///
  /// \param N Node to remove.
  /// \returns true if the node was found and removed.
  bool deleteNode(Node &N) {
    if (!Last)
      return false;

    Node *Cur = Last;
    while (Cur->Next.getPointer() != &N) {
      Cur = Cur->Next.getPointer();
      if (Cur->Next.getInt())
        return false;
    }

    Node *Target = Cur->Next.getPointer();
    if (Target == Cur) {
      Last = nullptr;
    } else if (Target == Last) {
      Cur->Next.setPointerAndInt(Target->Next.getPointer(), true);
      Last = Cur;
    } else {
      Cur->Next.setPointer(Target->Next.getPointer());
    }

    Target->Next.setPointerAndInt(Target, true);
    return true;
  }
};

/// Intrusive circular back-list of nodes of type \c T.
template <class T> class IntrusiveBackList : IntrusiveBackListBase {
public:
  /// Import emptiness query from the untyped base list.
  using IntrusiveBackListBase::empty;

  /// Append node \p N to the back of the list.
  ///
  /// \param N Unlinked node to append.
  void push_back(T &N) { IntrusiveBackListBase::push_back(N); }
  /// Insert node \p N at the front of the list.
  ///
  /// \param N Unlinked node to insert.
  void push_front(T &N) { IntrusiveBackListBase::push_front(N); }

  /// Return a reference to the last node.
  ///
  /// \returns A reference to the last node.
  T &back() { return *static_cast<T *>(Last); }
  /// Return a const reference to the last node.
  ///
  /// \returns A const reference to the last node.
  const T &back() const { return *static_cast<T *>(Last); }
  /// Return a reference to the first node.
  ///
  /// \returns A reference to the first node.
  T &front() {
    return *static_cast<T *>(Last ? Last->Next.getPointer() : nullptr);
  }
  /// Return a const reference to the first node.
  ///
  /// \returns A const reference to the first node.
  const T &front() const {
    return *static_cast<T *>(Last ? Last->Next.getPointer() : nullptr);
  }

  /// Steal all nodes from \p Other and append them to this list.
  ///
  /// \param Other List whose nodes are transferred into this list.
  void takeNodes(IntrusiveBackList<T> &Other) {
    if (Other.empty())
      return;

    T *FirstNode = static_cast<T *>(Other.Last->Next.getPointer());
    T *IterNode = FirstNode;
    do {
      // Keep a pointer to the node and increment the iterator.
      T *TmpNode = IterNode;
      IterNode = static_cast<T *>(IterNode->Next.getPointer());

      // Unlink the node and push it back to this list.
      TmpNode->Next.setPointerAndInt(TmpNode, true);
      push_back(*TmpNode);
    } while (IterNode != FirstNode);

    Other.Last = nullptr;
  }

  /// Delete node \p N from the list.
  ///
  /// Note this runs in O(N).
  ///
  /// \param N Node to remove.
  /// \returns true if the node was found and removed.
  bool deleteNode(Node &N) { return IntrusiveBackListBase::deleteNode(N); }

  /// Const forward iterator over nodes in an IntrusiveBackList.
  class const_iterator;
  /// Mutable forward iterator over nodes in an IntrusiveBackList.
  class iterator
      : public iterator_facade_base<iterator, std::forward_iterator_tag, T> {
    friend class const_iterator;

    Node *N = nullptr;

  public:
    /// Construct an end/singular iterator.
    iterator() = default;
    /// Construct an iterator referring to node \p N.
    ///
    /// \param N Node to point to.
    explicit iterator(T *N) : N(N) {}

    /// Advance to the next node and return this iterator.
    ///
    /// \returns A reference to this iterator after advancing.
    iterator &operator++() {
      N = N->getNext();
      return *this;
    }

    /// Return true if this iterator refers to a valid node.
    ///
    /// \returns True if this iterator refers to a valid node.
    explicit operator bool() const { return N; }
    /// Return a reference to the node at this position.
    ///
    /// \returns A reference to the node at this position.
    T &operator*() const { return *static_cast<T *>(N); }

    /// Return true if this iterator equals \p X.
    ///
    /// \param X Iterator to compare against.
    /// \returns True if the iterators refer to the same node.
    bool operator==(const iterator &X) const { return N == X.N; }
  };

  /// Const forward iterator over nodes in an IntrusiveBackList.
  class const_iterator
      : public iterator_facade_base<const_iterator, std::forward_iterator_tag,
                                    const T> {
    const Node *N = nullptr;

  public:
    /// Construct an end/singular const iterator.
    const_iterator() = default;
    // Placate MSVC by explicitly scoping 'iterator'.
    /// Construct a const iterator from mutable iterator \p X.
    ///
    /// \param X Mutable iterator to convert.
    const_iterator(IntrusiveBackList<T>::iterator X) : N(X.N) {}
    /// Construct a const iterator referring to node \p N.
    ///
    /// \param N Node to point to.
    explicit const_iterator(const T *N) : N(N) {}

    /// Advance to the next node and return this iterator.
    ///
    /// \returns A reference to this iterator after advancing.
    const_iterator &operator++() {
      N = N->getNext();
      return *this;
    }

    /// Return true if this iterator refers to a valid node.
    ///
    /// \returns True if this iterator refers to a valid node.
    explicit operator bool() const { return N; }
    /// Return a const reference to the node at this position.
    ///
    /// \returns A const reference to the node at this position.
    const T &operator*() const { return *static_cast<const T *>(N); }

    /// Return true if this iterator equals \p X.
    ///
    /// \param X Iterator to compare against.
    /// \returns True if the iterators refer to the same node.
    bool operator==(const const_iterator &X) const { return N == X.N; }
  };

  /// Return an iterator to the first node, or \a end() if empty.
  ///
  /// \returns An iterator to the first node, or \a end() if empty.
  iterator begin() {
    return Last ? iterator(static_cast<T *>(Last->Next.getPointer())) : end();
  }
  /// Return a const iterator to the first node, or \a end() if empty.
  ///
  /// \returns A const iterator to the first node, or \a end() if empty.
  const_iterator begin() const {
    return const_cast<IntrusiveBackList *>(this)->begin();
  }
  /// Return the past-the-end iterator.
  ///
  /// \returns The past-the-end iterator.
  iterator end() { return iterator(); }
  /// Return the past-the-end const iterator.
  ///
  /// \returns The past-the-end const iterator.
  const_iterator end() const { return const_iterator(); }

  /// Return an iterator referring to node \p N.
  ///
  /// \param N Node to wrap.
  /// \returns An iterator referring to node \p N.
  static iterator toIterator(T &N) { return iterator(&N); }
  /// Return a const iterator referring to node \p N.
  ///
  /// \param N Node to wrap.
  /// \returns A const iterator referring to node \p N.
  static const_iterator toIterator(const T &N) { return const_iterator(&N); }
};

/// A list of DIE values.
///
/// This is a singly-linked list, but instead of reversing the order of
/// insertion, we keep a pointer to the back of the list so we can push in
/// order.
///
/// There are two main reasons to choose a linked list over a customized
/// vector-like data structure.
///
///  1. For teardown efficiency, we want DIEs to be BumpPtrAllocated.  Using a
///     linked list here makes this way easier to accomplish.
///  2. Carrying an extra pointer per \a DIEValue isn't expensive.  45% of DIEs
///     have 2 or fewer values, and 90% have 5 or fewer.  A vector would be
///     over-allocated by 50% on average anyway, the same cost as the
///     linked-list node.
class DIEValueList {
  struct Node : IntrusiveBackListNode {
    DIEValue V;

    explicit Node(DIEValue V) : V(V) {}
  };

  using ListTy = IntrusiveBackList<Node>;

  ListTy List;

public:
  class const_value_iterator;
  /// Mutable forward iterator over the DIE values in the list.
  class value_iterator
      : public iterator_adaptor_base<value_iterator, ListTy::iterator,
                                     std::forward_iterator_tag, DIEValue> {
    friend class const_value_iterator;

    using iterator_adaptor =
        iterator_adaptor_base<value_iterator, ListTy::iterator,
                              std::forward_iterator_tag, DIEValue>;

  public:
    /// Construct an end/singular value iterator.
    value_iterator() = default;
    /// Construct a value iterator from underlying list iterator \p X.
    ///
    /// \param X Intrusive list iterator to adapt.
    explicit value_iterator(ListTy::iterator X) : iterator_adaptor(X) {}

    /// Return true if this iterator refers to a valid node.
    ///
    /// \returns True if this iterator refers to a valid node.
    explicit operator bool() const { return bool(wrapped()); }
    /// Return a reference to the DIE value at this position.
    ///
    /// \returns A reference to the DIE value at this position.
    DIEValue &operator*() const { return wrapped()->V; }
  };

  /// Const forward iterator over the DIE values in the list.
  class const_value_iterator : public iterator_adaptor_base<
                                   const_value_iterator, ListTy::const_iterator,
                                   std::forward_iterator_tag, const DIEValue> {
    using iterator_adaptor =
        iterator_adaptor_base<const_value_iterator, ListTy::const_iterator,
                              std::forward_iterator_tag, const DIEValue>;

  public:
    /// Construct an end/singular const value iterator.
    const_value_iterator() = default;
    /// Construct a const value iterator from mutable iterator \p X.
    ///
    /// \param X Mutable value iterator to convert.
    const_value_iterator(DIEValueList::value_iterator X)
        : iterator_adaptor(X.wrapped()) {}
    /// Construct a const value iterator from underlying list iterator \p X.
    ///
    /// \param X Const intrusive list iterator to adapt.
    explicit const_value_iterator(ListTy::const_iterator X)
        : iterator_adaptor(X) {}

    /// Return true if this iterator refers to a valid node.
    ///
    /// \returns True if this iterator refers to a valid node.
    explicit operator bool() const { return bool(wrapped()); }
    /// Return a const reference to the DIE value at this position.
    ///
    /// \returns A const reference to the DIE value at this position.
    const DIEValue &operator*() const { return wrapped()->V; }
  };

  /// Mutable range of DIE values.
  using value_range = iterator_range<value_iterator>;
  /// Const range of DIE values.
  using const_value_range = iterator_range<const_value_iterator>;

  /// Append value \p V allocated from \p Alloc and return an iterator to it.
  ///
  /// \param Alloc Bump allocator used to allocate the list node.
  /// \param V Value to append.
  /// \returns An iterator to the appended value.
  value_iterator addValue(BumpPtrAllocator &Alloc, const DIEValue &V) {
    List.push_back(*new (Alloc) Node(V));
    return value_iterator(ListTy::toIterator(List.back()));
  }
  /// Append a newly constructed DIE value and return an iterator to it.
  ///
  /// \param Alloc Bump allocator used to allocate the list node.
  /// \param Attribute DWARF attribute for the value.
  /// \param Form DWARF form for the value.
  /// \param Value Payload used to construct the DIE value.
  /// \returns An iterator to the appended value.
  template <class T>
  value_iterator addValue(BumpPtrAllocator &Alloc, dwarf::Attribute Attribute,
                          dwarf::Form Form, T &&Value) {
    return addValue(Alloc, DIEValue(Attribute, Form, std::forward<T>(Value)));
  }

  /* zr33: add method here */
  /// Replace the first value with attribute \p Attribute, updating its
  /// attribute, form, and payload.
  ///
  /// \param Alloc Bump allocator used to allocate the replacement value.
  /// \param Attribute Existing attribute to find.
  /// \param NewAttribute Attribute to assign to the replacement.
  /// \param Form Form to assign to the replacement.
  /// \param NewValue Payload for the replacement value.
  /// \returns true if a matching attribute was found and replaced.
  template <class T>
  bool replaceValue(BumpPtrAllocator &Alloc, dwarf::Attribute Attribute,
                    dwarf::Attribute NewAttribute, dwarf::Form Form,
                    T &&NewValue) {
    for (llvm::DIEValue &val : values()) {
      if (val.getAttribute() == Attribute) {
        val = *new (Alloc)
                  DIEValue(NewAttribute, Form, std::forward<T>(NewValue));
        return true;
      }
    }

    return false;
  }

  /// Replace the first value with attribute \p Attribute using \p Form and
  /// \p NewValue.
  ///
  /// \param Alloc Bump allocator used to allocate the replacement value.
  /// \param Attribute Existing attribute to find.
  /// \param Form Form to assign to the replacement.
  /// \param NewValue Payload for the replacement value.
  /// \returns true if a matching attribute was found and replaced.
  template <class T>
  bool replaceValue(BumpPtrAllocator &Alloc, dwarf::Attribute Attribute,
                    dwarf::Form Form, T &&NewValue) {
    for (llvm::DIEValue &val : values()) {
      if (val.getAttribute() == Attribute) {
        val = *new (Alloc) DIEValue(Attribute, Form, std::forward<T>(NewValue));
        return true;
      }
    }

    return false;
  }

  /// Replace the first value with attribute \p Attribute with \p NewValue.
  ///
  /// \param Alloc Bump allocator (unused; retained for overload uniformity).
  /// \param Attribute Existing attribute to find.
  /// \param Form Form argument retained for overload uniformity.
  /// \param NewValue Replacement DIE value.
  /// \returns true if a matching attribute was found and replaced.
  bool replaceValue(BumpPtrAllocator &Alloc, dwarf::Attribute Attribute,
                    dwarf::Form Form, DIEValue &NewValue) {
    for (llvm::DIEValue &val : values()) {
      if (val.getAttribute() == Attribute) {
        val = NewValue;
        return true;
      }
    }

    return false;
  }

  /// Delete the first value with attribute \p Attribute from the list.
  ///
  /// \param Attribute Attribute to remove.
  /// \returns true if a matching attribute was found and deleted.
  bool deleteValue(dwarf::Attribute Attribute) {

    for (auto &node : List) {
      if (node.V.getAttribute() == Attribute) {
        return List.deleteNode(node);
      }
    }

    return false;
  }
  /* end */

  /// Take ownership of the nodes in \p Other, and append them to the back of
  /// the list.
  ///
  /// \param Other List whose nodes are transferred into this list.
  void takeValues(DIEValueList &Other) { List.takeNodes(Other.List); }

  /// Return a mutable range over the values in this list.
  ///
  /// \returns A mutable range over the values in this list.
  value_range values() {
    return make_range(value_iterator(List.begin()), value_iterator(List.end()));
  }
  /// Return a const range over the values in this list.
  ///
  /// \returns A const range over the values in this list.
  const_value_range values() const {
    return make_range(const_value_iterator(List.begin()),
                      const_value_iterator(List.end()));
  }
};

//===--------------------------------------------------------------------===//
/// A structured debug information entry.  Has an abbreviation which
/// describes its organization.
class DIE : IntrusiveBackListNode, public DIEValueList {
  friend class IntrusiveBackList<DIE>;
  friend class DIEUnit;

  /// Dwarf unit relative offset.
  unsigned Offset = 0;
  /// Size of instance + children.
  unsigned Size = 0;
  unsigned AbbrevNumber = ~0u;
  /// Dwarf tag code.
  dwarf::Tag Tag = (dwarf::Tag)0;
  /// Set to true to force a DIE to emit an abbreviation that says it has
  /// children even when it doesn't. This is used for unit testing purposes.
  bool ForceChildren = false;
  /// Children DIEs.
  IntrusiveBackList<DIE> Children;

  /// The owner is either the parent DIE for children of other DIEs, or a
  /// DIEUnit which contains this DIE as its unit DIE.
  PointerUnion<DIE *, DIEUnit *> Owner;

  explicit DIE(dwarf::Tag Tag) : Tag(Tag) {}

public:
  /// Default construction is deleted; allocate DIEs with \a get().
  DIE() = delete;
  /// Copy construction is deleted; DIEs are not copyable.
  ///
  /// \param RHS Unused; copy construction is deleted.
  DIE(const DIE &RHS) = delete;
  /// Move construction is deleted; DIEs are not movable.
  ///
  /// \param RHS Unused; move construction is deleted.
  DIE(DIE &&RHS) = delete;
  /// Copy assignment is deleted; DIEs are not copyable.
  ///
  /// \param RHS Unused; copy assignment is deleted.
  DIE &operator=(const DIE &RHS) = delete;
  /// Move assignment is deleted; DIEs are not movable.
  ///
  /// \param RHS Unused; move assignment is deleted.
  DIE &operator=(const DIE &&RHS) = delete;

  /// Allocate a new DIE with tag \p Tag using \p Alloc.
  ///
  /// \param Alloc Bump allocator that owns the DIE.
  /// \param Tag DWARF tag for the new DIE.
  /// \returns A pointer to the newly allocated DIE.
  static DIE *get(BumpPtrAllocator &Alloc, dwarf::Tag Tag) {
    return new (Alloc) DIE(Tag);
  }

  // Accessors.
  /// Return the abbreviation number assigned to this DIE.
  ///
  /// \returns The abbreviation number assigned to this DIE.
  unsigned getAbbrevNumber() const { return AbbrevNumber; }
  /// Return the DWARF tag for this DIE.
  ///
  /// \returns The DWARF tag for this DIE.
  dwarf::Tag getTag() const { return Tag; }
  /// Get the compile/type unit relative offset of this DIE.
  ///
  /// \returns The compile/type unit relative offset of this DIE.
  unsigned getOffset() const {
    // A real Offset can't be zero because the unit headers are at offset zero.
    assert(Offset && "Offset being queried before it's been computed.");
    return Offset;
  }
  /// Return the size in bytes of this DIE including its children.
  ///
  /// \returns The size in bytes of this DIE including its children.
  unsigned getSize() const {
    // A real Size can't be zero because it includes the non-empty abbrev code.
    assert(Size && "Size being queried before it's been ocmputed.");
    return Size;
  }
  /// Return true if this DIE has children or children are forced.
  ///
  /// \returns True if this DIE has children or children are forced.
  bool hasChildren() const { return ForceChildren || !Children.empty(); }
  /// Force this DIE to claim children even when the child list is empty.
  ///
  /// \param B Whether to force a children abbreviation flag.
  void setForceChildren(bool B) { ForceChildren = B; }

  /// Iterator over child DIEs.
  using child_iterator = IntrusiveBackList<DIE>::iterator;
  /// Const iterator over child DIEs.
  using const_child_iterator = IntrusiveBackList<DIE>::const_iterator;
  /// Range of child DIEs.
  using child_range = iterator_range<child_iterator>;
  /// Const range of child DIEs.
  using const_child_range = iterator_range<const_child_iterator>;

  /// Return a mutable range over this DIE's children.
  ///
  /// \returns A mutable range over this DIE's children.
  child_range children() {
    return make_range(Children.begin(), Children.end());
  }
  /// Return a const range over this DIE's children.
  ///
  /// \returns A const range over this DIE's children.
  const_child_range children() const {
    return make_range(Children.begin(), Children.end());
  }

  /// Return the parent DIE, or null if this is a unit DIE.
  ///
  /// \returns The parent DIE, or null if this is a unit DIE.
  LLVM_ABI DIE *getParent() const;

  /// Generate the abbreviation for this DIE.
  ///
  /// Calculate the abbreviation for this, which should be uniqued and
  /// eventually used to call \a setAbbrevNumber().
  ///
  /// \returns The generated abbreviation for this DIE.
  LLVM_ABI DIEAbbrev generateAbbrev() const;

  /// Set the abbreviation number for this DIE.
  ///
  /// \param I Abbreviation number to assign.
  void setAbbrevNumber(unsigned I) { AbbrevNumber = I; }

  /// Get the absolute offset within the .debug_info or .debug_types section
  /// for this DIE.
  ///
  /// \returns Absolute offset of this DIE within its debug section.
  LLVM_ABI uint64_t getDebugSectionOffset() const;

  /// Compute the offset of this DIE and all its children.
  ///
  /// This function gets called just before we are going to generate the debug
  /// information and gives each DIE a chance to figure out its CU relative DIE
  /// offset, unique its abbreviation and fill in the abbreviation code, and
  /// return the unit offset that points to where the next DIE will be emitted
  /// within the debug unit section. After this function has been called for all
  /// DIE objects, the DWARF can be generated since all DIEs will be able to
  /// properly refer to other DIE objects since all DIEs have calculated their
  /// offsets.
  ///
  /// \param FormParams Used when calculating sizes.
  /// \param AbbrevSet the abbreviation used to unique DIE abbreviations.
  /// \param CUOffset the compile/type unit relative offset in bytes.
  /// \returns the offset for the DIE that follows this DIE within the
  /// current compile/type unit.
  LLVM_ABI unsigned
  computeOffsetsAndAbbrevs(const dwarf::FormParams &FormParams,
                           DIEAbbrevSet &AbbrevSet, unsigned CUOffset);

  /// Climb up the parent chain to get the compile unit or type unit DIE that
  /// this DIE belongs to.
  ///
  /// \returns the compile or type unit DIE that owns this DIE, or NULL if
  /// this DIE hasn't been added to a unit DIE.
  LLVM_ABI const DIE *getUnitDie() const;

  /// Climb up the parent chain to get the compile unit or type unit that this
  /// DIE belongs to.
  ///
  /// \returns the DIEUnit that represents the compile or type unit that owns
  /// this DIE, or NULL if this DIE hasn't been added to a unit DIE.
  LLVM_ABI DIEUnit *getUnit() const;

  /// Set the compile/type unit relative offset of this DIE.
  ///
  /// \param O Offset in bytes from the start of the unit.
  void setOffset(unsigned O) { Offset = O; }
  /// Set the size in bytes of this DIE including its children.
  ///
  /// \param S Size in bytes.
  void setSize(unsigned S) { Size = S; }

  /// Add a child to the DIE.
  ///
  /// \param Child Orphaned DIE to append as a child.
  /// \returns A reference to the appended child DIE.
  DIE &addChild(DIE *Child) {
    assert(!Child->getParent() && "Child should be orphaned");
    Child->Owner = this;
    Children.push_back(*Child);
    return Children.back();
  }

  /// Add a child to the front of this DIE's child list.
  ///
  /// \param Child Orphaned DIE to insert at the front.
  /// \returns A reference to the inserted child DIE.
  DIE &addChildFront(DIE *Child) {
    assert(!Child->getParent() && "Child should be orphaned");
    Child->Owner = this;
    Children.push_front(*Child);
    return Children.front();
  }

  /// Find a value in the DIE with the attribute given.
  ///
  /// Returns a default-constructed DIEValue (where \a DIEValue::getType()
  /// gives \a DIEValue::isNone) if no such attribute exists.
  ///
  /// \param Attribute DWARF attribute to look up.
  /// \returns The matching DIEValue, or an empty value if not found.
  LLVM_ABI DIEValue findAttribute(dwarf::Attribute Attribute) const;

  /// Print this DIE and its children to \p O.
  ///
  /// \param O Output stream.
  /// \param IndentCount Indentation depth for nested children.
  LLVM_ABI void print(raw_ostream &O, unsigned IndentCount = 0) const;
  /// Dump this DIE to standard error for debugging.
  LLVM_ABI void dump() const;
};

//===--------------------------------------------------------------------===//
/// Represents a compile or type unit.
class DIEUnit {
  /// The compile unit or type unit DIE. This variable must be an instance of
  /// DIE so that we can calculate the DIEUnit from any DIE by traversing the
  /// parent backchain and getting the Unit DIE, and then casting itself to a
  /// DIEUnit. This allows us to be able to find the DIEUnit for any DIE without
  /// having to store a pointer to the DIEUnit in each DIE instance.
  DIE Die;
  /// The section this unit will be emitted in. This may or may not be set to
  /// a valid section depending on the client that is emitting DWARF.
  MCSection *Section = nullptr;
  uint64_t Offset = 0; /// .debug_info or .debug_types absolute section offset.
protected:
  virtual ~DIEUnit() = default;

public:
  /// Construct a DIE unit whose unit DIE has the given DWARF tag.
  ///
  /// \param UnitTag DWARF tag for the unit DIE (compile or type unit).
  LLVM_ABI explicit DIEUnit(dwarf::Tag UnitTag);
  /// Copy construction is deleted; DIE units are not copyable.
  ///
  /// \param RHS Unused; copy construction is deleted.
  DIEUnit(const DIEUnit &RHS) = delete;
  /// Move construction is deleted; DIE units are not movable.
  ///
  /// \param RHS Unused; move construction is deleted.
  DIEUnit(DIEUnit &&RHS) = delete;
  /// Copy assignment is deleted; DIE units are not copyable.
  ///
  /// \param RHS Unused; copy assignment is deleted.
  void operator=(const DIEUnit &RHS) = delete;
  /// Move assignment is deleted; DIE units are not movable.
  ///
  /// \param RHS Unused; move assignment is deleted.
  void operator=(const DIEUnit &&RHS) = delete;
  /// Set the section that this DIEUnit will be emitted into.
  ///
  /// This function is used by some clients to set the section. Not all clients
  /// that emit DWARF use this section variable.
  ///
  /// \param Section Section that will contain this unit.
  void setSection(MCSection *Section) {
    assert(!this->Section);
    this->Section = Section;
  }

  /// Return the base symbol for cross-section relative references, if any.
  ///
  /// \returns The base symbol for cross-section relative references, or null.
  virtual const MCSymbol *getCrossSectionRelativeBaseAddress() const {
    return nullptr;
  }

  /// Return the section that this DIEUnit will be emitted into.
  ///
  /// \returns Section pointer which can be NULL.
  MCSection *getSection() const { return Section; }
  /// Set the absolute offset of this unit within its debug section.
  ///
  /// \param O Absolute section offset in bytes.
  void setDebugSectionOffset(uint64_t O) { Offset = O; }
  /// Return the absolute offset of this unit within its debug section.
  ///
  /// \returns Absolute offset of this unit within its debug section.
  uint64_t getDebugSectionOffset() const { return Offset; }
  /// Return the compile or type unit DIE for this unit.
  ///
  /// \returns The compile or type unit DIE for this unit.
  DIE &getUnitDie() { return Die; }
  /// Return the compile or type unit DIE for this unit.
  ///
  /// \returns The compile or type unit DIE for this unit.
  const DIE &getUnitDie() const { return Die; }
};

/// Concrete DIE unit used when no specialized unit behavior is required.
struct BasicDIEUnit final : DIEUnit {
  /// Construct a basic DIE unit with the given DWARF unit tag.
  ///
  /// \param UnitTag DWARF tag for the unit DIE (compile or type unit).
  explicit BasicDIEUnit(dwarf::Tag UnitTag) : DIEUnit(UnitTag) {}
};

//===--------------------------------------------------------------------===//
/// DIELoc - Represents an expression location.
//
class DIELoc : public DIEValueList {
  mutable unsigned Size = 0; // Size in bytes excluding size header.

public:
  /// Construct an empty location expression.
  DIELoc() = default;

  /// Calculate the size of the location expression.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \returns Size in bytes of the location expression excluding the size header.
  LLVM_ABI unsigned computeSize(const dwarf::FormParams &FormParams) const;

  // TODO: move setSize() and Size to DIEValueList.
  /// Set the cached size of this location expression in bytes.
  ///
  /// \param size Size in bytes excluding the size header.
  void setSize(unsigned size) { Size = size; }

  /// Choose the best DWARF form for this location expression.
  ///
  /// \param DwarfVersion DWARF version used to select exprloc vs block forms.
  /// \returns The best DWARF form for this location expression.
  dwarf::Form BestForm(unsigned DwarfVersion) const {
    if (DwarfVersion > 3)
      return dwarf::DW_FORM_exprloc;
    // Pre-DWARF4 location expressions were blocks and not exprloc.
    if ((uint8_t)Size == Size)
      return dwarf::DW_FORM_block1;
    if ((uint16_t)Size == Size)
      return dwarf::DW_FORM_block2;
    if ((uint32_t)Size == Size)
      return dwarf::DW_FORM_block4;
    return dwarf::DW_FORM_block;
  }

  /// Emit this location expression using \p Form via the asm printer.
  ///
  /// \param Asm Asm printer used to emit the value.
  /// \param Form DWARF form to use when emitting.
  LLVM_ABI void emitValue(const AsmPrinter *Asm, dwarf::Form Form) const;
  /// Return the size in bytes of this location when encoded with \p Form.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \param Form DWARF form to measure.
  /// \returns Size in bytes of the encoded location.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const;

  /// Print this location expression to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
};

//===--------------------------------------------------------------------===//
/// DIEBlock - Represents a block of values.
//
class DIEBlock : public DIEValueList {
  mutable unsigned Size = 0; // Size in bytes excluding size header.

public:
  /// Construct an empty block of DIE values.
  DIEBlock() = default;

  /// Calculate the size of the block excluding its size header.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \returns Size in bytes of the block excluding its size header.
  LLVM_ABI unsigned computeSize(const dwarf::FormParams &FormParams) const;

  // TODO: move setSize() and Size to DIEValueList.
  /// Set the cached size of this block in bytes.
  ///
  /// \param size Size in bytes excluding the size header.
  void setSize(unsigned size) { Size = size; }

  /// BestForm - Choose the best form for data.
  ///
  /// \returns The best DWARF block form for this data.
  dwarf::Form BestForm() const {
    if ((uint8_t)Size == Size)
      return dwarf::DW_FORM_block1;
    if ((uint16_t)Size == Size)
      return dwarf::DW_FORM_block2;
    if ((uint32_t)Size == Size)
      return dwarf::DW_FORM_block4;
    return dwarf::DW_FORM_block;
  }

  /// Emit this block using \p Form via the asm printer.
  ///
  /// \param Asm Asm printer used to emit the value.
  /// \param Form DWARF form to use when emitting.
  LLVM_ABI void emitValue(const AsmPrinter *Asm, dwarf::Form Form) const;
  /// Return the size in bytes of this block when encoded with \p Form.
  ///
  /// \param FormParams DWARF form size parameters for the target.
  /// \param Form DWARF form to measure.
  /// \returns Size in bytes of the encoded block.
  LLVM_ABI unsigned sizeOf(const dwarf::FormParams &FormParams,
                           dwarf::Form Form) const;

  /// Print this block to \p O.
  ///
  /// \param O Output stream.
  LLVM_ABI void print(raw_ostream &O) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_DIE_H
