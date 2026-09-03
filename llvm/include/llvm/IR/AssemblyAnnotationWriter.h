//===-- AssemblyAnnotationWriter.h - Annotation .ll files -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Clients of the assembly writer can use this interface to add their own
// special-purpose annotations to LLVM assembly language printouts.  Note that
// the assembly parser won't be able to parse these, in general, so
// implementations are advised to print stuff as LLVM comments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_ASSEMBLYANNOTATIONWRITER_H
#define LLVM_IR_ASSEMBLYANNOTATIONWRITER_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class Function;
class BasicBlock;
class Instruction;
class MDNode;
class Value;
class formatted_raw_ostream;

/// Interface for adding special-purpose annotations to LLVM assembly printouts.
///
/// Note that the assembly parser won't be able to parse these, in general, so
/// implementations are advised to print stuff as LLVM comments.
class LLVM_ABI AssemblyAnnotationWriter {
public:
  /// Destroy the annotation writer.
  virtual ~AssemblyAnnotationWriter();

  /// Emit a string right before the start of a function.
  /// \param F Function about to be printed.
  /// \param OS Stream to write the annotation to.
  virtual void emitFunctionAnnot(const Function *F,
                                 formatted_raw_ostream &OS) {}

  /// Emit a string right after the basic block label, before the first
  /// instruction.
  /// \param BB Basic block whose start is being printed.
  /// \param OS Stream to write the annotation to.
  virtual void emitBasicBlockStartAnnot(const BasicBlock *BB,
                                        formatted_raw_ostream &OS) {
  }

  /// Emit a string right after the basic block.
  /// \param BB Basic block whose end is being printed.
  /// \param OS Stream to write the annotation to.
  virtual void emitBasicBlockEndAnnot(const BasicBlock *BB,
                                      formatted_raw_ostream &OS) {
  }

  /// Emit a string right before an instruction is emitted.
  /// \param I Instruction about to be printed.
  /// \param OS Stream to write the annotation to.
  virtual void emitInstructionAnnot(const Instruction *I,
                                    formatted_raw_ostream &OS) {}

  /// Emit a string right before a metadata node is emitted.
  /// \param N Metadata node about to be printed.
  /// \param OS Stream to write the annotation to.
  virtual void emitMDNodeAnnot(const MDNode *N, formatted_raw_ostream &OS) {}

  /// Emit a comment to the right of an instruction or global value.
  /// \param V Value whose trailing info comment is being printed.
  /// \param OS Stream to write the comment to.
  virtual void printInfoComment(const Value &V, formatted_raw_ostream &OS) {}
};

} // End llvm namespace

#endif
