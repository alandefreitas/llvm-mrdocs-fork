//===- DebugLoc.h - Debug Location Information ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a number of light weight data structures used
// to describe and track debug location information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DEBUGLOC_H
#define LLVM_IR_DEBUGLOC_H

#include "llvm/Config/llvm-config.h"
#include "llvm/IR/TrackingMDRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {

class LLVMContext;
class raw_ostream;
class DILocation;
class Function;

#if LLVM_ENABLE_DEBUGLOC_TRACKING_COVERAGE
#if LLVM_ENABLE_DEBUGLOC_TRACKING_ORIGIN
extern bool DebugLocOriginCollectionEnabled;

struct DbgLocOrigin {
  static constexpr unsigned long MaxDepth = 16;
  using StackTracesTy =
      SmallVector<std::pair<int, std::array<void *, MaxDepth>>, 0>;
  StackTracesTy StackTraces;
  DbgLocOrigin(bool ShouldCollectTrace);
  void addTrace();
  const StackTracesTy &getOriginStackTraces() const { return StackTraces; };
};
#else
struct DbgLocOrigin {
  DbgLocOrigin(bool) {}
};
#endif
// Used to represent different "kinds" of DebugLoc, expressing that the
// instruction it is part of is either normal and should contain a valid
// DILocation, or otherwise describing the reason why the instruction does
// not contain a valid DILocation.
enum class DebugLocKind : uint8_t {
  // The instruction is expected to contain a valid DILocation.
  Normal,
  // The instruction is compiler-generated, i.e. it is not associated with any
  // line in the original source.
  CompilerGenerated,
  // The instruction has intentionally had its source location removed,
  // typically because it was moved outside of its original control-flow and
  // presenting the prior source location would be misleading for debuggers
  // or profilers.
  Dropped,
  // The instruction does not have a known or currently knowable source
  // location, e.g. the attribution is ambiguous in a way that can't be
  // represented, or determining the correct location is complicated and
  // requires future developer effort.
  Unknown,
  // DebugLoc is attached to an instruction that we don't expect to be
  // emitted, and so can omit a valid DILocation; we don't expect to ever try
  // and emit these into the line table, and trying to do so is a sign that
  // something has gone wrong (most likely a DebugLoc leaking from a transient
  // compiler-generated instruction).
  Temporary
};

// Extends a DILocation pointer to also store a DebugLocKind and Origin,
// allowing Debugify to ignore intentionally-empty DebugLocs and display the
// code responsible for generating unintentionally-empty DebugLocs.
// Currently we only need to track the Origin of this DILoc when using a
// DebugLoc that is not annotated (i.e. has DebugLocKind::Normal) and has a
// null DILocation, so only collect the origin stacktrace in those cases.
class DILocAndCoverageTracking : public DbgLocOrigin {
  DILocation *Loc;

public:
  DebugLocKind Kind;
  // Default constructor for empty DebugLocs.
  DILocAndCoverageTracking()
      : DbgLocOrigin(true), Loc(nullptr), Kind(DebugLocKind::Normal) {}
  // Valid or nullptr DILocation*, no annotative DebugLocKind.
  DILocAndCoverageTracking(const DILocation *Loc)
      : DbgLocOrigin(!Loc), Loc(const_cast<DILocation *>(Loc)),
        Kind(DebugLocKind::Normal) {}
  // Explicit DebugLocKind, which always means a nullptr DILocation*.
  DILocAndCoverageTracking(DebugLocKind Kind)
      : DbgLocOrigin(Kind == DebugLocKind::Normal), Loc(nullptr), Kind(Kind) {}

  operator DILocation *() const { return Loc; }
};
template <> struct simplify_type<DILocAndCoverageTracking> {
  using SimpleType = DILocation *;

  static DILocation *getSimplifiedValue(DILocAndCoverageTracking &MD) {
    return MD;
  }
};
template <> struct simplify_type<const DILocAndCoverageTracking> {
  using SimpleType = DILocation *;

  static DILocation *getSimplifiedValue(const DILocAndCoverageTracking &MD) {
    return MD;
  }
};

using DebugLocRef = DILocAndCoverageTracking;
#else
/// Alias for the stored debug-location representation (a \c DILocation * when
/// coverage tracking is disabled).
using DebugLocRef = DILocation *;
#endif // LLVM_ENABLE_DEBUGLOC_TRACKING_COVERAGE

/// A debug info location.
///
/// This class is a wrapper around an \a DILocation
/// pointer.
///
/// To avoid extra includes, \a DebugLoc doubles the \a DILocation API with a
/// one based on relatively opaque \a MDNode pointers.
class DebugLoc {
  DebugLocRef Loc = {};

public:
  /// Construct from an \a DILocation.
  ///
  /// \param L The DILocation to wrap, or null for an empty DebugLoc.
  DebugLoc(const DILocation *L = nullptr) : Loc(const_cast<DILocation *>(L)) {}

#if LLVM_ENABLE_DEBUGLOC_TRACKING_COVERAGE
  DebugLoc(DebugLocKind Kind) : Loc(Kind) {}
  DebugLocKind getKind() const { return Loc.Kind; }
#endif

#if LLVM_ENABLE_DEBUGLOC_TRACKING_COVERAGE
  /// Return a DebugLoc for a temporary, non-emitted instruction location.
  ///
  /// @return A DebugLoc annotated as temporary.
  static inline DebugLoc getTemporary() {
    return DebugLoc(DebugLocKind::Temporary);
  }
  /// Return a DebugLoc for an unknown or unknowable source location.
  ///
  /// @return A DebugLoc annotated as unknown.
  static inline DebugLoc getUnknown() {
    return DebugLoc(DebugLocKind::Unknown);
  }
  /// Return a DebugLoc for a compiler-generated instruction with no source line.
  ///
  /// @return A DebugLoc annotated as compiler-generated.
  static inline DebugLoc getCompilerGenerated() {
    return DebugLoc(DebugLocKind::CompilerGenerated);
  }
  /// Return a DebugLoc marking a location that was intentionally dropped.
  ///
  /// @return A DebugLoc annotated as dropped.
  static inline DebugLoc getDropped() {
    return DebugLoc(DebugLocKind::Dropped);
  }
#else
  /// Return a DebugLoc for a temporary, non-emitted instruction location.
  /// When coverage tracking is disabled, returns an empty DebugLoc.
  ///
  /// @return An empty DebugLoc used as a temporary location.
  static inline DebugLoc getTemporary() { return DebugLoc(); }
  /// Return a DebugLoc for an unknown or unknowable source location.
  /// When coverage tracking is disabled, returns an empty DebugLoc.
  ///
  /// @return An empty DebugLoc used as an unknown location.
  static inline DebugLoc getUnknown() { return DebugLoc(); }
  /// Return a DebugLoc for a compiler-generated instruction with no source line.
  /// When coverage tracking is disabled, returns an empty DebugLoc.
  ///
  /// @return An empty DebugLoc used as a compiler-generated location.
  static inline DebugLoc getCompilerGenerated() { return DebugLoc(); }
  /// Return a DebugLoc marking a location that was intentionally dropped.
  /// When coverage tracking is disabled, returns an empty DebugLoc.
  ///
  /// @return An empty DebugLoc used as a dropped location.
  static inline DebugLoc getDropped() { return DebugLoc(); }
#endif // LLVM_ENABLE_DEBUGLOC_TRACKING_COVERAGE

  /// Merge two instruction debug locations into one.
  ///
  /// When two instructions are combined into a single instruction we also
  /// need to combine the original locations into a single location.
  /// When the locations are the same we can use either location.
  /// When they differ, we need a third location which is distinct from
  /// either. If they share a common scope, use this scope and compare the
  /// line/column pair of the locations with the common scope:
  /// * if both match, keep the line and column;
  /// * if only the line number matches, keep the line and set the column as
  /// 0;
  /// * otherwise set line and column as 0.
  /// If they do not share a common scope the location is ambiguous and can't
  /// be represented in a line entry. In this case, set line and column as 0
  /// and use the scope of any location.
  ///
  /// \param LocA The first location to merge.
  /// \param LocB The second location to merge.
  /// @return The merged debug location.
  LLVM_ABI static DebugLoc getMergedLocation(DebugLoc LocA, DebugLoc LocB);

  /// Try to combine the vector of locations passed as input in a single one.
  /// This function applies getMergedLocation() repeatedly left-to-right.
  ///
  /// \param Locs The locations to be merged.
  /// @return The single merged debug location, or an empty DebugLoc if \p Locs
  /// is empty.
  LLVM_ABI static DebugLoc getMergedLocations(ArrayRef<DebugLoc> Locs);

  /// Return this DebugLoc if non-empty; otherwise return \p Other.
  ///
  /// In coverage-tracking builds, this also accounts for whether this or
  /// \p Other have an annotative DebugLocKind applied, such that if both are
  /// empty but exactly one has an annotation, we prefer that annotated
  /// location.
  ///
  /// \param Other The fallback location used when this DebugLoc is empty.
  /// @return This DebugLoc when non-empty (or annotated); otherwise \p Other.
  DebugLoc orElse(DebugLoc Other) const {
    if (*this)
      return *this;
#if LLVM_ENABLE_DEBUGLOC_TRACKING_COVERAGE
    if (Other)
      return Other;
    if (getKind() != DebugLocKind::Normal)
      return *this;
    if (Other.getKind() != DebugLocKind::Normal)
      return Other;
    return *this;
#else
    return Other;
#endif // LLVM_ENABLE_DEBUGLOC_TRACKING_COVERAGE
  }

#if LLVM_ENABLE_DEBUGLOC_TRACKING_ORIGIN
  const DbgLocOrigin::StackTracesTy &getOriginStackTraces() const {
    return Loc.getOriginStackTraces();
  }
  /// Return a copy of this DebugLoc, recording an origin stack trace.
  ///
  /// @return A copy of this DebugLoc with an added origin stack trace.
  DebugLoc getCopied() const {
    DebugLoc NewDL = *this;
    NewDL.Loc.addTrace();
    return NewDL;
  }
#else
  /// Return a copy of this DebugLoc.
  ///
  /// @return A copy of this DebugLoc.
  DebugLoc getCopied() const { return *this; }
#endif

  /// Get the underlying \a DILocation.
  ///
  /// \pre !*this or \c isa<DILocation>(getAsMDNode()).
  /// @{
  /// Get the underlying \a DILocation.
  ///
  /// @return The wrapped DILocation pointer, which may be null.
  DILocation *get() const { return Loc; }
  /// Convert to the underlying \a DILocation pointer.
  ///
  /// @return The wrapped DILocation pointer, which may be null.
  operator DILocation *() const { return get(); }
  /// Dereference the wrapped \c DILocation pointer.
  ///
  /// @return The wrapped DILocation pointer for member access.
  DILocation *operator->() const { return get(); }
  /// Return a reference to the underlying \a DILocation.
  ///
  /// @return A reference to the wrapped DILocation.
  DILocation &operator*() const { return *get(); }
  /// @}

  /// Check for null.
  ///
  /// Check for null in a way that is safe with broken debug info.  Unlike
  /// the conversion to \c DILocation, this doesn't require that \c Loc is of
  /// the right type.  Important for cases like \a llvm::StripDebugInfo() and
  /// \a Instruction::hasMetadata().
  ///
  /// @return True if this DebugLoc holds a non-null location.
  explicit operator bool() const { return Loc; }

  /// Constants for controlling inlined-at chain updates.
  enum {
    /// Replace the last inlined-at location rather than appending a new one.
    ReplaceLastInlinedAt = true
  };
  /// Rebuild the entire inlined-at chain for this instruction so that the top
  /// of the chain now is inlined-at the new call site.
  ///
  /// \param DL The original debug location whose inlined-at chain is rebuilt.
  /// \param InlinedAt The new outermost inlined-at in the chain.
  /// \param Ctx The LLVM context used to create new DILocation nodes.
  /// \param Cache Map reused to avoid recreating identical DILocation nodes.
  /// @return A DebugLoc whose inlined-at chain ends at \p InlinedAt.
  LLVM_ABI static DebugLoc
  appendInlinedAt(const DebugLoc &DL, DILocation *InlinedAt, LLVMContext &Ctx,
                  DenseMap<const MDNode *, MDNode *> &Cache);

  /// Return true if the source locations match, ignoring isImplicitCode and
  /// source atom info.
  ///
  /// \param Other The debug location to compare against.
  /// @return True if both locations describe the same source position.
  bool isSameSourceLocation(const DebugLoc &Other) const {
    if (get() == Other.get())
      return true;
    return ((bool)*this == (bool)Other) && getLine() == Other.getLine() &&
           getCol() == Other.getCol() && getScope() == Other.getScope() &&
           getInlinedAt() == Other.getInlinedAt();
  }

  /// Return the source line number for this location.
  ///
  /// @return The source line number, or 0 if empty.
  LLVM_ABI unsigned getLine() const;
  /// Return the source column number for this location.
  ///
  /// @return The source column number, or 0 if empty.
  LLVM_ABI unsigned getCol() const;
  /// Return the lexical scope metadata for this location.
  ///
  /// @return The scope MDNode, or null if empty.
  LLVM_ABI MDNode *getScope() const;
  /// Return the inlined-at location for this debug location, or null if none.
  ///
  /// @return The inlined-at DILocation, or null if none.
  LLVM_ABI DILocation *getInlinedAt() const;

  /// Get the fully inlined-at scope for a DebugLoc.
  ///
  /// Gets the inlined-at scope for a DebugLoc.
  ///
  /// @return The outermost inlined-at scope MDNode.
  LLVM_ABI MDNode *getInlinedAtScope() const;

  /// Rebuild the entire inline-at chain by replacing the subprogram at the
  /// end of the chain with NewSP.
  ///
  /// \param DL The original debug location whose inlined-at chain is rebuilt.
  /// \param NewSP The subprogram that replaces the one at the end of the chain.
  /// \param Ctx The LLVM context used to create new DILocation nodes.
  /// \param Cache Map reused to avoid recreating identical DILocation nodes.
  /// @return A DebugLoc whose inlined-at chain ends at \p NewSP.
  LLVM_ABI static DebugLoc
  replaceInlinedAtSubprogram(const DebugLoc &DL, DISubprogram &NewSP,
                             LLVMContext &Ctx,
                             DenseMap<const MDNode *, MDNode *> &Cache);

  /// Find the debug info location for the start of the function.
  ///
  /// Walk up the scope chain of given debug loc and find line number info
  /// for the function.
  ///
  /// FIXME: Remove this.  Users should use DILocation/DILocalScope API to
  /// find the subprogram, and then DILocation::get().
  ///
  /// @return A DebugLoc for the enclosing function's start, or empty if none.
  LLVM_ABI DebugLoc getFnDebugLoc() const;

  /// Return \c this as a bar \a MDNode.
  ///
  /// @return The underlying location as an MDNode pointer.
  LLVM_ABI MDNode *getAsMDNode() const;

  /// Check if the DebugLoc corresponds to an implicit code.
  ///
  /// @return True if this location describes compiler-inserted implicit code.
  LLVM_ABI bool isImplicitCode() const;
  /// Mark whether this debug location describes compiler-inserted implicit
  /// code.
  ///
  /// \param ImplicitCode True if this location describes implicit code.
  LLVM_ABI void setImplicitCode(bool ImplicitCode);

  /// Return true if both locations refer to the same underlying debug-loc
  /// representation.
  ///
  /// \param DL The other DebugLoc to compare against.
  /// @return True if both DebugLocs wrap the same underlying representation.
  bool operator==(const DebugLoc &DL) const { return Loc == DL.Loc; }
  /// Return true if the locations refer to different underlying representations.
  ///
  /// \param DL The other DebugLoc to compare against.
  /// @return True if the DebugLocs wrap different underlying representations.
  bool operator!=(const DebugLoc &DL) const { return Loc != DL.Loc; }

  /// Dump this debug location to stderr for debugging.
  LLVM_ABI void dump() const;

  /// Print this debug location to the given output stream.
  ///
  /// Prints source location /path/to/file.exe:line:col @[inlined at].
  ///
  /// \param OS The stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;
};

} // end namespace llvm

#endif // LLVM_IR_DEBUGLOC_H
