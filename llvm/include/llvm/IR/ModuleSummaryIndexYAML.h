//===-- llvm/ModuleSummaryIndexYAML.h - YAML I/O for summary ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MODULESUMMARYINDEXYAML_H
#define LLVM_IR_MODULESUMMARYINDEXYAML_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Support/YAMLTraits.h"
#include <algorithm>

namespace llvm {
namespace yaml {

/// YAMLIO scalar enumeration traits for \c TypeTestResolution::Kind.
template <> struct ScalarEnumerationTraits<TypeTestResolution::Kind> {
  /// Map type-test resolution kind enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Kind being mapped.
  static void enumeration(IO &io, TypeTestResolution::Kind &value) {
    io.enumCase(value, "Unknown", TypeTestResolution::Unknown);
    io.enumCase(value, "Unsat", TypeTestResolution::Unsat);
    io.enumCase(value, "ByteArray", TypeTestResolution::ByteArray);
    io.enumCase(value, "Inline", TypeTestResolution::Inline);
    io.enumCase(value, "Single", TypeTestResolution::Single);
    io.enumCase(value, "AllOnes", TypeTestResolution::AllOnes);
  }
};

/// YAMLIO mapping traits for \c TypeTestResolution.
template <> struct MappingTraits<TypeTestResolution> {
  /// Map type-test resolution fields to and from YAML.
  /// \param io YAML input/output state.
  /// \param res Resolution being mapped.
  static void mapping(IO &io, TypeTestResolution &res) {
    io.mapOptional("Kind", res.TheKind);
    io.mapOptional("SizeM1BitWidth", res.SizeM1BitWidth);
    io.mapOptional("AlignLog2", res.AlignLog2);
    io.mapOptional("SizeM1", res.SizeM1);
    io.mapOptional("BitMask", res.BitMask);
    io.mapOptional("InlineBits", res.InlineBits);
  }
};

/// YAMLIO scalar enumeration traits for
/// \c WholeProgramDevirtResolution::ByArg::Kind.
template <>
struct ScalarEnumerationTraits<WholeProgramDevirtResolution::ByArg::Kind> {
  /// Map by-argument whole-program-devirt kind enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Kind being mapped.
  static void enumeration(IO &io,
                          WholeProgramDevirtResolution::ByArg::Kind &value) {
    io.enumCase(value, "Indir", WholeProgramDevirtResolution::ByArg::Indir);
    io.enumCase(value, "UniformRetVal",
                WholeProgramDevirtResolution::ByArg::UniformRetVal);
    io.enumCase(value, "UniqueRetVal",
                WholeProgramDevirtResolution::ByArg::UniqueRetVal);
    io.enumCase(value, "VirtualConstProp",
                WholeProgramDevirtResolution::ByArg::VirtualConstProp);
  }
};

/// YAMLIO mapping traits for \c WholeProgramDevirtResolution::ByArg.
template <> struct MappingTraits<WholeProgramDevirtResolution::ByArg> {
  /// Map by-argument whole-program-devirt fields to and from YAML.
  /// \param io YAML input/output state.
  /// \param res Resolution being mapped.
  static void mapping(IO &io, WholeProgramDevirtResolution::ByArg &res) {
    io.mapOptional("Kind", res.TheKind);
    io.mapOptional("Info", res.Info);
    io.mapOptional("Byte", res.Byte);
    io.mapOptional("Bit", res.Bit);
  }
};

/// YAMLIO custom mapping traits for by-argument whole-program-devirt maps.
///
/// Map keys are comma-separated argument lists encoded as YAML mapping keys.
template <>
struct CustomMappingTraits<
    std::map<std::vector<uint64_t>, WholeProgramDevirtResolution::ByArg>> {
  /// Parse one comma-separated argument-key entry into \p V.
  /// \param io YAML input/output state.
  /// \param Key Comma-separated argument list used as the mapping key.
  /// \param V Destination map being populated.
  static void inputOne(
      IO &io, StringRef Key,
      std::map<std::vector<uint64_t>, WholeProgramDevirtResolution::ByArg> &V) {
    std::vector<uint64_t> Args;
    std::pair<StringRef, StringRef> P = {"", Key};
    while (!P.second.empty()) {
      P = P.second.split(',');
      uint64_t Arg;
      if (P.first.getAsInteger(0, Arg)) {
        io.setError("key not an integer");
        return;
      }
      Args.push_back(Arg);
    }
    io.mapRequired(Key, V[Args]);
  }
  /// Write each by-argument resolution in \p V with a comma-separated key.
  /// \param io YAML input/output state.
  /// \param V Source map being written.
  static void output(
      IO &io,
      std::map<std::vector<uint64_t>, WholeProgramDevirtResolution::ByArg> &V) {
    for (auto &P : V) {
      std::string Key;
      for (uint64_t Arg : P.first) {
        if (!Key.empty())
          Key += ',';
        Key += llvm::utostr(Arg);
      }
      io.mapRequired(Key, P.second);
    }
  }
};

/// YAMLIO scalar enumeration traits for \c WholeProgramDevirtResolution::Kind.
template <> struct ScalarEnumerationTraits<WholeProgramDevirtResolution::Kind> {
  /// Map whole-program-devirt kind enumerators to and from YAML.
  /// \param io YAML input/output state.
  /// \param value Kind being mapped.
  static void enumeration(IO &io, WholeProgramDevirtResolution::Kind &value) {
    io.enumCase(value, "Indir", WholeProgramDevirtResolution::Indir);
    io.enumCase(value, "SingleImpl", WholeProgramDevirtResolution::SingleImpl);
    io.enumCase(value, "BranchFunnel",
                WholeProgramDevirtResolution::BranchFunnel);
  }
};

/// YAMLIO mapping traits for \c WholeProgramDevirtResolution.
template <> struct MappingTraits<WholeProgramDevirtResolution> {
  /// Map whole-program-devirt resolution fields to and from YAML.
  /// \param io YAML input/output state.
  /// \param res Resolution being mapped.
  static void mapping(IO &io, WholeProgramDevirtResolution &res) {
    io.mapOptional("Kind", res.TheKind);
    io.mapOptional("SingleImplName", res.SingleImplName);
    io.mapOptional("ResByArg", res.ResByArg);
  }
};

/// YAMLIO custom mapping traits for type-ID whole-program-devirt maps.
///
/// Map keys are type-ID offsets encoded as decimal integer strings.
template <>
struct CustomMappingTraits<std::map<uint64_t, WholeProgramDevirtResolution>> {
  /// Parse one integer-key whole-program-devirt entry into \p V.
  /// \param io YAML input/output state.
  /// \param Key Decimal type-ID offset used as the mapping key.
  /// \param V Destination map being populated.
  static void inputOne(IO &io, StringRef Key,
                       std::map<uint64_t, WholeProgramDevirtResolution> &V) {
    uint64_t KeyInt;
    if (Key.getAsInteger(0, KeyInt)) {
      io.setError("key not an integer");
      return;
    }
    io.mapRequired(Key, V[KeyInt]);
  }
  /// Write each whole-program-devirt resolution in \p V with an integer key.
  /// \param io YAML input/output state.
  /// \param V Source map being written.
  static void output(IO &io, std::map<uint64_t, WholeProgramDevirtResolution> &V) {
    for (auto &P : V)
      io.mapRequired(llvm::utostr(P.first), P.second);
  }
};

/// YAMLIO mapping traits for \c TypeIdSummary.
template <> struct MappingTraits<TypeIdSummary> {
  /// Map type-ID summary fields to and from YAML.
  /// \param io YAML input/output state.
  /// \param summary Type-ID summary being mapped.
  static void mapping(IO &io, TypeIdSummary& summary) {
    io.mapOptional("TTRes", summary.TTRes);
    io.mapOptional("WPDRes", summary.WPDRes);
  }
};

/// YAML-friendly view of a global-value summary entry.
struct GlobalValueSummaryYaml {
  /// Linkage kind stored as an unsigned enumerator value.
  unsigned Linkage;
  /// Visibility kind stored as an unsigned enumerator value.
  unsigned Visibility;
  /// True when this value must not be imported into another module.
  bool NotEligibleToImport;
  /// True when the value is live in the summary index.
  bool Live;
  /// True when the value has local (DSO-local) linkage semantics.
  bool IsLocal;
  /// True when the value can be auto-hidden.
  bool CanAutoHide;
  /// Import kind stored as an unsigned enumerator value.
  unsigned ImportType;
  /// True when promotion must keep the original name.
  bool NoRenameOnPromotion;
  /// GUID of the aliasee when this entry is an alias summary.
  std::optional<uint64_t> Aliasee;
  /// Referenced global-value GUIDs for a function summary.
  std::vector<uint64_t> Refs = {};
  /// Type-test GUIDs referenced by a function summary.
  std::vector<uint64_t> TypeTests = {};
  /// Virtual calls assumed by type tests.
  std::vector<FunctionSummary::VFuncId> TypeTestAssumeVCalls = {};
  /// Virtual calls reached through type-checked loads.
  std::vector<FunctionSummary::VFuncId> TypeCheckedLoadVCalls = {};
  /// Constant virtual calls assumed by type tests.
  std::vector<FunctionSummary::ConstVCall> TypeTestAssumeConstVCalls = {};
  /// Constant virtual calls reached through type-checked loads.
  std::vector<FunctionSummary::ConstVCall> TypeCheckedLoadConstVCalls = {};
};

} // End yaml namespace
} // End llvm namespace

namespace llvm {
namespace yaml {

/// YAMLIO mapping traits for \c FunctionSummary::VFuncId.
template <> struct MappingTraits<FunctionSummary::VFuncId> {
  /// Map virtual-function ID fields to and from YAML.
  /// \param io YAML input/output state.
  /// \param id Virtual-function ID being mapped.
  static void mapping(IO &io, FunctionSummary::VFuncId& id) {
    io.mapOptional("GUID", id.GUID);
    io.mapOptional("Offset", id.Offset);
  }
};

/// YAMLIO mapping traits for \c FunctionSummary::ConstVCall.
template <> struct MappingTraits<FunctionSummary::ConstVCall> {
  /// Map constant virtual-call fields to and from YAML.
  /// \param io YAML input/output state.
  /// \param id Constant virtual call being mapped.
  static void mapping(IO &io, FunctionSummary::ConstVCall& id) {
    io.mapOptional("VFunc", id.VFunc);
    io.mapOptional("Args", id.Args);
  }
};

/// Sequences of virtual-function IDs use block formatting.
template <> struct SequenceElementTraits<FunctionSummary::VFuncId> {
  /// Emit sequences of virtual-function IDs in block style.
  static const bool flow = false;
};

/// Sequences of constant virtual calls use block formatting.
template <> struct SequenceElementTraits<FunctionSummary::ConstVCall> {
  /// Emit sequences of constant virtual calls in block style.
  static const bool flow = false;
};

} // End yaml namespace
} // End llvm namespace

namespace llvm {
namespace yaml {

/// YAMLIO mapping traits for \c GlobalValueSummaryYaml.
template <> struct MappingTraits<GlobalValueSummaryYaml> {
  /// Map YAML global-value summary fields to and from YAML.
  /// \param io YAML input/output state.
  /// \param summary Summary being mapped.
  static void mapping(IO &io, GlobalValueSummaryYaml &summary) {
    io.mapOptional("Linkage", summary.Linkage);
    io.mapOptional("Visibility", summary.Visibility);
    io.mapOptional("NotEligibleToImport", summary.NotEligibleToImport);
    io.mapOptional("Live", summary.Live);
    io.mapOptional("Local", summary.IsLocal);
    io.mapOptional("CanAutoHide", summary.CanAutoHide);
    io.mapOptional("ImportType", summary.ImportType);
    io.mapOptional("NoRenameOnPromotion", summary.NoRenameOnPromotion);
    io.mapOptional("Aliasee", summary.Aliasee);
    io.mapOptional("Refs", summary.Refs);
    io.mapOptional("TypeTests", summary.TypeTests);
    io.mapOptional("TypeTestAssumeVCalls", summary.TypeTestAssumeVCalls);
    io.mapOptional("TypeCheckedLoadVCalls", summary.TypeCheckedLoadVCalls);
    io.mapOptional("TypeTestAssumeConstVCalls",
                   summary.TypeTestAssumeConstVCalls);
    io.mapOptional("TypeCheckedLoadConstVCalls",
                   summary.TypeCheckedLoadConstVCalls);
  }
};

/// Sequences of YAML global-value summaries use block formatting.
template <> struct SequenceElementTraits<GlobalValueSummaryYaml> {
  /// Emit sequences of YAML global-value summaries in block style.
  static const bool flow = false;
};

} // End yaml namespace
} // End llvm namespace

namespace llvm {
namespace yaml {

// FIXME: Add YAML mappings for the rest of the module summary.
/// YAMLIO custom mapping traits for \c GlobalValueSummaryMapTy.
template <> struct CustomMappingTraits<GlobalValueSummaryMapTy> {
  /// Parse one GUID-keyed summary list entry into \p V.
  /// \param io YAML input/output state.
  /// \param Key Decimal GUID used as the mapping key.
  /// \param V Destination global-value summary map being populated.
  static void inputOne(IO &io, StringRef Key, GlobalValueSummaryMapTy &V) {
    std::vector<GlobalValueSummaryYaml> GVSums;
    io.mapRequired(Key, GVSums);
    uint64_t KeyInt;
    if (Key.getAsInteger(0, KeyInt)) {
      io.setError("key not an integer");
      return;
    }
    auto &Elem = V.try_emplace(KeyInt, /*IsAnalysis=*/false).first->second;
    for (auto &GVSum : GVSums) {
      GlobalValueSummary::GVFlags GVFlags(
          static_cast<GlobalValue::LinkageTypes>(GVSum.Linkage),
          static_cast<GlobalValue::VisibilityTypes>(GVSum.Visibility),
          GVSum.NotEligibleToImport, GVSum.Live, GVSum.IsLocal,
          GVSum.CanAutoHide,
          static_cast<GlobalValueSummary::ImportKind>(GVSum.ImportType),
          GVSum.NoRenameOnPromotion);
      if (GVSum.Aliasee) {
        auto ASum = std::make_unique<AliasSummary>(GVFlags);
        V.try_emplace(*GVSum.Aliasee, /*IsAnalysis=*/false);
        ValueInfo AliaseeVI(/*IsAnalysis=*/false, &*V.find(*GVSum.Aliasee));
        // Note: Aliasee cannot be filled until all summaries are loaded.
        // This is done in fixAliaseeLinks() which is called in
        // MappingTraits<ModuleSummaryIndex>::mapping().
        ASum->setAliasee(AliaseeVI, /*Aliasee=*/nullptr);
        Elem.addSummary(std::move(ASum));
        continue;
      }
      SmallVector<ValueInfo, 0> Refs;
      Refs.reserve(GVSum.Refs.size());
      for (auto &RefGUID : GVSum.Refs) {
        auto It = V.try_emplace(RefGUID, /*IsAnalysis=*/false).first;
        Refs.push_back(ValueInfo(/*IsAnalysis=*/false, &*It));
      }
      Elem.addSummary(std::make_unique<FunctionSummary>(
          GVFlags, /*NumInsts=*/0, FunctionSummary::FFlags{}, std::move(Refs),
          SmallVector<FunctionSummary::EdgeTy, 0>{}, std::move(GVSum.TypeTests),
          std::move(GVSum.TypeTestAssumeVCalls),
          std::move(GVSum.TypeCheckedLoadVCalls),
          std::move(GVSum.TypeTestAssumeConstVCalls),
          std::move(GVSum.TypeCheckedLoadConstVCalls),
          ArrayRef<FunctionSummary::ParamAccess>{}, ArrayRef<CallsiteInfo>{},
          ArrayRef<AllocInfo>{}));
    }
  }
  /// Write function and alias summaries from \p V sorted by GUID.
  /// \param io YAML input/output state.
  /// \param V Source global-value summary map being written.
  static void output(IO &io, GlobalValueSummaryMapTy &V) {
    // Sort by GUID for deterministic output.
    for (const auto &P : V.sortedRange()) {
      std::vector<GlobalValueSummaryYaml> GVSums;
      for (auto &Sum : P.second.getSummaryList()) {
        if (auto *FSum = dyn_cast<FunctionSummary>(Sum.get())) {
          std::vector<uint64_t> Refs;
          Refs.reserve(FSum->refs().size());
          for (auto &VI : FSum->refs())
            Refs.push_back(VI.getGUID());
          GVSums.push_back(GlobalValueSummaryYaml{
              FSum->flags().Linkage, FSum->flags().Visibility,
              static_cast<bool>(FSum->flags().NotEligibleToImport),
              static_cast<bool>(FSum->flags().Live),
              static_cast<bool>(FSum->flags().DSOLocal),
              static_cast<bool>(FSum->flags().CanAutoHide),
              FSum->flags().ImportType,
              static_cast<bool>(FSum->flags().NoRenameOnPromotion),
              /*Aliasee=*/std::nullopt, Refs, FSum->type_tests(),
              FSum->type_test_assume_vcalls(), FSum->type_checked_load_vcalls(),
              FSum->type_test_assume_const_vcalls(),
              FSum->type_checked_load_const_vcalls()});
        } else if (auto *ASum = dyn_cast<AliasSummary>(Sum.get());
                   ASum && ASum->hasAliasee()) {
          GVSums.push_back(GlobalValueSummaryYaml{
              ASum->flags().Linkage, ASum->flags().Visibility,
              static_cast<bool>(ASum->flags().NotEligibleToImport),
              static_cast<bool>(ASum->flags().Live),
              static_cast<bool>(ASum->flags().DSOLocal),
              static_cast<bool>(ASum->flags().CanAutoHide),
              ASum->flags().ImportType,
              static_cast<bool>(ASum->flags().NoRenameOnPromotion),
              /*Aliasee=*/ASum->getAliaseeGUID()});
        }
      }
      if (!GVSums.empty())
        io.mapRequired(llvm::utostr(P.first), GVSums);
    }
  }
  /// Resolve deferred alias-to-aliasee summary pointers in \p V.
  /// \param V Global-value summary map whose alias links are being fixed.
  static void fixAliaseeLinks(GlobalValueSummaryMapTy &V) {
    for (auto &P : V) {
      for (auto &Sum : P.second.getSummaryList()) {
        if (auto *Alias = dyn_cast<AliasSummary>(Sum.get())) {
          ValueInfo AliaseeVI = Alias->getAliaseeVI();
          auto AliaseeSL = AliaseeVI.getSummaryList();
          if (AliaseeSL.empty()) {
            ValueInfo EmptyVI;
            Alias->setAliasee(EmptyVI, nullptr);
          } else
            Alias->setAliasee(AliaseeVI, AliaseeSL[0].get());
        }
      }
    }
  }
};

/// YAMLIO custom mapping traits for \c TypeIdSummaryMapTy.
template <> struct CustomMappingTraits<TypeIdSummaryMapTy> {
  /// Parse one type-ID name entry into \p V.
  /// \param io YAML input/output state.
  /// \param Key Type-ID name used as the mapping key.
  /// \param V Destination type-ID summary map being populated.
  static void inputOne(IO &io, StringRef Key, TypeIdSummaryMapTy &V) {
    TypeIdSummary TId;
    io.mapRequired(Key, TId);
    V.insert({GlobalValue::getGUIDAssumingExternalLinkage(Key), {Key, TId}});
  }
  /// Write each type-ID summary in \p V keyed by its type-ID name.
  /// \param io YAML input/output state.
  /// \param V Source type-ID summary map being written.
  static void output(IO &io, TypeIdSummaryMapTy &V) {
    for (auto &TidIter : V)
      io.mapRequired(TidIter.second.first, TidIter.second.second);
  }
};

/// YAMLIO mapping traits for a name/GUID pair.
template <> struct MappingTraits<std::pair<StringRef, GlobalValue::GUID>> {
  /// Map name and GUID fields to and from YAML.
  /// \param io YAML input/output state.
  /// \param NameAndGUID Name/GUID pair being mapped.
  static void mapping(IO &io,
                      std::pair<StringRef, GlobalValue::GUID> &NameAndGUID) {
    io.mapRequired("Name", NameAndGUID.first);
    io.mapRequired("GUID", NameAndGUID.second);
  }
};

/// Pair of a symbol name and its GUID for YAML I/O.
using StringAndGUID = std::pair<llvm::StringRef, llvm::GlobalValue::GUID>;

/// Sequences of name/GUID pairs use block formatting.
template <> struct SequenceElementTraits<llvm::yaml::StringAndGUID> {
  /// Emit sequences of name/GUID pairs in block style.
  static const bool flow = false;
};

} // namespace yaml
} // namespace llvm

namespace llvm {
namespace yaml {

/// YAMLIO mapping traits for \c ModuleSummaryIndex.
template <> struct MappingTraits<ModuleSummaryIndex> {
  /// Map module summary index fields to and from YAML.
  /// \param io YAML input/output state.
  /// \param index Module summary index being mapped.
  static void mapping(IO &io, ModuleSummaryIndex& index) {
    io.mapOptional("GlobalValueMap", index.GlobalValueMap);
    if (!io.outputting())
      CustomMappingTraits<GlobalValueSummaryMapTy>::fixAliaseeLinks(
          index.GlobalValueMap);

    if (io.outputting()) {
      io.mapOptional("TypeIdMap", index.TypeIdMap);
    } else {
      TypeIdSummaryMapTy TypeIdMap;
      io.mapOptional("TypeIdMap", TypeIdMap);
      for (auto &[TypeGUID, TypeIdSummaryMap] : TypeIdMap) {
        // Save type id references in index and point TypeIdMap to use the
        // references owned by index.
        StringRef KeyRef = index.TypeIdSaver.save(TypeIdSummaryMap.first);
        index.TypeIdMap.insert(
            {TypeGUID, {KeyRef, std::move(TypeIdSummaryMap.second)}});
      }
    }

    io.mapOptional("WithGlobalValueDeadStripping",
                   index.WithGlobalValueDeadStripping);

    if (io.outputting()) {
      auto CfiFunctionDefs = index.CfiFunctionDefs.getSortedSymbols();
      io.mapOptional("CfiFunctionDefs", CfiFunctionDefs);
      auto CfiFunctionDecls(index.CfiFunctionDecls.getSortedSymbols());
      io.mapOptional("CfiFunctionDecls", CfiFunctionDecls);
    } else {
      std::vector<std::pair<StringRef, GlobalValue::GUID>> CfiFunctionDefs;
      io.mapOptional("CfiFunctionDefs", CfiFunctionDefs);
      for (auto &[S, G] : CfiFunctionDefs)
        index.CfiFunctionDefs.addSymbolWithThinLTOGUID(S, G);
      std::vector<std::pair<StringRef, GlobalValue::GUID>> CfiFunctionDecls;
      io.mapOptional("CfiFunctionDecls", CfiFunctionDecls);
      for (auto &[S, G] : CfiFunctionDecls)
        index.CfiFunctionDecls.addSymbolWithThinLTOGUID(S, G);
    }
  }
};

} // namespace yaml
} // namespace llvm

#endif
