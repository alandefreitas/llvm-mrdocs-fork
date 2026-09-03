//===- RandomIRBuilder.h - Utils for randomly mutation IR -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provides the Mutator class, which is used to mutate IR for fuzzing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_RANDOMIRBUILDER_H
#define LLVM_FUZZMUTATE_RANDOMIRBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include <random>

namespace llvm {
class AllocaInst;
class BasicBlock;
class Function;
class GlobalVariable;
class Instruction;
class LLVMContext;
class Module;
class Type;
class Value;

namespace fuzzerop {
class SourcePred;
}

/// Pseudo-random number generator type used by RandomIRBuilder.
using RandomEngine = std::mt19937;

/// Helper for selecting and creating IR values while fuzzing.
struct RandomIRBuilder {
  /// Pseudo-random number generator used for sampling.
  RandomEngine Rand;
  /// Types available when creating new values or functions.
  SmallVector<Type *, 16> KnownTypes;

  /// Minimum number of arguments when creating functions with a random arity.
  uint64_t MinArgNum = 0;
  /// Maximum number of arguments when creating functions with a random arity.
  uint64_t MaxArgNum = 5;
  /// Minimum number of functions expected in a module under mutation.
  uint64_t MinFunctionNum = 1;

  /// Construct a builder with a seed and the set of allowed types.
  /// \param Seed Seed for the pseudo-random number generator.
  /// \param AllowedTypes Types that may be used when creating new IR.
  RandomIRBuilder(int Seed, ArrayRef<Type *> AllowedTypes)
      : Rand(Seed), KnownTypes(AllowedTypes) {}

  // TODO: Try to make this a bit less of a random mishmash of functions.

  /// Create a stack memory at the head of the function, store \c Init to the
  /// memory if provided.
  /// \param F Function whose entry block receives the alloca.
  /// \param Ty Element type of the allocated stack memory.
  /// \param Init Optional value to store into the alloca after creating it.
  /// \return The newly created alloca instruction.
  LLVM_ABI AllocaInst *createStackMemory(Function *F, Type *Ty,
                                         Value *Init = nullptr);
  /// Find or create a global variable matching a source predicate.
  ///
  /// It will be initialized by random constants that satisfies \c Pred. It
  /// will also report whether this global variable found or created.
  /// \param M Module that owns or will own the global variable.
  /// \param Srcs Already selected source operands for the surrounding
  ///        operation.
  /// \param Pred Predicate that acceptable global values must satisfy.
  /// \return The chosen or newly created global, and whether it was created.
  LLVM_ABI std::pair<GlobalVariable *, bool>
  findOrCreateGlobalVariable(Module *M, ArrayRef<Value *> Srcs,
                             fuzzerop::SourcePred Pred);
  /// Kinds of values that can be used as an operation source.
  enum SourceType {
    /// An instruction already present in the current basic block.
    SrcFromInstInCurBlock,
    /// A formal argument of the enclosing function.
    FunctionArgument,
    /// An instruction in a basic block that dominates the current block.
    InstInDominator,
    /// A load from an existing or newly created global variable.
    SrcFromGlobalVariable,
    /// A newly created constant or stack value.
    NewConstOrStack,
    /// Sentinel marking the end of valid source kinds.
    EndOfValueSource,
  };
  /// Find or create a source value for an operation operand.
  ///
  /// This either selects an instruction in \c Insts or returns some new
  /// arbitrary Value.
  /// \param BB Basic block in which a new source may be inserted.
  /// \param Insts Candidate instructions to sample as sources.
  /// \return An existing or newly created source value.
  LLVM_ABI Value *findOrCreateSource(BasicBlock &BB,
                                     ArrayRef<Instruction *> Insts);
  /// Find or create a source value matching a predicate.
  ///
  /// This either selects an instruction in \c Insts that matches \c Pred, or
  /// returns some new Value that matches \c Pred. The values in \c Srcs should
  /// be source operands that have already been selected.
  /// \param BB Basic block in which a new source may be inserted.
  /// \param Insts Candidate instructions to sample as sources.
  /// \param Srcs Already selected source operands for the operation.
  /// \param Pred Predicate that acceptable source values must satisfy.
  /// \param allowConstant Whether newly created sources may be constants.
  /// \return An existing or newly created source value matching \c Pred.
  LLVM_ABI Value *findOrCreateSource(BasicBlock &BB,
                                     ArrayRef<Instruction *> Insts,
                                     ArrayRef<Value *> Srcs,
                                     fuzzerop::SourcePred Pred,
                                     bool allowConstant = true);
  /// Create some Value suitable as a source for some operation.
  /// \param BB Basic block in which the new source may be inserted.
  /// \param Insts Instructions available when looking for pointers to load.
  /// \param Srcs Already selected source operands for the operation.
  /// \param Pred Predicate that the new source value must satisfy.
  /// \param allowConstant Whether the new source may be a constant.
  /// \return A newly created source value matching \c Pred.
  LLVM_ABI Value *newSource(BasicBlock &BB, ArrayRef<Instruction *> Insts,
                            ArrayRef<Value *> Srcs, fuzzerop::SourcePred Pred,
                            bool allowConstant = true);

  /// Kinds of users that can consume a value as a sink.
  enum SinkType {
    /// An existing instruction in the current basic block.
    /// TODO: Also consider pointers in function argument.
    SinkToInstInCurBlock,
    /// A store through a pointer found in a dominating block.
    PointersInDominator,
    /// An instruction in a block dominated by the current block.
    InstInDominatee,
    /// A newly created store of the value.
    NewStore,
    /// A store into an existing or newly created global variable.
    SinkToGlobalVariable,
    /// Sentinel marking the end of valid sink kinds.
    EndOfValueSink,
  };
  /// Find a viable user for \c V in \c Insts, which should all be contained in
  /// \c BB. This may also create some new instruction in \c BB and use that.
  /// \param BB Basic block that contains \c Insts and any newly created user.
  /// \param Insts Candidate instructions in \c BB to connect as users of \c V.
  /// \param V Value that needs a sink user.
  /// \return The existing or newly created instruction that uses \c V.
  LLVM_ABI Instruction *connectToSink(BasicBlock &BB,
                                      ArrayRef<Instruction *> Insts, Value *V);
  /// Create a user for \c V in \c BB.
  /// \param BB Basic block in which the new sink is created.
  /// \param Insts Instructions used when searching for a store pointer.
  /// \param V Value to sink into a newly created user.
  /// \return The newly created instruction that uses \c V.
  LLVM_ABI Instruction *newSink(BasicBlock &BB, ArrayRef<Instruction *> Insts,
                                Value *V);
  /// Find a pointer-producing instruction among \c Insts.
  /// \param BB Basic block that contains \c Insts.
  /// \param Insts Candidate instructions to sample for pointer values.
  /// \return A pointer value from \c Insts, or nullptr if none are suitable.
  LLVM_ABI Value *findPointer(BasicBlock &BB, ArrayRef<Instruction *> Insts);
  /// Return a uniformly choosen type from \c AllowedTypes
  /// \return A type chosen uniformly from the builder's known types.
  LLVM_ABI Type *randomType();
  /// Create a function declaration with a fixed number of arguments.
  /// \param M Module that will own the new declaration.
  /// \param ArgNum Number of randomly typed arguments to give the function.
  /// \return The newly created function declaration.
  LLVM_ABI Function *createFunctionDeclaration(Module &M, uint64_t ArgNum);
  /// Create a function declaration with a random number of arguments.
  /// \param M Module that will own the new declaration.
  /// \return The newly created function declaration.
  LLVM_ABI Function *createFunctionDeclaration(Module &M);
  /// Create a function definition with a fixed number of arguments.
  /// \param M Module that will own the new function.
  /// \param ArgNum Number of randomly typed arguments to give the function.
  /// \return The newly created function definition.
  LLVM_ABI Function *createFunctionDefinition(Module &M, uint64_t ArgNum);
  /// Create a function definition with a random number of arguments.
  /// \param M Module that will own the new function.
  /// \return The newly created function definition.
  LLVM_ABI Function *createFunctionDefinition(Module &M);
};

} // namespace llvm

#endif // LLVM_FUZZMUTATE_RANDOMIRBUILDER_H
