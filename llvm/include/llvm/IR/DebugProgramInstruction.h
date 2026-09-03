//===-- llvm/DebugProgramInstruction.h - Stream of debug info ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Data structures for storing variable assignment information in LLVM. In the
// dbg.value design, a dbg.value intrinsic specifies the position in a block
// a source variable take on an LLVM Value:
//
//    %foo = add i32 1, %0
//    dbg.value(metadata i32 %foo, ...)
//    %bar = void call @ext(%foo);
//
// and all information is stored in the Value / Metadata hierarchy defined
// elsewhere in LLVM. In the "DbgRecord" design, each instruction /may/ have a
// connection with a DbgMarker, which identifies a position immediately before
// the instruction, and each DbgMarker /may/ then have connections to DbgRecords
// which record the variable assignment information. To illustrate:
//
//    %foo = add i32 1, %0
//       ; foo->DebugMarker == nullptr
//       ;; There are no variable assignments / debug records "in front" of
//       ;; the instruction for %foo, therefore it has no DebugMarker.
//    %bar = void call @ext(%foo)
//       ; bar->DebugMarker = {
//       ;   StoredDbgRecords = {
//       ;     DbgVariableRecord(metadata i32 %foo, ...)
//       ;   }
//       ; }
//       ;; There is a debug-info record in front of the %bar instruction,
//       ;; thus it points at a DbgMarker object. That DbgMarker contains a
//       ;; DbgVariableRecord in its ilist, storing the equivalent information
//       ;; to the dbg.value above: the Value, DILocalVariable, etc.
//
// This structure separates the two concerns of the position of the debug-info
// in the function, and the Value that it refers to. It also creates a new
// "place" in-between the Value / Metadata hierarchy where we can customise
// storage and allocation techniques to better suite debug-info workloads.
// NB: as of the initial prototype, none of that has actually been attempted
// yet.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DEBUGPROGRAMINSTRUCTION_H
#define LLVM_IR_DEBUGPROGRAMINSTRUCTION_H

#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator.h"
#include "llvm/IR/DbgVariableFragmentInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Instruction;
class BasicBlock;
class MDNode;
class Module;
class DbgVariableIntrinsic;
/// Forward declaration of the intrinsic wrapper for llvm.dbg.* info calls.
class DbgInfoIntrinsic;
class DbgLabelInst;
class DIAssignID;
class DbgMarker;
class DbgVariableRecord;
class raw_ostream;

/// Typed tracking MDNode reference that avoids needing the parameter type defined.
///
/// Necessary to avoid including DebugInfoMetadata.h, which has a significant
/// impact on compile times if included in this file.
template <typename T> class DbgRecordParamRef {
  TrackingMDNodeRef Ref;

public:
public:
  /// Construct a null reference.
  DbgRecordParamRef() = default;

  /// Construct from the templated type.
  /// \param Param Typed debug-metadata node to track.
  DbgRecordParamRef(const T *Param);

  /// Construct from an \a MDNode.
  ///
  /// Note: if \c Param does not have the template type, a verifier check will
  /// fail, and accessors will crash.  However, construction from other nodes
  /// is supported in order to handle forward references when reading textual
  /// IR.
  /// \param Param Metadata node to track (possibly a forward reference).
  explicit DbgRecordParamRef(const MDNode *Param);

  /// Get the underlying type.
  ///
  /// \pre !*this or \c isa<T>(getAsMDNode()).
  /// @{
  /// Get the underlying typed debug-metadata node.
  /// \return Typed pointer to the referenced debug-metadata node, or null.
  T *get() const;
  /// Convert to a typed pointer to the referenced debug-metadata node.
  /// \return Typed pointer to the referenced debug-metadata node, or null.
  operator T *() const { return get(); }
  /// Access members of the referenced debug-metadata node.
  /// \return Typed pointer to the referenced debug-metadata node.
  T *operator->() const { return get(); }
  /// Dereference the wrapped debug-metadata node.
  /// \return Reference to the referenced debug-metadata node.
  T &operator*() const { return *get(); }
  /// @}

  /// Check for null.
  ///
  /// Check for null in a way that is safe with broken debug info.
  /// \return True if this reference is non-null.
  explicit operator bool() const { return Ref; }

  /// Return \c this as a \a MDNode.
  /// \return The underlying metadata node, or null.
  MDNode *getAsMDNode() const { return Ref; }

  /// Return true if both references track the same metadata node.
  /// \param Other Other reference to compare.
  /// \return True if both references track the same metadata node.
  bool operator==(const DbgRecordParamRef &Other) const {
    return Ref == Other.Ref;
  }
  /// Return true if the referenced metadata nodes differ.
  /// \param Other Other reference to compare.
  /// \return True if the referenced metadata nodes differ.
  bool operator!=(const DbgRecordParamRef &Other) const {
    return Ref != Other.Ref;
  }
};

/// Explicit instantiation for \c DIExpression references.
extern template class LLVM_TEMPLATE_ABI DbgRecordParamRef<DIExpression>;
/// Explicit instantiation for \c DILabel references.
extern template class LLVM_TEMPLATE_ABI DbgRecordParamRef<DILabel>;
/// Explicit instantiation for \c DILocalVariable references.
extern template class LLVM_TEMPLATE_ABI DbgRecordParamRef<DILocalVariable>;

/// Base class for non-instruction debug metadata records positioned in IR.
///
/// Features various methods copied across from the Instruction class to aid
/// ease-of-use. DbgRecords should always be linked into a DbgMarker's
/// StoredDbgRecords list. The marker connects a DbgRecord back to its position
/// in the BasicBlock.
///
/// We need a discriminator for dyn/isa casts. In order to avoid paying for a
/// vtable for "virtual" functions too, subclasses must add a new discriminator
/// value (RecordKind) and cases to a few functions in the base class:
///   deleteRecord
///   clone
///   isIdenticalToWhenDefined
///   both print methods
///   createDebugIntrinsic
class DbgRecord : public ilist_node<DbgRecord> {
public:
  /// Marker that this DbgRecord is linked into.
  DbgMarker *Marker = nullptr;
  /// Subclass discriminator.
  enum Kind : uint8_t {
    ValueKind, ///< Discriminator for \c DbgVariableRecord.
    LabelKind, ///< Discriminator for \c DbgLabelRecord.
  };

protected:
  /// Source location associated with this debug record.
  DebugLoc DbgLoc;
  Kind RecordKind; ///< Subclass discriminator.

public:
  /// Construct a debug record of subclass \p RecordKind at source location \p DL.
  /// \param RecordKind Subclass discriminator for this record.
  /// \param DL Source location associated with this record.
  DbgRecord(Kind RecordKind, DebugLoc DL)
      : DbgLoc(DL), RecordKind(RecordKind) {}

  ///@{
  /// Delete this record, dispatching to the correct subclass destructor.
  ///
  /// Methods that dispatch to subclass implementations need to be manually
  /// updated when a new subclass is added.
  LLVM_ABI void deleteRecord();
  /// Clone this record, dispatching to the correct subclass.
  /// \return A newly allocated copy of this record.
  LLVM_ABI DbgRecord *clone() const;
  /// Print this record to \p O.
  /// \param O Output stream.
  /// \param IsForDebug Whether to use debug-oriented formatting.
  LLVM_ABI void print(raw_ostream &O, bool IsForDebug = false) const;
  /// Print this record using \p MST for value/type slot numbers.
  /// \param O Output stream.
  /// \param MST Module slot tracker for numbering.
  /// \param IsForDebug Whether to use debug-oriented formatting.
  LLVM_ABI void print(raw_ostream &O, ModuleSlotTracker &MST,
                      bool IsForDebug) const;
  /// Return true if this record matches \p R ignoring debug location differences.
  /// \param R Other record to compare against.
  /// \return True if the records match ignoring debug location.
  LLVM_ABI bool isIdenticalToWhenDefined(const DbgRecord &R) const;
  /// Convert this DbgRecord back into an appropriate llvm.dbg.* intrinsic.
  /// \param M Module in which to create the intrinsic.
  /// \param InsertBefore Optional position to insert this intrinsic.
  /// \returns A new llvm.dbg.* intrinsic representing this DbgRecord.
  LLVM_ABI DbgInfoIntrinsic *
  createDebugIntrinsic(Module *M, Instruction *InsertBefore) const;
  ///@}

  /// Same as isIdenticalToWhenDefined but checks DebugLoc too.
  /// \param R Other record to compare against.
  /// \return True if the records match including debug location.
  LLVM_ABI bool isEquivalentTo(const DbgRecord &R) const;

  /// Return the subclass discriminator for this record.
  /// \return The subclass discriminator for this record.
  Kind getRecordKind() const { return RecordKind; }

  /// Associate this record with the given \c DbgMarker position in a block.
  /// \param M Marker that owns this record's position.
  void setMarker(DbgMarker *M) { Marker = M; }

  /// Return the \c DbgMarker that links this record to its position in a block.
  /// \return The marker that owns this record, or null.
  DbgMarker *getMarker() { return Marker; }
  /// Return the \c DbgMarker that links this record to its position in a block.
  /// \return The marker that owns this record, or null.
  const DbgMarker *getMarker() const { return Marker; }

  /// Return the basic block that contains this debug record.
  /// \return The containing basic block.
  LLVM_ABI BasicBlock *getBlock();
  /// Return the basic block that contains this debug record.
  /// \return The containing basic block.
  LLVM_ABI const BasicBlock *getBlock() const;

  /// Return the function that contains this debug record.
  /// \return The containing function.
  LLVM_ABI Function *getFunction();
  /// Return the function that contains this debug record.
  /// \return The containing function.
  LLVM_ABI const Function *getFunction() const;

  /// Return the module that contains this debug record.
  /// \return The containing module.
  LLVM_ABI Module *getModule();
  /// Return the module that contains this debug record.
  /// \return The containing module.
  LLVM_ABI const Module *getModule() const;

  /// Return the LLVM context that owns this debug record.
  /// \return The LLVM context that owns this record.
  LLVM_ABI LLVMContext &getContext();
  /// Return the LLVM context that owns this debug record.
  /// \return The LLVM context that owns this record.
  LLVM_ABI const LLVMContext &getContext() const;

  /// Return the instruction whose program position this record is attached to.
  /// \return The instruction this record is attached to.
  LLVM_ABI Instruction *getInstruction();
  /// Return the instruction whose program position this record is attached to.
  /// \return The instruction this record is attached to.
  LLVM_ABI const Instruction *getInstruction() const;

  /// Return the basic block that contains this debug record.
  /// \return The containing basic block.
  LLVM_ABI BasicBlock *getParent();
  /// Return the basic block that contains this debug record.
  /// \return The containing basic block.
  LLVM_ABI const BasicBlock *getParent() const;

  /// Unlink this record from its marker without destroying it.
  LLVM_ABI void removeFromParent();
  /// Unlink this record from its marker and destroy it.
  LLVM_ABI void eraseFromParent();

  /// Return the next \c DbgRecord in this marker's list.
  /// \return The next record in the marker's list.
  DbgRecord *getNextNode() { return &*std::next(getIterator()); }
  /// Return the previous \c DbgRecord in this marker's list.
  /// \return The previous record in the marker's list.
  DbgRecord *getPrevNode() { return &*std::prev(getIterator()); }

  // Some generic lambdas supporting intrinsic-based debug-info mean we need
  // to support both iterator and instruction position based insertion.
  /// Insert this record immediately before \p InsertBefore.
  /// \param InsertBefore Existing record to insert before.
  LLVM_ABI void insertBefore(DbgRecord *InsertBefore);
  /// Insert this record immediately after \p InsertAfter.
  /// \param InsertAfter Existing record to insert after.
  LLVM_ABI void insertAfter(DbgRecord *InsertAfter);
  /// Detach from the current marker and reinsert immediately before \p MoveBefore.
  /// \param MoveBefore Existing record to move before.
  LLVM_ABI void moveBefore(DbgRecord *MoveBefore);
  /// Detach from the current marker and reinsert immediately after \p MoveAfter.
  /// \param MoveAfter Existing record to move after.
  LLVM_ABI void moveAfter(DbgRecord *MoveAfter);

  /// Insert this record immediately before the record at \p InsertBefore.
  /// \param InsertBefore Iterator to the existing record to insert before.
  LLVM_ABI void insertBefore(self_iterator InsertBefore);
  /// Insert this record immediately after the record at \p InsertAfter.
  /// \param InsertAfter Iterator to the existing record to insert after.
  LLVM_ABI void insertAfter(self_iterator InsertAfter);
  /// Detach and reinsert immediately before the record at \p MoveBefore.
  /// \param MoveBefore Iterator to the existing record to move before.
  LLVM_ABI void moveBefore(self_iterator MoveBefore);
  /// Detach and reinsert immediately after the record at \p MoveAfter.
  /// \param MoveAfter Iterator to the existing record to move after.
  LLVM_ABI void moveAfter(self_iterator MoveAfter);

  /// Return the source location associated with this debug record.
  /// \return The source location associated with this record.
  DebugLoc getDebugLoc() const { return DbgLoc; }
  /// Set the source location associated with this debug record.
  /// \param Loc New source location.
  void setDebugLoc(DebugLoc Loc) { DbgLoc = std::move(Loc); }

  /// Dump this record to stderr for debugging.
  LLVM_ABI void dump() const;

  /// Iterator over \c DbgRecord nodes in a marker's list.
  using self_iterator = simple_ilist<DbgRecord>::iterator;
  /// Const iterator over \c DbgRecord nodes in a marker's list.
  using const_self_iterator = simple_ilist<DbgRecord>::const_iterator;

protected:
  /// Protected destructor; use \c deleteRecord for cleanup without a vtable.
  ///
  /// Similarly to Value, we avoid paying the cost of a vtable by protecting the
  /// dtor and having deleteRecord dispatch cleanup. Use deleteRecord to delete
  /// a generic record.
  ~DbgRecord() = default;
};

/// Print \p R to \p OS.
/// \param OS Output stream.
/// \param R Debug record to print.
/// \return A reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const DbgRecord &R) {
  R.print(OS);
  return OS;
}

/// Records a position in IR for a source label (DILabel). Corresponds to the
/// llvm.dbg.label intrinsic.
class DbgLabelRecord : public DbgRecord {
  DbgRecordParamRef<DILabel> Label;

  /// This constructor intentionally left private, so that it is only called via
  /// "createUnresolvedDbgLabelRecord", which clearly expresses that it is for
  /// parsing only.
  /// \param Label Unresolved label metadata node from the parser.
  DbgLabelRecord(MDNode *Label);

public:
  /// Construct a label record for \p Label at source location \p DL.
  /// \param Label Source label described by this record.
  /// \param DL Source location associated with this record.
  LLVM_ABI DbgLabelRecord(DILabel *Label, DebugLoc DL);

  /// Create a label record from unresolved metadata during parsing.
  ///
  /// Trying to access the resulting DbgLabelRecord's fields before they are
  /// resolved, or if they resolve to the wrong type, will result in a crash.
  /// \param Label Unresolved label metadata node from the parser.
  /// \return A new unresolved label record.
  LLVM_ABI static DbgLabelRecord *createUnresolvedDbgLabelRecord(MDNode *Label);

  /// Clone this label record.
  /// \return A newly allocated copy of this label record.
  LLVM_ABI DbgLabelRecord *clone() const;
  /// Print this label record to \p O.
  /// \param O Output stream.
  /// \param IsForDebug Whether to use debug-oriented formatting.
  LLVM_ABI void print(raw_ostream &O, bool IsForDebug = false) const;
  /// Print this label record using \p MST for value/type slot numbers.
  /// \param ROS Output stream.
  /// \param MST Module slot tracker for numbering.
  /// \param IsForDebug Whether to use debug-oriented formatting.
  LLVM_ABI void print(raw_ostream &ROS, ModuleSlotTracker &MST,
                      bool IsForDebug) const;
  /// Convert this label record back into an llvm.dbg.label intrinsic.
  /// \param M Module in which to create the intrinsic.
  /// \param InsertBefore Optional position to insert this intrinsic.
  /// \return A new llvm.dbg.label intrinsic representing this record.
  LLVM_ABI DbgLabelInst *createDebugIntrinsic(Module *M,
                                              Instruction *InsertBefore) const;

  /// Set the source label described by this record.
  /// \param NewLabel Replacement source label.
  void setLabel(DILabel *NewLabel) { Label = NewLabel; }
  /// Return the source label described by this record.
  /// \return The source label described by this record.
  DILabel *getLabel() const { return Label.get(); }
  /// Return the label metadata node without type checking.
  /// \return The label metadata node without type checking.
  MDNode *getRawLabel() const { return Label.getAsMDNode(); };

  /// Support type inquiry through isa, cast, and dyn_cast.
  /// \param E Record to test.
  /// \return True if \p E is a \c DbgLabelRecord.
  static bool classof(const DbgRecord *E) {
    return E->getRecordKind() == LabelKind;
  }
};

/// Record of a variable value-assignment, aka a non instruction representation
/// of the dbg.value intrinsic.
///
/// This class inherits from DebugValueUser to allow LLVM's metadata facilities
/// to update our references to metadata beneath our feet.
class DbgVariableRecord : public DbgRecord, protected DebugValueUser {
  friend class DebugValueUser;

public:
  /// Kind of variable location this record describes.
  enum class LocationType : uint8_t {
    Declare,      ///< Corresponds to a dbg.declare intrinsic.
    Value,        ///< Corresponds to a dbg.value intrinsic.
    Assign,       ///< Corresponds to a dbg.assign intrinsic.
    DeclareValue, ///< Corresponds to a dbg.declare_value intrinsic.

    End, ///< Marks the end of the concrete types.
    Any, ///< To indicate all LocationTypes in searches.
  };
  /// Classification of whether this is a dbg.value, dbg.declare, or dbg.assign.
  ///
  /// FIXME: We could use spare padding bits from DbgRecord for this.
  LocationType Type;

  // NB: there is no explicit "Value" field in this class, it's effectively the
  // DebugValueUser superclass instead. The referred to Value can either be a
  // ValueAsMetadata or a DIArgList.

  /// Local variable described by this debug-info record.
  DbgRecordParamRef<DILocalVariable> Variable;
  /// DIExpression applied to the location/value component.
  DbgRecordParamRef<DIExpression> Expression;
  /// DIExpression applied to the address component of a dbg.assign.
  DbgRecordParamRef<DIExpression> AddressExpression;

public:
  /// Create a new DbgVariableRecord representing the intrinsic \p DVI, for
  /// example the assignment represented by a dbg.value.
  /// \param DVI Debug variable intrinsic to represent.
  LLVM_ABI DbgVariableRecord(const DbgVariableIntrinsic *DVI);
  /// Copy-construct a \c DbgVariableRecord, sharing the same metadata references.
  /// \param DVR Existing variable record to copy.
  LLVM_ABI DbgVariableRecord(const DbgVariableRecord &DVR);
  /// Directly construct a new DbgVariableRecord representing a dbg.value
  /// intrinsic assigning \p Location to the DV / Expr / DI variable.
  /// \param Location Location or value metadata for the variable.
  /// \param DV Local variable being described.
  /// \param Expr Expression applied to \p Location.
  /// \param DI Source location for this record.
  /// \param Type Kind of variable location this record describes.
  LLVM_ABI DbgVariableRecord(Metadata *Location, DILocalVariable *DV,
                             DIExpression *Expr, const DILocation *DI,
                             LocationType Type = LocationType::Value);
  /// Construct a dbg.assign-style variable record.
  /// \param Value Value component of the assignment.
  /// \param Variable Local variable being assigned.
  /// \param Expression Expression applied to the value component.
  /// \param AssignID Assign ID linking this record to a store.
  /// \param Address Address component of the assignment.
  /// \param AddressExpression Expression applied to the address component.
  /// \param DI Source location for this record.
  LLVM_ABI DbgVariableRecord(Metadata *Value, DILocalVariable *Variable,
                             DIExpression *Expression, DIAssignID *AssignID,
                             Metadata *Address, DIExpression *AddressExpression,
                             const DILocation *DI);

private:
  /// Private constructor for creating new instances during parsing only. Only
  /// called through `createUnresolvedDbgVariableRecord` below, which makes
  /// clear that this is used for parsing only, and will later return a subclass
  /// depending on which Type is passed.
  DbgVariableRecord(LocationType Type, Metadata *Val, MDNode *Variable,
                    MDNode *Expression, MDNode *AssignID, Metadata *Address,
                    MDNode *AddressExpression);

public:
  /// Create a variable record during parsing with possibly unresolved metadata.
  ///
  /// Although for some fields a generic `Metadata*` argument is accepted for
  /// forward type-references, the verifier and accessors will reject incorrect
  /// types later on. The function is used for all types of DbgVariableRecords
  /// for simplicity while parsing, but asserts if any necessary fields are empty
  /// or unused fields are not empty, i.e. if the #dbg_assign fields are used for
  /// a non-dbg-assign type.
  /// \param Type Kind of variable location this record describes.
  /// \param Val Location or value metadata (possibly unresolved).
  /// \param Variable Local variable metadata node.
  /// \param Expression Expression metadata node for the value/location.
  /// \param AssignID Assign ID metadata node, or null when unused.
  /// \param Address Address metadata for dbg.assign, or null when unused.
  /// \param AddressExpression Address expression metadata, or null when unused.
  /// \return A new unresolved variable record.
  LLVM_ABI static DbgVariableRecord *createUnresolvedDbgVariableRecord(
      LocationType Type, Metadata *Val, MDNode *Variable, MDNode *Expression,
      MDNode *AssignID, Metadata *Address, MDNode *AddressExpression);

  /// Create a dbg.assign-style record linking a value, address, and assign ID.
  /// \param Val Value component of the assignment.
  /// \param Variable Local variable being assigned.
  /// \param Expression Expression applied to the value component.
  /// \param AssignID Assign ID linking this record to a store.
  /// \param Address Address component of the assignment.
  /// \param AddressExpression Expression applied to the address component.
  /// \param DI Source location for this record.
  /// \return A new dbg.assign-style variable record.
  LLVM_ABI static DbgVariableRecord *
  createDVRAssign(Value *Val, DILocalVariable *Variable,
                  DIExpression *Expression, DIAssignID *AssignID,
                  Value *Address, DIExpression *AddressExpression,
                  const DILocation *DI);
  /// Create a dbg.assign record and link it to \p LinkedInstr's assign ID.
  /// \param LinkedInstr Instruction whose assign ID this record should share.
  /// \param Val Value component of the assignment.
  /// \param Variable Local variable being assigned.
  /// \param Expression Expression applied to the value component.
  /// \param Address Address component of the assignment.
  /// \param AddressExpression Expression applied to the address component.
  /// \param DI Source location for this record.
  /// \return A new dbg.assign record linked to \p LinkedInstr.
  LLVM_ABI static DbgVariableRecord *
  createLinkedDVRAssign(Instruction *LinkedInstr, Value *Val,
                        DILocalVariable *Variable, DIExpression *Expression,
                        Value *Address, DIExpression *AddressExpression,
                        const DILocation *DI);

  /// Create a dbg.value-style variable record.
  /// \param Location Location or value of the variable.
  /// \param DV Local variable being described.
  /// \param Expr Expression applied to \p Location.
  /// \param DI Source location for this record.
  /// \return A new dbg.value-style variable record.
  LLVM_ABI static DbgVariableRecord *
  createDbgVariableRecord(Value *Location, DILocalVariable *DV,
                          DIExpression *Expr, const DILocation *DI);
  /// Create a dbg.value-style record and insert it before \p InsertBefore.
  /// \param Location Location or value of the variable.
  /// \param DV Local variable being described.
  /// \param Expr Expression applied to \p Location.
  /// \param DI Source location for this record.
  /// \param InsertBefore Existing record to insert before.
  /// \return A new dbg.value-style record inserted before \p InsertBefore.
  LLVM_ABI static DbgVariableRecord *
  createDbgVariableRecord(Value *Location, DILocalVariable *DV,
                          DIExpression *Expr, const DILocation *DI,
                          DbgVariableRecord &InsertBefore);
  /// Create a dbg.declare-style variable record.
  /// \param Address Address of the local variable.
  /// \param DV Local variable being described.
  /// \param Expr Expression applied to \p Address.
  /// \param DI Source location for this record.
  /// \return A new dbg.declare-style variable record.
  LLVM_ABI static DbgVariableRecord *createDVRDeclare(Value *Address,
                                                      DILocalVariable *DV,
                                                      DIExpression *Expr,
                                                      const DILocation *DI);
  /// Create a dbg.declare-style record and insert it before \p InsertBefore.
  /// \param Address Address of the local variable.
  /// \param DV Local variable being described.
  /// \param Expr Expression applied to \p Address.
  /// \param DI Source location for this record.
  /// \param InsertBefore Existing record to insert before.
  /// \return A new dbg.declare-style record inserted before \p InsertBefore.
  LLVM_ABI static DbgVariableRecord *
  createDVRDeclare(Value *Address, DILocalVariable *DV, DIExpression *Expr,
                   const DILocation *DI, DbgVariableRecord &InsertBefore);

  /// Create a dbg.declare_value-style variable record.
  /// \param Address Value of the local variable.
  /// \param DV Local variable being described.
  /// \param Expr Expression applied to \p Address.
  /// \param DI Source location for this record.
  /// \return A new dbg.declare_value-style variable record.
  LLVM_ABI static DbgVariableRecord *
  createDVRDeclareValue(Value *Address, DILocalVariable *DV, DIExpression *Expr,
                        const DILocation *DI);
  /// Create a dbg.declare_value-style record and insert it before \p InsertBefore.
  /// \param Address Value of the local variable.
  /// \param DV Local variable being described.
  /// \param Expr Expression applied to \p Address.
  /// \param DI Source location for this record.
  /// \param InsertBefore Existing record to insert before.
  /// \return A new dbg.declare_value-style record inserted before \p InsertBefore.
  LLVM_ABI static DbgVariableRecord *
  createDVRDeclareValue(Value *Address, DILocalVariable *DV, DIExpression *Expr,
                        const DILocation *DI, DbgVariableRecord &InsertBefore);

  /// Bidirectional iterator over location operands as \c Value pointers.
  ///
  /// Internally uses direct pointer iteration over either a ValueAsMetadata* or
  /// a ValueAsMetadata**, dereferencing to the ValueAsMetadata.
  class location_op_iterator
      : public iterator_facade_base<location_op_iterator,
                                    std::bidirectional_iterator_tag, Value *> {
    PointerUnion<ValueAsMetadata *, ValueAsMetadata **> I;

  public:
    /// Construct an iterator over a single \c ValueAsMetadata location.
    /// \param SingleIter Single location metadata to iterate.
    location_op_iterator(ValueAsMetadata *SingleIter) : I(SingleIter) {}
    /// Iterate over a range stored as an array of \c ValueAsMetadata pointers.
    /// \param MultiIter Pointer to the first element of a multi-value array.
    location_op_iterator(ValueAsMetadata **MultiIter) : I(MultiIter) {}

    /// Copy-construct a location operand iterator.
    /// \param R Iterator to copy.
    location_op_iterator(const location_op_iterator &R) : I(R.I) {}
    /// Assign from another location operand iterator.
    /// \param R Iterator to assign from.
    /// \return A reference to this iterator.
    location_op_iterator &operator=(const location_op_iterator &R) {
      I = R.I;
      return *this;
    }
    /// Return true if both iterators refer to the same location operand.
    /// \param RHS Other iterator to compare.
    /// \return True if both iterators refer to the same location operand.
    bool operator==(const location_op_iterator &RHS) const {
      return I == RHS.I;
    }
    /// Return the \c Value referenced by the current location operand.
    /// \return The value referenced by the current location operand.
    const Value *operator*() const {
      ValueAsMetadata *VAM = isa<ValueAsMetadata *>(I)
                                 ? cast<ValueAsMetadata *>(I)
                                 : *cast<ValueAsMetadata **>(I);
      return VAM->getValue();
    };
    /// Return the \c Value referenced by the current location operand.
    /// \return The value referenced by the current location operand.
    Value *operator*() {
      ValueAsMetadata *VAM = isa<ValueAsMetadata *>(I)
                                 ? cast<ValueAsMetadata *>(I)
                                 : *cast<ValueAsMetadata **>(I);
      return VAM->getValue();
    }
    /// Advance to the next location operand in the single- or multi-value list.
    /// \return A reference to this iterator.
    location_op_iterator &operator++() {
      if (auto *VAM = dyn_cast<ValueAsMetadata *>(I))
        I = VAM + 1;
      else
        I = cast<ValueAsMetadata **>(I) + 1;
      return *this;
    }
    /// Move to the previous location operand in the single- or multi-value list.
    /// \return A reference to this iterator.
    location_op_iterator &operator--() {
      if (auto *VAM = dyn_cast<ValueAsMetadata *>(I))
        I = VAM - 1;
      else
        I = cast<ValueAsMetadata **>(I) - 1;
      return *this;
    }
  };

  /// Return true if this record corresponds to a dbg.declare.
  /// \return True if this record corresponds to a dbg.declare.
  bool isDbgDeclare() const { return Type == LocationType::Declare; }
  /// Return true if this record corresponds to a dbg.value.
  /// \return True if this record corresponds to a dbg.value.
  bool isDbgValue() const { return Type == LocationType::Value; }
  /// Return true if this record corresponds to a dbg.declare_value.
  /// \return True if this record corresponds to a dbg.declare_value.
  bool isDbgDeclareValue() const { return Type == LocationType::DeclareValue; }

  /// Get the locations corresponding to the referenced variable.
  ///
  /// Depending on the intrinsic, this could be the variable's value or its
  /// address.
  /// \return Iterator range over the location operand values.
  LLVM_ABI iterator_range<location_op_iterator> location_ops() const;

  /// Return the value at location operand index \p OpIdx.
  /// \param OpIdx Index into the location operand list.
  /// \return The value at location operand index \p OpIdx.
  LLVM_ABI Value *getVariableLocationOp(unsigned OpIdx) const;

  /// Replace every occurrence of \p OldValue with \p NewValue in the locations.
  /// \param OldValue Existing location value to replace.
  /// \param NewValue Replacement location value.
  /// \param AllowEmpty If true, allow replacing when no match is present.
  LLVM_ABI void replaceVariableLocationOp(Value *OldValue, Value *NewValue,
                                          bool AllowEmpty = false);
  /// Replace the location operand at \p OpIdx with \p NewValue.
  /// \param OpIdx Index into the location operand list.
  /// \param NewValue Replacement location value.
  LLVM_ABI void replaceVariableLocationOp(unsigned OpIdx, Value *NewValue);
  /// Append location operands and update the expression that uses them.
  ///
  /// Adding a new location operand will always result in this intrinsic using
  /// an ArgList, and must always be accompanied by a new expression that uses
  /// the new operand.
  /// \param NewValues Additional location values to append.
  /// \param NewExpr Expression that references the updated operand list.
  LLVM_ABI void addVariableLocationOps(ArrayRef<Value *> NewValues,
                                       DIExpression *NewExpr);

  /// Return the number of location operands describing this variable.
  /// \return The number of location operands describing this variable.
  LLVM_ABI unsigned getNumVariableLocationOps() const;

  /// Return true if this record's location is a \c DIArgList of values.
  /// \return True if this record's location is a \c DIArgList.
  bool hasArgList() const { return isa<DIArgList>(getRawLocation()); }
  /// Returns true if this DbgVariableRecord has no empty MDNodes in its
  /// location list.
  /// \return True if the first location operand is non-null.
  bool hasValidLocation() const { return getVariableLocationOp(0) != nullptr; }

  /// Does this describe the address of a local variable. True for dbg.addr
  /// and dbg.declare, but not dbg.value or dbg.declare_value, which describes
  /// its value.
  /// \return True if this record describes the address of a local variable.
  bool isAddressOfVariable() const { return Type == LocationType::Declare; }

  /// Determine if this describes the value of a local variable. It is false for
  /// dbg.declare, but true for dbg.value and dbg.declare_value, which describes
  /// its value.
  /// \return True if this record describes the value of a local variable.
  bool isValueOfVariable() const {
    return Type == LocationType::Value || Type == LocationType::DeclareValue;
  }

  /// Return whether this is a declare, value, or assign-style variable record.
  /// \return The location type of this variable record.
  LocationType getType() const { return Type; }

  /// Mark the variable location as a kill/undef.
  LLVM_ABI void setKillLocation();
  /// Return true if the location is undef, empty, or otherwise marks a kill.
  /// \return True if the location is a kill/undef.
  LLVM_ABI bool isKillLocation() const;

  /// Set the local variable described by this record.
  /// \param NewVar Replacement local variable.
  void setVariable(DILocalVariable *NewVar) { Variable = NewVar; }
  /// Return the local variable described by this record.
  /// \return The local variable described by this record.
  DILocalVariable *getVariable() const { return Variable.get(); };
  /// Return the variable metadata node without type checking.
  /// \return The variable metadata node without type checking.
  MDNode *getRawVariable() const { return Variable.getAsMDNode(); }

  /// Set the DIExpression that describes how to interpret the location.
  /// \param NewExpr Replacement DIExpression for the location/value.
  void setExpression(DIExpression *NewExpr) { Expression = NewExpr; }
  /// Return the DIExpression applied to the location/value component.
  /// \return The DIExpression applied to the location/value component.
  DIExpression *getExpression() const { return Expression.get(); }
  /// Return the expression metadata node without type checking.
  /// \return The expression metadata node without type checking.
  MDNode *getRawExpression() const { return Expression.getAsMDNode(); }

  /// Return the metadata operand for the first location description.
  ///
  /// i.e., dbg intrinsic dbg.value/declare operand and dbg.assign 1st location
  /// operand (the "value component"). Note the operand (singular) may be a
  /// DIArgList which is a list of values.
  /// \return The metadata operand for the first location description.
  Metadata *getRawLocation() const { return DebugValues[0]; }

  /// Return the value at location operand \p OpIdx (default: the first).
  /// \param OpIdx Index into the location operand list.
  /// \return The value at location operand \p OpIdx.
  Value *getValue(unsigned OpIdx = 0) const {
    return getVariableLocationOp(OpIdx);
  }

  /// Set the raw location metadata operand directly.
  ///
  /// Use of this should generally be avoided; instead,
  /// replaceVariableLocationOp and addVariableLocationOps should be used where
  /// possible to avoid creating invalid state.
  /// \param NewLocation Replacement location metadata.
  void setRawLocation(Metadata *NewLocation) {
    assert((isa<ValueAsMetadata>(NewLocation) || isa<DIArgList>(NewLocation) ||
            isa<MDNode>(NewLocation)) &&
           "Location for a DbgVariableRecord must be either ValueAsMetadata or "
           "DIArgList");
    resetDebugValue(0, NewLocation);
  }

  /// Return fragment info for the variable if the expression describes one.
  /// \return Fragment info, or std::nullopt if the expression has none.
  LLVM_ABI std::optional<DbgVariableFragmentInfo> getFragment() const;
  /// Return fragment info, or a fragment covering the whole variable if known.
  ///
  /// If no fragment exists and the variable size is unknown, returns a
  /// zero-sized fragment.
  /// \return Fragment info for the described portion of the variable.
  DbgVariableFragmentInfo getFragmentOrEntireVariable() const {
    if (auto Frag = getFragment())
      return *Frag;
    if (auto Sz = getFragmentSizeInBits())
      return {*Sz, 0};
    return {0, 0};
  }
  /// Get the size (in bits) of the variable, or fragment of the variable that
  /// is described.
  /// \return Size in bits of the described variable or fragment, if known.
  LLVM_ABI std::optional<uint64_t> getFragmentSizeInBits() const;

  /// Return true if this record matches \p Other including debug location.
  /// \param Other Other variable record to compare against.
  /// \return True if the records match including debug location.
  bool isEquivalentTo(const DbgVariableRecord &Other) const {
    return DbgLoc == Other.DbgLoc && isIdenticalToWhenDefined(Other);
  }
  // Matches the definition of the Instruction version, equivalent to above but
  // without checking DbgLoc.
  /// Return true if this record matches \p Other ignoring debug location.
  /// \param Other Other variable record to compare against.
  /// \return True if the records match ignoring debug location.
  bool isIdenticalToWhenDefined(const DbgVariableRecord &Other) const {
    return std::tie(Type, DebugValues, Variable, Expression,
                    AddressExpression) ==
           std::tie(Other.Type, Other.DebugValues, Other.Variable,
                    Other.Expression, Other.AddressExpression);
  }

  /// @name DbgAssign Methods
  /// @{
  /// Return true if this record corresponds to a dbg.assign.
  /// \return True if this record corresponds to a dbg.assign.
  bool isDbgAssign() const { return getType() == LocationType::Assign; }

  /// Return the address component of a dbg.assign.
  /// \return The address component of a dbg.assign.
  LLVM_ABI Value *getAddress() const;
  /// Return the address metadata operand without type checking.
  /// \return The address metadata operand without type checking.
  Metadata *getRawAddress() const {
    return isDbgAssign() ? DebugValues[1] : DebugValues[0];
  }
  /// Return the assign ID metadata operand without type checking.
  /// \return The assign ID metadata operand without type checking.
  Metadata *getRawAssignID() const { return DebugValues[2]; }
  /// Return the \c DIAssignID that links this assign record to its store.
  /// \return The \c DIAssignID that links this assign record to its store.
  LLVM_ABI DIAssignID *getAssignID() const;
  /// Return the DIExpression applied to the address component.
  /// \return The DIExpression applied to the address component.
  DIExpression *getAddressExpression() const { return AddressExpression.get(); }
  /// Return the address DIExpression metadata node without type checking.
  /// \return The address DIExpression metadata node without type checking.
  MDNode *getRawAddressExpression() const {
    return AddressExpression.getAsMDNode();
  }
  /// Set the DIExpression applied to the address component of a dbg.assign.
  /// \param NewExpr Replacement address DIExpression.
  void setAddressExpression(DIExpression *NewExpr) {
    AddressExpression = NewExpr;
  }
  /// Set the \c DIAssignID that links this assign record to its store.
  /// \param New Replacement assign ID.
  LLVM_ABI void setAssignId(DIAssignID *New);
  /// Set the address component of a dbg.assign to \p V.
  /// \param V Replacement address value.
  void setAddress(Value *V) { resetDebugValue(1, ValueAsMetadata::get(V)); }
  /// Kill the address component.
  LLVM_ABI void setKillAddress();
  /// Return true if the address component is a kill/undef.
  ///
  /// This doesn't take into account the position of the intrinsic, therefore a
  /// returned value of false does not guarantee the address is a valid location
  /// for the variable at the intrinsic's position in IR.
  /// \return True if the address component is a kill/undef.
  LLVM_ABI bool isKillAddress() const;

  /// @}

  /// Clone this variable record.
  /// \return A newly allocated copy of this variable record.
  LLVM_ABI DbgVariableRecord *clone() const;
  /// Convert this DbgVariableRecord back into a dbg.value intrinsic.
  /// \param M Module in which to create the intrinsic.
  /// \param InsertBefore Optional position to insert this intrinsic.
  /// \returns A new dbg.value intrinsic representing this DbgVariableRecord.
  LLVM_ABI DbgVariableIntrinsic *
  createDebugIntrinsic(Module *M, Instruction *InsertBefore) const;

  /// Print this variable record to \p O.
  /// \param O Output stream.
  /// \param IsForDebug Whether to use debug-oriented formatting.
  LLVM_ABI void print(raw_ostream &O, bool IsForDebug = false) const;
  /// Print this variable record using \p MST for value/type slot numbers.
  /// \param ROS Output stream.
  /// \param MST Module slot tracker for numbering.
  /// \param IsForDebug Whether to use debug-oriented formatting.
  LLVM_ABI void print(raw_ostream &ROS, ModuleSlotTracker &MST,
                      bool IsForDebug) const;

  /// Support type inquiry through isa, cast, and dyn_cast.
  /// \param E Record to test.
  /// \return True if \p E is a \c DbgVariableRecord.
  static bool classof(const DbgRecord *E) {
    return E->getRecordKind() == ValueKind;
  }
};

/// Filter the DbgRecord range to DbgVariableRecord types only and downcast.
static inline auto
filterDbgVars(iterator_range<simple_ilist<DbgRecord>::iterator> R) {
  return map_range(
      make_filter_range(R,
                        [](DbgRecord &E) { return isa<DbgVariableRecord>(E); }),
      [](DbgRecord &E) { return std::ref(cast<DbgVariableRecord>(E)); });
}

/// Per-instruction holder for debug-info records attached before an instruction.
///
/// If an Instruction is the position of some debugging information, it points
/// at a DbgMarker storing that info. Each marker points back at the instruction
/// that owns it. Various utilities are provided for manipulating the DbgRecords
/// contained within this marker.
///
/// This class has a rough surface area, because it's needed to preserve the
/// one arefact that we can't yet eliminate from the intrinsic / dbg.value
/// debug-info design: the order of records is significant, and duplicates can
/// exist. Thus, if one has a run of debug-info records such as:
///    dbg.value(...
///    %foo = barinst
///    dbg.value(...
/// and remove barinst, then the dbg.values must be preserved in the correct
/// order. Hence, the use of iterators to select positions to insert things
/// into, or the occasional InsertAtHead parameter indicating that new records
/// should go at the start of the list.
///
/// There are only five or six places in LLVM that truly rely on this ordering,
/// which we can improve in the future. Additionally, many improvements in the
/// way that debug-info is stored can be achieved in this class, at a future
/// date.
class DbgMarker {
public:
  /// Construct an empty marker with no attached instruction.
  DbgMarker() = default;
  /// Link back to the Instruction that owns this marker. Can be null during
  /// operations that move a marker from one instruction to another.
  Instruction *MarkedInstr = nullptr;

  /// Ordered list of \c DbgRecords attached at this instruction position.
  ///
  /// There is a one-to-one relationship between each debug intrinsic in a block
  /// and each DbgRecord once the representation has been converted, and the
  /// ordering is meaningful in the same way.
  simple_ilist<DbgRecord> StoredDbgRecords;
  /// Return true if this marker stores no debug records.
  /// \return True if this marker stores no debug records.
  bool empty() const { return StoredDbgRecords.empty(); }

  /// Return the basic block that owns the instruction this marker is attached to.
  /// \return The parent basic block of the marked instruction.
  LLVM_ABI const BasicBlock *getParent() const;
  /// Return the basic block that owns the instruction this marker is attached to.
  /// \return The parent basic block of the marked instruction.
  LLVM_ABI BasicBlock *getParent();

  /// Relocate stored records when this marker's instruction position goes away.
  ///
  /// Drop them onto the next instruction, or otherwise work out what to do with
  /// them. The stored debug records themselves should not be discarded.
  LLVM_ABI void removeMarker();
  /// Dump this marker to stderr for debugging.
  LLVM_ABI void dump() const;

  /// Unlink this marker from its instruction without destroying stored records.
  LLVM_ABI void removeFromParent();
  /// Drop this marker from its instruction, delete all stored \c DbgRecords, and
  /// destroy the marker itself.
  LLVM_ABI void eraseFromParent();

  /// Print this marker to \p O.
  /// \param O Output stream.
  /// \param IsForDebug Whether to use debug-oriented formatting.
  LLVM_ABI void print(raw_ostream &O, bool IsForDebug = false) const;
  /// Print this marker using \p MST for value/type slot numbers.
  /// \param ROS Output stream.
  /// \param MST Module slot tracker for numbering.
  /// \param IsForDebug Whether to use debug-oriented formatting.
  LLVM_ABI void print(raw_ostream &ROS, ModuleSlotTracker &MST,
                      bool IsForDebug) const;

  /// Produce a range over all the DbgRecords in this Marker.
  /// \return Iterator range over the stored debug records.
  LLVM_ABI iterator_range<simple_ilist<DbgRecord>::iterator>
  getDbgRecordRange();
  /// Produce a const range over all the DbgRecords in this Marker.
  /// \return Const iterator range over the stored debug records.
  LLVM_ABI iterator_range<simple_ilist<DbgRecord>::const_iterator>
  getDbgRecordRange() const;
  /// Transfer any DbgRecords from \p Src into this DbgMarker.
  /// \param Src Marker whose records are absorbed.
  /// \param InsertAtHead If true, place them before existing DbgRecords.
  LLVM_ABI void absorbDebugValues(DbgMarker &Src, bool InsertAtHead);
  /// Transfer the DbgRecords in \p Range from \p Src into this DbgMarker.
  /// \param Range Subrange of \p Src's records to absorb.
  /// \param Src Marker that currently owns \p Range.
  /// \param InsertAtHead If true, place them before existing DbgRecords.
  LLVM_ABI void
  absorbDebugValues(iterator_range<DbgRecord::self_iterator> Range,
                    DbgMarker &Src, bool InsertAtHead);
  /// Insert a DbgRecord into this DbgMarker, at the end of the list.
  /// \param New Record to insert.
  /// \param InsertAtHead If true, insert at the start instead.
  LLVM_ABI void insertDbgRecord(DbgRecord *New, bool InsertAtHead);
  /// Insert a DbgRecord prior to a DbgRecord contained within this marker.
  /// \param New Record to insert.
  /// \param InsertBefore Existing record in this marker to insert before.
  LLVM_ABI void insertDbgRecord(DbgRecord *New, DbgRecord *InsertBefore);
  /// Insert a DbgRecord after a DbgRecord contained within this marker.
  /// \param New Record to insert.
  /// \param InsertAfter Existing record in this marker to insert after.
  LLVM_ABI void insertDbgRecordAfter(DbgRecord *New, DbgRecord *InsertAfter);
  /// Clone DbgRecords from \p From into this marker.
  ///
  /// There are numerous options to customise the source/destination, due to
  /// gnarliness; see the class comment.
  /// \param From Marker to copy records from.
  /// \param FromHere If set, copy from this iterator to the end of \p From.
  /// \param InsertAtHead Place the cloned DbgRecords at the start of
  /// StoredDbgRecords.
  /// \returns Range over all the newly cloned DbgRecords.
  LLVM_ABI iterator_range<simple_ilist<DbgRecord>::iterator>
  cloneDebugInfoFrom(DbgMarker *From,
                     std::optional<simple_ilist<DbgRecord>::iterator> FromHere,
                     bool InsertAtHead = false);
  /// Erase all DbgRecords in this DbgMarker.
  LLVM_ABI void dropDbgRecords();
  /// Erase a single DbgRecord from this marker.
  ///
  /// In an ideal future, we would never erase an assignment in this way, but
  /// it's the equivalent to erasing a debug intrinsic from a block.
  /// \param DR Record in this marker to erase.
  LLVM_ABI void dropOneDbgRecord(DbgRecord *DR);

  /// Empty sentinel marker used when an instruction has no allocated marker.
  ///
  /// We generally act like all llvm Instructions have a range of DbgRecords
  /// attached to them, but in reality sometimes we don't allocate the DbgMarker
  /// to save time and memory, but still have to return ranges of DbgRecords.
  /// When we need to describe such an unallocated DbgRecord range, use this
  /// static markers range instead. This will bite us if someone tries to insert
  /// a DbgRecord in that range, but they should be using the Official (TM) API
  /// for that.
  LLVM_ABI static DbgMarker EmptyDbgMarker;
  /// Return an empty DbgRecord range backed by \c EmptyDbgMarker.
  /// \return An empty iterator range over debug records.
  static iterator_range<simple_ilist<DbgRecord>::iterator>
  getEmptyDbgRecordRange() {
    return make_range(EmptyDbgMarker.StoredDbgRecords.end(),
                      EmptyDbgMarker.StoredDbgRecords.end());
  }
};

/// Print \p Marker to \p OS.
/// \param OS Output stream.
/// \param Marker Marker to print.
/// \return A reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const DbgMarker &Marker) {
  Marker.print(OS);
  return OS;
}

/// Return the range of DbgRecords attached to \p DebugMarker.
///
/// Inlined because it is frequently called, but defined after \c DbgMarker.
/// Users such as Instruction pre-declare it and get an inlineable body here.
/// \param DebugMarker Marker whose records are requested, or null.
/// \return Range of records, or empty if \p DebugMarker is null.
inline iterator_range<simple_ilist<DbgRecord>::iterator>
getDbgRecordRange(DbgMarker *DebugMarker) {
  if (!DebugMarker)
    return DbgMarker::getEmptyDbgRecordRange();
  return DebugMarker->getDbgRecordRange();
}

/// Opaque C API conversions for \c DbgRecord (see CBindingWrapping.h).
/// \param P Opaque debug-record reference.
/// \return The unwrapped \c DbgRecord pointer.
inline DbgRecord *unwrap(LLVMDbgRecordRef P) {
  return reinterpret_cast<DbgRecord *>(P);
}

/// Wrap a \c DbgRecord pointer as an opaque \c LLVMDbgRecordRef.
/// \param P Debug record to wrap.
/// \return An opaque \c LLVMDbgRecordRef for \p P.
inline LLVMDbgRecordRef wrap(const DbgRecord *P) {
  return reinterpret_cast<LLVMDbgRecordRef>(const_cast<DbgRecord *>(P));
}

/// Unwrap an opaque \c LLVMDbgRecordRef as a \c DbgRecord subclass.
/// \param P Opaque debug-record reference.
/// \return \p P cast to subclass \c T.
template <typename T>
inline T *unwrap(LLVMDbgRecordRef P) {
  return cast<T>(unwrap(P));
}

} // namespace llvm

#endif // LLVM_IR_DEBUGPROGRAMINSTRUCTION_H
