//===- llvm/TableGen/Record.h - Classes for Table Records -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the main TableGen data structures, including the TableGen
// types, values, and high-level data structures.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_RECORD_H
#define LLVM_TABLEGEN_RECORD_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/TrailingObjects.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace llvm {
namespace detail {
struct RecordKeeperImpl;
} // namespace detail

class ListRecTy;
class Record;
class RecordKeeper;
class RecordVal;
class Resolver;
class StringInit;
class TypedInit;
class TGTimer;

//===----------------------------------------------------------------------===//
//  Type Classes
//===----------------------------------------------------------------------===//

/// Represents a TableGen type.
class RecTy {
public:
  /// Subclass discriminator (for dyn_cast<> et al.)
  enum RecTyKind {
    /// A single-bit type.
    BitRecTyKind,
    /// A fixed-width bit-vector type.
    BitsRecTyKind,
    /// An integer type.
    IntRecTyKind,
    /// A string type.
    StringRecTyKind,
    /// A list type.
    ListRecTyKind,
    /// A DAG type.
    DagRecTyKind,
    /// A record type.
    RecordRecTyKind
  };

private:
  RecTyKind Kind;
  /// The RecordKeeper that uniqued this Type.
  RecordKeeper &RK;
  /// ListRecTy of the list that has elements of this type. Its a cache that
  /// is populated on demand.
  mutable const ListRecTy *ListTy = nullptr;

public:
  /// Construct a type of kind \p K uniqued by \p RK.
  /// \param K The type kind.
  /// \param RK The record keeper that owns the type.
  RecTy(RecTyKind K, RecordKeeper &RK) : Kind(K), RK(RK) {}
  /// Destroy this type.
  virtual ~RecTy() = default;

  /// Return this type's kind.
  /// \return This type's kind.
  RecTyKind getRecTyKind() const { return Kind; }

  /// Return the RecordKeeper that uniqued this Type.
  /// \return The RecordKeeper that uniqued this Type.
  RecordKeeper &getRecordKeeper() const { return RK; }

  /// Return the TableGen spelling of this type.
  /// \return The TableGen spelling of this type.
  virtual std::string getAsString() const = 0;
  /// Print this type to \p OS.
  /// \param OS The output stream.
  void print(raw_ostream &OS) const { OS << getAsString(); }
  /// Print this type to the standard error stream.
  void dump() const;

  /// Return true if all values of 'this' type can be converted to the specified
  /// type.
  /// \param RHS The destination type.
  /// \return True if all values of 'this' type can be converted to the specified type.
  virtual bool typeIsConvertibleTo(const RecTy *RHS) const;

  /// Return true if 'this' type is equal to or a subtype of RHS. For example,
  /// a bit set is not an int, but they are convertible.
  /// \param RHS The type to test against.
  /// \return True if 'this' type is equal to or a subtype of RHS.
  virtual bool typeIsA(const RecTy *RHS) const;

  /// Returns the type representing list<thistype>.
  /// \return The type representing list<thistype>.
  const ListRecTy *getListTy() const;
};

/// Write \p Ty to \p OS.
/// \param OS The output stream.
/// \param Ty The type to print.
/// \return The output stream.
inline raw_ostream &operator<<(raw_ostream &OS, const RecTy &Ty) {
  Ty.print(OS);
  return OS;
}

/// 'bit' - Represent a single bit
class BitRecTy : public RecTy {
  friend detail::RecordKeeperImpl;

  BitRecTy(RecordKeeper &RK) : RecTy(BitRecTyKind, RK) {}

public:
  /// Return true if \p RT is a BitRecTy.
  /// \param RT The type to test.
  /// \return True if \p RT is a BitRecTy.
  static bool classof(const RecTy *RT) {
    return RT->getRecTyKind() == BitRecTyKind;
  }

  /// Return the uniqued BitRecTy for \p RK.
  /// \param RK The record keeper that owns the type.
  /// \return The uniqued BitRecTy for \p RK.
  static const BitRecTy *get(RecordKeeper &RK);

  /// Return the TableGen spelling of this type.
  /// \return The TableGen spelling of this type.
  std::string getAsString() const override { return "bit"; }

  /// Return true if this type is convertible to \p RHS.
  /// \param RHS The destination type.
  /// \return True if this type is convertible to \p RHS.
  bool typeIsConvertibleTo(const RecTy *RHS) const override;
};

/// 'bits<n>' - Represent a fixed number of bits
class BitsRecTy : public RecTy {
  unsigned Size;

  explicit BitsRecTy(RecordKeeper &RK, unsigned Sz)
      : RecTy(BitsRecTyKind, RK), Size(Sz) {}

public:
  /// Return true if \p RT is a BitsRecTy.
  /// \param RT The type to test.
  /// \return True if \p RT is a BitsRecTy.
  static bool classof(const RecTy *RT) {
    return RT->getRecTyKind() == BitsRecTyKind;
  }

  /// Return the uniqued BitsRecTy for \p Sz bits in \p RK.
  /// \param RK The record keeper that owns the type.
  /// \param Sz The number of bits.
  /// \return The uniqued BitsRecTy for \p Sz bits in \p RK.
  static const BitsRecTy *get(RecordKeeper &RK, unsigned Sz);

  /// Return the number of bits in this type.
  /// \return The number of bits in this type.
  unsigned getNumBits() const { return Size; }

  /// Return the TableGen spelling of this type.
  /// \return The TableGen spelling of this type.
  std::string getAsString() const override;

  /// Return true if this type is convertible to \p RHS.
  /// \param RHS The destination type.
  /// \return True if this type is convertible to \p RHS.
  bool typeIsConvertibleTo(const RecTy *RHS) const override;
};

/// 'int' - Represent an integer value of no particular size
class IntRecTy : public RecTy {
  friend detail::RecordKeeperImpl;

  IntRecTy(RecordKeeper &RK) : RecTy(IntRecTyKind, RK) {}

public:
  /// Return true if \p RT is an IntRecTy.
  /// \param RT The type to test.
  /// \return True if \p RT is an IntRecTy.
  static bool classof(const RecTy *RT) {
    return RT->getRecTyKind() == IntRecTyKind;
  }

  /// Return the uniqued IntRecTy for \p RK.
  /// \param RK The record keeper that owns the type.
  /// \return The uniqued IntRecTy for \p RK.
  static const IntRecTy *get(RecordKeeper &RK);

  /// Return the TableGen spelling of this type.
  /// \return The TableGen spelling of this type.
  std::string getAsString() const override { return "int"; }

  /// Return true if this type is convertible to \p RHS.
  /// \param RHS The destination type.
  /// \return True if this type is convertible to \p RHS.
  bool typeIsConvertibleTo(const RecTy *RHS) const override;
};

/// 'string' - Represent an string value
class StringRecTy : public RecTy {
  friend detail::RecordKeeperImpl;

  StringRecTy(RecordKeeper &RK) : RecTy(StringRecTyKind, RK) {}

public:
  /// Return true if \p RT is a StringRecTy.
  /// \param RT The type to test.
  /// \return True if \p RT is a StringRecTy.
  static bool classof(const RecTy *RT) {
    return RT->getRecTyKind() == StringRecTyKind;
  }

  /// Return the uniqued StringRecTy for \p RK.
  /// \param RK The record keeper that owns the type.
  /// \return The uniqued StringRecTy for \p RK.
  static const StringRecTy *get(RecordKeeper &RK);

  /// Return the TableGen spelling of this type.
  /// \return The TableGen spelling of this type.
  std::string getAsString() const override;

  /// Return true if this type is convertible to \p RHS.
  /// \param RHS The destination type.
  /// \return True if this type is convertible to \p RHS.
  bool typeIsConvertibleTo(const RecTy *RHS) const override;
};

/// 'list<Ty>' - Represent a list of element values, all of which must be of
/// the specified type. The type is stored in ElementTy.
class ListRecTy : public RecTy {
  friend const ListRecTy *RecTy::getListTy() const;

  const RecTy *ElementTy;

  explicit ListRecTy(const RecTy *T)
      : RecTy(ListRecTyKind, T->getRecordKeeper()), ElementTy(T) {}

public:
  /// Return true if \p RT is a ListRecTy.
  /// \param RT The type to test.
  /// \return True if \p RT is a ListRecTy.
  static bool classof(const RecTy *RT) {
    return RT->getRecTyKind() == ListRecTyKind;
  }

  /// Return the uniqued ListRecTy for element type \p T.
  /// \param T The list element type.
  /// \return The uniqued ListRecTy for element type \p T.
  static const ListRecTy *get(const RecTy *T) { return T->getListTy(); }
  /// Return this list's element type.
  /// \return This list's element type.
  const RecTy *getElementType() const { return ElementTy; }

  /// Return the TableGen spelling of this type.
  /// \return The TableGen spelling of this type.
  std::string getAsString() const override;

  /// Return true if this type is convertible to \p RHS.
  /// \param RHS The destination type.
  /// \return True if this type is convertible to \p RHS.
  bool typeIsConvertibleTo(const RecTy *RHS) const override;

  /// Return true if this type is a \p RHS.
  /// \param RHS The type to test against.
  /// \return True if this type is a \p RHS.
  bool typeIsA(const RecTy *RHS) const override;
};

/// 'dag' - Represent a dag fragment
class DagRecTy : public RecTy {
  friend detail::RecordKeeperImpl;

  DagRecTy(RecordKeeper &RK) : RecTy(DagRecTyKind, RK) {}

public:
  /// Return true if \p RT is a DagRecTy.
  /// \param RT The type to test.
  /// \return True if \p RT is a DagRecTy.
  static bool classof(const RecTy *RT) {
    return RT->getRecTyKind() == DagRecTyKind;
  }

  /// Return the uniqued DagRecTy for \p RK.
  /// \param RK The record keeper that owns the type.
  /// \return The uniqued DagRecTy for \p RK.
  static const DagRecTy *get(RecordKeeper &RK);

  /// Return the TableGen spelling of this type.
  /// \return The TableGen spelling of this type.
  std::string getAsString() const override;
};

/// '[classname]' - Type of record values that have zero or more superclasses.
///
/// The list of superclasses is non-redundant, i.e. only contains classes that
/// are not the superclass of some other listed class.
class RecordRecTy final : public RecTy,
                          public FoldingSetNode,
                          private TrailingObjects<RecordRecTy, const Record *> {
  friend TrailingObjects;
  friend class Record;
  friend detail::RecordKeeperImpl;

  unsigned NumClasses;

  /// Construct a record type owned by \p RK with superclasses \p Classes.
  /// \param RK The record keeper that owns the type.
  /// \param Classes The non-redundant superclass list.
  explicit RecordRecTy(RecordKeeper &RK, ArrayRef<const Record *> Classes);

public:
  /// Copying a RecordRecTy is not supported.
  /// \param Other Unused; copy construction is deleted.
  RecordRecTy(const RecordRecTy &Other) = delete;
  /// Assigning a RecordRecTy is not supported.
  /// \param Other Unused; copy assignment is deleted.
  RecordRecTy &operator=(const RecordRecTy &Other) = delete;

  // Do not use sized deallocation due to trailing objects.
  /// Deallocate the record type at \p Ptr.
  /// \param Ptr The allocation to deallocate.
  void operator delete(void *Ptr) { ::operator delete(Ptr); }

  /// Return true if \p RT is a RecordRecTy.
  /// \param RT The type to test.
  /// \return True if \p RT is a RecordRecTy.
  static bool classof(const RecTy *RT) {
    return RT->getRecTyKind() == RecordRecTyKind;
  }

  /// Get the record type with the given non-redundant list of superclasses.
  /// \param RK The record keeper that owns the type.
  /// \param Classes The non-redundant superclass list.
  /// \return The record type with the given non-redundant list of superclasses.
  static const RecordRecTy *get(RecordKeeper &RK,
                                ArrayRef<const Record *> Classes);
  /// Return the uniqued RecordRecTy for \p Class.
  /// \param Class The record class.
  /// \return The uniqued RecordRecTy for \p Class.
  static const RecordRecTy *get(const Record *Class);

  /// Add this type's profile to \p ID.
  /// \param ID The folding-set profile to populate.
  void Profile(FoldingSetNodeID &ID) const;

  /// Return this type's direct superclasses.
  /// \return This type's direct superclasses.
  ArrayRef<const Record *> getClasses() const {
    return getTrailingObjects(NumClasses);
  }

  /// An iterator over direct superclasses.
  using const_record_iterator = const Record *const *;

  /// Return an iterator to the first direct superclass.
  /// \return An iterator to the first direct superclass.
  const_record_iterator classes_begin() const { return getClasses().begin(); }
  /// Return an iterator past the last direct superclass.
  /// \return An iterator past the last direct superclass.
  const_record_iterator classes_end() const { return getClasses().end(); }

  /// Return the TableGen spelling of this type.
  /// \return The TableGen spelling of this type.
  std::string getAsString() const override;

  /// Return true if this type has \p Class as a superclass.
  /// \param Class The superclass to test for.
  /// \return True if this type has \p Class as a superclass.
  bool isSubClassOf(const Record *Class) const;
  /// Return true if this type is convertible to \p RHS.
  /// \param RHS The destination type.
  /// \return True if this type is convertible to \p RHS.
  bool typeIsConvertibleTo(const RecTy *RHS) const override;

  /// Return true if this type is a \p RHS.
  /// \param RHS The type to test against.
  /// \return True if this type is a \p RHS.
  bool typeIsA(const RecTy *RHS) const override;
};

/// Find a common type that T1 and T2 convert to.
/// Return 0 if no such type exists.
/// \param T1 The first type.
/// \param T2 The second type.
/// \return A common type that both convert to, or null if none exists.
const RecTy *resolveTypes(const RecTy *T1, const RecTy *T2);

//===----------------------------------------------------------------------===//
//  Initializer Classes
//===----------------------------------------------------------------------===//

/// Represents a TableGen initializer value.
class Init {
protected:
  /// Discriminator enum (for isa<>, dyn_cast<>, et al.)
  ///
  /// This enum is laid out by a preorder traversal of the inheritance
  /// hierarchy, and does not contain an entry for abstract classes, as per
  /// the recommendation in docs/HowToSetUpLLVMStyleRTTI.rst.
  ///
  /// We also explicitly include "first" and "last" values for each
  /// interior node of the inheritance tree, to make it easier to read the
  /// corresponding classof().
  ///
  /// We could pack these a bit tighter by not having the IK_FirstXXXInit
  /// and IK_LastXXXInit be their own values, but that would degrade
  /// readability for really no benefit.
  enum InitKind : uint8_t {
    /// A sentinel before all initializer kinds.
    IK_First, // unused; silence a spurious warning
    /// The first typed initializer kind.
    IK_FirstTypedInit,
    /// A single-bit initializer.
    IK_BitInit,
    /// A bit-vector initializer.
    IK_BitsInit,
    /// A DAG initializer.
    IK_DagInit,
    /// A record definition initializer.
    IK_DefInit,
    /// A record field initializer.
    IK_FieldInit,
    /// An integer initializer.
    IK_IntInit,
    /// A list initializer.
    IK_ListInit,
    /// The first operation initializer kind.
    IK_FirstOpInit,
    /// A binary operation initializer.
    IK_BinOpInit,
    /// A ternary operation initializer.
    IK_TernOpInit,
    /// A unary operation initializer.
    IK_UnOpInit,
    /// The last operation initializer kind.
    IK_LastOpInit,
    /// A conditional operation initializer.
    IK_CondOpInit,
    /// A fold operation initializer.
    IK_FoldOpInit,
    /// A type-test operation initializer.
    IK_IsAOpInit,
    /// An existence-test operation initializer.
    IK_ExistsOpInit,
    /// An instances operation initializer.
    IK_InstancesOpInit,
    /// An anonymous-name initializer.
    IK_AnonymousNameInit,
    /// A string initializer.
    IK_StringInit,
    /// A variable initializer.
    IK_VarInit,
    /// A variable-bit initializer.
    IK_VarBitInit,
    /// A variable-definition initializer.
    IK_VarDefInit,
    /// The last typed initializer kind.
    IK_LastTypedInit,
    /// An uninitialized-value initializer.
    IK_UnsetInit,
    /// An argument initializer.
    IK_ArgumentInit,
  };

private:
  const InitKind Kind;

protected:
  /// Stores the opcode for unary, binary, and ternary operation initializers.
  uint8_t Opc;

private:
  virtual void anchor();

public:
  /// Get the kind (type) of the value.
  /// \return The kind (type) of the value.
  InitKind getKind() const { return Kind; }

  /// Get the record keeper that initialized this Init.
  /// \return The record keeper that initialized this Init.
  RecordKeeper &getRecordKeeper() const;

protected:
  /// Construct an initializer of kind \p K with opcode \p Opc.
  /// \param K The initializer kind.
  /// \param Opc The optional operation opcode.
  explicit Init(InitKind K, uint8_t Opc = 0) : Kind(K), Opc(Opc) {}

public:
  /// Copying an Init is not supported.
  /// \param Other Unused; copy construction is deleted.
  Init(const Init &Other) = delete;
  /// Assigning an Init is not supported.
  /// \param Other Unused; copy assignment is deleted.
  Init &operator=(const Init &Other) = delete;
  /// Destroy this initializer.
  virtual ~Init() = default;

  /// Is this a complete value with no unset (uninitialized) subvalues?
  /// \return True if this is a complete value with no unset (uninitialized) subvalues.
  virtual bool isComplete() const { return true; }

  /// Is this a concrete and fully resolved value without any references or
  /// stuck operations? Unset values are concrete.
  /// \return True if this is a concrete and fully resolved value without any references or stuck operations.
  virtual bool isConcrete() const { return false; }

  /// Print this value.
  /// \param OS The output stream.
  void print(raw_ostream &OS) const { OS << getAsString(); }

  /// Convert this value to a literal form.
  /// \return This value in literal form.
  virtual std::string getAsString() const = 0;

  /// Convert this value to a literal form,
  /// without adding quotes around a string.
  /// \return This value in literal form without quotes around strings.
  virtual std::string getAsUnquotedString() const { return getAsString(); }

  /// Debugging method that may be called through a debugger; just
  /// invokes print on stderr.
  void dump() const;

  /// If this value is convertible to type \p Ty, return a value whose
  /// type is \p Ty, generating a !cast operation if required.
  /// Otherwise, return null.
  /// \param Ty The destination type.
  /// \return A value of type \p Ty, or null if conversion is not possible.
  virtual const Init *getCastTo(const RecTy *Ty) const = 0;

  /// Convert this initializer to \p Ty.
  ///
  /// Return null if conversion is not possible, including when the value's
  /// type is convertible to \p Ty but it has unresolved references.
  /// \param Ty The destination type.
  /// \return The converted initializer, or null if conversion is not possible.
  virtual const Init *convertInitializerTo(const RecTy *Ty) const = 0;

  /// Select a bit range from this initializer.
  ///
  /// Return the specified bits as a new \p Init of type \p bits, or null if
  /// bit selection is not legal for this value.
  /// \param Bits The selected bit indices.
  /// \return The selected bits as a new initializer, or null if bit selection is not legal.
  virtual const Init *
  convertInitializerBitRange(ArrayRef<unsigned> Bits) const {
    return nullptr;
  }

  /// This function is used to implement the FieldInit class.
  /// Implementors of this method should return the type of the named
  /// field if they are of type record.
  /// \param FieldName The name of the field.
  /// \return The type of the named field, or null if not applicable.
  virtual const RecTy *getFieldType(const StringInit *FieldName) const {
    return nullptr;
  }

  /// Resolve references in this initializer.
  ///
  /// This is used by initializers that refer to variables which may not be
  /// defined when the expression is formed. When a variable is assigned later,
  /// the method lets its value propagate to users.
  /// \param R The resolver for referenced variables.
  /// \return The resolved initializer.
  virtual const Init *resolveReferences(Resolver &R) const { return this; }

  /// Get the \p Init value of the specified bit.
  /// \param Bit The bit index.
  /// \return The \p Init value of the specified bit.
  virtual const Init *getBit(unsigned Bit) const = 0;
};

/// Write \p I to \p OS.
/// \param OS The output stream.
/// \param I The initializer to print.
/// \return The output stream.
inline raw_ostream &operator<<(raw_ostream &OS, const Init &I) {
  I.print(OS); return OS;
}

/// This is the common superclass of types that have a specific,
/// explicit type, stored in ValueTy.
class TypedInit : public Init {
  const RecTy *ValueTy;

protected:
  /// Construct a typed initializer of kind \p K and type \p T.
  /// \param K The initializer kind.
  /// \param T The explicit value type.
  /// \param Opc The optional operation opcode.
  explicit TypedInit(InitKind K, const RecTy *T, uint8_t Opc = 0)
      : Init(K, Opc), ValueTy(T) {}

public:
  /// Copying a TypedInit is not supported.
  /// \param Other Unused; copy construction is deleted.
  TypedInit(const TypedInit &Other) = delete;
  /// Assigning a TypedInit is not supported.
  /// \param Other Unused; copy assignment is deleted.
  TypedInit &operator=(const TypedInit &Other) = delete;

  /// Return true if \p I is a TypedInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a TypedInit.
  static bool classof(const Init *I) {
    return I->getKind() >= IK_FirstTypedInit &&
           I->getKind() <= IK_LastTypedInit;
  }

  /// Get the type of the Init as a RecTy.
  /// \return The type of the Init as a RecTy.
  const RecTy *getType() const { return ValueTy; }

  /// Get the record keeper that initialized this Init.
  /// \return The record keeper that initialized this Init.
  RecordKeeper &getRecordKeeper() const { return ValueTy->getRecordKeeper(); }

  /// Return this initializer cast to \p Ty.
  /// \param Ty The destination type.
  /// \return This initializer cast to \p Ty.
  const Init *getCastTo(const RecTy *Ty) const override;
  /// Return this initializer converted to \p Ty.
  /// \param Ty The destination type.
  /// \return This initializer converted to \p Ty.
  const Init *convertInitializerTo(const RecTy *Ty) const override;

  /// Return the bits selected by \p Bits.
  /// \param Bits The selected bit indices.
  /// \return The bits selected by \p Bits.
  const Init *
  convertInitializerBitRange(ArrayRef<unsigned> Bits) const override;

  /// This method is used to implement the FieldInit class.
  /// Implementors of this method should return the type of the named field if
  /// they are of type record.
  /// \param FieldName The name of the field.
  /// \return The type of the named field, or null if not applicable.
  const RecTy *getFieldType(const StringInit *FieldName) const override;
};

/// '?' - Represents an uninitialized value.
class UnsetInit final : public Init {
  friend detail::RecordKeeperImpl;

  /// The record keeper that initialized this Init.
  RecordKeeper &RK;

  /// Construct the singleton unset initializer for \p RK.
  /// \param RK The record keeper that owns the initializer.
  UnsetInit(RecordKeeper &RK) : Init(IK_UnsetInit), RK(RK) {}

public:
  /// Copying an UnsetInit is not supported.
  /// \param Other Unused; copy construction is deleted.
  UnsetInit(const UnsetInit &Other) = delete;
  /// Assigning an UnsetInit is not supported.
  /// \param Other Unused; copy assignment is deleted.
  UnsetInit &operator=(const UnsetInit &Other) = delete;

  /// Return true if \p I is an UnsetInit.
  /// \param I The initializer to test.
  /// \return True if \p I is an UnsetInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_UnsetInit;
  }

  /// Get the singleton unset Init.
  /// \param RK The record keeper that owns the initializer.
  /// \return The singleton unset Init.
  static UnsetInit *get(RecordKeeper &RK);

  /// Get the record keeper that initialized this Init.
  /// \return The record keeper that initialized this Init.
  RecordKeeper &getRecordKeeper() const { return RK; }

  /// Return this initializer cast to \p Ty.
  /// \param Ty The destination type.
  /// \return This initializer cast to \p Ty.
  const Init *getCastTo(const RecTy *Ty) const override;
  /// Return this initializer converted to \p Ty.
  /// \param Ty The destination type.
  /// \return This initializer converted to \p Ty.
  const Init *convertInitializerTo(const RecTy *Ty) const override;

  /// Return this unset initializer for \p Bit.
  /// \param Bit The bit index.
  /// \return This unset initializer for \p Bit.
  const Init *getBit(unsigned Bit) const override { return this; }

  /// Is this a complete value with no unset (uninitialized) subvalues?
  /// \return True if this is a complete value with no unset (uninitialized) subvalues.
  bool isComplete() const override { return false; }

  /// Return true because unset initializers are concrete.
  /// \return True because unset initializers are concrete.
  bool isConcrete() const override { return true; }

  /// Get the string representation of the Init.
  /// \return The string representation of the Init.
  std::string getAsString() const override { return "?"; }
};

/// Stores either a positional argument index or a named argument key.
using ArgAuxType = std::variant<unsigned, const Init *>;
/// Represents a positional or named TableGen argument.
class ArgumentInit final : public Init, public FoldingSetNode {
public:
  /// Enumerates positional and named argument kinds.
  enum Kind {
    /// A positional argument.
    Positional,
    /// A named argument.
    Named,
  };

private:
  const Init *Value;
  ArgAuxType Aux;

protected:
  /// Construct an argument with \p Value and auxiliary data \p Aux.
  /// \param Value The argument value.
  /// \param Aux The positional index or named key.
  explicit ArgumentInit(const Init *Value, ArgAuxType Aux)
      : Init(IK_ArgumentInit), Value(Value), Aux(Aux) {}

public:
  /// Copying an ArgumentInit is not supported.
  /// \param Other Unused; copy construction is deleted.
  ArgumentInit(const ArgumentInit &Other) = delete;
  /// Assigning an ArgumentInit is not supported.
  /// \param Other Unused; copy assignment is deleted.
  ArgumentInit &operator=(const ArgumentInit &Other) = delete;

  /// Return true if \p I is an ArgumentInit.
  /// \param I The initializer to test.
  /// \return True if \p I is an ArgumentInit.
  static bool classof(const Init *I) { return I->getKind() == IK_ArgumentInit; }

  /// Return the record keeper that owns this argument.
  /// \return The record keeper that owns this argument.
  RecordKeeper &getRecordKeeper() const { return Value->getRecordKeeper(); }

  /// Return the uniqued ArgumentInit for \p Value and \p Aux.
  /// \param Value The argument value.
  /// \param Aux The positional index or named key.
  /// \return The uniqued ArgumentInit for \p Value and \p Aux.
  static const ArgumentInit *get(const Init *Value, ArgAuxType Aux);

  /// Return true if this is a positional argument.
  /// \return True if this is a positional argument.
  bool isPositional() const { return Aux.index() == Positional; }
  /// Return true if this is a named argument.
  /// \return True if this is a named argument.
  bool isNamed() const { return Aux.index() == Named; }

  /// Return this argument's value.
  /// \return This argument's value.
  const Init *getValue() const { return Value; }
  /// Return this positional argument's index.
  /// \return This positional argument's index.
  unsigned getIndex() const {
    assert(isPositional() && "Should be positional!");
    return std::get<Positional>(Aux);
  }
  /// Return this named argument's key.
  /// \return This named argument's key.
  const Init *getName() const {
    assert(isNamed() && "Should be named!");
    return std::get<Named>(Aux);
  }
  /// Return a copy of this argument with \p Value.
  /// \param Value The replacement argument value.
  /// \return A copy of this argument with \p Value.
  const ArgumentInit *cloneWithValue(const Init *Value) const {
    return get(Value, Aux);
  }

  /// Add this argument's profile to \p ID.
  /// \param ID The folding-set profile to populate.
  void Profile(FoldingSetNodeID &ID) const;

  /// Resolve references using \p R.
  /// \param R The resolver for referenced variables.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;
  /// Return the TableGen spelling of this value.
  /// \return The TableGen spelling of this value.
  std::string getAsString() const override {
    if (isPositional())
      return utostr(getIndex()) + ": " + Value->getAsString();
    if (isNamed())
      return getName()->getAsString() + ": " + Value->getAsString();
    llvm_unreachable("Unsupported argument type!");
    return "";
  }

  /// Return false because arguments are never complete.
  /// \return False because arguments are never complete.
  bool isComplete() const override { return false; }
  /// Return false because arguments are never concrete.
  /// \return False because arguments are never concrete.
  bool isConcrete() const override { return false; }
  /// Return bit \p Bit from this argument's value.
  /// \param Bit The bit index.
  /// \return Bit \p Bit from this argument's value.
  const Init *getBit(unsigned Bit) const override { return Value->getBit(Bit); }
  /// Return this argument's value cast to \p Ty.
  /// \param Ty The destination type.
  /// \return This argument's value cast to \p Ty.
  const Init *getCastTo(const RecTy *Ty) const override {
    return Value->getCastTo(Ty);
  }
  /// Return this argument's value converted to \p Ty.
  /// \param Ty The destination type.
  /// \return This argument's value converted to \p Ty.
  const Init *convertInitializerTo(const RecTy *Ty) const override {
    return Value->convertInitializerTo(Ty);
  }
};

/// 'true'/'false' - Represent a concrete initializer for a bit.
class BitInit final : public TypedInit {
  friend detail::RecordKeeperImpl;

  bool Value;

  explicit BitInit(bool V, const RecTy *T)
      : TypedInit(IK_BitInit, T), Value(V) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  BitInit(const BitInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  BitInit &operator=(const BitInit &Other) = delete;

  /// Return true if \p I is a BitInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a BitInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_BitInit;
  }

  /// Return the bit initializer for \p V in \p RK.
  /// \param RK The record keeper that owns the initializer.
  /// \param V The bit value.
  /// \return The bit initializer for \p V in \p RK.
  static BitInit *get(RecordKeeper &RK, bool V);

  /// Return this initializer's bit value.
  /// \return This initializer's bit value.
  bool getValue() const { return Value; }

  /// Convert this initializer to \p Ty.
  /// \param Ty The destination type.
  /// \return The converted initializer, or null if conversion is not possible.
  const Init *convertInitializerTo(const RecTy *Ty) const override;

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override {
    assert(Bit < 1 && "Bit index out of range!");
    return this;
  }

  /// Return whether this initializer is concrete.
  /// \return Whether this initializer is concrete.
  bool isConcrete() const override { return true; }
  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override { return Value ? "1" : "0"; }
};

/// '{ a, b, c }' - Represents an initializer for a BitsRecTy value.
/// It contains a vector of bits, whose size is determined by the type.
class BitsInit final : public TypedInit,
                       public FoldingSetNode,
                       private TrailingObjects<BitsInit, const Init *> {
  friend TrailingObjects;
  unsigned NumBits;

  BitsInit(RecordKeeper &RK, ArrayRef<const Init *> Bits);

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  BitsInit(const BitsInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  BitsInit &operator=(const BitsInit &Other) = delete;

  /// Deallocate this trailing-object initializer without sized deallocation.
  /// \param Ptr The allocation to deallocate.
  void operator delete(void *Ptr) { ::operator delete(Ptr); }

  /// Return true if \p I is a BitsInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a BitsInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_BitsInit;
  }

  /// Return a bits initializer containing \p Range in \p RK.
  /// \param RK The record keeper that owns the initializer.
  /// \param Range The bit initializers.
  /// \return A bits initializer containing \p Range in \p RK.
  static BitsInit *get(RecordKeeper &RK, ArrayRef<const Init *> Range);

  /// Add this initializer to \p ID.
  /// \param ID The folding-set identifier.
  void Profile(FoldingSetNodeID &ID) const;

  /// Return the number of bits.
  /// \return The number of bits.
  unsigned getNumBits() const { return NumBits; }

  /// Convert this initializer to \p Ty.
  /// \param Ty The destination type.
  /// \return The converted initializer, or null if conversion is not possible.
  const Init *convertInitializerTo(const RecTy *Ty) const override;
  /// Convert the bit range \p Bits to an initializer.
  /// \param Bits The selected bit indices.
  /// \return The selected bits as an initializer, or null if bit selection is not legal.
  const Init *
  convertInitializerBitRange(ArrayRef<unsigned> Bits) const override;
  /// Convert this initializer to an integer when possible.
  /// \return The integer value, or std::nullopt if conversion is not possible.
  std::optional<int64_t> convertInitializerToInt() const;

  /// Return the set of known bits as a 64-bit integer.
  /// \return The set of known bits as a 64-bit integer.
  uint64_t convertKnownBitsToInt() const;

  /// Return whether every bit is complete.
  /// \return Whether every bit is complete.
  bool isComplete() const override;
  /// Return whether all bits are incomplete.
  /// \return Whether all bits are incomplete.
  bool allInComplete() const;
  /// Return whether every bit is concrete.
  /// \return Whether every bit is concrete.
  bool isConcrete() const override;
  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return the contained bit initializers.
  /// \return The contained bit initializers.
  ArrayRef<const Init *> getBits() const { return getTrailingObjects(NumBits); }

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override { return getBits()[Bit]; }
};

/// '7' - Represent an initialization by a literal integer value.
class IntInit final : public TypedInit {
  int64_t Value;

  explicit IntInit(RecordKeeper &RK, int64_t V)
      : TypedInit(IK_IntInit, IntRecTy::get(RK)), Value(V) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  IntInit(const IntInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  IntInit &operator=(const IntInit &Other) = delete;

  /// Return true if \p I is an IntInit.
  /// \param I The initializer to test.
  /// \return True if \p I is an IntInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_IntInit;
  }

  /// Return the integer initializer for \p V in \p RK.
  /// \param RK The record keeper that owns the initializer.
  /// \param V The integer value.
  /// \return The integer initializer for \p V in \p RK.
  static IntInit *get(RecordKeeper &RK, int64_t V);

  /// Return this initializer's integer value.
  /// \return This initializer's integer value.
  int64_t getValue() const { return Value; }

  /// Convert this initializer to \p Ty.
  /// \param Ty The destination type.
  /// \return The converted initializer, or null if conversion is not possible.
  const Init *convertInitializerTo(const RecTy *Ty) const override;
  /// Convert the bit range \p Bits to an initializer.
  /// \param Bits The selected bit indices.
  /// \return The selected bits as an initializer, or null if bit selection is not legal.
  const Init *
  convertInitializerBitRange(ArrayRef<unsigned> Bits) const override;

  /// Return whether this initializer is concrete.
  /// \return Whether this initializer is concrete.
  bool isConcrete() const override { return true; }
  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override {
    return BitInit::get(getRecordKeeper(), (Value & (1ULL << Bit)) != 0);
  }
};

/// Represents an anonymous record name.
class AnonymousNameInit final : public TypedInit {
  unsigned Value;

  explicit AnonymousNameInit(RecordKeeper &RK, unsigned V)
      : TypedInit(IK_AnonymousNameInit, StringRecTy::get(RK)), Value(V) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  AnonymousNameInit(const AnonymousNameInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  AnonymousNameInit &operator=(const AnonymousNameInit &Other) = delete;

  /// Return true if \p I is an AnonymousNameInit.
  /// \param I The initializer to test.
  /// \return True if \p I is an AnonymousNameInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_AnonymousNameInit;
  }

  /// Return the anonymous name initializer for \p V in \p RK.
  /// \param RK The record keeper that owns the initializer.
  /// \param V The anonymous name number.
  /// \return The anonymous name initializer for \p V in \p RK.
  static AnonymousNameInit *get(RecordKeeper &RK, unsigned V);

  /// Return this initializer's anonymous name number.
  /// \return This initializer's anonymous name number.
  unsigned getValue() const { return Value; }

  /// Return the string initializer for this name.
  /// \return The string initializer for this name.
  const StringInit *getNameInit() const;

  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override {
    llvm_unreachable("Illegal bit reference off string");
  }
};

/// Represents an initializer containing a string value.
class StringInit final : public TypedInit {
public:
  /// Selects the textual representation of a string.
  enum StringFormat {
    /// Format the value as `"text"`.
    SF_String,
    /// Format the value as `[{text}]`.
    SF_Code,
  };

private:
  StringRef Value;
  StringFormat Format;

  explicit StringInit(RecordKeeper &RK, StringRef V, StringFormat Fmt)
      : TypedInit(IK_StringInit, StringRecTy::get(RK)), Value(V), Format(Fmt) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  StringInit(const StringInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  StringInit &operator=(const StringInit &Other) = delete;

  /// Return true if \p I is a StringInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a StringInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_StringInit;
  }

  /// Return the string initializer for the given value and format.
  /// \param RK The record keeper that owns the initializer.
  /// \param Value The string value.
  /// \param Fmt The textual format.
  /// \return The string initializer for the given value and format.
  static const StringInit *get(RecordKeeper &RK, StringRef Value,
                               StringFormat Fmt = SF_String);

  /// Return the stronger of two string formats.
  /// \param Fmt1 The first format.
  /// \param Fmt2 The second format.
  /// \return The stronger of two string formats.
  static StringFormat determineFormat(StringFormat Fmt1, StringFormat Fmt2) {
    return (Fmt1 == SF_Code || Fmt2 == SF_Code) ? SF_Code : SF_String;
  }

  /// Return the contained string value.
  /// \return The contained string value.
  StringRef getValue() const { return Value; }
  /// Return the textual format.
  /// \return The textual format.
  StringFormat getFormat() const { return Format; }
  /// Return whether this string uses code format.
  /// \return Whether this string uses code format.
  bool hasCodeFormat() const { return Format == SF_Code; }

  /// Convert this initializer to \p Ty.
  /// \param Ty The destination type.
  /// \return The converted initializer, or null if conversion is not possible.
  const Init *convertInitializerTo(const RecTy *Ty) const override;

  /// Return whether this initializer is concrete.
  /// \return Whether this initializer is concrete.
  bool isConcrete() const override { return true; }

  /// Return this initializer as quoted TableGen source text.
  /// \return This initializer as quoted TableGen source text.
  std::string getAsString() const override {
    if (Format == SF_String)
      return "\"" + Value.str() + "\"";
    else
      return "[{" + Value.str() + "}]";
  }

  /// Return the contained string without quotes.
  /// \return The contained string without quotes.
  std::string getAsUnquotedString() const override { return Value.str(); }

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override {
    llvm_unreachable("Illegal bit reference off string");
  }
};

/// Represents an initializer containing a list of values.
class ListInit final : public TypedInit,
                       public FoldingSetNode,
                       private TrailingObjects<ListInit, const Init *> {
  friend TrailingObjects;
  unsigned NumElements;

public:
  /// Iterator over the list elements.
  using const_iterator = const Init *const *;

private:
  explicit ListInit(ArrayRef<const Init *> Elements, const RecTy *EltTy);

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  ListInit(const ListInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  ListInit &operator=(const ListInit &Other) = delete;

  /// Deallocate this trailing-object initializer without sized deallocation.
  /// \param Ptr The allocation to deallocate.
  void operator delete(void *Ptr) { ::operator delete(Ptr); }

  /// Return true if \p I is a ListInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a ListInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_ListInit;
  }
  /// Return a list initializer for \p Range with element type \p EltTy.
  /// \param Range The list elements.
  /// \param EltTy The element type.
  /// \return A list initializer for \p Range with element type \p EltTy.
  static const ListInit *get(ArrayRef<const Init *> Range, const RecTy *EltTy);

  /// Add this initializer to \p ID.
  /// \param ID The folding-set identifier.
  void Profile(FoldingSetNodeID &ID) const;

  /// Return the list elements.
  /// \return The list elements.
  ArrayRef<const Init *> getElements() const {
    return ArrayRef(getTrailingObjects(), NumElements);
  }

  LLVM_DEPRECATED("Use getElements instead", "getElements")
  /// Return the list elements.
  /// \return The list elements.
  ArrayRef<const Init *> getValues() const { return getElements(); }

  /// Return the element at index \p Idx.
  /// \param Idx The element index.
  /// \return The element at index \p Idx.
  const Init *getElement(unsigned Idx) const { return getElements()[Idx]; }

  /// Return the type of each list element.
  /// \return The type of each list element.
  const RecTy *getElementType() const {
    return cast<ListRecTy>(getType())->getElementType();
  }

  /// Return the element at \p Idx as a record.
  /// \param Idx The element index.
  /// \return The element at \p Idx as a record.
  const Record *getElementAsRecord(unsigned Idx) const;

  /// Convert this initializer to \p Ty.
  /// \param Ty The destination type.
  /// \return The converted initializer, or null if conversion is not possible.
  const Init *convertInitializerTo(const RecTy *Ty) const override;

  /// Resolve variable references using \p R.
  ///
  /// This method is used by classes that refer to other
  /// variables which may not be defined at the time they expression is formed.
  /// If a value is set for the variable later, this method will be called on
  /// users of the value to allow the value to propagate out.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return whether every element is complete.
  /// \return Whether every element is complete.
  bool isComplete() const override;
  /// Return whether every element is concrete.
  /// \return Whether every element is concrete.
  bool isConcrete() const override;
  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;

  /// Return an iterator to the first element.
  /// \return An iterator to the first element.
  const_iterator begin() const { return getElements().begin(); }
  /// Return an iterator past the last element.
  /// \return An iterator past the last element.
  const_iterator end() const { return getElements().end(); }

  /// Return the number of elements.
  /// \return The number of elements.
  size_t size() const { return NumElements; }
  /// Return whether the list is empty.
  /// \return Whether the list is empty.
  bool empty() const { return NumElements == 0; }

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override {
    llvm_unreachable("Illegal bit reference off list");
  }
};

/// Base class for initializer operators.
class OpInit : public TypedInit {
protected:
  /// Construct an operator initializer of kind \p K and type \p Type.
  /// \param K The initializer kind.
  /// \param Type The explicit value type.
  /// \param Opc The operation opcode.
  explicit OpInit(InitKind K, const RecTy *Type, uint8_t Opc)
      : TypedInit(K, Type, Opc) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  OpInit(const OpInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  OpInit &operator=(const OpInit &Other) = delete;

  /// Return true if \p I is an OpInit.
  /// \param I The initializer to test.
  /// \return True if \p I is an OpInit.
  static bool classof(const Init *I) {
    return I->getKind() >= IK_FirstOpInit &&
           I->getKind() <= IK_LastOpInit;
  }

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const final;
};

/// Represents a unary initializer operator.
class UnOpInit final : public OpInit, public FoldingSetNode {
public:
  /// Enumerates unary initializer operations.
  enum UnaryOp : uint8_t {
    /// Convert a string to lowercase.
    TOLOWER,
    /// Convert a string to uppercase.
    TOUPPER,
    /// Convert an initializer to a type.
    CAST,
    /// Negate a bit or integer value.
    NOT,
    /// Return the first element of a list.
    HEAD,
    /// Return all but the first element of a list.
    TAIL,
    /// Return the size of a collection.
    SIZE,
    /// Return whether a collection is empty.
    EMPTY,
    /// Return a DAG operator.
    GETDAGOP,
    /// Return a DAG operator name.
    GETDAGOPNAME,
    /// Return the base-2 logarithm of an integer.
    LOG2,
    /// Return a source representation of an initializer.
    REPR,
    /// Flatten a list of lists.
    LISTFLATTEN,
    /// Return whether an initializer has a value.
    INITIALIZED,
  };

private:
  const Init *LHS;

  UnOpInit(UnaryOp opc, const Init *lhs, const RecTy *Type)
      : OpInit(IK_UnOpInit, Type, opc), LHS(lhs) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  UnOpInit(const UnOpInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  UnOpInit &operator=(const UnOpInit &Other) = delete;

  /// Return true if \p I is a UnOpInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a UnOpInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_UnOpInit;
  }

  /// Return a unary operator initializer.
  /// \param opc The unary operation.
  /// \param lhs The operand.
  /// \param Type The result type.
  /// \return A unary operator initializer.
  static const UnOpInit *get(UnaryOp opc, const Init *lhs, const RecTy *Type);

  /// Add this initializer to \p ID.
  /// \param ID The folding-set identifier.
  void Profile(FoldingSetNodeID &ID) const;

  /// Return the unary operation.
  /// \return The unary operation.
  UnaryOp getOpcode() const { return (UnaryOp)Opc; }
  /// Return the operand.
  /// \return The operand.
  const Init *getOperand() const { return LHS; }

  /// Fold this initializer when possible.
  /// \param CurRec The record providing evaluation context.
  /// \param IsFinal Whether evaluation has reached the final pass.
  /// \return The folded initializer.
  const Init *Fold(const Record *CurRec, bool IsFinal = false) const;

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;
};

/// Represents a binary initializer operator.
class BinOpInit final : public OpInit, public FoldingSetNode {
public:
  /// Enumerates binary initializer operations.
  enum BinaryOp : uint8_t {
    /// Add two integers.
    ADD,
    /// Subtract two integers.
    SUB,
    /// Multiply two integers.
    MUL,
    /// Divide two integers.
    DIV,
    /// Compute bitwise conjunction.
    AND,
    /// Compute bitwise disjunction.
    OR,
    /// Compute bitwise exclusive disjunction.
    XOR,
    /// Shift bits left.
    SHL,
    /// Shift bits right with sign extension.
    SRA,
    /// Shift bits right with zero extension.
    SRL,
    /// Concatenate two lists.
    LISTCONCAT,
    /// Repeat a value into a list.
    LISTSPLAT,
    /// Remove list elements.
    LISTREMOVE,
    /// Return a list element.
    LISTELEM,
    /// Return a list slice.
    LISTSLICE,
    /// Concatenate two ranges.
    RANGEC,
    /// Concatenate two strings.
    STRCONCAT,
    /// Interleave two lists.
    INTERLEAVE,
    /// Concatenate two DAGs or values.
    CONCAT,
    /// Match a regular expression.
    MATCH,
    /// Test equality.
    EQ,
    /// Test inequality.
    NE,
    /// Test less-than-or-equal.
    LE,
    /// Test less-than.
    LT,
    /// Test greater-than-or-equal.
    GE,
    /// Test greater-than.
    GT,
    /// Return a DAG argument.
    GETDAGARG,
    /// Return a DAG argument name.
    GETDAGNAME,
    /// Set a DAG operator.
    SETDAGOP,
    /// Set a DAG operator name.
    SETDAGOPNAME
  };

private:
  const Init *LHS, *RHS;

  BinOpInit(BinaryOp opc, const Init *lhs, const Init *rhs, const RecTy *Type)
      : OpInit(IK_BinOpInit, Type, opc), LHS(lhs), RHS(rhs) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  BinOpInit(const BinOpInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  BinOpInit &operator=(const BinOpInit &Other) = delete;

  /// Return true if \p I is a BinOpInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a BinOpInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_BinOpInit;
  }

  /// Return a binary operator initializer.
  /// \param opc The binary operation.
  /// \param lhs The left operand.
  /// \param rhs The right operand.
  /// \param Type The result type.
  /// \return A binary operator initializer.
  static const BinOpInit *get(BinaryOp opc, const Init *lhs, const Init *rhs,
                              const RecTy *Type);
  /// Return a string-concatenation initializer.
  /// \param lhs The left operand.
  /// \param rhs The right operand.
  /// \return A string-concatenation initializer.
  static const Init *getStrConcat(const Init *lhs, const Init *rhs);
  /// Return a list-concatenation initializer.
  /// \param lhs The typed left operand.
  /// \param rhs The right operand.
  /// \return A list-concatenation initializer.
  static const Init *getListConcat(const TypedInit *lhs, const Init *rhs);

  /// Add this initializer to \p ID.
  /// \param ID The folding-set identifier.
  void Profile(FoldingSetNodeID &ID) const;

  /// Return the binary operation.
  /// \return The binary operation.
  BinaryOp getOpcode() const { return (BinaryOp)Opc; }
  /// Return the left operand.
  /// \return The left operand.
  const Init *getLHS() const { return LHS; }
  /// Return the right operand.
  /// \return The right operand.
  const Init *getRHS() const { return RHS; }

  /// Compare \p LHS and \p RHS using operation \p Opc.
  /// \param Opc The comparison operation.
  /// \param LHS The left initializer.
  /// \param RHS The right initializer.
  /// \return The comparison result, or std::nullopt if the operands cannot be compared.
  std::optional<bool> CompareInit(unsigned Opc, const Init *LHS,
                                  const Init *RHS) const;

  /// Fold this initializer when possible.
  /// \param CurRec The record providing evaluation context.
  /// \return The folded initializer.
  const Init *Fold(const Record *CurRec) const;

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;
};

/// Represents a ternary initializer operator.
class TernOpInit final : public OpInit, public FoldingSetNode {
public:
  /// Enumerates ternary initializer operations.
  enum TernaryOp : uint8_t {
    /// Substitute text in a string.
    SUBST,
    /// Transform every list element.
    FOREACH,
    /// Keep list elements matching a predicate.
    FILTER,
    /// Select one of two values.
    IF,
    /// Construct a DAG initializer.
    DAG,
    /// Construct an integer range.
    RANGE,
    /// Extract a substring.
    SUBSTR,
    /// Find a substring.
    FIND,
    /// Set a DAG argument.
    SETDAGARG,
    /// Set a DAG argument name.
    SETDAGNAME,
    /// Sort a list.
    SORT,
  };

private:
  const Init *LHS, *MHS, *RHS;

  TernOpInit(TernaryOp opc, const Init *lhs, const Init *mhs, const Init *rhs,
             const RecTy *Type)
      : OpInit(IK_TernOpInit, Type, opc), LHS(lhs), MHS(mhs), RHS(rhs) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  TernOpInit(const TernOpInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  TernOpInit &operator=(const TernOpInit &Other) = delete;

  /// Return true if \p I is a TernOpInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a TernOpInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_TernOpInit;
  }

  /// Return a ternary operator initializer.
  /// \param opc The ternary operation.
  /// \param lhs The first operand.
  /// \param mhs The second operand.
  /// \param rhs The third operand.
  /// \param Type The result type.
  /// \return A ternary operator initializer.
  static const TernOpInit *get(TernaryOp opc, const Init *lhs, const Init *mhs,
                               const Init *rhs, const RecTy *Type);

  /// Add this initializer to \p ID.
  /// \param ID The folding-set identifier.
  void Profile(FoldingSetNodeID &ID) const;

  /// Return the ternary operation.
  /// \return The ternary operation.
  TernaryOp getOpcode() const { return (TernaryOp)Opc; }
  /// Return the first operand.
  /// \return The first operand.
  const Init *getLHS() const { return LHS; }
  /// Return the second operand.
  /// \return The second operand.
  const Init *getMHS() const { return MHS; }
  /// Return the third operand.
  /// \return The third operand.
  const Init *getRHS() const { return RHS; }

  /// Fold this initializer when possible.
  /// \param CurRec The record providing evaluation context.
  /// \return The folded initializer.
  const Init *Fold(const Record *CurRec) const;

  /// Return whether all operands are complete.
  /// \return Whether all operands are complete.
  bool isComplete() const override {
    return LHS->isComplete() && MHS->isComplete() && RHS->isComplete();
  }

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;
};

/// Represents a conditional initializer.
///
/// Selects the first value whose condition is true, or reports an error.
class CondOpInit final : public TypedInit,
                         public FoldingSetNode,
                         private TrailingObjects<CondOpInit, const Init *> {
  friend TrailingObjects;
  unsigned NumConds;
  const RecTy *ValType;

  CondOpInit(ArrayRef<const Init *> Conds, ArrayRef<const Init *> Values,
             const RecTy *Type);

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  CondOpInit(const CondOpInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  CondOpInit &operator=(const CondOpInit &Other) = delete;

  /// Return true if \p I is a CondOpInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a CondOpInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_CondOpInit;
  }

  /// Return a conditional initializer.
  /// \param Conds The conditions.
  /// \param Values The values corresponding to \p Conds.
  /// \param Type The result type.
  /// \return A conditional initializer.
  static const CondOpInit *get(ArrayRef<const Init *> Conds,
                               ArrayRef<const Init *> Values,
                               const RecTy *Type);

  /// Add this initializer to \p ID.
  /// \param ID The folding-set identifier.
  void Profile(FoldingSetNodeID &ID) const;

  /// Return the type of the conditional values.
  /// \return The type of the conditional values.
  const RecTy *getValType() const { return ValType; }

  /// Return the number of conditions.
  /// \return The number of conditions.
  unsigned getNumConds() const { return NumConds; }

  /// Return condition \p Num.
  /// \param Num The condition index.
  /// \return Condition \p Num.
  const Init *getCond(unsigned Num) const { return getConds()[Num]; }

  /// Return the value for condition \p Num.
  /// \param Num The condition index.
  /// \return The value for condition \p Num.
  const Init *getVal(unsigned Num) const { return getVals()[Num]; }

  /// Return the conditional expressions.
  /// \return The conditional expressions.
  ArrayRef<const Init *> getConds() const {
    return getTrailingObjects(NumConds);
  }

  /// Return the conditional values.
  /// \return The conditional values.
  ArrayRef<const Init *> getVals() const {
    return ArrayRef(getTrailingObjects() + NumConds, NumConds);
  }

  /// Return paired conditions and values.
  /// \return Paired conditions and values.
  auto getCondAndVals() const { return zip_equal(getConds(), getVals()); }

  /// Fold this initializer when possible.
  /// \param CurRec The record providing evaluation context.
  /// \return The folded initializer.
  const Init *Fold(const Record *CurRec) const;

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return whether this initializer is concrete.
  /// \return Whether this initializer is concrete.
  bool isConcrete() const override;
  /// Return whether this initializer is complete.
  /// \return Whether this initializer is complete.
  bool isComplete() const override;
  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;

  /// Iterator over conditional expressions.
  using const_case_iterator = SmallVectorImpl<const Init *>::const_iterator;
  /// Iterator over conditional values.
  using const_val_iterator = SmallVectorImpl<const Init *>::const_iterator;

  /// Return an iterator to the first condition.
  /// \return An iterator to the first condition.
  inline const_case_iterator  arg_begin() const { return getConds().begin(); }
  /// Return an iterator past the last condition.
  /// \return An iterator past the last condition.
  inline const_case_iterator  arg_end  () const { return getConds().end(); }

  /// Return the number of conditions.
  /// \return The number of conditions.
  inline size_t              case_size () const { return NumConds; }
  /// Return whether there are no conditions.
  /// \return Whether there are no conditions.
  inline bool                case_empty() const { return NumConds == 0; }

  /// Return an iterator to the first conditional value.
  /// \return An iterator to the first conditional value.
  inline const_val_iterator name_begin() const { return getVals().begin();}
  /// Return an iterator past the last conditional value.
  /// \return An iterator past the last conditional value.
  inline const_val_iterator name_end  () const { return getVals().end(); }

  /// Return the number of conditional values.
  /// \return The number of conditional values.
  inline size_t              val_size () const { return NumConds; }
  /// Return whether there are no conditional values.
  /// \return Whether there are no conditional values.
  inline bool                val_empty() const { return NumConds == 0; }

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override;
};

/// !foldl (a, b, expr, start, lst) - Fold over a list.
class FoldOpInit final : public TypedInit, public FoldingSetNode {
private:
  const Init *Start, *List, *A, *B, *Expr;

  FoldOpInit(const Init *Start, const Init *List, const Init *A, const Init *B,
             const Init *Expr, const RecTy *Type)
      : TypedInit(IK_FoldOpInit, Type), Start(Start), List(List), A(A), B(B),
        Expr(Expr) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  FoldOpInit(const FoldOpInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  FoldOpInit &operator=(const FoldOpInit &Other) = delete;

  /// Return true if \p I is a FoldOpInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a FoldOpInit.
  static bool classof(const Init *I) { return I->getKind() == IK_FoldOpInit; }

  /// Return a list-fold initializer.
  /// \param Start The initial accumulator value.
  /// \param List The list to fold.
  /// \param A The accumulator variable.
  /// \param B The element variable.
  /// \param Expr The expression evaluated for each element.
  /// \param Type The result type.
  /// \return A list-fold initializer.
  static const FoldOpInit *get(const Init *Start, const Init *List,
                               const Init *A, const Init *B, const Init *Expr,
                               const RecTy *Type);

  /// Add this initializer to \p ID.
  /// \param ID The folding-set identifier.
  void Profile(FoldingSetNodeID &ID) const;

  /// Fold this initializer when possible.
  /// \param CurRec The record providing evaluation context.
  /// \return The folded initializer.
  const Init *Fold(const Record *CurRec) const;

  /// Return whether this initializer is complete.
  /// \return Whether this initializer is complete.
  bool isComplete() const override { return false; }

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override;

  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;
};

/// !isa<type>(expr) - Dynamically determine the type of an expression.
class IsAOpInit final : public TypedInit, public FoldingSetNode {
private:
  const RecTy *CheckType;
  const Init *Expr;

  IsAOpInit(const RecTy *CheckType, const Init *Expr)
      : TypedInit(IK_IsAOpInit, IntRecTy::get(CheckType->getRecordKeeper())),
        CheckType(CheckType), Expr(Expr) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  IsAOpInit(const IsAOpInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  IsAOpInit &operator=(const IsAOpInit &Other) = delete;

  /// Return true if \p I is an IsAOpInit.
  /// \param I The initializer to test.
  /// \return True if \p I is an IsAOpInit.
  static bool classof(const Init *I) { return I->getKind() == IK_IsAOpInit; }

  /// Return a dynamic type-test initializer.
  /// \param CheckType The type to test.
  /// \param Expr The expression to test.
  /// \return A dynamic type-test initializer.
  static const IsAOpInit *get(const RecTy *CheckType, const Init *Expr);

  /// Add this initializer to \p ID.
  /// \param ID The folding-set identifier.
  void Profile(FoldingSetNodeID &ID) const;

  /// Fold this initializer when possible.
  /// \return The folded initializer.
  const Init *Fold() const;

  /// Return whether this initializer is complete.
  /// \return Whether this initializer is complete.
  bool isComplete() const override { return false; }

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override;

  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;
};

/// !exists<type>(expr) - Dynamically determine if a record of `type` named
/// `expr` exists.
class ExistsOpInit final : public TypedInit, public FoldingSetNode {
private:
  const RecTy *CheckType;
  const Init *Expr;

  ExistsOpInit(const RecTy *CheckType, const Init *Expr)
      : TypedInit(IK_ExistsOpInit, IntRecTy::get(CheckType->getRecordKeeper())),
        CheckType(CheckType), Expr(Expr) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  ExistsOpInit(const ExistsOpInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  ExistsOpInit &operator=(const ExistsOpInit &Other) = delete;

  /// Return true if \p I is an ExistsOpInit.
  /// \param I The initializer to test.
  /// \return True if \p I is an ExistsOpInit.
  static bool classof(const Init *I) { return I->getKind() == IK_ExistsOpInit; }

  /// Return a record-existence test initializer.
  /// \param CheckType The type of record to find.
  /// \param Expr The record name expression.
  /// \return A record-existence test initializer.
  static const ExistsOpInit *get(const RecTy *CheckType, const Init *Expr);

  /// Add this initializer to \p ID.
  /// \param ID The folding-set identifier.
  void Profile(FoldingSetNodeID &ID) const;

  /// Fold this initializer when possible.
  /// \param CurRec The record providing evaluation context.
  /// \param IsFinal Whether evaluation has reached the final pass.
  /// \return The folded initializer.
  const Init *Fold(const Record *CurRec, bool IsFinal = false) const;

  /// Return whether this initializer is complete.
  /// \return Whether this initializer is complete.
  bool isComplete() const override { return false; }

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override;

  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;
};

/// Represents an initializer that finds matching records.
///
/// Produces records of a type, optionally filtering names with a regex.
class InstancesOpInit final : public TypedInit, public FoldingSetNode {
private:
  const RecTy *Type;
  const Init *Regex;

  InstancesOpInit(const RecTy *Type, const Init *Regex)
      : TypedInit(IK_InstancesOpInit, ListRecTy::get(Type)), Type(Type),
        Regex(Regex) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  InstancesOpInit(const InstancesOpInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  InstancesOpInit &operator=(const InstancesOpInit &Other) = delete;

  /// Return true if \p I is an InstancesOpInit.
  /// \param I The initializer to test.
  /// \return True if \p I is an InstancesOpInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_InstancesOpInit;
  }

  /// Return an initializer that finds instances of \p Type.
  /// \param Type The record type to find.
  /// \param Regex The optional name-filter expression.
  /// \return An initializer that finds instances of \p Type.
  static const InstancesOpInit *get(const RecTy *Type, const Init *Regex);

  /// Add this initializer to \p ID.
  /// \param ID The folding-set identifier.
  void Profile(FoldingSetNodeID &ID) const;

  /// Fold this initializer when possible.
  /// \param CurRec The record providing evaluation context.
  /// \param IsFinal Whether evaluation has reached the final pass.
  /// \return The folded initializer.
  const Init *Fold(const Record *CurRec, bool IsFinal = false) const;

  /// Return whether this initializer is complete.
  /// \return Whether this initializer is complete.
  bool isComplete() const override { return false; }

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override {
    llvm_unreachable("Illegal bit reference off !instances");
  }

  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;
};

/// 'Opcode' - Represent a reference to an entire variable object.
class VarInit final : public TypedInit {
  const Init *VarName;

  explicit VarInit(const Init *VN, const RecTy *T)
      : TypedInit(IK_VarInit, T), VarName(VN) {}

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  VarInit(const VarInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  VarInit &operator=(const VarInit &Other) = delete;

  /// Return true if \p I is a VarInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a VarInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_VarInit;
  }

  /// Return a variable initializer named \p VN with type \p T.
  /// \param VN The variable name.
  /// \param T The variable type.
  /// \return A variable initializer named \p VN with type \p T.
  static const VarInit *get(StringRef VN, const RecTy *T);
  /// Return a variable initializer named by \p VN with type \p T.
  /// \param VN The variable name initializer.
  /// \param T The variable type.
  /// \return A variable initializer named by \p VN with type \p T.
  static const VarInit *get(const Init *VN, const RecTy *T);

  /// Return the variable name.
  /// \return The variable name.
  StringRef getName() const;
  /// Return the initializer containing the variable name.
  /// \return The initializer containing the variable name.
  const Init *getNameInit() const { return VarName; }

  /// Return the variable name as unquoted source text.
  /// \return The variable name as unquoted source text.
  std::string getNameInitAsString() const {
    return getNameInit()->getAsUnquotedString();
  }

  /// Resolve variable references using \p R.
  ///
  /// This method is used by classes that refer to other
  /// variables which may not be defined at the time they expression is formed.
  /// If a value is set for the variable later, this method will be called on
  /// users of the value to allow the value to propagate out.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override;

  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override { return std::string(getName()); }
};

/// Opcode{0} - Represent access to one bit of a variable or field.
class VarBitInit final : public TypedInit {
  const TypedInit *TI;
  unsigned Bit;

  VarBitInit(const TypedInit *T, unsigned B)
      : TypedInit(IK_VarBitInit, BitRecTy::get(T->getRecordKeeper())), TI(T),
        Bit(B) {
    assert(T->getType() &&
           (isa<IntRecTy>(T->getType()) ||
            (isa<BitsRecTy>(T->getType()) &&
             cast<BitsRecTy>(T->getType())->getNumBits() > B)) &&
           "Illegal VarBitInit expression!");
  }

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  VarBitInit(const VarBitInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  VarBitInit &operator=(const VarBitInit &Other) = delete;

  /// Return true if \p I is a VarBitInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a VarBitInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_VarBitInit;
  }

  /// Return a variable-bit initializer.
  /// \param T The referenced variable initializer.
  /// \param B The selected bit index.
  /// \return A variable-bit initializer.
  static const VarBitInit *get(const TypedInit *T, unsigned B);

  /// Return the referenced variable initializer.
  /// \return The referenced variable initializer.
  const Init *getBitVar() const { return TI; }
  /// Return the selected bit index.
  /// \return The selected bit index.
  unsigned getBitNum() const { return Bit; }

  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;
  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return the bit at index \p B.
  /// \param B The bit index.
  /// \return The bit at index \p B.
  const Init *getBit(unsigned B) const override {
    assert(B < 1 && "Bit index out of range!");
    return this;
  }
};

/// AL - Represent a reference to a 'def' in the description
class DefInit final : public TypedInit {
  friend class Record;

  const Record *Def;

  explicit DefInit(const Record *D);

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  DefInit(const DefInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  DefInit &operator=(const DefInit &Other) = delete;

  /// Return true if \p I is a DefInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a DefInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_DefInit;
  }

  /// Convert this initializer to \p Ty.
  /// \param Ty The destination type.
  /// \return The converted initializer, or null if conversion is not possible.
  const Init *convertInitializerTo(const RecTy *Ty) const override;

  /// Return the referenced record definition.
  /// \return The referenced record definition.
  const Record *getDef() const { return Def; }

  /// Return the type of field \p FieldName.
  /// \param FieldName The field name.
  /// \return The type of field \p FieldName.
  const RecTy *getFieldType(const StringInit *FieldName) const override;

  /// Return whether this initializer is concrete.
  /// \return Whether this initializer is concrete.
  bool isConcrete() const override { return true; }
  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override {
    llvm_unreachable("Illegal bit reference off def");
  }
};

/// classname<targs...> - Represent an uninstantiated anonymous class
/// instantiation.
class VarDefInit final
    : public TypedInit,
      public FoldingSetNode,
      private TrailingObjects<VarDefInit, const ArgumentInit *> {
  friend TrailingObjects;
  SMLoc Loc;
  const Record *Class;
  const DefInit *Def = nullptr; // after instantiation
  unsigned NumArgs;

  explicit VarDefInit(SMLoc Loc, const Record *Class,
                      ArrayRef<const ArgumentInit *> Args);

  const DefInit *instantiate();

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  VarDefInit(const VarDefInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  VarDefInit &operator=(const VarDefInit &Other) = delete;

  /// Deallocate this trailing-object initializer without sized deallocation.
  /// \param Ptr The allocation to deallocate.
  void operator delete(void *Ptr) { ::operator delete(Ptr); }

  /// Return true if \p I is a VarDefInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a VarDefInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_VarDefInit;
  }
  /// Return an uninstantiated anonymous-class initializer.
  /// \param Loc The source location.
  /// \param Class The anonymous class.
  /// \param Args The class arguments.
  /// \return An uninstantiated anonymous-class initializer.
  static const VarDefInit *get(SMLoc Loc, const Record *Class,
                               ArrayRef<const ArgumentInit *> Args);

  /// Add this initializer to \p ID.
  /// \param ID The folding-set identifier.
  void Profile(FoldingSetNodeID &ID) const;

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;
  /// Instantiate this anonymous-class initializer.
  /// \return Instantiate this anonymous-class initializer.
  const Init *Fold() const;

  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;

  /// Return argument \p i.
  /// \param i The argument index.
  /// \return Argument \p i.
  const ArgumentInit *getArg(unsigned i) const { return args()[i]; }

  /// Iterator over anonymous-class arguments.
  using const_iterator = const ArgumentInit *const *;

  /// Return an iterator to the first argument.
  /// \return An iterator to the first argument.
  const_iterator args_begin() const { return args().begin(); }
  /// Return an iterator past the last argument.
  /// \return An iterator past the last argument.
  const_iterator args_end() const { return args().end(); }

  /// Return the number of arguments.
  /// \return The number of arguments.
  size_t         args_size () const { return NumArgs; }
  /// Return whether there are no arguments.
  /// \return Whether there are no arguments.
  bool           args_empty() const { return NumArgs == 0; }

  /// Return the anonymous-class arguments.
  /// \return The anonymous-class arguments.
  ArrayRef<const ArgumentInit *> args() const {
    return getTrailingObjects(NumArgs);
  }

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override {
    llvm_unreachable("Illegal bit reference off anonymous def");
  }
};

/// X.Y - Represent a reference to a subfield of a variable
class FieldInit final : public TypedInit {
  const Init *Rec;             // Record we are referring to
  const StringInit *FieldName; // Field we are accessing

  FieldInit(const Init *R, const StringInit *FN)
      : TypedInit(IK_FieldInit, R->getFieldType(FN)), Rec(R), FieldName(FN) {
#ifndef NDEBUG
    if (!getType()) {
      llvm::errs() << "In Record = " << Rec->getAsString()
                   << ", got FieldName = " << *FieldName
                   << " with non-record type!\n";
      llvm_unreachable("FieldInit with non-record type!");
    }
#endif
  }

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  FieldInit(const FieldInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  FieldInit &operator=(const FieldInit &Other) = delete;

  /// Return true if \p I is a FieldInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a FieldInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_FieldInit;
  }

  /// Return a field-reference initializer.
  /// \param R The record expression.
  /// \param FN The field name.
  /// \return A field-reference initializer.
  static const FieldInit *get(const Init *R, const StringInit *FN);

  /// Return the record expression.
  /// \return The record expression.
  const Init *getRecord() const { return Rec; }
  /// Return the field name.
  /// \return The field name.
  const StringInit *getFieldName() const { return FieldName; }

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override;

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;
  /// Fold this initializer when possible.
  /// \param CurRec The record providing evaluation context.
  /// \return The folded initializer.
  const Init *Fold(const Record *CurRec) const;

  /// Return whether this initializer is concrete.
  /// \return Whether this initializer is concrete.
  bool isConcrete() const override;
  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override {
    return Rec->getAsString() + "." + FieldName->getValue().str();
  }
};

/// Represents a DAG initializer.
///
/// A DAG has an operator and a possibly empty list of named arguments.
class DagInit final
    : public TypedInit,
      public FoldingSetNode,
      private TrailingObjects<DagInit, const Init *, const StringInit *> {
  friend TrailingObjects;

  const Init *Val;
  const StringInit *ValName;
  unsigned NumArgs;

  DagInit(const Init *V, const StringInit *VN, ArrayRef<const Init *> Args,
          ArrayRef<const StringInit *> ArgNames);

  size_t numTrailingObjects(OverloadToken<const Init *>) const {
    return NumArgs;
  }

public:
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  DagInit(const DagInit &Other) = delete;
  /// Deleted copy-assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  DagInit &operator=(const DagInit &Other) = delete;

  /// Return true if \p I is a DagInit.
  /// \param I The initializer to test.
  /// \return True if \p I is a DagInit.
  static bool classof(const Init *I) {
    return I->getKind() == IK_DagInit;
  }

  /// Return a DAG initializer.
  /// \param V The DAG operator.
  /// \param VN The optional operator name.
  /// \param Args The argument values.
  /// \param ArgNames The optional argument names.
  /// \return A DAG initializer.
  static const DagInit *get(const Init *V, const StringInit *VN,
                            ArrayRef<const Init *> Args,
                            ArrayRef<const StringInit *> ArgNames);

  /// Return an unnamed-operator DAG initializer.
  /// \param V The DAG operator.
  /// \param Args The argument values.
  /// \param ArgNames The optional argument names.
  /// \return An unnamed-operator DAG initializer.
  static const DagInit *get(const Init *V, ArrayRef<const Init *> Args,
                            ArrayRef<const StringInit *> ArgNames) {
    return DagInit::get(V, nullptr, Args, ArgNames);
  }

  /// Return a DAG initializer from paired arguments and names.
  /// \param V The DAG operator.
  /// \param VN The optional operator name.
  /// \param ArgAndNames The argument values and optional names.
  /// \return A DAG initializer from paired arguments and names.
  static const DagInit *
  get(const Init *V, const StringInit *VN,
      ArrayRef<std::pair<const Init *, const StringInit *>> ArgAndNames);

  /// Return an unnamed-operator DAG initializer from paired arguments.
  /// \param V The DAG operator.
  /// \param ArgAndNames The argument values and optional names.
  /// \return An unnamed-operator DAG initializer from paired arguments.
  static const DagInit *
  get(const Init *V,
      ArrayRef<std::pair<const Init *, const StringInit *>> ArgAndNames) {
    return DagInit::get(V, nullptr, ArgAndNames);
  }

  /// Add this initializer to \p ID.
  /// \param ID The folding-set identifier.
  void Profile(FoldingSetNodeID &ID) const;

  /// Return the DAG operator.
  /// \return The DAG operator.
  const Init *getOperator() const { return Val; }
  /// Return the DAG operator as a record definition.
  /// \param Loc Source locations for diagnostics.
  /// \return The DAG operator as a record definition.
  const Record *getOperatorAsDef(ArrayRef<SMLoc> Loc) const;

  /// Return the optional DAG operator name.
  /// \return The optional DAG operator name.
  const StringInit *getName() const { return ValName; }

  /// Return the optional DAG operator name as a string.
  /// \return The optional DAG operator name as a string.
  StringRef getNameStr() const {
    return ValName ? ValName->getValue() : StringRef();
  }

  /// Return the number of DAG arguments.
  /// \return The number of DAG arguments.
  unsigned getNumArgs() const { return NumArgs; }

  /// Return argument \p Num.
  /// \param Num The argument index.
  /// \return Argument \p Num.
  const Init *getArg(unsigned Num) const { return getArgs()[Num]; }

  /// Return the index of argument \p Name, if it exists.
  /// \param Name The argument name.
  /// \return The index of argument \p Name, if it exists.
  std::optional<unsigned> getArgNo(StringRef Name) const;

  /// Return the name of argument \p Num.
  /// \param Num The argument index.
  /// \return The name of argument \p Num.
  const StringInit *getArgName(unsigned Num) const {
    return getArgNames()[Num];
  }

  /// Return the name of argument \p Num as a string.
  /// \param Num The argument index.
  /// \return The name of argument \p Num as a string.
  StringRef getArgNameStr(unsigned Num) const {
    const StringInit *Init = getArgName(Num);
    return Init ? Init->getValue() : StringRef();
  }

  /// Return the DAG arguments.
  /// \return The DAG arguments.
  ArrayRef<const Init *> getArgs() const {
    return getTrailingObjects<const Init *>(NumArgs);
  }

  /// Return the optional DAG argument names.
  /// \return The optional DAG argument names.
  ArrayRef<const StringInit *> getArgNames() const {
    return getTrailingObjects<const StringInit *>(NumArgs);
  }

  /// Return paired DAG arguments and names.
  /// \return Paired DAG arguments and names.
  auto getArgAndNames() const {
    auto Zip = llvm::zip_equal(getArgs(), getArgNames());
    using EltTy = decltype(*adl_begin(Zip));
    return llvm::map_range(Zip, [](const EltTy &E) {
      return std::make_pair(std::get<0>(E), std::get<1>(E));
    });
  }

  /// Resolve variable references using \p R.
  /// \param R The reference resolver.
  /// \return The resolved initializer.
  const Init *resolveReferences(Resolver &R) const override;

  /// Return whether this initializer is concrete.
  /// \return Whether this initializer is concrete.
  bool isConcrete() const override;
  /// Return this initializer as TableGen source text.
  /// \return This initializer as TableGen source text.
  std::string getAsString() const override;

  /// Iterator over DAG arguments.
  using const_arg_iterator = SmallVectorImpl<const Init *>::const_iterator;
  /// Iterator over DAG argument names.
  using const_name_iterator =
      SmallVectorImpl<const StringInit *>::const_iterator;

  /// Return an iterator to the first argument.
  /// \return An iterator to the first argument.
  inline const_arg_iterator  arg_begin() const { return getArgs().begin(); }
  /// Return an iterator past the last argument.
  /// \return An iterator past the last argument.
  inline const_arg_iterator  arg_end  () const { return getArgs().end(); }

  /// Return the number of arguments.
  /// \return The number of arguments.
  inline size_t              arg_size () const { return NumArgs; }
  /// Return whether there are no arguments.
  /// \return Whether there are no arguments.
  inline bool                arg_empty() const { return NumArgs == 0; }

  /// Return an iterator to the first argument name.
  /// \return An iterator to the first argument name.
  inline const_name_iterator name_begin() const { return getArgNames().begin();}
  /// Return an iterator past the last argument name.
  /// \return An iterator past the last argument name.
  inline const_name_iterator name_end  () const { return getArgNames().end(); }

  /// Return the bit at index \p Bit.
  /// \param Bit The bit index.
  /// \return The bit at index \p Bit.
  const Init *getBit(unsigned Bit) const override {
    llvm_unreachable("Illegal bit reference off dag");
  }
};

//===----------------------------------------------------------------------===//
//  High-Level Classes
//===----------------------------------------------------------------------===//

/// This class represents a field in a record, including its name, type,
/// value, and source location.
class RecordVal {
  friend class Record;

public:
  /// Kinds of fields that a record can contain.
  enum FieldKind {
    /// A normal record field.
    FK_Normal,
    /// A field that can be nonconcrete, declared with `field`.
    FK_NonconcreteOK,
    /// A template argument field.
    FK_TemplateArg,
  };

private:
  const Init *Name;
  SMLoc Loc; // Source location of definition of name.
  PointerIntPair<const RecTy *, 2, FieldKind> TyAndKind;
  const Init *Value;
  bool IsUsed = false;

  /// Reference locations to this record value.
  SmallVector<SMRange, 0> ReferenceLocs;

public:
  /// Construct a record field.
  ///
  /// \param N Field name initializer.
  /// \param T Field type.
  /// \param K Field kind.
  RecordVal(const Init *N, const RecTy *T, FieldKind K);
  /// Construct a record field with its definition location.
  ///
  /// \param N Field name initializer.
  /// \param Loc Field definition location.
  /// \param T Field type.
  /// \param K Field kind.
  RecordVal(const Init *N, SMLoc Loc, const RecTy *T, FieldKind K);

  /// Get the record keeper used to unique this value.
  /// \return The record keeper used to unique this value.
  RecordKeeper &getRecordKeeper() const { return Name->getRecordKeeper(); }

  /// Get the name of the field as a StringRef.
  /// \return The name of the field as a StringRef.
  StringRef getName() const;

  /// Get the name of the field as an Init.
  /// \return The name of the field as an Init.
  const Init *getNameInit() const { return Name; }

  /// Get the name of the field as a std::string.
  /// \return The name of the field as a std::string.
  std::string getNameInitAsString() const {
    return getNameInit()->getAsUnquotedString();
  }

  /// Get the source location of the point where the field was defined.
  /// \return The source location of the point where the field was defined.
  SMLoc getLoc() const { return Loc; }

  /// Is this a field where nonconcrete values are okay?
  /// \return True if this is a field where nonconcrete values are okay.
  bool isNonconcreteOK() const {
    return TyAndKind.getInt() == FK_NonconcreteOK;
  }

  /// Is this a template argument?
  /// \return True if this is a template argument.
  bool isTemplateArg() const {
    return TyAndKind.getInt() == FK_TemplateArg;
  }

  /// Get the type of the field value as a RecTy.
  /// \return The type of the field value as a RecTy.
  const RecTy *getType() const { return TyAndKind.getPointer(); }

  /// Get the type of the field for printing purposes.
  /// \return The type of the field for printing purposes.
  std::string getPrintType() const;

  /// Get the value of the field as an Init.
  /// \return The value of the field as an Init.
  const Init *getValue() const { return Value; }

  /// Set the field value.
  ///
  /// \param V New field value.
  /// \return True if the value could not be cast to the field type; false on success.
  bool setValue(const Init *V);

  /// Set the field value and its source location.
  ///
  /// \param V New field value.
  /// \param NewLoc Source location of the new value.
  /// \return True if the value could not be cast to the field type; false on success.
  bool setValue(const Init *V, SMLoc NewLoc);

  /// Add a reference to this record value.
  ///
  /// \param Loc Reference location.
  void addReferenceLoc(SMRange Loc) { ReferenceLocs.push_back(Loc); }

  /// Return the references of this record value.
  /// \return The references of this record value.
  ArrayRef<SMRange> getReferenceLocs() const { return ReferenceLocs; }

  /// Mark whether this value is used.
  ///
  /// \param Used Whether the value is used.
  void setUsed(bool Used) { IsUsed = Used; }
  /// Return whether this value is used.
  /// \return Whether this value is used.
  bool isUsed() const { return IsUsed; }

  /// Print this field to the standard error stream.
  void dump() const;

  /// Print the value to an output stream, possibly with a semicolon.
  ///
  /// \param OS Output stream.
  /// \param PrintSem Whether to append a semicolon.
  void print(raw_ostream &OS, bool PrintSem = true) const;
};

/// Write a record field to an output stream.
///
/// \param OS Output stream.
/// \param RV Record field to write.
/// \return The output stream.
inline raw_ostream &operator<<(raw_ostream &OS, const RecordVal &RV) {
  RV.print(OS << "  ");
  return OS;
}

/// Represents a TableGen class or definition.
class Record {
public:
  /// Stores an assertion declared on a record.
  struct AssertionInfo {
    /// Source location of the assertion.
    SMLoc Loc;
    /// Initializer evaluated as the assertion condition.
    const Init *Condition;
    /// Initializer evaluated as the assertion message.
    const Init *Message;

    /// Construct record assertion information.
    ///
    /// \param Loc Source location of the assertion.
    /// \param Condition Initializer evaluated as the condition.
    /// \param Message Initializer evaluated as the message.
    AssertionInfo(SMLoc Loc, const Init *Condition, const Init *Message)
        : Loc(Loc), Condition(Condition), Message(Message) {}
  };

  /// Stores a dump directive declared on a record.
  struct DumpInfo {
    /// Source location of the dump directive.
    SMLoc Loc;
    /// Initializer evaluated as the dump message.
    const Init *Message;

    /// Construct record dump information.
    ///
    /// \param Loc Source location of the dump directive.
    /// \param Message Initializer evaluated as the dump message.
    DumpInfo(SMLoc Loc, const Init *Message) : Loc(Loc), Message(Message) {}
  };

  /// Kinds of TableGen records.
  enum RecordKind {
    /// A named definition.
    RK_Def,
    /// An anonymous definition.
    RK_AnonymousDef,
    /// A class declaration.
    RK_Class,
    /// A multiclass declaration.
    RK_MultiClass,
  };

private:
  const Init *Name;
  // Location where record was instantiated, followed by the location of
  // multiclass prototypes used, and finally by the locations of references to
  // this record.
  SmallVector<SMLoc, 4> Locs;
  SmallVector<SMLoc, 0> ForwardDeclarationLocs;
  mutable SmallVector<SMRange, 0> ReferenceLocs;
  SmallVector<const Init *, 0> TemplateArgs;
  SmallVector<RecordVal, 0> Values;
  SmallVector<AssertionInfo, 0> Assertions;
  SmallVector<DumpInfo, 0> Dumps;

  // Direct superclasses, which are roots of the inheritance forest (yes, it
  // must be a forest; diamond-shaped inheritance is not allowed).
  SmallVector<std::pair<const Record *, SMRange>, 0> DirectSuperClasses;

  // Tracks Record instances. Not owned by Record.
  RecordKeeper &TrackedRecords;

  // The DefInit corresponding to this record.
  mutable DefInit *CorrespondingDefInit = nullptr;

  // Unique record ID.
  unsigned ID;

  RecordKind Kind;

  void checkName();

public:
  /// Construct a record from an initializer name.
  ///
  /// \param N Record name initializer.
  /// \param locs Record source locations.
  /// \param records Record keeper that tracks the record.
  /// \param Kind Record kind.
  explicit Record(const Init *N, ArrayRef<SMLoc> locs, RecordKeeper &records,
                  RecordKind Kind = RK_Def)
      : Name(N), Locs(locs), TrackedRecords(records),
        ID(getNewUID(N->getRecordKeeper())), Kind(Kind) {
    checkName();
  }

  /// Construct a record from a string name.
  ///
  /// \param N Record name.
  /// \param locs Record source locations.
  /// \param records Record keeper that tracks the record.
  /// \param Kind Record kind.
  explicit Record(StringRef N, ArrayRef<SMLoc> locs, RecordKeeper &records,
                  RecordKind Kind = RK_Def)
      : Record(StringInit::get(records, N), locs, records, Kind) {}

  /// Copy a record while assigning it a new unique identifier.
  ///
  /// \param O Record to copy.
  Record(const Record &O)
      : Name(O.Name), Locs(O.Locs), TemplateArgs(O.TemplateArgs),
        Values(O.Values), Assertions(O.Assertions),
        DirectSuperClasses(O.DirectSuperClasses),
        TrackedRecords(O.TrackedRecords), ID(getNewUID(O.getRecords())),
        Kind(O.Kind) {}

  /// Allocate a unique record identifier.
  ///
  /// \param RK Record keeper that owns the identifier sequence.
  /// \return Allocate a unique record identifier.
  static unsigned getNewUID(RecordKeeper &RK);

  /// Return this record's unique identifier.
  /// \return This record's unique identifier.
  unsigned getID() const { return ID; }

  /// Return this record's name.
  /// \return This record's name.
  StringRef getName() const { return cast<StringInit>(Name)->getValue(); }

  /// Return this record's name initializer.
  /// \return This record's name initializer.
  const Init *getNameInit() const { return Name; }

  /// Return this record's unquoted name.
  /// \return This record's unquoted name.
  std::string getNameInitAsString() const {
    return getNameInit()->getAsUnquotedString();
  }

  /// Set this record's name and update its record keeper.
  ///
  /// \param Name New name initializer.
  void setName(const Init *Name);

  /// Return this record's source locations.
  /// \return This record's source locations.
  ArrayRef<SMLoc> getLoc() const { return Locs; }
  /// Append a source location for this record.
  ///
  /// \param Loc Source location to append.
  void appendLoc(SMLoc Loc) { Locs.push_back(Loc); }

  /// Return this record's forward declaration locations.
  /// \return This record's forward declaration locations.
  ArrayRef<SMLoc> getForwardDeclarationLocs() const {
    return ForwardDeclarationLocs;
  }

  /// Add a reference to this record value.
  ///
  /// \param Loc Reference location.
  void appendReferenceLoc(SMRange Loc) const { ReferenceLocs.push_back(Loc); }

  /// Return the references of this record value.
  /// \return The references of this record value.
  ArrayRef<SMRange> getReferenceLocs() const { return ReferenceLocs; }

  /// Update a class location after encountering a definition.
  ///
  /// \param Loc Definition location.
  void updateClassLoc(SMLoc Loc);

  /// Return the type implied by this record's superclasses.
  /// \return The type implied by this record's superclasses.
  const RecordRecTy *getType() const;

  /// get the corresponding DefInit.
  /// \return The corresponding DefInit.
  DefInit *getDefInit() const;

  /// Return whether this record is a class.
  /// \return Whether this record is a class.
  bool isClass() const { return Kind == RK_Class; }

  /// Return whether this record is a multiclass.
  /// \return Whether this record is a multiclass.
  bool isMultiClass() const { return Kind == RK_MultiClass; }

  /// Return whether this record is an anonymous definition.
  /// \return Whether this record is an anonymous definition.
  bool isAnonymous() const { return Kind == RK_AnonymousDef; }

  /// Return this record's template arguments.
  /// \return This record's template arguments.
  ArrayRef<const Init *> getTemplateArgs() const { return TemplateArgs; }

  /// Return this record's fields.
  /// \return This record's fields.
  ArrayRef<RecordVal> getValues() const { return Values; }

  /// Return assertions declared on this record.
  /// \return Assertions declared on this record.
  ArrayRef<AssertionInfo> getAssertions() const { return Assertions; }
  /// Return dump directives declared on this record.
  /// \return Dump directives declared on this record.
  ArrayRef<DumpInfo> getDumps() const { return Dumps; }

  /// Append all superclasses in post-order to \p Classes.
  ///
  /// \param Classes Destination sequence for superclass records.
  void getSuperClasses(std::vector<const Record *> &Classes) const {
    for (const Record *SC : make_first_range(DirectSuperClasses)) {
      SC->getSuperClasses(Classes);
      Classes.push_back(SC);
    }
  }

  /// Return all superclasses in post-order.
  /// \return All superclasses in post-order.
  std::vector<const Record *> getSuperClasses() const {
    std::vector<const Record *> Classes;
    getSuperClasses(Classes);
    return Classes;
  }

  /// Determine whether this record has the specified direct superclass.
  ///
  /// \param SuperClass Superclass to search for.
  /// \return Determine whether this record has the specified direct superclass.
  bool hasDirectSuperClass(const Record *SuperClass) const {
    return is_contained(make_first_range(DirectSuperClasses), SuperClass);
  }

  /// Return the direct superclasses of this record.
  /// \return The direct superclasses of this record.
  ArrayRef<std::pair<const Record *, SMRange>> getDirectSuperClasses() const {
    return DirectSuperClasses;
  }

  /// Return whether \p Name is a template argument.
  ///
  /// \param Name Name initializer to test.
  /// \return Whether \p Name is a template argument.
  bool isTemplateArg(const Init *Name) const {
    return llvm::is_contained(TemplateArgs, Name);
  }

  /// Return the field with initializer name \p Name.
  ///
  /// \param Name Field name initializer.
  /// \return The field with initializer name \p Name.
  const RecordVal *getValue(const Init *Name) const {
    for (const RecordVal &Val : Values)
      if (Val.Name == Name) return &Val;
    return nullptr;
  }

  /// Return the field with string name \p Name.
  ///
  /// \param Name Field name.
  /// \return The field with string name \p Name.
  const RecordVal *getValue(StringRef Name) const {
    return getValue(StringInit::get(getRecords(), Name));
  }

  /// Return the mutable field with initializer name \p Name.
  ///
  /// \param Name Field name initializer.
  /// \return The mutable field with initializer name \p Name.
  RecordVal *getValue(const Init *Name) {
    return const_cast<RecordVal *>(
        static_cast<const Record *>(this)->getValue(Name));
  }

  /// Return the mutable field with string name \p Name.
  ///
  /// \param Name Field name.
  /// \return The mutable field with string name \p Name.
  RecordVal *getValue(StringRef Name) {
    return const_cast<RecordVal *>(
        static_cast<const Record *>(this)->getValue(Name));
  }

  /// Add a template argument.
  ///
  /// \param Name Template argument name initializer.
  void addTemplateArg(const Init *Name) {
    assert(!isTemplateArg(Name) && "Template arg already defined!");
    TemplateArgs.push_back(Name);
  }

  /// Add a field to this record.
  ///
  /// \param RV Field to add.
  void addValue(const RecordVal &RV) {
    assert(getValue(RV.getNameInit()) == nullptr && "Value already added!");
    Values.push_back(RV);
  }

  /// Remove the field with initializer name \p Name.
  ///
  /// \param Name Field name initializer.
  void removeValue(const Init *Name) {
    auto It = llvm::find_if(
        Values, [Name](const RecordVal &V) { return V.getNameInit() == Name; });
    if (It == Values.end())
      llvm_unreachable("Cannot remove an entry that does not exist!");
    Values.erase(It);
  }

  /// Remove the field with string name \p Name.
  ///
  /// \param Name Field name.
  void removeValue(StringRef Name) {
    removeValue(StringInit::get(getRecords(), Name));
  }

  /// Add an assertion to this record.
  ///
  /// \param Loc Assertion source location.
  /// \param Condition Initializer evaluated as the condition.
  /// \param Message Initializer evaluated as the message.
  void addAssertion(SMLoc Loc, const Init *Condition, const Init *Message) {
    Assertions.push_back(AssertionInfo(Loc, Condition, Message));
  }

  /// Add a dump directive to this record.
  ///
  /// \param Loc Dump directive source location.
  /// \param Message Initializer evaluated as the message.
  void addDump(SMLoc Loc, const Init *Message) {
    Dumps.push_back(DumpInfo(Loc, Message));
  }

  /// Append assertions from another record.
  ///
  /// \param Rec Record that supplies the assertions.
  void appendAssertions(const Record *Rec) {
    Assertions.append(Rec->Assertions);
  }

  /// Append dump directives from another record.
  ///
  /// \param Rec Record that supplies the dump directives.
  void appendDumps(const Record *Rec) { Dumps.append(Rec->Dumps); }

  /// Check assertions declared on this record.
  void checkRecordAssertions();
  /// Emit dump directives declared on this record.
  void emitRecordDumps();
  /// Check for unused template arguments.
  void checkUnusedTemplateArgs();

  /// Return whether this record derives from \p R.
  ///
  /// \param R Potential base record.
  /// \return Whether this record derives from \p R.
  bool isSubClassOf(const Record *R) const {
    for (const Record *SC : make_first_range(DirectSuperClasses)) {
      if (SC == R || SC->isSubClassOf(R))
        return true;
    }
    return false;
  }

  /// Return whether this record derives from the named record.
  ///
  /// \param Name Potential base record name.
  /// \return Whether this record derives from the named record.
  bool isSubClassOf(StringRef Name) const {
    for (const Record *SC : make_first_range(DirectSuperClasses)) {
      if (const auto *SI = dyn_cast<StringInit>(SC->getNameInit())) {
        if (SI->getValue() == Name)
          return true;
      } else if (SC->getNameInitAsString() == Name) {
        return true;
      }
      if (SC->isSubClassOf(Name))
        return true;
    }
    return false;
  }

  /// Add a direct superclass.
  ///
  /// \param R Superclass record.
  /// \param Range Source range of the superclass reference.
  void addDirectSuperClass(const Record *R, SMRange Range) {
    assert(!CorrespondingDefInit &&
           "changing type of record after it has been referenced");
    assert(!isSubClassOf(R) && "Already subclassing record!");
    DirectSuperClasses.emplace_back(R, Range);
  }

  /// If there are any field references that refer to fields that have been
  /// filled in, we can propagate the values now.
  ///
  /// This is a final resolve: any error messages, e.g. due to undefined !cast
  /// references, are generated now.
  ///
  /// \param NewName Optional replacement name initializer.
  void resolveReferences(const Init *NewName = nullptr);

  /// Apply the resolver to the name of the record as well as to the
  /// initializers of all fields of the record except SkipVal.
  ///
  /// The resolver should not resolve any of the fields itself, to avoid
  /// recursion / infinite loops.
  ///
  /// \param R Resolver to apply.
  /// \param SkipVal Optional field to exclude from resolution.
  void resolveReferences(Resolver &R, const RecordVal *SkipVal = nullptr);

  /// Return the record keeper tracking this record.
  /// \return The record keeper tracking this record.
  RecordKeeper &getRecords() const {
    return TrackedRecords;
  }

  /// Print this record to the standard error stream.
  void dump() const;

  //===--------------------------------------------------------------------===//
  // High-level methods useful to tablegen back-ends
  //

  /// Return the source location for the named field.
  ///
  /// \param FieldName Field name.
  /// \return The source location for the named field.
  SMLoc getFieldLoc(StringRef FieldName) const;

  /// Return the initializer for field \p FieldName.
  ///
  /// Throws an exception if the field does not exist.
  ///
  /// \param FieldName Field name.
  /// \return The initializer for field \p FieldName.
  const Init *getValueInit(StringRef FieldName) const;

  /// Return true if the named field is unset.
  ///
  /// \param FieldName Field name.
  /// \return True if the named field is unset.
  bool isValueUnset(StringRef FieldName) const {
    return isa<UnsetInit>(getValueInit(FieldName));
  }

  /// Return field \p FieldName as a string.
  ///
  /// Throws an exception if the field does not exist or its value is not a
  /// string.
  ///
  /// \param FieldName Field name.
  /// \return Field \p FieldName as a string.
  StringRef getValueAsString(StringRef FieldName) const;

  /// Return field \p FieldName as an optional string.
  ///
  /// Throws an exception if the value is not a string and returns
  /// std::nullopt if the field does not exist.
  ///
  /// \param FieldName Field name.
  /// \return Field \p FieldName as an optional string.
  std::optional<StringRef> getValueAsOptionalString(StringRef FieldName) const;

  /// Return field \p FieldName as a BitsInit.
  ///
  /// Throws an exception if the field does not exist or its value has the
  /// wrong type.
  ///
  /// \param FieldName Field name.
  /// \return Field \p FieldName as a BitsInit.
  const BitsInit *getValueAsBitsInit(StringRef FieldName) const;

  /// Return field \p FieldName as a ListInit.
  ///
  /// Throws an exception if the field does not exist or its value has the
  /// wrong type.
  ///
  /// \param FieldName Field name.
  /// \return Field \p FieldName as a ListInit.
  const ListInit *getValueAsListInit(StringRef FieldName) const;

  /// Return field \p FieldName as a vector of records.
  ///
  /// Throws an exception if the field does not exist or its value has the
  /// wrong type.
  ///
  /// \param FieldName Field name.
  /// \return Field \p FieldName as a vector of records.
  std::vector<const Record *> getValueAsListOfDefs(StringRef FieldName) const;

  /// Return field \p FieldName as a vector of integers.
  ///
  /// Throws an exception if the field does not exist or its value has the
  /// wrong type.
  ///
  /// \param FieldName Field name.
  /// \return Field \p FieldName as a vector of integers.
  std::vector<int64_t> getValueAsListOfInts(StringRef FieldName) const;

  /// Return field \p FieldName as a vector of strings.
  ///
  /// Throws an exception if the field does not exist or its value has the
  /// wrong type.
  ///
  /// \param FieldName Field name.
  /// \return Field \p FieldName as a vector of strings.
  std::vector<StringRef> getValueAsListOfStrings(StringRef FieldName) const;

  /// Return field \p FieldName as a record.
  ///
  /// Throws an exception if the field does not exist or its value has the
  /// wrong type.
  ///
  /// \param FieldName Field name.
  /// \return Field \p FieldName as a record.
  const Record *getValueAsDef(StringRef FieldName) const;

  /// Return field \p FieldName as an optional record.
  ///
  /// Returns null if the field exists but is uninitialized (set to `?`), and
  /// throws an exception if the field does not exist or its value has the
  /// wrong type.
  ///
  /// \param FieldName Field name.
  /// \return Field \p FieldName as an optional record.
  const Record *getValueAsOptionalDef(StringRef FieldName) const;

  /// Return field \p FieldName as a bit.
  ///
  /// Throws an exception if the field does not exist or its value has the
  /// wrong type.
  ///
  /// \param FieldName Field name.
  /// \return The field value as a bit.
  bool getValueAsBit(StringRef FieldName) const;

  /// Return field \p FieldName as a bit, allowing an unset value.
  ///
  /// If the field is unset, sets \p Unset to true and returns false.
  ///
  /// \param FieldName Field name.
  /// \param Unset Receives whether the field is unset.
  /// \return The bit value, or false if the field is unset.
  bool getValueAsBitOrUnset(StringRef FieldName, bool &Unset) const;

  /// Return field \p FieldName as an int64_t.
  ///
  /// Throws an exception if the field does not exist or its value has the
  /// wrong type.
  ///
  /// \param FieldName Field name.
  /// \return Field \p FieldName as an int64_t.
  int64_t getValueAsInt(StringRef FieldName) const;

  /// Return field \p FieldName as a DagInit.
  ///
  /// Throws an exception if the field does not exist or its value has the
  /// wrong type.
  ///
  /// \param FieldName Field name.
  /// \return Field \p FieldName as a DagInit.
  const DagInit *getValueAsDag(StringRef FieldName) const;
};

/// Write a record to an output stream.
///
/// \param OS Output stream.
/// \param R Record to write.
/// \return The output stream.
raw_ostream &operator<<(raw_ostream &OS, const Record &R);

/// Owns and indexes the records parsed from a TableGen input.
class RecordKeeper {
  using RecordMap = std::map<std::string, std::unique_ptr<Record>, std::less<>>;
  using GlobalMap = std::map<std::string, const Init *, std::less<>>;

public:
  /// Construct an empty record keeper.
  RecordKeeper();
  /// Destroy this record keeper.
  ~RecordKeeper();

  /// Return the internal implementation of the RecordKeeper.
  /// \return The internal implementation of the RecordKeeper.
  detail::RecordKeeperImpl &getImpl() { return *Impl; }

  /// Get the main TableGen input file's name.
  /// \return The main TableGen input file's name.
  StringRef getInputFilename() const { return InputFilename; }

  /// Get the map of classes.
  /// \return The map of classes.
  const RecordMap &getClasses() const { return Classes; }

  /// Get the map of records (defs).
  /// \return The map of records (defs).
  const RecordMap &getDefs() const { return Defs; }

  /// Get the map of global variables.
  /// \return The map of global variables.
  const GlobalMap &getGlobals() const { return ExtraGlobals; }

  /// Get the class with the specified name.
  ///
  /// \param Name Class name.
  /// \return The class with the specified name.
  const Record *getClass(StringRef Name) const {
    auto I = Classes.find(Name);
    return I == Classes.end() ? nullptr : I->second.get();
  }

  /// Get the concrete record with the specified name.
  ///
  /// \param Name Record name.
  /// \return The concrete record with the specified name.
  const Record *getDef(StringRef Name) const {
    auto I = Defs.find(Name);
    return I == Defs.end() ? nullptr : I->second.get();
  }

  /// Get the \p Init value of the specified global variable.
  ///
  /// \param Name Global variable name.
  /// \return The \p Init value of the specified global variable.
  const Init *getGlobal(StringRef Name) const {
    if (const Record *R = getDef(Name))
      return R->getDefInit();
    auto It = ExtraGlobals.find(Name);
    return It == ExtraGlobals.end() ? nullptr : It->second;
  }

  /// Save the main TableGen input filename.
  ///
  /// \param Filename Input filename.
  void saveInputFilename(std::string Filename) {
    InputFilename = std::move(Filename);
  }

  /// Add a class record.
  ///
  /// \param R Class record to add.
  void addClass(std::unique_ptr<Record> R) {
    bool Ins =
        Classes.try_emplace(std::string(R->getName()), std::move(R)).second;
    (void)Ins;
    assert(Ins && "Class already exists");
  }

  /// Add a definition record.
  ///
  /// \param R Definition record to add.
  void addDef(std::unique_ptr<Record> R) {
    bool Ins = Defs.try_emplace(std::string(R->getName()), std::move(R)).second;
    (void)Ins;
    assert(Ins && "Record already exists");
    // Clear cache
    if (!Cache.empty())
      Cache.clear();
  }

  /// Add a global variable.
  ///
  /// \param Name Global variable name.
  /// \param I Initializer for the global variable.
  void addExtraGlobal(StringRef Name, const Init *I) {
    bool Ins = ExtraGlobals.try_emplace(std::string(Name), I).second;
    (void)Ins;
    assert(!getDef(Name));
    assert(Ins && "Global already exists");
  }

  /// Return a new unique anonymous record name.
  /// \return A new unique anonymous record name.
  const Init *getNewAnonymousName();

  /// Return the timer associated with this record keeper.
  /// \return The timer associated with this record keeper.
  TGTimer &getTimer() const { return *Timer; }

  //===--------------------------------------------------------------------===//
  // High-level helper methods, useful for tablegen backends.

  /// Get all the concrete records that inherit from the one specified
  /// class. The class must be defined.
  ///
  /// \param ClassName Base class name.
  /// \return All concrete records that inherit from the specified class.
  ArrayRef<const Record *> getAllDerivedDefinitions(StringRef ClassName) const;

  /// Get all the concrete records that inherit from all the specified
  /// classes. The classes must be defined.
  ///
  /// \param ClassNames Base class names.
  /// \return All concrete records that inherit from all the specified classes.
  std::vector<const Record *>
  getAllDerivedDefinitions(ArrayRef<StringRef> ClassNames) const;

  /// Get all the concrete records that inherit from specified class, if the
  /// class is defined. Returns an empty vector if the class is not defined.
  ///
  /// \param ClassName Base class name.
  /// \return All concrete records that inherit from the class, or an empty vector if the class is not defined.
  ArrayRef<const Record *>
  getAllDerivedDefinitionsIfDefined(StringRef ClassName) const;

  /// Print the record keeper to the standard error stream.
  void dump() const;

  /// Print allocation statistics.
  ///
  /// \param OS Output stream.
  void dumpAllocationStats(raw_ostream &OS) const;

private:
  RecordKeeper(RecordKeeper &&) = delete;
  RecordKeeper(const RecordKeeper &) = delete;
  RecordKeeper &operator=(RecordKeeper &&) = delete;
  RecordKeeper &operator=(const RecordKeeper &) = delete;

  std::string InputFilename;
  RecordMap Classes, Defs;
  mutable std::map<std::string, std::vector<const Record *>> Cache;
  GlobalMap ExtraGlobals;

  /// The internal uniquer implementation of the RecordKeeper.
  std::unique_ptr<detail::RecordKeeperImpl> Impl;
  std::unique_ptr<TGTimer> Timer;
};

/// Sort record pointers by name.
struct LessRecord {
  /// Compare two records by numeric-aware name order.
  ///
  /// \param Rec1 First record.
  /// \param Rec2 Second record.
  /// \return True when the left-hand side should be ordered before the right-hand side.
  bool operator()(const Record *Rec1, const Record *Rec2) const {
    return Rec1->getName().compare_numeric(Rec2->getName()) < 0;
  }
};

/// Sort record pointers by unique identifier.
///
/// Use this for deterministic ordering because it compares only two unsigned
/// values, unlike the other sorting predicates, which manipulate strings.
struct LessRecordByID {
  /// Compare two records by unique identifier.
  ///
  /// \param LHS First record.
  /// \param RHS Second record.
  /// \return True when the left-hand side should be ordered before the right-hand side.
  bool operator()(const Record *LHS, const Record *RHS) const {
    return LHS->getID() < RHS->getID();
  }
};

/// Sorting predicate to sort record pointers by their Name field.
struct LessRecordFieldName {
  /// Compare two records by their `Name` field.
  ///
  /// \param Rec1 First record.
  /// \param Rec2 Second record.
  /// \return True when the left-hand side should be ordered before the right-hand side.
  bool operator()(const Record *Rec1, const Record *Rec2) const {
    return Rec1->getValueAsString("Name") < Rec2->getValueAsString("Name");
  }
};

/// Sort register records by position order and register-name components.
struct LessRecordRegister {
  /// Splits a register name into alternating alphabetic and numeric parts.
  struct RecordParts {
    /// Alternating digit-status and name components.
    SmallVector<std::pair< bool, StringRef>, 4> Parts;

    /// Split a register name into comparable parts.
    ///
    /// \param Rec Register name.
    RecordParts(StringRef Rec) {
      if (Rec.empty())
        return;

      size_t Len = 0;
      const char *Start = Rec.data();
      const char *Curr = Start;
      bool IsDigitPart = isDigit(Curr[0]);
      for (size_t I = 0, E = Rec.size(); I != E; ++I, ++Len) {
        bool IsDigit = isDigit(Curr[I]);
        if (IsDigit != IsDigitPart) {
          Parts.emplace_back(IsDigitPart, StringRef(Start, Len));
          Len = 0;
          Start = &Curr[I];
          IsDigitPart = isDigit(Curr[I]);
        }
      }
      // Push the last part.
      Parts.emplace_back(IsDigitPart, StringRef(Start, Len));
    }

    /// Return the number of name components.
    /// \return The number of name components.
    size_t size() { return Parts.size(); }

    /// Return the component at \p Idx.
    ///
    /// \param Idx Component index.
    /// \return The component at \p Idx.
    std::pair<bool, StringRef> getPart(size_t Idx) { return Parts[Idx]; }
  };

  /// Compare two register records.
  ///
  /// \param Rec1 First record.
  /// \param Rec2 Second record.
  /// \return True when the left-hand side should be ordered before the right-hand side.
  bool operator()(const Record *Rec1, const Record *Rec2) const {
    int64_t LHSPositionOrder = Rec1->getValueAsInt("PositionOrder");
    int64_t RHSPositionOrder = Rec2->getValueAsInt("PositionOrder");
    if (LHSPositionOrder != RHSPositionOrder)
      return LHSPositionOrder < RHSPositionOrder;

    RecordParts LHSParts(StringRef(Rec1->getName()));
    RecordParts RHSParts(StringRef(Rec2->getName()));

    size_t LHSNumParts = LHSParts.size();
    size_t RHSNumParts = RHSParts.size();
    assert (LHSNumParts && RHSNumParts && "Expected at least one part!");

    if (LHSNumParts != RHSNumParts)
      return LHSNumParts < RHSNumParts;

    // We expect the registers to be of the form [_a-zA-Z]+([0-9]*[_a-zA-Z]*)*.
    for (size_t I = 0, E = LHSNumParts; I < E; I+=2) {
      std::pair<bool, StringRef> LHSPart = LHSParts.getPart(I);
      std::pair<bool, StringRef> RHSPart = RHSParts.getPart(I);
      // Expect even part to always be alpha.
      assert (LHSPart.first == false && RHSPart.first == false &&
              "Expected both parts to be alpha.");
      if (int Res = LHSPart.second.compare(RHSPart.second))
        return Res < 0;
    }
    for (size_t I = 1, E = LHSNumParts; I < E; I+=2) {
      std::pair<bool, StringRef> LHSPart = LHSParts.getPart(I);
      std::pair<bool, StringRef> RHSPart = RHSParts.getPart(I);
      // Expect odd part to always be numeric.
      assert (LHSPart.first == true && RHSPart.first == true &&
              "Expected both parts to be numeric.");
      if (LHSPart.second.size() != RHSPart.second.size())
        return LHSPart.second.size() < RHSPart.second.size();

      unsigned LHSVal, RHSVal;

      bool LHSFailed = LHSPart.second.getAsInteger(10, LHSVal); (void)LHSFailed;
      assert(!LHSFailed && "Unable to convert LHS to integer.");
      bool RHSFailed = RHSPart.second.getAsInteger(10, RHSVal); (void)RHSFailed;
      assert(!RHSFailed && "Unable to convert RHS to integer.");

      if (LHSVal != RHSVal)
        return LHSVal < RHSVal;
    }
    return LHSNumParts < RHSNumParts;
  }
};

/// Write a record keeper to an output stream.
///
/// \param OS Output stream.
/// \param RK Record keeper to write.
/// \return The output stream.
raw_ostream &operator<<(raw_ostream &OS, const RecordKeeper &RK);

//===----------------------------------------------------------------------===//
//  Resolvers
//===----------------------------------------------------------------------===//

/// Interface for looking up the initializer for a variable name, used by
/// Init::resolveReferences.
class Resolver {
  const Record *CurRec;
  bool IsFinal = false;

public:
  /// Construct a resolver for a record.
  ///
  /// \param CurRec Record whose initializers are resolved.
  explicit Resolver(const Record *CurRec) : CurRec(CurRec) {}
  /// Destroy this resolver.
  virtual ~Resolver() = default;

  /// Return the record whose initializers are resolved.
  /// \return The record whose initializers are resolved.
  const Record *getCurrentRecord() const { return CurRec; }

  /// Return the initializer for the given variable name (should normally be a
  /// StringInit), or nullptr if the name could not be resolved.
  ///
  /// \param VarName Variable name initializer.
  /// \return The initializer for the given variable name (should normally be a StringInit), or nullptr if the name could not be resolved.
  virtual const Init *resolve(const Init *VarName) = 0;

  /// Return whether bits that resolve to `?` should remain unresolved.
  ///
  /// This represents instruction encodings by retaining references to unset
  /// variables in a record.
  /// \return Whether bits that resolve to `?` should remain unresolved.
  virtual bool keepUnsetBits() const { return false; }

  /// Return whether this is the final resolution step.
  ///
  /// Error reporting and related constant folding should occur only during the
  /// final step, before adding a record to the RecordKeeper.
  /// \return Whether this is the final resolution step.
  bool isFinal() const { return IsFinal; }

  /// Set whether this is the final resolution step.
  ///
  /// \param Final Whether this is the final step.
  void setFinal(bool Final) { IsFinal = Final; }
};

/// Resolve arbitrary mappings.
class MapResolver final : public Resolver {
  struct MappedValue {
    const Init *V;
    bool Resolved;

    MappedValue() : V(nullptr), Resolved(false) {}
    MappedValue(const Init *V, bool Resolved) : V(V), Resolved(Resolved) {}
  };

  DenseMap<const Init *, MappedValue> Map;

public:
  /// Construct a resolver backed by an arbitrary mapping.
  ///
  /// \param CurRec Record whose initializers are resolved.
  explicit MapResolver(const Record *CurRec = nullptr) : Resolver(CurRec) {}

  /// Associate a key initializer with a replacement value.
  ///
  /// \param Key Initializer to resolve.
  /// \param Value Replacement initializer.
  void set(const Init *Key, const Init *Value) { Map[Key] = {Value, false}; }

  /// Return whether the mapping for \p VarName is complete.
  ///
  /// \param VarName Mapped initializer to inspect.
  /// \return Whether the mapping for \p VarName is complete.
  bool isComplete(Init *VarName) const {
    auto It = Map.find(VarName);
    assert(It != Map.end() && "key must be present in map");
    return It->second.V->isComplete();
  }

  /// Resolve \p VarName through this mapping.
  ///
  /// \param VarName Variable name initializer.
  /// \return The resolved initializer.
  const Init *resolve(const Init *VarName) override;
};

/// Resolve all variables from a record except for unset variables.
class RecordResolver final : public Resolver {
  DenseMap<const Init *, const Init *> Cache;
  SmallVector<const Init *, 4> Stack;
  const Init *Name = nullptr;

public:
  /// Construct a resolver for record fields.
  ///
  /// \param R Record whose fields are resolved.
  explicit RecordResolver(const Record &R) : Resolver(&R) {}

  /// Set the replacement name for the record.
  ///
  /// \param NewName Replacement name initializer.
  void setName(const Init *NewName) { Name = NewName; }

  /// Resolve \p VarName from the current record.
  ///
  /// \param VarName Variable name initializer.
  /// \return The resolved initializer.
  const Init *resolve(const Init *VarName) override;

  /// Keep bits unresolved when resolving them produces an unset value.
  /// \return True; unset bits are kept unresolved.
  bool keepUnsetBits() const override { return true; }
};

/// Delegate resolving to a sub-resolver, but shadow some variable names.
class ShadowResolver final : public Resolver {
  Resolver &R;
  DenseSet<const Init *> Shadowed;

public:
  /// Construct a resolver that shadows selected variable names.
  ///
  /// \param R Resolver to delegate to.
  explicit ShadowResolver(Resolver &R)
      : Resolver(R.getCurrentRecord()), R(R) {
    setFinal(R.isFinal());
  }

  /// Add a variable name to shadow.
  ///
  /// \param Key Variable name initializer.
  void addShadow(const Init *Key) { Shadowed.insert(Key); }

  /// Resolve \p VarName unless it is shadowed.
  ///
  /// \param VarName Variable name initializer.
  /// \return The resolved initializer.
  const Init *resolve(const Init *VarName) override {
    if (Shadowed.count(VarName))
      return nullptr;
    return R.resolve(VarName);
  }
};

/// (Optionally) delegate resolving to a sub-resolver, and keep track whether
/// there were unresolved references.
class TrackUnresolvedResolver final : public Resolver {
  Resolver *R;
  bool FoundUnresolved = false;

public:
  /// Construct a resolver that tracks unresolved references.
  ///
  /// \param R Optional resolver to delegate to.
  explicit TrackUnresolvedResolver(Resolver *R = nullptr)
      : Resolver(R ? R->getCurrentRecord() : nullptr), R(R) {}

  /// Return whether resolution encountered an unresolved reference.
  /// \return Whether resolution encountered an unresolved reference.
  bool foundUnresolved() const { return FoundUnresolved; }

  /// Resolve \p VarName and track an unresolved result.
  ///
  /// \param VarName Variable name initializer.
  /// \return The resolved initializer.
  const Init *resolve(const Init *VarName) override;
};

/// Do not resolve anything, but keep track of whether a given variable was
/// referenced.
class HasReferenceResolver final : public Resolver {
  const Init *VarNameToTrack;
  bool Found = false;

public:
  /// Construct a resolver that tracks references to one variable.
  ///
  /// \param VarNameToTrack Variable name initializer to track.
  explicit HasReferenceResolver(const Init *VarNameToTrack)
      : Resolver(nullptr), VarNameToTrack(VarNameToTrack) {}

  /// Return whether the tracked variable was referenced.
  /// \return Whether the tracked variable was referenced.
  bool found() const { return Found; }

  /// Record whether \p VarName matches the tracked variable.
  ///
  /// \param VarName Variable name initializer.
  /// \return Always null; matching is recorded as a side effect.
  const Init *resolve(const Init *VarName) override;
};

/// Emit detailed textual representations of all records.
///
/// \param RK Record keeper containing the records.
/// \param OS Output stream.
void EmitDetailedRecords(const RecordKeeper &RK, raw_ostream &OS);
/// Emit a JSON representation of all records.
///
/// \param RK Record keeper containing the records.
/// \param OS Output stream.
void EmitJSON(const RecordKeeper &RK, raw_ostream &OS);

} // end namespace llvm

#endif // LLVM_TABLEGEN_RECORD_H
