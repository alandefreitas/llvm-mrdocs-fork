//===-- Operations.h - ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementations of common fuzzer operation descriptors for building an IR
// mutator.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_OPERATIONS_H
#define LLVM_FUZZMUTATE_OPERATIONS_H

#include "llvm/FuzzMutate/OpDescriptor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Getters for the default sets of operations, per general category.
/// @{

/// Append integer arithmetic and icmp descriptors to \p Ops.
/// \param Ops Collection of operation descriptors to extend.
LLVM_ABI void describeFuzzerIntOps(std::vector<fuzzerop::OpDescriptor> &Ops);

/// Append floating-point arithmetic and fcmp descriptors to \p Ops.
/// \param Ops Collection of operation descriptors to extend.
LLVM_ABI void describeFuzzerFloatOps(std::vector<fuzzerop::OpDescriptor> &Ops);

/// Append control-flow operation descriptors to \p Ops.
/// \param Ops Collection of operation descriptors to extend.
LLVM_ABI void
describeFuzzerControlFlowOps(std::vector<fuzzerop::OpDescriptor> &Ops);

/// Append pointer operation descriptors to \p Ops.
/// \param Ops Collection of operation descriptors to extend.
LLVM_ABI void
describeFuzzerPointerOps(std::vector<fuzzerop::OpDescriptor> &Ops);

/// Append aggregate extractvalue and insertvalue descriptors to \p Ops.
/// \param Ops Collection of operation descriptors to extend.
LLVM_ABI void
describeFuzzerAggregateOps(std::vector<fuzzerop::OpDescriptor> &Ops);

/// Append vector extractelement, insertelement, and shufflevector
/// descriptors to \p Ops.
/// \param Ops Collection of operation descriptors to extend.
LLVM_ABI void describeFuzzerVectorOps(std::vector<fuzzerop::OpDescriptor> &Ops);

/// Append unary operation descriptors to \p Ops.
/// \param Ops Collection of operation descriptors to extend.
LLVM_ABI void
describeFuzzerUnaryOperations(std::vector<fuzzerop::OpDescriptor> &Ops);

/// Append miscellaneous operation descriptors to \p Ops.
/// \param Ops Collection of operation descriptors to extend.
LLVM_ABI void describeFuzzerOtherOps(std::vector<fuzzerop::OpDescriptor> &Ops);
/// @}

namespace fuzzerop {
/// Return a descriptor that builds a select instruction.
/// \param Weight Relative weight used when sampling this operation.
/// \return Descriptor that builds a select instruction.
LLVM_ABI OpDescriptor selectDescriptor(unsigned Weight);

/// Return a descriptor that builds an fneg instruction.
/// \param Weight Relative weight used when sampling this operation.
/// \return Descriptor that builds an fneg instruction.
LLVM_ABI OpDescriptor fnegDescriptor(unsigned Weight);

/// Return a descriptor that builds the given binary operator.
/// \param Weight Relative weight used when sampling this operation.
/// \param Op Binary opcode to emit, such as Add or FMul.
/// \return Descriptor that builds the given binary operator.
LLVM_ABI OpDescriptor binOpDescriptor(unsigned Weight,
                                      Instruction::BinaryOps Op);

/// Return a descriptor that builds an icmp or fcmp with the given predicate.
/// \param Weight Relative weight used when sampling this operation.
/// \param CmpOp Comparison kind, either ICmp or FCmp.
/// \param Pred Predicate applied by the comparison, such as ICMP_EQ.
/// \return Descriptor that builds an icmp or fcmp with the given predicate.
LLVM_ABI OpDescriptor cmpOpDescriptor(unsigned Weight,
                                      Instruction::OtherOps CmpOp,
                                      CmpInst::Predicate Pred);

/// Return a descriptor that splits the current block and may insert a backedge.
/// \param Weight Relative weight used when sampling this operation.
/// \return Descriptor that splits the current block and may insert a backedge.
LLVM_ABI OpDescriptor splitBlockDescriptor(unsigned Weight);

/// Return a descriptor that builds a getelementptr instruction.
/// \param Weight Relative weight used when sampling this operation.
/// \return Descriptor that builds a getelementptr instruction.
LLVM_ABI OpDescriptor gepDescriptor(unsigned Weight);

/// Return a descriptor that builds an extractvalue instruction.
/// \param Weight Relative weight used when sampling this operation.
/// \return Descriptor that builds an extractvalue instruction.
LLVM_ABI OpDescriptor extractValueDescriptor(unsigned Weight);

/// Return a descriptor that builds an insertvalue instruction.
/// \param Weight Relative weight used when sampling this operation.
/// \return Descriptor that builds an insertvalue instruction.
LLVM_ABI OpDescriptor insertValueDescriptor(unsigned Weight);

/// Return a descriptor that builds an extractelement instruction.
/// \param Weight Relative weight used when sampling this operation.
/// \return Descriptor that builds an extractelement instruction.
LLVM_ABI OpDescriptor extractElementDescriptor(unsigned Weight);

/// Return a descriptor that builds an insertelement instruction.
/// \param Weight Relative weight used when sampling this operation.
/// \return Descriptor that builds an insertelement instruction.
LLVM_ABI OpDescriptor insertElementDescriptor(unsigned Weight);

/// Return a descriptor that builds a shufflevector instruction.
/// \param Weight Relative weight used when sampling this operation.
/// \return Descriptor that builds a shufflevector instruction.
LLVM_ABI OpDescriptor shuffleVectorDescriptor(unsigned Weight);

} // namespace fuzzerop

} // namespace llvm

#endif // LLVM_FUZZMUTATE_OPERATIONS_H
