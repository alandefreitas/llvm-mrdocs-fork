//===---- llvm/MDBuilder.h - Builder for LLVM metadata ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MDBuilder class, which is used as a convenient way to
// create LLVM metadata with a consistent and simplified interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MDBUILDER_H
#define LLVM_IR_MDBUILDER_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include <utility>

namespace llvm {

class APInt;
template <typename T> class ArrayRef;
class LLVMContext;
class Constant;
class ConstantAsMetadata;
class Function;
class MDNode;
class MDString;
class Metadata;

/// Builder for creating LLVM metadata with a consistent interface.
class MDBuilder {
  LLVMContext &Context;

public:
  /// The weight for a branch taken with high probability.
  ///
  /// This is the weight used for Likely branches, for example, as used by
  /// __builtin_expect* or when profile indicates a branch is taken with very
  /// high probability.
  static constexpr uint32_t kLikelyBranchWeight = (1U << 20) - 1;

  /// The weight for a branch taken with low probability.
  ///
  /// This is the weight used for unlikely branches, for example, as used by
  /// __builtin_expect* or when profile indicates a branch is taken with very
  /// low probability.
  static constexpr uint32_t kUnlikelyBranchWeight = 1;

  /// Construct a metadata builder for the given context.
  /// \param context LLVM context used to create metadata nodes.
  MDBuilder(LLVMContext &context) : Context(context) {}

  /// Return the given string as metadata.
  /// \param Str String to wrap as an MDString.
  /// \return MDString wrapping the given string.
  LLVM_ABI MDString *createString(StringRef Str);

  /// Return the given constant as metadata.
  /// \param C Constant to wrap as ConstantAsMetadata.
  /// \return ConstantAsMetadata wrapping the given constant.
  LLVM_ABI ConstantAsMetadata *createConstant(Constant *C);

  //===------------------------------------------------------------------===//
  // FPMath metadata.
  //===------------------------------------------------------------------===//

  /// Return metadata with the given settings.
  ///
  /// The special value 0.0 for the Accuracy parameter indicates the default
  /// (maximal precision) setting.
  /// \param Accuracy Desired floating-point accuracy, or 0.0 for default.
  /// \return FPMath metadata node, or null for the default accuracy.
  LLVM_ABI MDNode *createFPMath(float Accuracy);

  //===------------------------------------------------------------------===//
  // Prof metadata.
  //===------------------------------------------------------------------===//

  /// Return metadata containing two branch weights.
  /// \param TrueWeight the weight of the true branch
  /// \param FalseWeight the weight of the false branch
  /// \param IsExpected Whether these weights come from __builtin_expect*.
  /// \return Metadata node containing the two branch weights.
  LLVM_ABI MDNode *createBranchWeights(uint32_t TrueWeight,
                                       uint32_t FalseWeight,
                                       bool IsExpected = false);

  /// Return metadata containing two branch weights, with significant bias
  /// towards `true` destination.
  /// \return Metadata node with likely true-branch weights.
  LLVM_ABI MDNode *createLikelyBranchWeights();

  /// Return metadata containing two branch weights, with significant bias
  /// towards `false` destination.
  /// \return Metadata node with unlikely true-branch weights.
  LLVM_ABI MDNode *createUnlikelyBranchWeights();

  /// Return metadata containing a number of branch weights.
  /// \param Weights the weights of all the branches
  /// \param IsExpected Whether these weights come from __builtin_expect*.
  /// \return Metadata node containing the branch weights.
  LLVM_ABI MDNode *createBranchWeights(ArrayRef<uint32_t> Weights,
                                       bool IsExpected = false);

  /// Return metadata specifying that a branch or switch is unpredictable.
  /// \return Metadata node marking the branch or switch as unpredictable.
  LLVM_ABI MDNode *createUnpredictable();

  /// Return metadata for a function entry count and related PGO data.
  ///
  /// Contains the entry \p Count for a function, a boolean \p Synthetic
  /// indicating whether the counts were synthetized, and the GUIDs stored in
  /// \p Imports that need to be imported for sample PGO, to enable the same
  /// inlines as the profiled optimized binary.
  /// \param Count Entry count for the function.
  /// \param Synthetic Whether the counts were synthesized.
  /// \param Imports GUIDs that need to be imported for sample PGO, or null.
  /// \return Metadata node for the function entry count and PGO data.
  LLVM_ABI MDNode *
  createFunctionEntryCount(uint64_t Count, bool Synthetic,
                           const DenseSet<GlobalValue::GUID> *Imports);

  /// Return metadata containing the section prefix for a global object.
  /// \param Prefix Section prefix string.
  /// \return Metadata node containing the section prefix.
  LLVM_ABI MDNode *createGlobalObjectSectionPrefix(StringRef Prefix);

  /// Return metadata containing the pseudo probe descriptor for a function.
  /// \param GUID Function GUID for the probe descriptor.
  /// \param Hash Configuration hash for the probe descriptor.
  /// \param FName Function name associated with the probe.
  /// \return Metadata node for the pseudo probe descriptor.
  LLVM_ABI MDNode *createPseudoProbeDesc(uint64_t GUID, uint64_t Hash,
                                         StringRef FName);

  /// Return metadata containing llvm statistics.
  /// \param LLVMStatsVec Pairs of statistic names and values.
  /// \return Metadata node containing the llvm statistics.
  LLVM_ABI MDNode *
  createLLVMStats(ArrayRef<std::pair<StringRef, uint64_t>> LLVMStatsVec);

  //===------------------------------------------------------------------===//
  // Range metadata.
  //===------------------------------------------------------------------===//

  /// Return metadata describing the range [Lo, Hi).
  /// \param Lo Inclusive lower bound of the range.
  /// \param Hi Exclusive upper bound of the range.
  /// \return Metadata node describing the range [Lo, Hi).
  LLVM_ABI MDNode *createRange(const APInt &Lo, const APInt &Hi);

  /// Return metadata describing the range [Lo, Hi).
  /// \param Lo Inclusive lower bound of the range.
  /// \param Hi Exclusive upper bound of the range.
  /// \return Metadata node describing the range [Lo, Hi).
  LLVM_ABI MDNode *createRange(Constant *Lo, Constant *Hi);

  //===------------------------------------------------------------------===//
  // Callees metadata.
  //===------------------------------------------------------------------===//

  /// Return metadata indicating the possible callees of indirect calls.
  /// \param Callees Possible callee functions.
  /// \return Metadata node listing the possible callees.
  LLVM_ABI MDNode *createCallees(ArrayRef<Function *> Callees);

  //===------------------------------------------------------------------===//
  // Callback metadata.
  //===------------------------------------------------------------------===//

  /// Return metadata describing a callback (see llvm::AbstractCallSite).
  /// \param CalleeArgNo Argument index of the callback callee.
  /// \param Arguments Mapping from callback parameters to call-site arguments.
  /// \param VarArgsArePassed Whether varargs are forwarded to the callback.
  /// \return Metadata node describing the callback encoding.
  LLVM_ABI MDNode *createCallbackEncoding(unsigned CalleeArgNo,
                                          ArrayRef<int> Arguments,
                                          bool VarArgsArePassed);

  /// Merge the new callback encoding \p NewCB into \p ExistingCallbacks.
  /// \param ExistingCallbacks Existing callback metadata node, or null.
  /// \param NewCB New callback encoding to merge in.
  /// \return Metadata node with the merged callback encodings.
  LLVM_ABI MDNode *mergeCallbackEncodings(MDNode *ExistingCallbacks,
                                          MDNode *NewCB);

  /// Return metadata feeding to the CodeGen about how to generate a function
  /// prologue for the "function" santizier.
  /// \param PrologueSig Prologue signature constant.
  /// \param RTTI RTTI pointer constant.
  /// \return Metadata node for the RTTI pointer prologue.
  LLVM_ABI MDNode *createRTTIPointerPrologue(Constant *PrologueSig,
                                             Constant *RTTI);

  //===------------------------------------------------------------------===//
  // PC sections metadata.
  //===------------------------------------------------------------------===//

  /// A pair of PC section name with auxilliary constant data.
  using PCSection = std::pair<StringRef, SmallVector<Constant *>>;

  /// Return metadata for PC sections.
  /// \param Sections PC section names paired with auxiliary constants.
  /// \return Metadata node describing the PC sections.
  LLVM_ABI MDNode *createPCSections(ArrayRef<PCSection> Sections);

  //===------------------------------------------------------------------===//
  // AA metadata.
  //===------------------------------------------------------------------===//

protected:
  /// Return metadata for an anonymous AA root node (scope or TBAA).
  ///
  /// Each returned node is distinct from all other metadata and will never
  /// be identified (uniqued) with anything else.
  /// \param Name Optional name for the root node.
  /// \param Extra Optional extra operand, such as an alias scope domain.
  /// \return Distinct anonymous AA root metadata node.
  LLVM_ABI MDNode *createAnonymousAARoot(StringRef Name = StringRef(),
                                         MDNode *Extra = nullptr);

public:
  /// Return metadata for an anonymous TBAA root node.
  ///
  /// Each returned node is distinct from all other metadata and will never be
  /// identified (uniqued) with anything else.
  /// \return Distinct anonymous TBAA root metadata node.
  MDNode *createAnonymousTBAARoot() {
    return createAnonymousAARoot();
  }

  /// Return metadata for an anonymous alias scope domain node.
  ///
  /// Each returned node is distinct from all other metadata and will never
  /// be identified (uniqued) with anything else.
  /// \param Name Optional name for the domain node.
  /// \return Distinct anonymous alias scope domain metadata node.
  MDNode *createAnonymousAliasScopeDomain(StringRef Name = StringRef()) {
    return createAnonymousAARoot(Name);
  }

  /// Return metadata for an anonymous alias scope node.
  ///
  /// Each returned node is distinct from all other metadata and will never
  /// be identified (uniqued) with anything else.
  /// \param Domain Alias scope domain for the new scope.
  /// \param Name Optional name for the scope node.
  /// \return Distinct anonymous alias scope metadata node.
  MDNode *createAnonymousAliasScope(MDNode *Domain,
                                    StringRef Name = StringRef()) {
    return createAnonymousAARoot(Name, Domain);
  }

  /// Return metadata appropriate for a TBAA root node with the given
  /// name.  This may be identified (uniqued) with other roots with the same
  /// name.
  /// \param Name Name used to identify (unique) the TBAA root.
  /// \return TBAA root metadata node for the given name.
  LLVM_ABI MDNode *createTBAARoot(StringRef Name);

  /// Return metadata appropriate for an alias scope domain node with
  /// the given name. This may be identified (uniqued) with other roots with
  /// the same name.
  /// \param Name Name used to identify (unique) the alias scope domain.
  /// \return Alias scope domain metadata node for the given name.
  LLVM_ABI MDNode *createAliasScopeDomain(StringRef Name);

  /// Return metadata appropriate for an alias scope node with
  /// the given name. This may be identified (uniqued) with other scopes with
  /// the same name and domain.
  /// \param Name Name used to identify (unique) the alias scope.
  /// \param Domain Alias scope domain that owns this scope.
  /// \return Alias scope metadata node for the given name and domain.
  LLVM_ABI MDNode *createAliasScope(StringRef Name, MDNode *Domain);

  /// Return metadata for a non-root TBAA node with the given name,
  /// parent in the TBAA tree, and value for 'pointsToConstantMemory'.
  /// \param Name Name of the TBAA node.
  /// \param Parent Parent node in the TBAA tree.
  /// \param isConstant Whether the node points to constant memory.
  /// \return Non-root TBAA metadata node.
  LLVM_ABI MDNode *createTBAANode(StringRef Name, MDNode *Parent,
                                  bool isConstant = false);

  /// Description of a field in a TBAA struct type.
  struct TBAAStructField {
    /// Byte offset of the field within the struct.
    uint64_t Offset;
    /// Size of the field in bytes.
    uint64_t Size;
    /// TBAA type node describing the field's type.
    MDNode *Type;
    /// Construct a TBAA struct field description.
    /// \param Offset Byte offset of the field within the struct.
    /// \param Size Size of the field in bytes.
    /// \param Type TBAA type node describing the field's type.
    TBAAStructField(uint64_t Offset, uint64_t Size, MDNode *Type) :
      Offset(Offset), Size(Size), Type(Type) {}
  };

  /// Return metadata for a tbaa.struct node with the given
  /// struct field descriptions.
  /// \param Fields Struct field descriptions for the tbaa.struct node.
  /// \return tbaa.struct metadata node for the given fields.
  LLVM_ABI MDNode *createTBAAStructNode(ArrayRef<TBAAStructField> Fields);

  /// Return metadata for a TBAA struct node in the type DAG
  /// with the given name, a list of pairs (offset, field type in the type DAG).
  /// \param Name Name of the TBAA struct type node.
  /// \param Fields Pairs of field type nodes and their offsets.
  /// \return TBAA struct type metadata node.
  LLVM_ABI MDNode *
  createTBAAStructTypeNode(StringRef Name,
                           ArrayRef<std::pair<MDNode *, uint64_t>> Fields);

  /// Return metadata for a TBAA scalar type node with the
  /// given name, an offset and a parent in the TBAA type DAG.
  /// \param Name Name of the scalar type node.
  /// \param Parent Parent node in the TBAA type DAG.
  /// \param Offset Offset of this type relative to its parent.
  /// \return TBAA scalar type metadata node.
  LLVM_ABI MDNode *createTBAAScalarTypeNode(StringRef Name, MDNode *Parent,
                                            uint64_t Offset = 0);

  /// Return metadata for a TBAA tag node with the given
  /// base type, access type and offset relative to the base type.
  /// \param BaseType Base type node in the TBAA type DAG.
  /// \param AccessType Access type node in the TBAA type DAG.
  /// \param Offset Offset of the access relative to the base type.
  /// \param IsConstant Whether the accessed object is constant.
  /// \return TBAA struct tag metadata node.
  LLVM_ABI MDNode *createTBAAStructTagNode(MDNode *BaseType, MDNode *AccessType,
                                           uint64_t Offset,
                                           bool IsConstant = false);

  /// Return metadata for a TBAA type node in the TBAA type DAG with the
  /// given parent type, size in bytes, type identifier and a list of fields.
  /// \param Parent Parent type node in the TBAA type DAG.
  /// \param Size Size of the type in bytes.
  /// \param Id Type identifier metadata.
  /// \param Fields Optional field descriptions for aggregate types.
  /// \return TBAA type metadata node in the type DAG.
  LLVM_ABI MDNode *createTBAATypeNode(
      MDNode *Parent, uint64_t Size, Metadata *Id,
      ArrayRef<TBAAStructField> Fields = ArrayRef<TBAAStructField>());

  /// Return metadata for a TBAA access tag.
  ///
  /// The tag uses the given base type, final access type, offset of the access
  /// relative to the base type, size of the access and flag indicating whether
  /// the accessed object can be considered immutable for the purposes of the
  /// TBAA analysis.
  /// \param BaseType Base type node in the TBAA type DAG.
  /// \param AccessType Final access type node in the TBAA type DAG.
  /// \param Offset Offset of the access relative to the base type.
  /// \param Size Size of the access in bytes.
  /// \param IsImmutable Whether the accessed object may be treated as immutable.
  /// \return TBAA access tag metadata node.
  LLVM_ABI MDNode *createTBAAAccessTag(MDNode *BaseType, MDNode *AccessType,
                                       uint64_t Offset, uint64_t Size,
                                       bool IsImmutable = false);

  /// Return mutable version of the given mutable or immutable TBAA
  /// access tag.
  /// \param Tag Existing TBAA access tag to make mutable.
  /// \return Mutable TBAA access tag metadata node.
  LLVM_ABI MDNode *createMutableTBAAAccessTag(MDNode *Tag);

  /// Return metadata containing an irreducible loop header weight.
  /// \param Weight Weight associated with the irreducible loop header.
  /// \return Metadata node containing the irreducible loop header weight.
  LLVM_ABI MDNode *createIrrLoopHeaderWeight(uint64_t Weight);
};

} // end namespace llvm

#endif
