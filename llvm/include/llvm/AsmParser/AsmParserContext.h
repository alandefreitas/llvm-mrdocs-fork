//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_ASMPARSERCONTEXT_H
#define LLVM_ASMPARSER_ASMPARSERCONTEXT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntervalMap.h"
#include "llvm/AsmParser/FileLoc.h"
#include "llvm/IR/Value.h"
#include <optional>

namespace llvm {
class BasicBlock;

/// Registry of file location information for LLVM IR constructs.
///
/// This class provides access to the file location information
/// for various LLVM IR constructs. Currently, it supports Function,
/// BasicBlock and Instruction locations.
///
/// When available, it can answer queries about what is at a given
/// file location, as well as where in a file a given IR construct
/// is.
///
/// This information is optionally emitted by the LLParser while
/// it reads LLVM textual IR.
class AsmParserContext {
  using FMap =
      IntervalMap<FileLoc, Function *,
                  IntervalMapImpl::NodeSizer<FileLoc, Function *>::LeafSize,
                  IntervalMapHalfOpenInfo<FileLoc>>;

  DenseMap<Function *, FileLocRange> Functions;
  FMap::Allocator FAllocator;
  FMap FunctionsInverse = FMap(FAllocator);

  DenseMap<BasicBlock *, FileLocRange> Blocks;
  using BBMap =
      IntervalMap<FileLoc, BasicBlock *,
                  IntervalMapImpl::NodeSizer<FileLoc, BasicBlock *>::LeafSize,
                  IntervalMapHalfOpenInfo<FileLoc>>;
  BBMap::Allocator BBAllocator;
  BBMap BlocksInverse = BBMap(BBAllocator);
  DenseMap<Value *, FileLocRange> InstructionsAndArguments;
  using VMap =
      IntervalMap<FileLoc, Value *,
                  IntervalMapImpl::NodeSizer<FileLoc, Value *>::LeafSize,
                  IntervalMapHalfOpenInfo<FileLoc>>;
  VMap::Allocator VAllocator;
  VMap InstructionsAndArgumentsInverse = VMap(VAllocator);

  VMap ReferencedValues = VMap(VAllocator);

public:
  /// Get the recorded source range of a function.
  ///
  /// \param F Function whose location is requested.
  /// \return The range, or \c std::nullopt if none is stored.
  LLVM_ABI std::optional<FileLocRange>
  getFunctionLocation(const Function *F) const;
  /// Get the recorded source range of a basic block.
  ///
  /// \param BB Basic block whose location is requested.
  /// \return The range, or \c std::nullopt if none is stored.
  LLVM_ABI std::optional<FileLocRange>
  getBlockLocation(const BasicBlock *BB) const;
  /// Get the recorded source range of an instruction or argument.
  ///
  /// \p IA must be an \c Instruction or \c Argument.
  /// \param IA Instruction or function argument whose location is requested.
  /// \return The range, or \c std::nullopt if none is stored.
  LLVM_ABI std::optional<FileLocRange>
  getInstructionOrArgumentLocation(const Value *IA) const;
  /// Get the function at the requested location range.
  ///
  /// If no single function occupies the queried range, or the record is
  /// missing, a nullptr is returned.
  /// \param Query Source range to look up.
  /// \return The function, or \c nullptr if none is found.
  LLVM_ABI Function *getFunctionAtLocation(const FileLocRange &Query) const;
  /// Get the function at the requested location.
  ///
  /// If no function occupies the queried location, or the record is missing, a
  /// nullptr is returned.
  /// \param Query Source location to look up.
  /// \return The function, or \c nullptr if none is found.
  LLVM_ABI Function *getFunctionAtLocation(const FileLoc &Query) const;
  /// Get the block at the requested location range.
  ///
  /// If no single block occupies the queried range, or the record is missing, a
  /// nullptr is returned.
  /// \param Query Source range to look up.
  /// \return The basic block, or \c nullptr if none is found.
  LLVM_ABI BasicBlock *getBlockAtLocation(const FileLocRange &Query) const;
  /// Get the block at the requested location.
  ///
  /// If no block occupies the queried location, or the record is missing, a
  /// nullptr is returned.
  /// \param Query Source location to look up.
  /// \return The basic block, or \c nullptr if none is found.
  LLVM_ABI BasicBlock *getBlockAtLocation(const FileLoc &Query) const;
  /// Get the instruction or function argument at the requested location range.
  ///
  /// If no single instruction occupies the queried range, or the record is
  /// missing, a nullptr is returned.
  /// \param Query Source range to look up.
  /// \return The instruction or argument, or \c nullptr if none is found.
  LLVM_ABI Value *
  getInstructionOrArgumentAtLocation(const FileLocRange &Query) const;
  /// Get the instruction or function argument at the requested location.
  ///
  /// If no instruction occupies the queried location, or the record is missing,
  /// a nullptr is returned.
  /// \param Query Source location to look up.
  /// \return The instruction or argument, or \c nullptr if none is found.
  LLVM_ABI Value *
  getInstructionOrArgumentAtLocation(const FileLoc &Query) const;
  /// Get value referenced at the requested location.
  ///
  /// If no value occupies the queried location, or the record is missing,
  /// a nullptr is returned.
  /// \param Query Source location to look up.
  /// \return The referenced value, or \c nullptr if none is found.
  LLVM_ABI Value *getValueReferencedAtLocation(const FileLoc &Query) const;
  /// Get value referenced at the requested location range.
  ///
  /// If no value occupies the queried location, or the record is missing,
  /// a nullptr is returned.
  /// \param Query Source range to look up.
  /// \return The referenced value, or \c nullptr if none is found.
  LLVM_ABI Value *
  getValueReferencedAtLocation(const FileLocRange &Query) const;
  /// Record the source range of a function.
  ///
  /// \param F Function to associate with \p Loc.
  /// \param Loc Source range occupied by \p F.
  /// \return True if the location was newly recorded.
  LLVM_ABI bool addFunctionLocation(Function *F, const FileLocRange &Loc);
  /// Record the source range of a basic block.
  ///
  /// \param BB Basic block to associate with \p Loc.
  /// \param Loc Source range occupied by \p BB.
  /// \return True if the location was newly recorded.
  LLVM_ABI bool addBlockLocation(BasicBlock *BB, const FileLocRange &Loc);
  /// Record the source range of an instruction or function argument.
  ///
  /// \p IA must be an \c Instruction or \c Argument.
  /// \param IA Instruction or argument to associate with \p Loc.
  /// \param Loc Source range occupied by \p IA.
  /// \return True if the location was newly recorded.
  LLVM_ABI bool addInstructionOrArgumentLocation(Value *IA,
                                                 const FileLocRange &Loc);
  /// Record a source range at which a value is referenced.
  ///
  /// \param V Value referenced at \p Loc.
  /// \param Loc Source range of the reference.
  /// \return Always true.
  LLVM_ABI bool addValueReferenceAtLocation(Value *V, const FileLocRange &Loc);
};
} // namespace llvm

#endif
