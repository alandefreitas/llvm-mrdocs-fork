//===- Value.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_VALUE_H
#define LLVM_SANDBOXIR_VALUE_H

#include "llvm/IR/Metadata.h"
#include "llvm/IR/Value.h"
#include "llvm/SandboxIR/Use.h"
#include "llvm/Support/Compiler.h"

namespace llvm::sandboxir {

// Forward declare all classes to avoid some MSVC build errors.
#define DEF_INSTR(ID, OPC, CLASS) class CLASS;
#define DEF_CONST(ID, CLASS) class CLASS;
#define DEF_USER(ID, CLASS) class CLASS;
#define DEF_DISABLE_AUTO_UNDEF // ValuesDefFilesList.def includes multiple .def
#include "llvm/SandboxIR/ValuesDefFilesList.def"
#undef DEF_INSTR
#undef DEF_CONST
#undef DEF_USER
class Context;
class FuncletPadInst;
class Type;
class GlobalValue;
class GlobalObject;
class Module;
class UnaryInstruction;
class CmpInst;
class IntrinsicInst;
class Operator;
class OverflowingBinaryOperator;
class FPMathOperator;
class Region;
class UncondBrInst;
class CondBrInst;

/// Iterator for the `Use` edges of a Value's users.
/// \Returns a `Use` when dereferenced.
class UserUseIterator {
  sandboxir::Use Use;
  /// Don't let the user create a non-empty UserUseIterator.
  UserUseIterator(const class Use &Use) : Use(Use) {}
  friend class Value; // For constructor

public:
  /// Signed distance between iterators.
  using difference_type = std::ptrdiff_t;
  /// Use type referred to by this iterator.
  using value_type = sandboxir::Use;
  /// Pointer to a Use.
  using pointer = value_type *;
  /// Reference to a Use.
  using reference = value_type &;
  /// Input-iterator traversal category.
  using iterator_category = std::input_iterator_tag;

  /// Construct a singular (empty) iterator.
  UserUseIterator() = default;
  /// Dereference to the current Use edge.
  /// \Returns The current Use edge.
  value_type operator*() const { return Use; }
  /// Advance to the next Use edge.
  /// \Returns This iterator after advancing.
  LLVM_ABI UserUseIterator &operator++();
  /// Return true if this iterator and \p Other refer to the same Use.
  /// \param Other Iterator to compare against.
  /// \Returns True if both iterators refer to the same Use.
  bool operator==(const UserUseIterator &Other) const {
    return Use == Other.Use;
  }
  /// Return true if this iterator and \p Other refer to different Uses.
  /// \param Other Iterator to compare against.
  /// \Returns True if the iterators refer to different Uses.
  bool operator!=(const UserUseIterator &Other) const {
    return !(*this == Other);
  }
  /// Return the current Use edge.
  /// \Returns The current Use edge.
  const sandboxir::Use &getUse() const { return Use; }
};

/// A SandboxIR Value has users. This is the base class.
class Value {
public:
  /// Identifies the concrete SandboxIR Value subclass.
  enum class ClassID : unsigned {
/// Expand to a ClassID enumerator for a Value subclass.
/// \param ID Enumerator name for the ClassID.
/// \param CLASS SandboxIR Value subclass type.
#define DEF_VALUE(ID, CLASS) ID,
/// Expand to a ClassID enumerator for a User subclass.
/// \param ID Enumerator name for the ClassID.
/// \param CLASS SandboxIR User subclass type.
#define DEF_USER(ID, CLASS) ID,
/// Expand to a ClassID enumerator for a Constant subclass.
/// \param ID Enumerator name for the ClassID.
/// \param CLASS SandboxIR Constant subclass type.
#define DEF_CONST(ID, CLASS) ID,
/// Expand to a ClassID enumerator for an Instruction subclass.
/// \param ID Enumerator name for the ClassID.
/// \param OPC Opcode list for the instruction.
/// \param CLASS SandboxIR Instruction subclass type.
#define DEF_INSTR(ID, OPC, CLASS) ID,
#define DEF_DISABLE_AUTO_UNDEF // ValuesDefFilesList.def includes multiple .def
#include "llvm/SandboxIR/ValuesDefFilesList.def"
#undef DEF_VALUE
#undef DEF_USER
#undef DEF_CONST
#undef DEF_INSTR
  };

protected:
  /// Return the string name of subclass identifier \p ID.
  /// \param ID Subclass identifier to stringify.
  /// \Returns The string name of \p ID.
  static const char *getSubclassIDStr(ClassID ID) {
    switch (ID) {
#define DEF_VALUE(ID, CLASS)                                                   \
  case ClassID::ID:                                                            \
    return #ID;
#define DEF_USER(ID, CLASS)                                                    \
  case ClassID::ID:                                                            \
    return #ID;
#define DEF_CONST(ID, CLASS)                                                   \
  case ClassID::ID:                                                            \
    return #ID;
#define DEF_INSTR(ID, OPC, CLASS)                                              \
  case ClassID::ID:                                                            \
    return #ID;
#define DEF_DISABLE_AUTO_UNDEF // ValuesDefFilesList.def includes multiple .def
#include "llvm/SandboxIR/ValuesDefFilesList.def"
#undef DEF_VALUE
#undef DEF_USER
#undef DEF_CONST
#undef DEF_INSTR
    }
    llvm_unreachable("Unimplemented ID");
  }

  /// For isa/dyn_cast.
  ClassID SubclassID;
#ifndef NDEBUG
  /// A unique ID used for forming the name (used for debugging).
  unsigned UID;
#endif
  /// Corresponding LLVM IR Value for this SandboxIR Value.
  ///
  /// NOTE: Some sandboxir Instructions, like Packs, may include more than one
  /// value and in these cases `Val` points to the last instruction in program
  /// order.
  llvm::Value *Val = nullptr;

  friend class Context;               // For getting `Val`.
  friend class User;                  // For getting `Val`.
  friend class Use;                   // For getting `Val`.
  friend class VAArgInst;             // For getting `Val`.
  friend class FreezeInst;            // For getting `Val`.
  friend class FenceInst;             // For getting `Val`.
  friend class SelectInst;            // For getting `Val`.
  friend class ExtractElementInst;    // For getting `Val`.
  friend class InsertElementInst;     // For getting `Val`.
  friend class ShuffleVectorInst;     // For getting `Val`.
  friend class ExtractValueInst;      // For getting `Val`.
  friend class InsertValueInst;       // For getting `Val`.
  friend class UncondBrInst;          // For getting `Val`.
  friend class CondBrInst;            // For getting `Val`.
  friend class LoadInst;              // For getting `Val`.
  friend class StoreInst;             // For getting `Val`.
  friend class ReturnInst;            // For getting `Val`.
  friend class CallBase;              // For getting `Val`.
  friend class CallInst;              // For getting `Val`.
  friend class InvokeInst;            // For getting `Val`.
  friend class CallBrInst;            // For getting `Val`.
  friend class LandingPadInst;        // For getting `Val`.
  friend class FuncletPadInst;        // For getting `Val`.
  friend class CatchPadInst;          // For getting `Val`.
  friend class CleanupPadInst;        // For getting `Val`.
  friend class CatchReturnInst;       // For getting `Val`.
  friend class GetElementPtrInst;     // For getting `Val`.
  friend class ResumeInst;            // For getting `Val`.
  friend class CatchSwitchInst;       // For getting `Val`.
  friend class CleanupReturnInst;     // For getting `Val`.
  friend class SwitchInst;            // For getting `Val`.
  friend class UnaryOperator;         // For getting `Val`.
  friend class BinaryOperator;        // For getting `Val`.
  friend class AtomicRMWInst;         // For getting `Val`.
  friend class AtomicCmpXchgInst;     // For getting `Val`.
  friend class AllocaInst;            // For getting `Val`.
  friend class CastInst;              // For getting `Val`.
  friend class PHINode;               // For getting `Val`.
  friend class UnreachableInst;       // For getting `Val`.
  friend class CatchSwitchAddHandler; // For `Val`.
  friend class CmpInst;               // For getting `Val`.
  friend class ConstantArray;         // For `Val`.
  friend class ConstantStruct;        // For `Val`.
  friend class ConstantVector;        // For `Val`.
  friend class ConstantAggregateZero; // For `Val`.
  friend class ConstantPointerNull;   // For `Val`.
  friend class UndefValue;            // For `Val`.
  friend class PoisonValue;           // For `Val`.
  friend class BlockAddress;          // For `Val`.
  friend class GlobalValue;           // For `Val`.
  friend class DSOLocalEquivalent;    // For `Val`.
  friend class GlobalObject;          // For `Val`.
  friend class GlobalIFunc;           // For `Val`.
  friend class GlobalVariable;        // For `Val`.
  friend class GlobalAlias;           // For `Val`.
  friend class NoCFIValue;            // For `Val`.
  friend class ConstantPtrAuth;       // For `Val`.
  friend class ConstantExpr;          // For `Val`.
  friend class Utils;                 // For `Val`.
  friend class Module;                // For `Val`.
  friend class IntrinsicInst;         // For `Val`.
  friend class Operator;              // For `Val`.
  friend class OverflowingBinaryOperator; // For `Val`.
  friend class FPMathOperator;            // For `Val`.
  // Region needs to manipulate metadata in the underlying LLVM Value, we don't
  // expose metadata in sandboxir.
  friend class Region;
  friend class ScoreBoard; // Needs access to `Val` for the instruction cost.
  friend class ConstantDataArray; // For `Val`
  friend class ConstantDataVector; // For `Val`

#define DEF_INSTR(ID, OPC, CLASS) friend class CLASS;
#define DEF_DISABLE_AUTO_UNDEF // ValuesDefFilesList.def includes multiple .def
#include "llvm/SandboxIR/ValuesDefFilesList.def"
#undef DEF_INSTR

  /// All values point to the context.
  Context &Ctx;
  /// Clear the underlying LLVM Value pointer.
  ///
  /// This is used by eraseFromParent().
  void clearValue() { Val = nullptr; }
  /// Helper that maps LLVM operand-user iterators to SandboxIR types.
  template <typename ItTy, typename SBTy> friend class LLVMOpUserItToSBTy;

  /// Construct a Value of subclass \p SubclassID wrapping LLVM Value \p Val.
  /// \param SubclassID Concrete subclass identifier.
  /// \param Val Underlying LLVM IR value.
  /// \param Ctx SandboxIR context that owns this value.
  LLVM_ABI Value(ClassID SubclassID, llvm::Value *Val, Context &Ctx);
  /// Disable copies.
  /// \param Other Unused copy source.
  Value(const Value &Other) = delete;
  /// Disable copy assignment.
  /// \param Other Unused copy source.
  Value &operator=(const Value &Other) = delete;

public:
  /// Destroy this Value.
  virtual ~Value() = default;
  /// Return the subclass identifier for this Value.
  /// \Returns The subclass identifier for this Value.
  ClassID getSubclassID() const { return SubclassID; }

  /// Iterator over Use edges from this Value's users.
  using use_iterator = UserUseIterator;
  /// Const iterator over Use edges from this Value's users.
  using const_use_iterator = UserUseIterator;

  /// Return an iterator to the first Use of this Value.
  /// \Returns An iterator to the first Use of this Value.
  LLVM_ABI use_iterator use_begin();
  /// Return a const iterator to the first Use of this Value.
  /// \Returns A const iterator to the first Use of this Value.
  const_use_iterator use_begin() const {
    return const_cast<Value *>(this)->use_begin();
  }
  /// Return an iterator past the last Use of this Value.
  /// \Returns An iterator past the last Use of this Value.
  use_iterator use_end() { return use_iterator(Use(nullptr, nullptr, Ctx)); }
  /// Return a const iterator past the last Use of this Value.
  /// \Returns A const iterator past the last Use of this Value.
  const_use_iterator use_end() const {
    return const_cast<Value *>(this)->use_end();
  }

  /// Return a range over the Use edges of this Value.
  /// \Returns A range over the Use edges of this Value.
  iterator_range<use_iterator> uses() {
    return make_range<use_iterator>(use_begin(), use_end());
  }
  /// Return a const range over the Use edges of this Value.
  /// \Returns A const range over the Use edges of this Value.
  iterator_range<const_use_iterator> uses() const {
    return make_range<const_use_iterator>(use_begin(), use_end());
  }

  /// Helper for mapped_iterator.
  struct UseToUser {
    /// Map a Use edge to its User.
    /// \param Use Use edge whose User is returned.
    /// \Returns The User of \p Use.
    User *operator()(const Use &Use) const { return &*Use.getUser(); }
  };

  /// Iterator over Users of this Value.
  using user_iterator = mapped_iterator<sandboxir::UserUseIterator, UseToUser>;
  /// Const iterator over Users of this Value.
  using const_user_iterator = user_iterator;

  /// Return an iterator to the first User of this Value.
  /// \Returns An iterator to the first User of this Value.
  LLVM_ABI user_iterator user_begin();
  /// Return an iterator past the last User of this Value.
  /// \Returns An iterator past the last User of this Value.
  user_iterator user_end() {
    return user_iterator(Use(nullptr, nullptr, Ctx), UseToUser());
  }
  /// Return a const iterator to the first User of this Value.
  /// \Returns A const iterator to the first User of this Value.
  const_user_iterator user_begin() const {
    return const_cast<Value *>(this)->user_begin();
  }
  /// Return a const iterator past the last User of this Value.
  /// \Returns A const iterator past the last User of this Value.
  const_user_iterator user_end() const {
    return const_cast<Value *>(this)->user_end();
  }

  /// Return a range over the Users of this Value.
  /// \Returns A range over the Users of this Value.
  iterator_range<user_iterator> users() {
    return make_range<user_iterator>(user_begin(), user_end());
  }
  /// Return a const range over the Users of this Value.
  /// \Returns A const range over the Users of this Value.
  iterator_range<const_user_iterator> users() const {
    return make_range<const_user_iterator>(user_begin(), user_end());
  }
  /// Return the number of user edges (not necessarily to unique users).
  ///
  /// \Returns the number of user edges (not necessarily to unique users).
  /// WARNING: This is a linear-time operation.
  LLVM_ABI unsigned getNumUses() const;
  /// Return true if this value has N uses or more.
  ///
  /// This is logically equivalent to getNumUses() >= N. WARNING: This can be
  /// expensive, as it is linear to the number of users.
  /// \param Num Minimum number of uses to check for.
  /// \Returns True if this value has at least \p Num uses.
  bool hasNUsesOrMore(unsigned Num) const {
    unsigned Cnt = 0;
    for (auto It = use_begin(), ItE = use_end(); It != ItE; ++It) {
      if (++Cnt >= Num)
        return true;
    }
    return false;
  }
  /// Return true if this Value has exactly N uses.
  /// \param Num Exact number of uses to check for.
  /// \Returns True if this Value has exactly \p Num uses.
  bool hasNUses(unsigned Num) const {
    unsigned Cnt = 0;
    for (auto It = use_begin(), ItE = use_end(); It != ItE; ++It) {
      if (++Cnt > Num)
        return false;
    }
    return Cnt == Num;
  }

  /// Return the SandboxIR type of this Value.
  /// \Returns The SandboxIR type of this Value.
  LLVM_ABI Type *getType() const;

  /// Return the SandboxIR context that owns this Value.
  /// \Returns The SandboxIR context that owns this Value.
  Context &getContext() const { return Ctx; }

  /// Replace uses of this Value with \p OtherV when \p ShouldReplace is true.
  /// \param OtherV Replacement value.
  /// \param ShouldReplace Predicate selecting which uses to replace.
  LLVM_ABI void
  replaceUsesWithIf(Value *OtherV,
                    llvm::function_ref<bool(const Use &)> ShouldReplace);
  /// Replace all uses of this Value with \p Other.
  /// \param Other Replacement value.
  LLVM_ABI void replaceAllUsesWith(Value *Other);

  /// Return the LLVM IR name of the bottom-most LLVM value.
  ///
  /// \Returns the LLVM IR name of the bottom-most LLVM value.
  StringRef getName() const { return Val->getName(); }

#ifndef NDEBUG
  /// Should crash if there is something wrong with the instruction.
  virtual void verify() const = 0;
  /// Returns the unique id in the form 'SB<number>.' like 'SB1.'
  /// \Returns The unique id string in the form 'SB<number>.'.
  std::string getUid() const;
  /// Dump the common header used by Value dumps to \p OS.
  /// \param OS Output stream.
  virtual void dumpCommonHeader(raw_ostream &OS) const;
  /// Dump the common footer used by Value dumps to \p OS.
  /// \param OS Output stream.
  void dumpCommonFooter(raw_ostream &OS) const;
  /// Dump the common prefix used by Value dumps to \p OS.
  /// \param OS Output stream.
  void dumpCommonPrefix(raw_ostream &OS) const;
  /// Dump the common suffix used by Value dumps to \p OS.
  /// \param OS Output stream.
  void dumpCommonSuffix(raw_ostream &OS) const;
  /// Print this Value as an operand to \p OS.
  /// \param OS Output stream.
  void printAsOperandCommon(raw_ostream &OS) const;
  /// Stream-print SandboxIR Value \p V to \p OS.
  /// \param OS Output stream.
  /// \param V Value to print.
  /// \Returns \p OS after printing \p V.
  friend raw_ostream &operator<<(raw_ostream &OS, const sandboxir::Value &V) {
    V.dumpOS(OS);
    return OS;
  }
  /// Dump this Value to \p OS.
  /// \param OS Output stream.
  virtual void dumpOS(raw_ostream &OS) const = 0;
  /// Dump this Value to dbgs().
  LLVM_DUMP_METHOD void dump() const;
#endif
};

/// Opaque SandboxIR wrapper for LLVM values without a dedicated subclass.
class OpaqueValue : public Value {
protected:
  /// Construct an OpaqueValue wrapping LLVM Value \p V.
  /// \param V Underlying LLVM IR value.
  /// \param Ctx SandboxIR context that owns this value.
  OpaqueValue(llvm::Value *V, Context &Ctx)
      : Value(ClassID::OpaqueValue, V, Ctx) {}
  friend class Context; // For constructor.

public:
  /// For isa/dyn_cast.
  /// \param From Value to test for OpaqueValue.
  /// \Returns True if \p From is an OpaqueValue.
  static bool classof(const Value *From) {
    return From->getSubclassID() == ClassID::OpaqueValue;
  }
#ifndef NDEBUG
  /// Verify that this wraps metadata or inline assembly.
  void verify() const override {
    assert((isa<llvm::MetadataAsValue>(Val) || isa<llvm::InlineAsm>(Val)) &&
           "Expected Metadata or InlineAssembly!");
  }
  /// Dump this OpaqueValue to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const override {
    dumpCommonPrefix(OS);
    dumpCommonSuffix(OS);
  }
#endif // NDEBUG
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_VALUE_H
