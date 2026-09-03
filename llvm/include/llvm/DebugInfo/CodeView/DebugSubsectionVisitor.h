//===- DebugSubsectionVisitor.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTIONVISITOR_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTIONVISITOR_H

#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/Support/Error.h"

namespace llvm {

namespace codeview {

class DebugChecksumsSubsectionRef;
class DebugSubsectionRecord;
class DebugInlineeLinesSubsectionRef;
class DebugCrossModuleExportsSubsectionRef;
class DebugCrossModuleImportsSubsectionRef;
class DebugFrameDataSubsectionRef;
class DebugLinesSubsectionRef;
class DebugStringTableSubsectionRef;
class DebugSymbolRVASubsectionRef;
class DebugSymbolsSubsectionRef;
class DebugUnknownSubsectionRef;

/// Callback interface for walking CodeView debug subsections.
class DebugSubsectionVisitor {
public:
  /// Destroy the subsection visitor.
  virtual ~DebugSubsectionVisitor() = default;

  /// Visit an unrecognized debug subsection.
  ///
  /// \param Unknown The unknown subsection to visit.
  ///
  /// \returns An Error if visitation fails, or success otherwise.
  virtual Error visitUnknown(DebugUnknownSubsectionRef &Unknown) {
    return Error::success();
  }
  /// Visit a lines debug subsection.
  ///
  /// \param Lines The lines subsection to visit.
  /// \param State Shared string table and file checksums state.
  ///
  /// \returns An Error if visitation fails, or success otherwise.
  virtual Error visitLines(DebugLinesSubsectionRef &Lines,
                           const StringsAndChecksumsRef &State) = 0;
  /// Visit a file checksums debug subsection.
  ///
  /// \param Checksums The file checksums subsection to visit.
  /// \param State Shared string table and file checksums state.
  ///
  /// \returns An Error if visitation fails, or success otherwise.
  virtual Error visitFileChecksums(DebugChecksumsSubsectionRef &Checksums,
                                   const StringsAndChecksumsRef &State) = 0;
  /// Visit an inlinee lines debug subsection.
  ///
  /// \param Inlinees The inlinee lines subsection to visit.
  /// \param State Shared string table and file checksums state.
  ///
  /// \returns An Error if visitation fails, or success otherwise.
  virtual Error visitInlineeLines(DebugInlineeLinesSubsectionRef &Inlinees,
                                  const StringsAndChecksumsRef &State) = 0;
  /// Visit a cross-module exports debug subsection.
  ///
  /// \param CSE The cross-module exports subsection to visit.
  /// \param State Shared string table and file checksums state.
  ///
  /// \returns An Error if visitation fails, or success otherwise.
  virtual Error
  visitCrossModuleExports(DebugCrossModuleExportsSubsectionRef &CSE,
                          const StringsAndChecksumsRef &State) = 0;
  /// Visit a cross-module imports debug subsection.
  ///
  /// \param CSE The cross-module imports subsection to visit.
  /// \param State Shared string table and file checksums state.
  ///
  /// \returns An Error if visitation fails, or success otherwise.
  virtual Error
  visitCrossModuleImports(DebugCrossModuleImportsSubsectionRef &CSE,
                          const StringsAndChecksumsRef &State) = 0;

  /// Visit a string table debug subsection.
  ///
  /// \param ST The string table subsection to visit.
  /// \param State Shared string table and file checksums state.
  ///
  /// \returns An Error if visitation fails, or success otherwise.
  virtual Error visitStringTable(DebugStringTableSubsectionRef &ST,
                                 const StringsAndChecksumsRef &State) = 0;

  /// Visit a symbols debug subsection.
  ///
  /// \param CSE The symbols subsection to visit.
  /// \param State Shared string table and file checksums state.
  ///
  /// \returns An Error if visitation fails, or success otherwise.
  virtual Error visitSymbols(DebugSymbolsSubsectionRef &CSE,
                             const StringsAndChecksumsRef &State) = 0;

  /// Visit a frame data debug subsection.
  ///
  /// \param FD The frame data subsection to visit.
  /// \param State Shared string table and file checksums state.
  ///
  /// \returns An Error if visitation fails, or success otherwise.
  virtual Error visitFrameData(DebugFrameDataSubsectionRef &FD,
                               const StringsAndChecksumsRef &State) = 0;
  /// Visit a COFF symbol RVA debug subsection.
  ///
  /// \param RVAs The COFF symbol RVA subsection to visit.
  /// \param State Shared string table and file checksums state.
  ///
  /// \returns An Error if visitation fails, or success otherwise.
  virtual Error visitCOFFSymbolRVAs(DebugSymbolRVASubsectionRef &RVAs,
                                    const StringsAndChecksumsRef &State) = 0;
};

/// Dispatch a single debug subsection record to the appropriate visitor method.
///
/// \param R The subsection record to visit.
/// \param V The visitor to receive the subsection.
/// \param State Shared string table and file checksums state.
///
/// \returns An Error if visitation fails, or success otherwise.
LLVM_ABI Error visitDebugSubsection(const DebugSubsectionRecord &R,
                                    DebugSubsectionVisitor &V,
                                    const StringsAndChecksumsRef &State);

namespace detail {
template <typename T>
Error visitDebugSubsections(T &&FragmentRange, DebugSubsectionVisitor &V,
                            StringsAndChecksumsRef &State) {
  State.initialize(std::forward<T>(FragmentRange));

  for (const DebugSubsectionRecord &L : FragmentRange) {
    if (auto EC = visitDebugSubsection(L, V, State))
      return EC;
  }
  return Error::success();
}
} // namespace detail

/// Visit each debug subsection in \p FragmentRange.
///
/// \param FragmentRange Range of debug subsection records to visit.
/// \param V The visitor to receive each subsection.
///
/// \returns An Error if visitation fails, or success otherwise.
template <typename T>
Error visitDebugSubsections(T &&FragmentRange, DebugSubsectionVisitor &V) {
  StringsAndChecksumsRef State;
  return detail::visitDebugSubsections(std::forward<T>(FragmentRange), V,
                                       State);
}

/// Visit each debug subsection in \p FragmentRange using the given string table.
///
/// \param FragmentRange Range of debug subsection records to visit.
/// \param V The visitor to receive each subsection.
/// \param Strings String table used to resolve subsection references.
///
/// \returns An Error if visitation fails, or success otherwise.
template <typename T>
Error visitDebugSubsections(T &&FragmentRange, DebugSubsectionVisitor &V,
                            const DebugStringTableSubsectionRef &Strings) {
  StringsAndChecksumsRef State(Strings);
  return detail::visitDebugSubsections(std::forward<T>(FragmentRange), V,
                                       State);
}

/// Visit each debug subsection in \p FragmentRange using the given string table
/// and checksums.
///
/// \param FragmentRange Range of debug subsection records to visit.
/// \param V The visitor to receive each subsection.
/// \param Strings String table used to resolve subsection references.
/// \param Checksums File checksums used to resolve subsection references.
///
/// \returns An Error if visitation fails, or success otherwise.
template <typename T>
Error visitDebugSubsections(T &&FragmentRange, DebugSubsectionVisitor &V,
                            const DebugStringTableSubsectionRef &Strings,
                            const DebugChecksumsSubsectionRef &Checksums) {
  StringsAndChecksumsRef State(Strings, Checksums);
  return detail::visitDebugSubsections(std::forward<T>(FragmentRange), V,
                                       State);
}

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTIONVISITOR_H
