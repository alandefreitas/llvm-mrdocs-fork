//===-- IRMutator.h - Mutation engine for fuzzing IR ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provides the IRMutator class, which drives mutations on IR based on a
// configurable set of strategies. Some common strategies are also included
// here.
//
// Fuzzer-friendly (de)serialization functions are also provided, as these
// are usually needed when mutating IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_IRMUTATOR_H
#define LLVM_FUZZMUTATE_IRMUTATOR_H

#include "llvm/FuzzMutate/OpDescriptor.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <optional>

namespace llvm {
class BasicBlock;
class Function;
class Instruction;
class Module;

struct RandomIRBuilder;

/// Base class for describing how to mutate a module. mutation functions for
/// each IR unit forward to the contained unit.
class LLVM_ABI IRMutationStrategy {
public:
  /// Destroy this IR mutation strategy.
  virtual ~IRMutationStrategy() = default;

  /// Provide a weight to bias towards choosing this strategy for a mutation.
  ///
  /// The value of the weight is arbitrary, but a good default is "the number of
  /// distinct ways in which this strategy can mutate a unit". This can also be
  /// used to prefer strategies that shrink the overall size of the result when
  /// we start getting close to \c MaxSize.
  /// \param CurrentSize Current size of the module being mutated.
  /// \param MaxSize Maximum allowed module size.
  /// \param CurrentWeight Combined weight of strategies already considered.
  /// \return Weight used to bias selection of this strategy.
  virtual uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                             uint64_t CurrentWeight) = 0;

  /// Mutate a module, by default forwarding to a contained function.
  /// \param M Module to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  virtual void mutate(Module &M, RandomIRBuilder &IB);
  /// Mutate a function, by default forwarding to a contained basic block.
  /// \param F Function to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  virtual void mutate(Function &F, RandomIRBuilder &IB);
  /// Mutate a basic block, by default forwarding to a contained instruction.
  /// \param BB Basic block to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  virtual void mutate(BasicBlock &BB, RandomIRBuilder &IB);
  /// Mutate an instruction; the default implementation is unreachable.
  /// \param I Instruction to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  virtual void mutate(Instruction &I, RandomIRBuilder &IB) {
    llvm_unreachable("Strategy does not implement any mutators");
  }
};

/// Callback that returns an allowed IR type for a given LLVM context.
using TypeGetter = std::function<Type *(LLVMContext &)>;

/// Entry point for configuring and running IR mutations.
class IRMutator {
  std::vector<TypeGetter> AllowedTypes;
  std::vector<std::unique_ptr<IRMutationStrategy>> Strategies;

public:
  /// Construct an IR mutator with the given allowed types and strategies.
  /// \param AllowedTypes Callbacks that produce types available during mutation.
  /// \param Strategies Mutation strategies to choose among when mutating.
  IRMutator(std::vector<TypeGetter> &&AllowedTypes,
            std::vector<std::unique_ptr<IRMutationStrategy>> &&Strategies)
      : AllowedTypes(std::move(AllowedTypes)),
        Strategies(std::move(Strategies)) {}

  /// Calculate the size of module as the number of objects in it, i.e.
  /// instructions, basic blocks, functions, and aliases.
  ///
  /// \param M module
  /// \return number of objects in module
  LLVM_ABI static size_t getModuleSize(const Module &M);

  /// Mutate given module. No change will be made if no strategy is selected.
  ///
  /// \param M  module to mutate
  /// \param Seed seed for random mutation
  /// \param MaxSize max module size (see getModuleSize)
  LLVM_ABI void mutateModule(Module &M, int Seed, size_t MaxSize);
};

/// Strategy that injects operations into the function.
class LLVM_ABI InjectorIRStrategy : public IRMutationStrategy {
  std::vector<fuzzerop::OpDescriptor> Operations;

  std::optional<fuzzerop::OpDescriptor> chooseOperation(Value *Src,
                                                        RandomIRBuilder &IB);

public:
  /// Construct an injector that uses the default set of operations.
  InjectorIRStrategy() : Operations(getDefaultOps()) {}
  /// Construct an injector that uses the given operation descriptors.
  /// \param Operations Operation descriptors available for injection.
  InjectorIRStrategy(std::vector<fuzzerop::OpDescriptor> &&Operations)
      : Operations(std::move(Operations)) {}
  /// Return the default set of operation descriptors for injection.
  /// \return Default operation descriptors available for injection.
  static std::vector<fuzzerop::OpDescriptor> getDefaultOps();

  /// Provide a weight based on the number of available operations.
  /// \param CurrentSize Current size of the module being mutated.
  /// \param MaxSize Maximum allowed module size.
  /// \param CurrentWeight Combined weight of strategies already considered.
  /// \return Number of available operations, used as this strategy's weight.
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return Operations.size();
  }

  /// Bring base-class mutate overloads into this class.
  using IRMutationStrategy::mutate;
  /// Inject operations into a function.
  /// \param F Function to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  void mutate(Function &F, RandomIRBuilder &IB) override;
  /// Inject operations into a basic block.
  /// \param BB Basic block to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  void mutate(BasicBlock &BB, RandomIRBuilder &IB) override;
};

/// Strategy that deletes instructions when the Module is too large.
class LLVM_ABI InstDeleterIRStrategy : public IRMutationStrategy {
public:
  /// Provide a weight that grows when the module size approaches the limit.
  /// \param CurrentSize Current size of the module being mutated.
  /// \param MaxSize Maximum allowed module size.
  /// \param CurrentWeight Combined weight of strategies already considered.
  /// \return Weight used to bias selection of this strategy.
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override;

  /// Bring base-class mutate overloads into this class.
  using IRMutationStrategy::mutate;
  /// Delete instructions from a function when size pressure is high.
  /// \param F Function to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  void mutate(Function &F, RandomIRBuilder &IB) override;
  /// Delete or neutralize a selected instruction.
  /// \param Inst Instruction to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  void mutate(Instruction &Inst, RandomIRBuilder &IB) override;
};

/// Strategy that modifies instruction attributes and operands.
class LLVM_ABI InstModificationIRStrategy : public IRMutationStrategy {
public:
  /// Provide a fixed weight for instruction modification.
  /// \param CurrentSize Current size of the module being mutated.
  /// \param MaxSize Maximum allowed module size.
  /// \param CurrentWeight Combined weight of strategies already considered.
  /// \return Fixed weight used to bias selection of this strategy.
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return 4;
  }

  /// Bring base-class mutate overloads into this class.
  using IRMutationStrategy::mutate;
  /// Modify attributes or operands of an instruction.
  /// \param Inst Instruction to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  void mutate(Instruction &Inst, RandomIRBuilder &IB) override;
};

/// Strategy that generates new function calls and inserts function signatures
/// to the modules. If any signatures are present in the module it will be
/// called.
class LLVM_ABI InsertFunctionStrategy : public IRMutationStrategy {
public:
  /// Provide a fixed weight for function-call insertion.
  /// \param CurrentSize Current size of the module being mutated.
  /// \param MaxSize Maximum allowed module size.
  /// \param CurrentWeight Combined weight of strategies already considered.
  /// \return Fixed weight used to bias selection of this strategy.
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return 10;
  }

  /// Bring base-class mutate overloads into this class.
  using IRMutationStrategy::mutate;
  /// Insert a call to an existing or newly declared function.
  /// \param BB Basic block to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  void mutate(BasicBlock &BB, RandomIRBuilder &IB) override;
};

/// Strategy to split a random block and insert a random CFG in between.
class LLVM_ABI InsertCFGStrategy : public IRMutationStrategy {
private:
  uint64_t MaxNumCases;
  enum CFGToSink { Return, DirectSink, SinkOrSelfLoop, EndOfCFGToLink };

public:
  /// Construct a CFG-insertion strategy with an optional case limit.
  /// \param MNC Maximum number of switch cases to consider when building CFG.
  InsertCFGStrategy(uint64_t MNC = 8) : MaxNumCases(MNC){};
  /// Provide a fixed weight for CFG insertion.
  /// \param CurrentSize Current size of the module being mutated.
  /// \param MaxSize Maximum allowed module size.
  /// \param CurrentWeight Combined weight of strategies already considered.
  /// \return Fixed weight used to bias selection of this strategy.
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return 5;
  }

  /// Split a block and insert a random control-flow graph in between.
  /// \param BB Basic block to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  void mutate(BasicBlock &BB, RandomIRBuilder &IB) override;

private:
  void connectBlocksToSink(ArrayRef<BasicBlock *> Blocks, BasicBlock *Sink,
                           RandomIRBuilder &IB);
};

/// Strategy to insert PHI Nodes at the head of each basic block.
class LLVM_ABI InsertPHIStrategy : public IRMutationStrategy {
public:
  /// Provide a fixed weight for PHI-node insertion.
  /// \param CurrentSize Current size of the module being mutated.
  /// \param MaxSize Maximum allowed module size.
  /// \param CurrentWeight Combined weight of strategies already considered.
  /// \return Fixed weight used to bias selection of this strategy.
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return 2;
  }

  /// Insert PHI nodes at the head of a basic block.
  /// \param BB Basic block to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  void mutate(BasicBlock &BB, RandomIRBuilder &IB) override;
};

/// Strategy to select a random instruction and add a new sink (user) to it to
/// increate data dependency.
class LLVM_ABI SinkInstructionStrategy : public IRMutationStrategy {
public:
  /// Provide a fixed weight for sinking instructions.
  /// \param CurrentSize Current size of the module being mutated.
  /// \param MaxSize Maximum allowed module size.
  /// \param CurrentWeight Combined weight of strategies already considered.
  /// \return Fixed weight used to bias selection of this strategy.
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return 2;
  }

  /// Select an instruction in a function and add a new sink user.
  /// \param F Function to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  void mutate(Function &F, RandomIRBuilder &IB) override;
  /// Select an instruction in a basic block and add a new sink user.
  /// \param BB Basic block to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  void mutate(BasicBlock &BB, RandomIRBuilder &IB) override;
};

/// Strategy to randomly select a block and shuffle the operations without
/// affecting data dependency.
class LLVM_ABI ShuffleBlockStrategy : public IRMutationStrategy {
public:
  /// Provide a fixed weight for block shuffling.
  /// \param CurrentSize Current size of the module being mutated.
  /// \param MaxSize Maximum allowed module size.
  /// \param CurrentWeight Combined weight of strategies already considered.
  /// \return Fixed weight used to bias selection of this strategy.
  uint64_t getWeight(size_t CurrentSize, size_t MaxSize,
                     uint64_t CurrentWeight) override {
    return 2;
  }

  /// Shuffle instructions in a basic block without breaking dependencies.
  /// \param BB Basic block to mutate.
  /// \param IB Builder used to select and create IR during mutation.
  void mutate(BasicBlock &BB, RandomIRBuilder &IB) override;
};

/// Fuzzer friendly interface for the llvm bitcode parser.
///
/// \param Data Bitcode we are going to parse
/// \param Size Size of the 'Data' in bytes
/// \param Context LLVM context used to own the parsed module
/// \return New module or nullptr in case of error
LLVM_ABI std::unique_ptr<Module> parseModule(const uint8_t *Data, size_t Size,
                                             LLVMContext &Context);

/// Fuzzer friendly interface for the llvm bitcode printer.
///
/// \param M Module to print
/// \param Dest Location to store serialized module
/// \param MaxSize Size of the destination buffer
/// \return Number of bytes that were written. When module size exceeds MaxSize
///         returns 0 and leaves Dest unchanged.
LLVM_ABI size_t writeModule(const Module &M, uint8_t *Dest, size_t MaxSize);

/// Try to parse module and verify it. May output verification errors to the
/// errs().
/// \param Data Bitcode we are going to parse
/// \param Size Size of the 'Data' in bytes
/// \param Context LLVM context used to own the parsed module
/// \return New module or nullptr in case of error.
LLVM_ABI std::unique_ptr<Module>
parseAndVerify(const uint8_t *Data, size_t Size, LLVMContext &Context);

} // namespace llvm

#endif // LLVM_FUZZMUTATE_IRMUTATOR_H
