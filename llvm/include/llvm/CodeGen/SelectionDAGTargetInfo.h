//==- llvm/CodeGen/SelectionDAGTargetInfo.h - SelectionDAG Info --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SelectionDAGTargetInfo class, which targets can
// subclass to parameterize the SelectionDAG lowering and instruction
// selection process.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTIONDAGTARGETINFO_H
#define LLVM_CODEGEN_SELECTIONDAGTARGETINFO_H

#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/SDNodeInfo.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/Support/CodeGen.h"
#include <utility>

namespace llvm {

class CallInst;
class SelectionDAG;

//===----------------------------------------------------------------------===//
/// Targets can subclass this to parameterize the
/// SelectionDAG lowering and instruction selection process.
///
class LLVM_ABI SelectionDAGTargetInfo {
public:
  /// Construct a default SelectionDAG target info.
  explicit SelectionDAGTargetInfo() = default;
  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is deleted.
  SelectionDAGTargetInfo(const SelectionDAGTargetInfo &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other Unused; copy assignment is deleted.
  SelectionDAGTargetInfo &operator=(const SelectionDAGTargetInfo &Other) =
      delete;
  /// Destroy the SelectionDAG target info.
  virtual ~SelectionDAGTargetInfo();

  /// Returns the name of the given target-specific opcode, suitable for
  /// debug printing.
  ///
  /// \param Opcode Target-specific ISD opcode.
  /// \return Name of the opcode for debug printing, or null.
  virtual const char *getTargetNodeName(unsigned Opcode) const {
    return nullptr;
  }

  /// Returns true if a node with the given target-specific opcode has a memory
  /// operand.
  ///
  /// Nodes with such opcodes can only be created with
  /// `SelectionDAG::getMemIntrinsicNode`.
  ///
  /// \param Opcode Target-specific ISD opcode.
  /// \return True if a node with this opcode has a memory operand.
  virtual bool isTargetMemoryOpcode(unsigned Opcode) const { return false; }

  /// Returns true if a node with the given target-specific opcode has
  /// strict floating-point semantics.
  ///
  /// \param Opcode Target-specific ISD opcode.
  /// \return True if a node with this opcode has strict floating-point
  /// semantics.
  virtual bool isTargetStrictFPOpcode(unsigned Opcode) const { return false; }

  /// Returns true if a node with the given target-specific opcode
  /// may raise a floating-point exception.
  ///
  /// \param Opcode Target-specific ISD opcode.
  /// \return True if a node with this opcode may raise a floating-point
  /// exception.
  virtual bool mayRaiseFPException(unsigned Opcode) const;

  /// Checks that the given target-specific node is valid. Aborts if it is not.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param N Target-specific node to verify.
  virtual void verifyTargetNode(const SelectionDAG &DAG,
                                const SDNode *N) const {}

  /// Emit target-specific code that performs a memcpy.
  ///
  /// This can be used by targets to provide code sequences for cases
  /// that don't fit the target's parameters for simple loads/stores and can be
  /// more efficient than using a library call. This function can return a null
  /// SDValue if the target declines to use custom code and a different
  /// lowering strategy should be used.
  ///
  /// If AlwaysInline is true, the size is constant and the target should not
  /// emit any calls and is strongly encouraged to attempt to emit inline code
  /// even if it is beyond the usual threshold because this intrinsic is being
  /// expanded in a place where calls are not feasible (e.g. within the prologue
  /// for another call). If the target chooses to decline an AlwaysInline
  /// request here, legalize will resort to using simple loads and stores.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param dl Debug location for new nodes.
  /// \param Chain Incoming chain operand.
  /// \param Op1 Destination pointer.
  /// \param Op2 Source pointer.
  /// \param Op3 Number of bytes to copy.
  /// \param DstAlign Alignment of the destination.
  /// \param SrcAlign Alignment of the source.
  /// \param isVolatile Whether the memory operation is volatile.
  /// \param AlwaysInline Whether the expansion must avoid calls.
  /// \param DstPtrInfo Pointer info for the destination.
  /// \param SrcPtrInfo Pointer info for the source.
  /// \return Custom memcpy chain, or a null SDValue to decline.
  virtual SDValue EmitTargetCodeForMemcpy(SelectionDAG &DAG, const SDLoc &dl,
                                          SDValue Chain, SDValue Op1,
                                          SDValue Op2, SDValue Op3,
                                          Align DstAlign, Align SrcAlign,
                                          bool isVolatile, bool AlwaysInline,
                                          MachinePointerInfo DstPtrInfo,
                                          MachinePointerInfo SrcPtrInfo) const {
    return SDValue();
  }

  /// Emit target-specific code that performs a memmove.
  ///
  /// This can be used by targets to provide code sequences for cases
  /// that don't fit the target's parameters for simple loads/stores and can be
  /// more efficient than using a library call. This function can return a null
  /// SDValue if the target declines to use custom code and a different
  /// lowering strategy should be used.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param dl Debug location for new nodes.
  /// \param Chain Incoming chain operand.
  /// \param Op1 Destination pointer.
  /// \param Op2 Source pointer.
  /// \param Op3 Number of bytes to move.
  /// \param DstAlign Alignment of the destination.
  /// \param SrcAlign Alignment of the source.
  /// \param isVolatile Whether the memory operation is volatile.
  /// \param DstPtrInfo Pointer info for the destination.
  /// \param SrcPtrInfo Pointer info for the source.
  /// \return Custom memmove chain, or a null SDValue to decline.
  virtual SDValue EmitTargetCodeForMemmove(
      SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Op1,
      SDValue Op2, SDValue Op3, Align DstAlign, Align SrcAlign, bool isVolatile,
      MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
    return SDValue();
  }

  /// Emit target-specific code that performs a memset.
  ///
  /// This can be used by targets to provide code sequences for cases
  /// that don't fit the target's parameters for simple stores and can be more
  /// efficient than using a library call. This function can return a null
  /// SDValue if the target declines to use custom code and a different
  /// lowering strategy should be used. Note that if AlwaysInline is true the
  /// function has to return a valid SDValue.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param dl Debug location for new nodes.
  /// \param Chain Incoming chain operand.
  /// \param Op1 Destination pointer.
  /// \param Op2 Value to store into each byte.
  /// \param Op3 Number of bytes to set.
  /// \param Alignment Alignment of the destination.
  /// \param isVolatile Whether the memory operation is volatile.
  /// \param AlwaysInline Whether the expansion must avoid calls.
  /// \param DstPtrInfo Pointer info for the destination.
  /// \return Custom memset chain, or a null SDValue to decline.
  virtual SDValue EmitTargetCodeForMemset(SelectionDAG &DAG, const SDLoc &dl,
                                          SDValue Chain, SDValue Op1,
                                          SDValue Op2, SDValue Op3,
                                          Align Alignment, bool isVolatile,
                                          bool AlwaysInline,
                                          MachinePointerInfo DstPtrInfo) const {
    return SDValue();
  }

  /// Emit target-specific code that performs a strstr, in cases where that is
  /// faster than a libcall.
  ///
  /// The first returned SDValue is the result of the strstr and the second is
  /// the chain. Both SDValues can be null if a normal libcall should be used.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param dl Debug location for new nodes.
  /// \param Chain Incoming chain operand.
  /// \param Op1 Haystack string pointer.
  /// \param Op2 Needle string pointer.
  /// \param CI Call instruction being expanded, if any.
  /// \return Result and chain SDValues, or nulls to use a libcall.
  virtual std::pair<SDValue, SDValue>
  EmitTargetCodeForStrstr(SelectionDAG &DAG, const SDLoc &dl, SDValue Chain,
                          SDValue Op1, SDValue Op2, const CallInst *CI) const {
    return std::make_pair(SDValue(), SDValue());
  }

  /// Emit target-specific code that performs a memccpy, in cases where that is
  /// faster than a libcall.
  ///
  /// The first returned SDValue is the result of the memccpy and the second is
  /// the chain. Both SDValues can be null if a normal libcall should be used.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param dl Debug location for new nodes.
  /// \param Chain Incoming chain operand.
  /// \param Dst Destination pointer.
  /// \param Src Source pointer.
  /// \param C Stop character.
  /// \param Size Maximum number of bytes to copy.
  /// \param CI Call instruction being expanded, if any.
  /// \return Result and chain SDValues, or nulls to use a libcall.
  virtual std::pair<SDValue, SDValue>
  EmitTargetCodeForMemccpy(SelectionDAG &DAG, const SDLoc &dl, SDValue Chain,
                           SDValue Dst, SDValue Src, SDValue C, SDValue Size,
                           const CallInst *CI) const {
    return std::make_pair(SDValue(), SDValue());
  }

  /// Emit target-specific code that performs a memcmp/bcmp, in cases where that
  /// is faster than a libcall.
  ///
  /// The first returned SDValue is the result of the memcmp and the second is
  /// the chain. Both SDValues can be null if a normal libcall should be used.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param dl Debug location for new nodes.
  /// \param Chain Incoming chain operand.
  /// \param Op1 First memory region pointer.
  /// \param Op2 Second memory region pointer.
  /// \param Op3 Number of bytes to compare.
  /// \param CI Call instruction being expanded, if any.
  /// \return Result and chain SDValues, or nulls to use a libcall.
  virtual std::pair<SDValue, SDValue>
  EmitTargetCodeForMemcmp(SelectionDAG &DAG, const SDLoc &dl, SDValue Chain,
                          SDValue Op1, SDValue Op2, SDValue Op3,
                          const CallInst *CI) const {
    return std::make_pair(SDValue(), SDValue());
  }

  /// Emit target-specific code that performs a memchr, in cases where that is
  /// faster than a libcall.
  ///
  /// The first returned SDValue is the result of the memchr and the second is
  /// the chain. Both SDValues can be null if a normal libcall should be used.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param dl Debug location for new nodes.
  /// \param Chain Incoming chain operand.
  /// \param Src Source memory region pointer.
  /// \param Char Character value to search for.
  /// \param Length Number of bytes to search.
  /// \param SrcPtrInfo Pointer info for the source.
  /// \return Result and chain SDValues, or nulls to use a libcall.
  virtual std::pair<SDValue, SDValue>
  EmitTargetCodeForMemchr(SelectionDAG &DAG, const SDLoc &dl, SDValue Chain,
                          SDValue Src, SDValue Char, SDValue Length,
                          MachinePointerInfo SrcPtrInfo) const {
    return std::make_pair(SDValue(), SDValue());
  }

  /// Emit target-specific code that performs a strcpy or stpcpy, in cases
  /// where that is faster than a libcall.
  ///
  /// The first returned SDValue is the result of the copy (the start
  /// of the destination string for strcpy, a pointer to the null terminator
  /// for stpcpy) and the second is the chain.  Both SDValues can be null
  /// if a normal libcall should be used.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param DL Debug location for new nodes.
  /// \param Chain Incoming chain operand.
  /// \param Dest Destination string pointer.
  /// \param Src Source string pointer.
  /// \param DestPtrInfo Pointer info for the destination.
  /// \param SrcPtrInfo Pointer info for the source.
  /// \param isStpcpy True to emit stpcpy rather than strcpy.
  /// \param CI Call instruction being expanded, if any.
  /// \return Result and chain SDValues, or nulls to use a libcall.
  virtual std::pair<SDValue, SDValue> EmitTargetCodeForStrcpy(
      SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dest,
      SDValue Src, MachinePointerInfo DestPtrInfo,
      MachinePointerInfo SrcPtrInfo, bool isStpcpy, const CallInst *CI) const {
    return std::make_pair(SDValue(), SDValue());
  }

  /// Emit target-specific code that performs a strcmp, in cases where that is
  /// faster than a libcall.
  ///
  /// The first returned SDValue is the result of the strcmp and the second is
  /// the chain. Both SDValues can be null if a normal libcall should be used.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param dl Debug location for new nodes.
  /// \param Chain Incoming chain operand.
  /// \param Op1 First string pointer.
  /// \param Op2 Second string pointer.
  /// \param Op1PtrInfo Pointer info for the first string.
  /// \param Op2PtrInfo Pointer info for the second string.
  /// \param CI Call instruction being expanded, if any.
  /// \return Result and chain SDValues, or nulls to use a libcall.
  virtual std::pair<SDValue, SDValue> EmitTargetCodeForStrcmp(
      SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Op1,
      SDValue Op2, MachinePointerInfo Op1PtrInfo, MachinePointerInfo Op2PtrInfo,
      const CallInst *CI) const {
    return std::make_pair(SDValue(), SDValue());
  }

  /// Emit target-specific code that performs a strlen, in cases where that is
  /// faster than a libcall.
  ///
  /// The first returned SDValue is the result of the strlen and the second is
  /// the chain. Both SDValues can be null if a normal libcall should be used.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param DL Debug location for new nodes.
  /// \param Chain Incoming chain operand.
  /// \param Src String pointer.
  /// \param CI Call instruction being expanded, if any.
  /// \return Result and chain SDValues, or nulls to use a libcall.
  virtual std::pair<SDValue, SDValue>
  EmitTargetCodeForStrlen(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                          SDValue Src, const CallInst *CI) const {
    return std::make_pair(SDValue(), SDValue());
  }

  /// Emit target-specific code that performs a strnlen, in cases where that is
  /// faster than a libcall.
  ///
  /// The first returned SDValue is the result of the strnlen and the second is
  /// the chain. Both SDValues can be null if a normal libcall should be used.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param DL Debug location for new nodes.
  /// \param Chain Incoming chain operand.
  /// \param Src String pointer.
  /// \param MaxLength Maximum number of characters to examine.
  /// \param SrcPtrInfo Pointer info for the source.
  /// \return Result and chain SDValues, or nulls to use a libcall.
  virtual std::pair<SDValue, SDValue>
  EmitTargetCodeForStrnlen(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                           SDValue Src, SDValue MaxLength,
                           MachinePointerInfo SrcPtrInfo) const {
    return std::make_pair(SDValue(), SDValue());
  }

  /// Emit target-specific code that performs a set-tag memory operation.
  ///
  /// Returns a null SDValue if the target declines to use custom code.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param dl Debug location for new nodes.
  /// \param Chain Incoming chain operand.
  /// \param Addr Address of the memory region to tag.
  /// \param Size Size of the region in bytes.
  /// \param DstPtrInfo Pointer info for the destination.
  /// \param ZeroData Whether associated data should be zeroed.
  /// \return Chain SDValue for the set-tag operation, or null to decline.
  virtual SDValue EmitTargetCodeForSetTag(SelectionDAG &DAG, const SDLoc &dl,
                                          SDValue Chain, SDValue Addr,
                                          SDValue Size,
                                          MachinePointerInfo DstPtrInfo,
                                          bool ZeroData) const {
    return SDValue();
  }

  /// Return true if the DAG Combiner should disable generic combines.
  ///
  /// \param OptLevel Codegen optimization level.
  /// \return True if generic combines should be disabled.
  virtual bool disableGenericCombines(CodeGenOptLevel OptLevel) const {
    return false;
  }
};

/// Proxy class that targets should inherit from if they wish to use
/// the generated node descriptions.
class LLVM_ABI SelectionDAGGenTargetInfo : public SelectionDAGTargetInfo {
protected:
  /// Generated SDNode descriptions for target-specific opcodes.
  const SDNodeInfo &GenNodeInfo;

  /// Construct from generated node info.
  ///
  /// \param GenNodeInfo Generated SDNode descriptions to use.
  explicit SelectionDAGGenTargetInfo(const SDNodeInfo &GenNodeInfo)
      : GenNodeInfo(GenNodeInfo) {}

public:
  /// Destroy the generated SelectionDAG target info.
  ~SelectionDAGGenTargetInfo() override;

  /// Returns the name of the given target-specific opcode, suitable for
  /// debug printing.
  ///
  /// \param Opcode Target-specific ISD opcode.
  /// \return Name of the opcode for debug printing.
  const char *getTargetNodeName(unsigned Opcode) const override {
    assert(GenNodeInfo.hasDesc(Opcode) &&
           "The name should be provided by the derived class");
    return GenNodeInfo.getName(Opcode).data();
  }

  /// Returns true if a node with the given target-specific opcode has a memory
  /// operand.
  ///
  /// \param Opcode Target-specific ISD opcode.
  /// \return True if a node with this opcode has a memory operand.
  bool isTargetMemoryOpcode(unsigned Opcode) const override {
    if (GenNodeInfo.hasDesc(Opcode))
      return GenNodeInfo.getDesc(Opcode).hasProperty(SDNPMemOperand);
    return false;
  }

  /// Returns true if a node with the given target-specific opcode has
  /// strict floating-point semantics.
  ///
  /// \param Opcode Target-specific ISD opcode.
  /// \return True if a node with this opcode has strict floating-point
  /// semantics.
  bool isTargetStrictFPOpcode(unsigned Opcode) const override {
    if (GenNodeInfo.hasDesc(Opcode))
      return GenNodeInfo.getDesc(Opcode).hasFlag(SDNFIsStrictFP);
    return false;
  }

  /// Checks that the given target-specific node is valid. Aborts if it is not.
  ///
  /// \param DAG SelectionDAG providing context.
  /// \param N Target-specific node to verify.
  void verifyTargetNode(const SelectionDAG &DAG,
                        const SDNode *N) const override {
    if (GenNodeInfo.hasDesc(N->getOpcode()))
      GenNodeInfo.verifyNode(DAG, N);
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SELECTIONDAGTARGETINFO_H
