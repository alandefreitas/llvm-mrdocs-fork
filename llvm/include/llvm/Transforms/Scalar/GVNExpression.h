//===- GVNExpression.h - GVN Expression classes -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// The header file for the GVN pass that contains expression handling
/// classes
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_GVNEXPRESSION_H
#define LLVM_TRANSFORMS_SCALAR_GVNEXPRESSION_H

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ArrayRecycler.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

namespace llvm {

class BasicBlock;
class Type;

/// Classes representing value-numbered expressions for the GVN pass.
namespace GVNExpression {

/// Discriminator for the Expression class hierarchy.
enum ExpressionType {
  /// Base expression kind with no additional payload.
  ET_Base,
  /// Expression representing a constant value.
  ET_Constant,
  /// Expression representing a variable value.
  ET_Variable,
  /// Expression representing a dead or unavailable value.
  ET_Dead,
  /// Expression representing an unrecognized instruction.
  ET_Unknown,
  /// Sentinel marking the start of the BasicExpression subclass range.
  ET_BasicStart,
  /// Expression with an opcode, type, and operand list.
  ET_Basic,
  /// Expression representing an aggregate insert/extract value.
  ET_AggregateValue,
  /// Expression representing a PHI node.
  ET_Phi,
  /// Sentinel marking the start of the MemoryExpression subclass range.
  ET_MemoryStart,
  /// Expression representing a call.
  ET_Call,
  /// Expression representing a load.
  ET_Load,
  /// Expression representing a store.
  ET_Store,
  /// Sentinel marking the end of the MemoryExpression subclass range.
  ET_MemoryEnd,
  /// Sentinel marking the end of the BasicExpression subclass range.
  ET_BasicEnd
};

/// Base class for value-numbered expressions used by GVN.
class LLVM_ABI Expression {
private:
  ExpressionType EType;
  unsigned Opcode;
  mutable hash_code HashVal = 0;

public:
  /// Construct an expression of kind \p ET with opcode \p O.
  /// \param ET Expression kind discriminator.
  /// \param O Instruction opcode, or a sentinel when unused.
  Expression(ExpressionType ET = ET_Base, unsigned O = ~2U)
      : EType(ET), Opcode(O) {}
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  Expression(const Expression &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  Expression &operator=(const Expression &Other) = delete;
  /// Destroy this expression.
  virtual ~Expression();

  /// Return true if this expression is not equal to \p Other.
  /// \param Other Expression to compare against.
  /// \return True if the expressions are not equal.
  bool operator!=(const Expression &Other) const { return !(*this == Other); }
  /// Return true if this expression equals \p Other for value numbering.
  /// \param Other Expression to compare against.
  /// \return True if the expressions are equal for value numbering.
  bool operator==(const Expression &Other) const {
    if (getOpcode() != Other.getOpcode())
      return false;
    // Compare the expression type for anything but load and store.
    // For load and store we set the opcode to zero to make them equal.
    if (getExpressionType() != ET_Load && getExpressionType() != ET_Store &&
        getExpressionType() != Other.getExpressionType())
      return false;

    return equals(Other);
  }

  /// Return the cached hash, computing it if it has not been stored yet.
  ///
  /// It's theoretically possible for a thing to hash to zero.  In that case,
  /// we will just compute the hash a few extra times, which is no worse that
  /// we did before, which was to compute it always.
  /// \return The cached or newly computed hash value.
  hash_code getComputedHash() const {
    if (static_cast<unsigned>(HashVal) == 0)
      HashVal = getHashValue();
    return HashVal;
  }

  /// Return true if this expression is equal to \p Other ignoring ignored
  /// fields.
  /// \param Other Expression to compare against.
  /// \return True if the expressions are equal ignoring ignored fields.
  virtual bool equals(const Expression &Other) const { return true; }

  /// Return true if the two expressions are exactly the same, including the
  /// normally ignored fields.
  /// \param Other Expression to compare against.
  /// \return True if the expressions are exactly equal.
  virtual bool exactlyEquals(const Expression &Other) const {
    return getExpressionType() == Other.getExpressionType() && equals(Other);
  }

  /// Return the instruction opcode associated with this expression.
  /// \return The instruction opcode, or a sentinel when unused.
  unsigned getOpcode() const { return Opcode; }
  /// Set the instruction opcode associated with this expression.
  /// \param opcode New opcode value.
  void setOpcode(unsigned opcode) { Opcode = opcode; }
  /// Return the ExpressionType discriminator for this expression.
  /// \return The ExpressionType discriminator.
  ExpressionType getExpressionType() const { return EType; }

  /// Return a hash of this expression, deliberately omitting the expression
  /// type.
  /// \return Hash of this expression without the expression type.
  virtual hash_code getHashValue() const { return getOpcode(); }

  /// Print expression-specific fields to \p OS.
  /// \param OS Stream to print to.
  /// \param PrintEType If true, include the expression type in the output.
  virtual void printInternal(raw_ostream &OS, bool PrintEType) const {
    if (PrintEType)
      OS << "etype = " << getExpressionType() << ",";
    OS << "opcode = " << getOpcode() << ", ";
  }

  /// Print this expression to \p OS.
  /// \param OS Stream to print to.
  void print(raw_ostream &OS) const {
    OS << "{ ";
    printInternal(OS, true);
    OS << "}";
  }

  /// Dump this expression to stderr for debugging.
  LLVM_DUMP_METHOD void dump() const;
};

/// Print \p E to \p OS.
/// \param OS Stream to print to.
/// \param E Expression to print.
/// \return The output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const Expression &E) {
  E.print(OS);
  return OS;
}

/// Expression with an opcode, result type, and a list of Value operands.
class LLVM_ABI BasicExpression : public Expression {
private:
  using RecyclerType = ArrayRecycler<Value *>;
  using RecyclerCapacity = RecyclerType::Capacity;

  Value **Operands = nullptr;
  unsigned MaxOperands;
  unsigned NumOperands = 0;
  Type *ValueType = nullptr;

public:
  /// Construct a basic expression that can hold \p NumOperands operands.
  /// \param NumOperands Maximum number of operands to allocate space for.
  BasicExpression(unsigned NumOperands)
      : BasicExpression(NumOperands, ET_Basic) {}
  /// Construct a basic expression of kind \p ET that can hold \p NumOperands
  /// operands.
  /// \param NumOperands Maximum number of operands to allocate space for.
  /// \param ET Expression kind discriminator.
  BasicExpression(unsigned NumOperands, ExpressionType ET)
      : Expression(ET), MaxOperands(NumOperands) {}
  /// Deleted default constructor.
  BasicExpression() = delete;
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  BasicExpression(const BasicExpression &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  BasicExpression &operator=(const BasicExpression &Other) = delete;
  /// Destroy this basic expression.
  ~BasicExpression() override;

  /// Return true if \p EB is a BasicExpression or subclass.
  /// \param EB Expression to test.
  /// \return True if \p EB is a BasicExpression or subclass.
  static bool classof(const Expression *EB) {
    ExpressionType ET = EB->getExpressionType();
    return ET > ET_BasicStart && ET < ET_BasicEnd;
  }

  /// Swap two operands. Used during GVN to put commutative operands in
  /// order.
  /// \param First Index of the first operand to swap.
  /// \param Second Index of the second operand to swap.
  void swapOperands(unsigned First, unsigned Second) {
    std::swap(Operands[First], Operands[Second]);
  }

  /// Return the operand at index \p N.
  /// \param N Zero-based operand index.
  /// \return The operand Value at index \p N.
  Value *getOperand(unsigned N) const {
    assert(Operands && "Operands not allocated");
    assert(N < NumOperands && "Operand out of range");
    return Operands[N];
  }

  /// Set the operand at index \p N to \p V.
  /// \param N Zero-based operand index.
  /// \param V New operand value.
  void setOperand(unsigned N, Value *V) {
    assert(Operands && "Operands not allocated before setting");
    assert(N < NumOperands && "Operand out of range");
    Operands[N] = V;
  }

  /// Return the number of operands currently stored.
  /// \return The number of operands currently stored.
  unsigned getNumOperands() const { return NumOperands; }

  /// Iterator over mutable operand Value pointers.
  using op_iterator = Value **;
  /// Iterator over const operand Value pointers.
  using const_op_iterator = Value *const *;

  /// Return an iterator to the first operand.
  /// \return Iterator to the first operand.
  op_iterator op_begin() { return Operands; }
  /// Return an iterator past the last operand.
  /// \return Iterator past the last operand.
  op_iterator op_end() { return Operands + NumOperands; }
  /// Return a const iterator to the first operand.
  /// \return Const iterator to the first operand.
  const_op_iterator op_begin() const { return Operands; }
  /// Return a const iterator past the last operand.
  /// \return Const iterator past the last operand.
  const_op_iterator op_end() const { return Operands + NumOperands; }
  /// Return a mutable range over the operands.
  /// \return Mutable range covering the operands.
  iterator_range<op_iterator> operands() {
    return iterator_range<op_iterator>(op_begin(), op_end());
  }
  /// Return a const range over the operands.
  /// \return Const range covering the operands.
  iterator_range<const_op_iterator> operands() const {
    return iterator_range<const_op_iterator>(op_begin(), op_end());
  }

  /// Append \p Arg as the next operand.
  /// \param Arg Operand value to append.
  void op_push_back(Value *Arg) {
    assert(NumOperands < MaxOperands && "Tried to add too many operands");
    assert(Operands && "Operandss not allocated before pushing");
    Operands[NumOperands++] = Arg;
  }
  /// Return true if this expression has no operands.
  /// \return True if this expression has no operands.
  bool op_empty() const { return getNumOperands() == 0; }

  /// Allocate storage for operands from \p Recycler using \p Allocator.
  /// \param Recycler Array recycler that manages operand slabs.
  /// \param Allocator Bump allocator backing the recycler.
  void allocateOperands(RecyclerType &Recycler, BumpPtrAllocator &Allocator) {
    assert(!Operands && "Operands already allocated");
    Operands = Recycler.allocate(RecyclerCapacity::get(MaxOperands), Allocator);
  }
  /// Return the operand storage to \p Recycler.
  /// \param Recycler Array recycler that owns the operand slab.
  void deallocateOperands(RecyclerType &Recycler) {
    Recycler.deallocate(RecyclerCapacity::get(MaxOperands), Operands);
  }

  /// Set the result type of this expression to \p T.
  /// \param T Result type.
  void setType(Type *T) { ValueType = T; }
  /// Return the result type of this expression.
  /// \return The result type of this expression.
  Type *getType() const { return ValueType; }

  /// Return true if this expression equals \p Other, including type and
  /// operands.
  /// \param Other Expression to compare against.
  /// \return True if type and operands match.
  bool equals(const Expression &Other) const override {
    if (getOpcode() != Other.getOpcode())
      return false;

    const auto &OE = cast<BasicExpression>(Other);
    return getType() == OE.getType() && NumOperands == OE.NumOperands &&
           std::equal(op_begin(), op_end(), OE.op_begin());
  }

  /// Return a hash combining the base hash, type, and operands.
  /// \return Hash combining the base hash, type, and operands.
  hash_code getHashValue() const override {
    return hash_combine(this->Expression::getHashValue(), ValueType,
                        hash_combine_range(operands()));
  }

  /// Print basic-expression fields to \p OS.
  /// \param OS Stream to print to.
  /// \param PrintEType If true, include the expression type in the output.
  void printInternal(raw_ostream &OS, bool PrintEType) const override {
    if (PrintEType)
      OS << "ExpressionTypeBasic, ";

    this->Expression::printInternal(OS, false);
    OS << "operands = {";
    for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
      OS << "[" << i << "] = ";
      Operands[i]->printAsOperand(OS);
      OS << "  ";
    }
    OS << "} ";
  }
};

/// Output iterator that appends Value* operands to a BasicExpression.
class op_inserter {
private:
  using Container = BasicExpression;

  Container *BE;

public:
  /// Iterator category tag for this output iterator.
  using iterator_category = std::output_iterator_tag;
  /// Value type exposed by this output iterator (unused).
  using value_type = void;
  /// Difference type exposed by this output iterator (unused).
  using difference_type = void;
  /// Pointer type exposed by this output iterator (unused).
  using pointer = void;
  /// Reference type exposed by this output iterator (unused).
  using reference = void;

  /// Construct an inserter that appends to \p E.
  /// \param E Expression that receives appended operands.
  explicit op_inserter(BasicExpression &E) : BE(&E) {}
  /// Construct an inserter that appends to \p E.
  /// \param E Expression that receives appended operands.
  explicit op_inserter(BasicExpression *E) : BE(E) {}

  /// Append \p val to the target expression.
  /// \param val Operand value to append.
  /// \return Reference to this inserter.
  op_inserter &operator=(Value *val) {
    BE->op_push_back(val);
    return *this;
  }
  /// Return this inserter (no-op dereference for output iterators).
  /// \return Reference to this inserter.
  op_inserter &operator*() { return *this; }
  /// Advance this inserter (no-op for output iterators).
  /// \return Reference to this inserter.
  op_inserter &operator++() { return *this; }
  /// Advance this inserter (no-op post-increment for output iterators).
  /// \param Unused Unused postfix-discriminator parameter.
  /// \return Reference to this inserter.
  op_inserter &operator++(int Unused) { return *this; }
};

/// BasicExpression that also tracks a MemorySSA memory leader.
class MemoryExpression : public BasicExpression {
private:
  const MemoryAccess *MemoryLeader;

public:
  /// Construct a memory expression with \p NumOperands, kind \p EType, and
  /// memory leader \p MemoryLeader.
  /// \param NumOperands Maximum number of operands to allocate space for.
  /// \param EType Expression kind discriminator.
  /// \param MemoryLeader MemorySSA access that leads this expression.
  MemoryExpression(unsigned NumOperands, enum ExpressionType EType,
                   const MemoryAccess *MemoryLeader)
      : BasicExpression(NumOperands, EType), MemoryLeader(MemoryLeader) {}
  /// Deleted default constructor.
  MemoryExpression() = delete;
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  MemoryExpression(const MemoryExpression &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  MemoryExpression &operator=(const MemoryExpression &Other) = delete;

  /// Return true if \p EB is a MemoryExpression or subclass.
  /// \param EB Expression to test.
  /// \return True if \p EB is a MemoryExpression or subclass.
  static bool classof(const Expression *EB) {
    return EB->getExpressionType() > ET_MemoryStart &&
           EB->getExpressionType() < ET_MemoryEnd;
  }

  /// Return a hash combining the basic hash and the memory leader.
  /// \return Hash combining the basic hash and the memory leader.
  hash_code getHashValue() const override {
    return hash_combine(this->BasicExpression::getHashValue(), MemoryLeader);
  }

  /// Return true if this expression equals \p Other, including the memory
  /// leader.
  /// \param Other Expression to compare against.
  /// \return True if operands and memory leader match.
  bool equals(const Expression &Other) const override {
    if (!this->BasicExpression::equals(Other))
      return false;
    const MemoryExpression &OtherMCE = cast<MemoryExpression>(Other);

    return MemoryLeader == OtherMCE.MemoryLeader;
  }

  /// Return the MemorySSA access that leads this expression.
  /// \return The MemorySSA access that leads this expression.
  const MemoryAccess *getMemoryLeader() const { return MemoryLeader; }
  /// Set the MemorySSA access that leads this expression.
  /// \param ML New memory leader.
  void setMemoryLeader(const MemoryAccess *ML) { MemoryLeader = ML; }
};

/// MemoryExpression representing a call instruction.
class LLVM_ABI CallExpression final : public MemoryExpression {
private:
  CallInst *Call;

public:
  /// Construct a call expression for \p C with \p NumOperands and memory
  /// leader \p MemoryLeader.
  /// \param NumOperands Maximum number of operands to allocate space for.
  /// \param C Call instruction represented by this expression.
  /// \param MemoryLeader MemorySSA access that leads this expression.
  CallExpression(unsigned NumOperands, CallInst *C,
                 const MemoryAccess *MemoryLeader)
      : MemoryExpression(NumOperands, ET_Call, MemoryLeader), Call(C) {}
  /// Deleted default constructor.
  CallExpression() = delete;
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  CallExpression(const CallExpression &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  CallExpression &operator=(const CallExpression &Other) = delete;
  /// Destroy this call expression.
  ~CallExpression() override;

  /// Return true if \p EB is a CallExpression.
  /// \param EB Expression to test.
  /// \return True if \p EB is a CallExpression.
  static bool classof(const Expression *EB) {
    return EB->getExpressionType() == ET_Call;
  }

  /// Return true if this call expression equals \p Other for value numbering.
  /// \param Other Expression to compare against.
  /// \return True if the call expressions are equal for value numbering.
  bool equals(const Expression &Other) const override;
  /// Return true if this call expression is exactly equal to \p Other,
  /// including the call instruction pointer.
  /// \param Other Expression to compare against.
  /// \return True if the expressions are exactly equal.
  bool exactlyEquals(const Expression &Other) const override {
    return Expression::exactlyEquals(Other) &&
           cast<CallExpression>(Other).Call == Call;
  }

  /// Print call-expression fields to \p OS.
  /// \param OS Stream to print to.
  /// \param PrintEType If true, include the expression type in the output.
  void printInternal(raw_ostream &OS, bool PrintEType) const override {
    if (PrintEType)
      OS << "ExpressionTypeCall, ";
    this->BasicExpression::printInternal(OS, false);
    OS << " represents call at ";
    Call->printAsOperand(OS);
  }
};

/// MemoryExpression representing a load instruction.
class LLVM_ABI LoadExpression final : public MemoryExpression {
private:
  LoadInst *Load;

public:
  /// Construct a load expression for \p L with \p NumOperands and memory
  /// leader \p MemoryLeader.
  /// \param NumOperands Maximum number of operands to allocate space for.
  /// \param L Load instruction represented by this expression.
  /// \param MemoryLeader MemorySSA access that leads this expression.
  LoadExpression(unsigned NumOperands, LoadInst *L,
                 const MemoryAccess *MemoryLeader)
      : LoadExpression(ET_Load, NumOperands, L, MemoryLeader) {}

  /// Construct a load expression of kind \p EType for \p L.
  /// \param EType Expression kind discriminator.
  /// \param NumOperands Maximum number of operands to allocate space for.
  /// \param L Load instruction represented by this expression.
  /// \param MemoryLeader MemorySSA access that leads this expression.
  LoadExpression(enum ExpressionType EType, unsigned NumOperands, LoadInst *L,
                 const MemoryAccess *MemoryLeader)
      : MemoryExpression(NumOperands, EType, MemoryLeader), Load(L) {}

  /// Deleted default constructor.
  LoadExpression() = delete;
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  LoadExpression(const LoadExpression &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  LoadExpression &operator=(const LoadExpression &Other) = delete;
  /// Destroy this load expression.
  ~LoadExpression() override;

  /// Return true if \p EB is a LoadExpression.
  /// \param EB Expression to test.
  /// \return True if \p EB is a LoadExpression.
  static bool classof(const Expression *EB) {
    return EB->getExpressionType() == ET_Load;
  }

  /// Return the load instruction represented by this expression.
  /// \return The load instruction represented by this expression.
  LoadInst *getLoadInst() const { return Load; }
  /// Set the load instruction represented by this expression.
  /// \param L New load instruction.
  void setLoadInst(LoadInst *L) { Load = L; }

  /// Return true if this load expression equals \p Other for value numbering.
  /// \param Other Expression to compare against.
  /// \return True if the load expressions are equal for value numbering.
  bool equals(const Expression &Other) const override;
  /// Return true if this load expression is exactly equal to \p Other,
  /// including the load instruction pointer.
  /// \param Other Expression to compare against.
  /// \return True if the expressions are exactly equal.
  bool exactlyEquals(const Expression &Other) const override {
    return Expression::exactlyEquals(Other) &&
           cast<LoadExpression>(Other).getLoadInst() == getLoadInst();
  }

  /// Print load-expression fields to \p OS.
  /// \param OS Stream to print to.
  /// \param PrintEType If true, include the expression type in the output.
  void printInternal(raw_ostream &OS, bool PrintEType) const override {
    if (PrintEType)
      OS << "ExpressionTypeLoad, ";
    this->BasicExpression::printInternal(OS, false);
    OS << " represents Load at ";
    Load->printAsOperand(OS);
    OS << " with MemoryLeader " << *getMemoryLeader();
  }
};

/// MemoryExpression representing a store instruction.
class LLVM_ABI StoreExpression final : public MemoryExpression {
private:
  StoreInst *Store;
  Value *StoredValue;

public:
  /// Construct a store expression for \p S storing \p StoredValue.
  /// \param NumOperands Maximum number of operands to allocate space for.
  /// \param S Store instruction represented by this expression.
  /// \param StoredValue Value being stored.
  /// \param MemoryLeader MemorySSA access that leads this expression.
  StoreExpression(unsigned NumOperands, StoreInst *S, Value *StoredValue,
                  const MemoryAccess *MemoryLeader)
      : MemoryExpression(NumOperands, ET_Store, MemoryLeader), Store(S),
        StoredValue(StoredValue) {}
  /// Deleted default constructor.
  StoreExpression() = delete;
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  StoreExpression(const StoreExpression &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  StoreExpression &operator=(const StoreExpression &Other) = delete;
  /// Destroy this store expression.
  ~StoreExpression() override;

  /// Return true if \p EB is a StoreExpression.
  /// \param EB Expression to test.
  /// \return True if \p EB is a StoreExpression.
  static bool classof(const Expression *EB) {
    return EB->getExpressionType() == ET_Store;
  }

  /// Return the store instruction represented by this expression.
  /// \return The store instruction represented by this expression.
  StoreInst *getStoreInst() const { return Store; }
  /// Return the value being stored by this expression.
  /// \return The value being stored by this expression.
  Value *getStoredValue() const { return StoredValue; }

  /// Return true if this store expression equals \p Other for value
  /// numbering.
  /// \param Other Expression to compare against.
  /// \return True if the store expressions are equal for value numbering.
  bool equals(const Expression &Other) const override;

  /// Return true if this store expression is exactly equal to \p Other,
  /// including the store instruction pointer.
  /// \param Other Expression to compare against.
  /// \return True if the expressions are exactly equal.
  bool exactlyEquals(const Expression &Other) const override {
    return Expression::exactlyEquals(Other) &&
           cast<StoreExpression>(Other).getStoreInst() == getStoreInst();
  }

  /// Print store-expression fields to \p OS.
  /// \param OS Stream to print to.
  /// \param PrintEType If true, include the expression type in the output.
  void printInternal(raw_ostream &OS, bool PrintEType) const override {
    if (PrintEType)
      OS << "ExpressionTypeStore, ";
    this->BasicExpression::printInternal(OS, false);
    OS << " represents Store  " << *Store;
    OS << " with StoredValue ";
    StoredValue->printAsOperand(OS);
    OS << " and MemoryLeader " << *getMemoryLeader();
  }
};

/// BasicExpression representing insertvalue/extractvalue aggregate operations.
class LLVM_ABI AggregateValueExpression final : public BasicExpression {
private:
  unsigned MaxIntOperands;
  unsigned NumIntOperands = 0;
  unsigned *IntOperands = nullptr;

public:
  /// Construct an aggregate-value expression with value and integer operands.
  /// \param NumOperands Maximum number of Value operands.
  /// \param NumIntOperands Maximum number of integer index operands.
  AggregateValueExpression(unsigned NumOperands, unsigned NumIntOperands)
      : BasicExpression(NumOperands, ET_AggregateValue),
        MaxIntOperands(NumIntOperands) {}
  /// Deleted default constructor.
  AggregateValueExpression() = delete;
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  AggregateValueExpression(const AggregateValueExpression &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  AggregateValueExpression &
  operator=(const AggregateValueExpression &Other) = delete;
  /// Destroy this aggregate-value expression.
  ~AggregateValueExpression() override;

  /// Return true if \p EB is an AggregateValueExpression.
  /// \param EB Expression to test.
  /// \return True if \p EB is an AggregateValueExpression.
  static bool classof(const Expression *EB) {
    return EB->getExpressionType() == ET_AggregateValue;
  }

  /// Iterator over mutable integer index operands.
  using int_arg_iterator = unsigned *;
  /// Iterator over const integer index operands.
  using const_int_arg_iterator = const unsigned *;

  /// Return an iterator to the first integer operand.
  /// \return Iterator to the first integer operand.
  int_arg_iterator int_op_begin() { return IntOperands; }
  /// Return an iterator past the last integer operand.
  /// \return Iterator past the last integer operand.
  int_arg_iterator int_op_end() { return IntOperands + NumIntOperands; }
  /// Return a const iterator to the first integer operand.
  /// \return Const iterator to the first integer operand.
  const_int_arg_iterator int_op_begin() const { return IntOperands; }
  /// Return a const iterator past the last integer operand.
  /// \return Const iterator past the last integer operand.
  const_int_arg_iterator int_op_end() const {
    return IntOperands + NumIntOperands;
  }
  /// Return the number of integer operands currently stored.
  /// \return The number of integer operands currently stored.
  unsigned int_op_size() const { return NumIntOperands; }
  /// Return true if this expression has no integer operands.
  /// \return True if this expression has no integer operands.
  bool int_op_empty() const { return NumIntOperands == 0; }
  /// Append \p IntOperand as the next integer index operand.
  /// \param IntOperand Integer index to append.
  void int_op_push_back(unsigned IntOperand) {
    assert(NumIntOperands < MaxIntOperands &&
           "Tried to add too many int operands");
    assert(IntOperands && "Operands not allocated before pushing");
    IntOperands[NumIntOperands++] = IntOperand;
  }

  /// Allocate storage for integer operands from \p Allocator.
  /// \param Allocator Bump allocator used for the integer operand array.
  void allocateIntOperands(BumpPtrAllocator &Allocator) {
    assert(!IntOperands && "Operands already allocated");
    IntOperands = Allocator.Allocate<unsigned>(MaxIntOperands);
  }

  /// Return true if this expression equals \p Other, including integer
  /// operands.
  /// \param Other Expression to compare against.
  /// \return True if value and integer operands match.
  bool equals(const Expression &Other) const override {
    if (!this->BasicExpression::equals(Other))
      return false;
    const AggregateValueExpression &OE = cast<AggregateValueExpression>(Other);
    return NumIntOperands == OE.NumIntOperands &&
           std::equal(int_op_begin(), int_op_end(), OE.int_op_begin());
  }

  /// Return a hash combining the basic hash and integer operands.
  /// \return Hash combining the basic hash and integer operands.
  hash_code getHashValue() const override {
    return hash_combine(this->BasicExpression::getHashValue(),
                        hash_combine_range(int_op_begin(), int_op_end()));
  }

  /// Print aggregate-value expression fields to \p OS.
  /// \param OS Stream to print to.
  /// \param PrintEType If true, include the expression type in the output.
  void printInternal(raw_ostream &OS, bool PrintEType) const override {
    if (PrintEType)
      OS << "ExpressionTypeAggregateValue, ";
    this->BasicExpression::printInternal(OS, false);
    OS << ", intoperands = {";
    for (unsigned i = 0, e = int_op_size(); i != e; ++i) {
      OS << "[" << i << "] = " << IntOperands[i] << "  ";
    }
    OS << "}";
  }
};

/// Output iterator that appends integer index operands to an
/// AggregateValueExpression.
class int_op_inserter {
private:
  using Container = AggregateValueExpression;

  Container *AVE;

public:
  /// Iterator category tag for this output iterator.
  using iterator_category = std::output_iterator_tag;
  /// Value type exposed by this output iterator (unused).
  using value_type = void;
  /// Difference type exposed by this output iterator (unused).
  using difference_type = void;
  /// Pointer type exposed by this output iterator (unused).
  using pointer = void;
  /// Reference type exposed by this output iterator (unused).
  using reference = void;

  /// Construct an inserter that appends to \p E.
  /// \param E Expression that receives appended integer operands.
  explicit int_op_inserter(AggregateValueExpression &E) : AVE(&E) {}
  /// Construct an inserter that appends to \p E.
  /// \param E Expression that receives appended integer operands.
  explicit int_op_inserter(AggregateValueExpression *E) : AVE(E) {}

  /// Append \p val to the target expression.
  /// \param val Integer index operand to append.
  /// \return Reference to this inserter.
  int_op_inserter &operator=(unsigned int val) {
    AVE->int_op_push_back(val);
    return *this;
  }
  /// Return this inserter (no-op dereference for output iterators).
  /// \return Reference to this inserter.
  int_op_inserter &operator*() { return *this; }
  /// Advance this inserter (no-op for output iterators).
  /// \return Reference to this inserter.
  int_op_inserter &operator++() { return *this; }
  /// Advance this inserter (no-op post-increment for output iterators).
  /// \param Unused Unused postfix-discriminator parameter.
  /// \return Reference to this inserter.
  int_op_inserter &operator++(int Unused) { return *this; }
};

/// BasicExpression representing a PHI node in a specific basic block.
class LLVM_ABI PHIExpression final : public BasicExpression {
private:
  BasicBlock *BB;

public:
  /// Construct a PHI expression for block \p B with \p NumOperands.
  /// \param NumOperands Maximum number of operands to allocate space for.
  /// \param B Basic block owning the PHI.
  PHIExpression(unsigned NumOperands, BasicBlock *B)
      : BasicExpression(NumOperands, ET_Phi), BB(B) {}
  /// Deleted default constructor.
  PHIExpression() = delete;
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  PHIExpression(const PHIExpression &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  PHIExpression &operator=(const PHIExpression &Other) = delete;
  /// Destroy this PHI expression.
  ~PHIExpression() override;

  /// Return true if \p EB is a PHIExpression.
  /// \param EB Expression to test.
  /// \return True if \p EB is a PHIExpression.
  static bool classof(const Expression *EB) {
    return EB->getExpressionType() == ET_Phi;
  }

  /// Return true if this PHI expression equals \p Other, including the block.
  /// \param Other Expression to compare against.
  /// \return True if operands and basic block match.
  bool equals(const Expression &Other) const override {
    if (!this->BasicExpression::equals(Other))
      return false;
    const PHIExpression &OE = cast<PHIExpression>(Other);
    return BB == OE.BB;
  }

  /// Return a hash combining the basic hash and the basic block.
  /// \return Hash combining the basic hash and the basic block.
  hash_code getHashValue() const override {
    return hash_combine(this->BasicExpression::getHashValue(), BB);
  }

  /// Print PHI-expression fields to \p OS.
  /// \param OS Stream to print to.
  /// \param PrintEType If true, include the expression type in the output.
  void printInternal(raw_ostream &OS, bool PrintEType) const override {
    if (PrintEType)
      OS << "ExpressionTypePhi, ";
    this->BasicExpression::printInternal(OS, false);
    OS << "bb = " << BB;
  }
};

/// Expression representing a dead or unavailable value.
class DeadExpression final : public Expression {
public:
  /// Construct a dead expression.
  DeadExpression() : Expression(ET_Dead) {}
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  DeadExpression(const DeadExpression &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  DeadExpression &operator=(const DeadExpression &Other) = delete;

  /// Return true if \p E is a DeadExpression.
  /// \param E Expression to test.
  /// \return True if \p E is a DeadExpression.
  static bool classof(const Expression *E) {
    return E->getExpressionType() == ET_Dead;
  }
};

/// Expression representing a variable Value.
class VariableExpression final : public Expression {
private:
  Value *VariableValue;

public:
  /// Construct a variable expression for \p V.
  /// \param V Value represented by this expression.
  VariableExpression(Value *V) : Expression(ET_Variable), VariableValue(V) {}
  /// Deleted default constructor.
  VariableExpression() = delete;
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  VariableExpression(const VariableExpression &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  VariableExpression &operator=(const VariableExpression &Other) = delete;

  /// Return true if \p EB is a VariableExpression.
  /// \param EB Expression to test.
  /// \return True if \p EB is a VariableExpression.
  static bool classof(const Expression *EB) {
    return EB->getExpressionType() == ET_Variable;
  }

  /// Return the Value represented by this expression.
  /// \return The Value represented by this expression.
  Value *getVariableValue() const { return VariableValue; }
  /// Set the Value represented by this expression.
  /// \param V New variable value.
  void setVariableValue(Value *V) { VariableValue = V; }

  /// Return true if this variable expression equals \p Other.
  /// \param Other Expression to compare against.
  /// \return True if the variable values match.
  bool equals(const Expression &Other) const override {
    const VariableExpression &OC = cast<VariableExpression>(Other);
    return VariableValue == OC.VariableValue;
  }

  /// Return a hash combining the base hash, type, and variable value.
  /// \return Hash combining the base hash, type, and variable value.
  hash_code getHashValue() const override {
    return hash_combine(this->Expression::getHashValue(),
                        VariableValue->getType(), VariableValue);
  }

  /// Print variable-expression fields to \p OS.
  /// \param OS Stream to print to.
  /// \param PrintEType If true, include the expression type in the output.
  void printInternal(raw_ostream &OS, bool PrintEType) const override {
    if (PrintEType)
      OS << "ExpressionTypeVariable, ";
    this->Expression::printInternal(OS, false);
    OS << " variable = " << *VariableValue;
  }
};

/// Expression representing a Constant.
class ConstantExpression final : public Expression {
private:
  Constant *ConstantValue = nullptr;

public:
  /// Construct an empty constant expression.
  ConstantExpression() : Expression(ET_Constant) {}
  /// Construct a constant expression for \p constantValue.
  /// \param constantValue Constant represented by this expression.
  ConstantExpression(Constant *constantValue)
      : Expression(ET_Constant), ConstantValue(constantValue) {}
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  ConstantExpression(const ConstantExpression &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  ConstantExpression &operator=(const ConstantExpression &Other) = delete;

  /// Return true if \p EB is a ConstantExpression.
  /// \param EB Expression to test.
  /// \return True if \p EB is a ConstantExpression.
  static bool classof(const Expression *EB) {
    return EB->getExpressionType() == ET_Constant;
  }

  /// Return the Constant represented by this expression.
  /// \return The Constant represented by this expression.
  Constant *getConstantValue() const { return ConstantValue; }
  /// Set the Constant represented by this expression.
  /// \param V New constant value.
  void setConstantValue(Constant *V) { ConstantValue = V; }

  /// Return true if this constant expression equals \p Other.
  /// \param Other Expression to compare against.
  /// \return True if the constant values match.
  bool equals(const Expression &Other) const override {
    const ConstantExpression &OC = cast<ConstantExpression>(Other);
    return ConstantValue == OC.ConstantValue;
  }

  /// Return a hash combining the base hash, type, and constant value.
  /// \return Hash combining the base hash, type, and constant value.
  hash_code getHashValue() const override {
    return hash_combine(this->Expression::getHashValue(),
                        ConstantValue->getType(), ConstantValue);
  }

  /// Print constant-expression fields to \p OS.
  /// \param OS Stream to print to.
  /// \param PrintEType If true, include the expression type in the output.
  void printInternal(raw_ostream &OS, bool PrintEType) const override {
    if (PrintEType)
      OS << "ExpressionTypeConstant, ";
    this->Expression::printInternal(OS, false);
    OS << " constant = " << *ConstantValue;
  }
};

/// Expression representing an unrecognized instruction.
class UnknownExpression final : public Expression {
private:
  Instruction *Inst;

public:
  /// Construct an unknown expression for instruction \p I.
  /// \param I Instruction represented by this expression.
  UnknownExpression(Instruction *I) : Expression(ET_Unknown), Inst(I) {}
  /// Deleted default constructor.
  UnknownExpression() = delete;
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  UnknownExpression(const UnknownExpression &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  UnknownExpression &operator=(const UnknownExpression &Other) = delete;

  /// Return true if \p EB is an UnknownExpression.
  /// \param EB Expression to test.
  /// \return True if \p EB is an UnknownExpression.
  static bool classof(const Expression *EB) {
    return EB->getExpressionType() == ET_Unknown;
  }

  /// Return the instruction represented by this expression.
  /// \return The instruction represented by this expression.
  Instruction *getInstruction() const { return Inst; }
  /// Set the instruction represented by this expression.
  /// \param I New instruction.
  void setInstruction(Instruction *I) { Inst = I; }

  /// Return true if this unknown expression equals \p Other.
  /// \param Other Expression to compare against.
  /// \return True if the instructions match.
  bool equals(const Expression &Other) const override {
    const auto &OU = cast<UnknownExpression>(Other);
    return Inst == OU.Inst;
  }

  /// Return a hash combining the base hash and the instruction.
  /// \return Hash combining the base hash and the instruction.
  hash_code getHashValue() const override {
    return hash_combine(this->Expression::getHashValue(), Inst);
  }

  /// Print unknown-expression fields to \p OS.
  /// \param OS Stream to print to.
  /// \param PrintEType If true, include the expression type in the output.
  void printInternal(raw_ostream &OS, bool PrintEType) const override {
    if (PrintEType)
      OS << "ExpressionTypeUnknown, ";
    this->Expression::printInternal(OS, false);
    OS << " inst = " << *Inst;
  }
};

} // end namespace GVNExpression

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_GVNEXPRESSION_H
