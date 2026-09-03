//===- RootSignatureMetadata.h - HLSL Root Signature helpers --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains a library for working with HLSL Root Signatures and
/// their metadata representation.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_HLSL_ROOTSIGNATUREMETADATA_H
#define LLVM_FRONTEND_HLSL_ROOTSIGNATUREMETADATA_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Frontend/HLSL/HLSLRootSignature.h"
#include "llvm/IR/Constants.h"
#include "llvm/MC/DXContainerRootSignature.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class LLVMContext;
class MDNode;
class Metadata;

namespace hlsl {
namespace rootsig {
/// Error describing a failed root-signature validation or parse.
class RootSignatureValidationError
    : public ErrorInfo<RootSignatureValidationError> {
public:
  /// RTTI identifier used by ErrorInfo::classID.
  LLVM_ABI static char ID;
  /// Human-readable description of the validation failure.
  std::string Msg;

  /// Construct an error with message \p Msg.
  ///
  /// \param Msg Human-readable description of the failure.
  RootSignatureValidationError(const Twine &Msg) : Msg(Msg.str()) {}

  /// Write this error's message to \p OS.
  ///
  /// \param OS Stream to receive the error message.
  void log(raw_ostream &OS) const override { OS << Msg; }

  /// Convert this error to a \c std::error_code.
  ///
  /// Root-signature validation has no corresponding \c std::error_code;
  /// returns \c inconvertibleErrorCode().
  ///
  /// \return \c inconvertibleErrorCode(), since no mapped error code exists.
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }
};

/// Builds LLVM metadata nodes from an in-memory root signature.
class MetadataBuilder {
public:
  /// Construct a builder for \p Elements in context \p Ctx.
  ///
  /// \param Ctx LLVM context that owns the generated metadata.
  /// \param Elements In-memory root signature elements to encode.
  MetadataBuilder(llvm::LLVMContext &Ctx, ArrayRef<RootElement> Elements)
      : Ctx(Ctx), Elements(Elements) {}

  /// Iterates through elements and dispatches onto the correct Build* method
  ///
  /// Accumulates the root signature and returns the Metadata node that is just
  /// a list of all the elements
  ///
  /// \return Metadata node listing all encoded root signature elements.
  LLVM_ABI MDNode *BuildRootSignature();

private:
  /// Define the various builders for the different metadata types
  MDNode *BuildRootFlags(const dxbc::RootFlags &Flags);
  MDNode *BuildRootConstants(const RootConstants &Constants);
  MDNode *BuildRootDescriptor(const RootDescriptor &Descriptor);
  MDNode *BuildDescriptorTable(const DescriptorTable &Table);
  MDNode *BuildDescriptorTableClause(const DescriptorTableClause &Clause);
  MDNode *BuildStaticSampler(const StaticSampler &Sampler);

  llvm::LLVMContext &Ctx;
  ArrayRef<RootElement> Elements;
  SmallVector<Metadata *> GeneratedMetadata;
};

/// Kind of root-signature element encoded in metadata.
enum class RootSignatureElementKind {
  /// Invalid or unrecognized element kind.
  Error = 0,
  /// Root flags element.
  RootFlags = 1,
  /// Root constants element.
  RootConstants = 2,
  /// Shader resource view (SRV) root descriptor.
  SRV = 3,
  /// Unordered access view (UAV) root descriptor.
  UAV = 4,
  /// Constant buffer view (CBV) root descriptor.
  CBV = 5,
  /// Descriptor table element.
  DescriptorTable = 6,
  /// Static sampler element.
  StaticSamplers = 7
};

/// Parses root-signature metadata into a container description.
class MetadataParser {
public:
  /// Construct a parser for root-signature metadata node \p Root.
  ///
  /// \param Root Metadata node holding the root signature elements.
  MetadataParser(MDNode *Root) : Root(Root) {}

  /// Parse the metadata into a root signature for \p Version.
  ///
  /// \param Version Root signature version to target when parsing.
  /// \return Parsed root signature description, or an error on failure.
  LLVM_ABI llvm::Expected<llvm::mcdxbc::RootSignatureDesc>
  ParseRootSignature(uint32_t Version);

private:
  llvm::Error parseRootFlags(mcdxbc::RootSignatureDesc &RSD,
                             MDNode *RootFlagNode);
  llvm::Error parseRootConstants(mcdxbc::RootSignatureDesc &RSD,
                                 MDNode *RootConstantNode);
  llvm::Error parseRootDescriptors(mcdxbc::RootSignatureDesc &RSD,
                                   MDNode *RootDescriptorNode,
                                   RootSignatureElementKind ElementKind);
  llvm::Error parseDescriptorRange(mcdxbc::DescriptorTable &Table,
                                   MDNode *RangeDescriptorNode);
  llvm::Error parseDescriptorTable(mcdxbc::RootSignatureDesc &RSD,
                                   MDNode *DescriptorTableNode);
  llvm::Error parseRootSignatureElement(mcdxbc::RootSignatureDesc &RSD,
                                        MDNode *Element);
  llvm::Error parseStaticSampler(mcdxbc::RootSignatureDesc &RSD,
                                 MDNode *StaticSamplerNode);

  llvm::Error validateRootSignature(const llvm::mcdxbc::RootSignatureDesc &RSD);

  MDNode *Root;
};

} // namespace rootsig
} // namespace hlsl
} // namespace llvm

#endif // LLVM_FRONTEND_HLSL_ROOTSIGNATUREMETADATA_H
