//==-- llvm/FileCheck/FileCheck.h --------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file has some utilities to use FileCheck as an API
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FILECHECK_FILECHECK_H
#define LLVM_FILECHECK_FILECHECK_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/SMLoc.h"
#include <bitset>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class MemoryBuffer;
class SourceMgr;
template <typename T> class SmallVectorImpl;

/// Contains info about various FileCheck options.
struct FileCheckRequest {
  /// Prefixes for check directives (e.g. "CHECK").
  std::vector<StringRef> CheckPrefixes;
  /// Prefixes that mark comment lines to ignore.
  std::vector<StringRef> CommentPrefixes;
  /// If true, do not canonicalize whitespace in the input.
  bool NoCanonicalizeWhiteSpace = false;
  /// Patterns that must not appear anywhere in the input.
  std::vector<StringRef> ImplicitCheckNot;
  /// Global variable definitions of the form "VAR=value".
  std::vector<StringRef> GlobalDefines;
  /// If true, allow the input file to be empty.
  bool AllowEmptyInput = false;
  /// If true, allow check prefixes that are never used.
  bool AllowUnusedPrefixes = false;
  /// If true, patterns must match entire lines.
  bool MatchFullLines = false;
  /// If true, matching is case-insensitive.
  bool IgnoreCase = false;
  /// If true, the default "CHECK" prefix is in use.
  bool IsDefaultCheckPrefix = false;
  /// If true, variables go out of scope at CHECK-LABEL.
  bool EnableVarScope = false;
  /// If true, allow overlapping matches among CHECK-DAG groups.
  bool AllowDeprecatedDagOverlap = false;
  /// If true, emit extra diagnostics for discarded matches.
  bool Verbose = false;
  /// If true, emit even more verbose diagnostics.
  bool VerboseVerbose = false;
};

/// Utilities describing FileCheck directive kinds and modifiers.
namespace Check {

/// Kinds of FileCheck directives and related parse outcomes.
enum FileCheckKind {
  /// No check kind (default / unset).
  CheckNone = 0,
  /// A misspelled check directive prefix.
  CheckMisspelled,
  /// A plain CHECK directive.
  CheckPlain,
  /// A CHECK-NEXT directive.
  CheckNext,
  /// A CHECK-SAME directive.
  CheckSame,
  /// A CHECK-NOT directive.
  CheckNot,
  /// A CHECK-DAG directive.
  CheckDAG,
  /// A CHECK-LABEL directive.
  CheckLabel,
  /// A CHECK-EMPTY directive.
  CheckEmpty,
  /// A comment directive (not a real check).
  CheckComment,

  /// Indicates the pattern only matches the end of file. This is used for
  /// trailing CHECK-NOTs.
  CheckEOF,

  /// Marks when parsing found a -NOT check combined with another CHECK suffix.
  CheckBadNot,

  /// Marks when parsing found a -COUNT directive with invalid count value.
  CheckBadCount
};

/// Modifiers that can be applied to a FileCheck directive.
enum FileCheckKindModifier {
  /// Modifies directive to perform literal match.
  ModifierLiteral = 0,

  /// Number of modifier enumerators (not a modifier itself).
  Size
};

/// Describes a FileCheck directive's kind, count, and modifiers.
class FileCheckType {
  FileCheckKind Kind;
  int Count; ///< optional Count for some checks
  /// Modifers for the check directive.
  std::bitset<FileCheckKindModifier::Size> Modifiers;

public:
  /// Construct a FileCheckType with the given \p Kind.
  /// \param Kind The directive kind; defaults to \c CheckNone.
  FileCheckType(FileCheckKind Kind = CheckNone) : Kind(Kind), Count(1) {}
  /// Copy-construct a FileCheckType.
  /// \param Other The FileCheckType to copy.
  FileCheckType(const FileCheckType &Other) = default;
  /// Copy-assign a FileCheckType.
  /// \param Other The FileCheckType to copy from.
  /// \returns A reference to this FileCheckType.
  FileCheckType &operator=(const FileCheckType &Other) = default;

  /// Convert to the underlying FileCheckKind.
  /// \returns The underlying FileCheckKind.
  operator FileCheckKind() const { return Kind; }

  /// Return the expected match count for this check.
  /// \returns The expected match count.
  int getCount() const { return Count; }
  /// Set the expected match count to \p C and return this.
  /// \param C The new match count.
  /// \returns A reference to this FileCheckType.
  LLVM_ABI FileCheckType &setCount(int C);

  /// Return true if this check uses literal (non-regex) matching.
  /// \returns true if this check uses literal matching.
  bool isLiteralMatch() const {
    return Modifiers[FileCheckKindModifier::ModifierLiteral];
  }
  /// Enable or disable literal matching and return this.
  /// \param Literal Whether literal matching should be enabled.
  /// \returns A reference to this FileCheckType.
  FileCheckType &setLiteralMatch(bool Literal = true) {
    Modifiers.set(FileCheckKindModifier::ModifierLiteral, Literal);
    return *this;
  }

  /// Return a description of this check type using \p Prefix.
  /// \param Prefix The check prefix to include in the description.
  /// \returns A description of this check type.
  LLVM_ABI std::string getDescription(StringRef Prefix) const;

  /// Return a description of the active modifiers.
  /// \returns A description of the active modifiers.
  LLVM_ABI std::string getModifiersDescription() const;
};
} // namespace Check

class MatchResultDiag;

/// Abstract base class for recording a FileCheck diagnostic for a pattern
/// (e.g., \c CHECK-NEXT directive or \c --implicit-check-not).
///
/// \c FileCheckDiag has two direct derived classes:
/// - \c MatchResultDiag records a match result for a pattern.  There might be
///   more than one for a single pattern.  For example, for \c CHECK-DAG there
///   might be several discarded matches before either a good match or a failure
///   to match.
/// - \c MatchNoteDiag provides an additional note about the most recent
///   \c MatchResultDiag emitted by a FileCheck invocation.  For example, there
///   might be a fuzzy match after a failure to match.
///
/// Throughout this class hierarchy, a pattern is said to be either expected or
/// excluded depending on whether the pattern must have or must not have a match
/// in order for it to succeed.  For example, a \c CHECK directive's pattern is
/// expected, and a \c CHECK-NOT directive's pattern is excluded.
class FileCheckDiag {
public:
  /// Discriminator for concrete FileCheckDiag subclasses.
  enum FileCheckDiagKind {
    /// First MatchResultDiag kind value.
    MatchResultDiag_First,
    /// A MatchFoundDiag (pattern matched the input).
    MatchFoundDiag = MatchResultDiag_First,
    /// A MatchNoneDiag (pattern did not match the input).
    MatchNoneDiag,
    /// Last MatchResultDiag kind value.
    MatchResultDiag_Last = MatchNoneDiag,
    /// First MatchNoteDiag kind value.
    MatchNoteDiag_First,
    /// A MatchFuzzyDiag (fuzzy match suggestion).
    MatchFuzzyDiag = MatchNoteDiag_First,
    /// A MatchCustomNoteDiag (custom note text).
    MatchCustomNoteDiag,
    /// Last MatchNoteDiag kind value.
    MatchNoteDiag_Last = MatchCustomNoteDiag
  };

private:
  const FileCheckDiagKind Kind;

public:
  /// Construct a diagnostic of the given \p Kind.
  /// \param Kind Discriminator for the concrete diagnostic subclass.
  FileCheckDiag(FileCheckDiagKind Kind) : Kind(Kind) {}
  /// Destructor is purely virtual to ensure this remains an abstract class.
  virtual ~FileCheckDiag() = 0;
  /// Of what derived class is this an instance?
  /// \returns The discriminator for the concrete diagnostic subclass.
  FileCheckDiagKind getKind() const { return Kind; }
  /// If this is a \c MatchResultDiag, return itself.  If this is a
  /// \c MatchNoteDiag, return its associated \c MatchResultDiag.
  /// \returns The associated \c MatchResultDiag.
  virtual const MatchResultDiag &getMatchResultDiag() const = 0;
  /// Does this diagnostic reveal a new error?
  ///
  /// For \c MatchResultDiag, \c !isError() is not always the same as a
  /// successful pattern match result.  For \c MatchNoteDiag, \c !isError()
  /// does not indicate the lack of an error but rather the lack of an
  /// additional error beyond its associated \c MatchResultDiag.  See
  /// documentation on derived types for details.
  /// \returns true if this diagnostic reveals a new error.
  virtual bool isError() const = 0;
  /// Return the input range matched by this diagnostic, if any.
  ///
  /// Returns the range for text that was matched in some way (e.g., successful
  /// pattern match, discarded pattern match, or variable capture), or
  /// \c std::nullopt if the diagnostic has no such input range.
  /// \returns The matched input range, or \c std::nullopt if none.
  virtual std::optional<SMRange> getMatchRange() const = 0;
};

/// Abstract base class for recording a FileCheck diagnostic that reports a
/// match result for a pattern.
class MatchResultDiag : public FileCheckDiag {
private:
  Check::FileCheckType CheckTy;
  SMLoc CheckLoc;
  SMRange SearchRange;

public:
  /// Construct a match-result diagnostic.
  /// \param Kind Discriminator for the concrete MatchResultDiag subclass.
  /// \param CheckTy Type of the pattern being matched.
  /// \param CheckLoc Location of the pattern in the check file.
  /// \param SearchRange Input range searched for a match.
  MatchResultDiag(FileCheckDiagKind Kind, const Check::FileCheckType &CheckTy,
                  SMLoc CheckLoc, SMRange SearchRange)
      : FileCheckDiag(Kind), CheckTy(CheckTy), CheckLoc(CheckLoc),
        SearchRange(SearchRange) {}
  /// Destructor is purely virtual to ensure this remains an abstract class.
  virtual ~MatchResultDiag() = 0;
  /// Is \p FCD an instance of \c MatchResultDiag?
  /// \param FCD Diagnostic to test.
  /// \returns true if \p FCD is a \c MatchResultDiag.
  static bool classof(const FileCheckDiag *FCD) {
    FileCheckDiagKind Kind = FCD->getKind();
    return MatchResultDiag_First <= Kind && Kind <= MatchResultDiag_Last;
  }
  /// Get itself.
  /// \returns A reference to this \c MatchResultDiag.
  const MatchResultDiag &getMatchResultDiag() const override { return *this; }
  /// What is the type of pattern for this match result?
  /// \returns The type of the pattern for this match result.
  Check::FileCheckType getCheckTy() const { return CheckTy; }
  /// Where is the pattern for this match result?
  /// \returns The location of the pattern in the check file.
  SMLoc getCheckLoc() const { return CheckLoc; }
  /// What is the search range for the match result?
  /// \returns The input range searched for a match.
  SMRange getSearchRange() const { return SearchRange; }
};

/// \c MatchResultDiag for a pattern that matched the input.
class MatchFoundDiag : public MatchResultDiag {
public:
  /// Outcome of a pattern that matched the input.
  enum StatusTy {
    /// Indicates a good match for an expected pattern.
    Success,
    /// Indicates a match for an excluded pattern (error).
    Excluded,
    /// Indicates a match for an expected pattern, but the match is on the
    /// wrong line (error).
    WrongLine,
    /// Indicates a discarded match for an expected pattern (not an error).
    Discarded
  };

private:
  StatusTy Status;
  SMRange MatchRange;

public:
  /// Construct a diagnostic for a pattern that matched.
  /// \param CheckTy Type of the pattern.
  /// \param CheckLoc Location of the pattern in the check file.
  /// \param Status Whether the match is a success or which failure kind.
  /// \param MatchRange Input range that was matched.
  /// \param SearchRange Input range that was searched.
  MatchFoundDiag(const Check::FileCheckType &CheckTy, SMLoc CheckLoc,
                 StatusTy Status, SMRange MatchRange, SMRange SearchRange)
      : MatchResultDiag(FileCheckDiag::MatchFoundDiag, CheckTy, CheckLoc,
                        SearchRange),
        Status(Status), MatchRange(MatchRange) {}
  /// Is \p FCD an instance of \c MatchFoundDiag?
  /// \param FCD Diagnostic to test.
  /// \returns true if \p FCD is a \c MatchFoundDiag.
  static bool classof(const FileCheckDiag *FCD) {
    return FCD->getKind() == FileCheckDiag::MatchFoundDiag;
  }
  /// Does this match produce an error?
  ///
  /// This is not always the same as \c getStatus()!=Success.  For example,
  /// \c CHECK-DAG discarded matches are neither successful matches nor errors.
  /// \returns true if this match is an error.
  bool isError() const override {
    return Status != Success && Status != Discarded;
  }
  /// Was this a successful match?  If not, why not?
  ///
  /// See \c isError comments for the relationship between the two.
  /// \returns The status describing the outcome of the match.
  StatusTy getStatus() const { return Status; }
  /// Adjust a successful status to a non-successful status.
  ///
  /// This is designed to be called while emitting diagnostics.  It is not
  /// designed to be called by a diagnostic presentation layer like
  /// `-dump-input`.
  ///
  /// For example, a match that was originally thought to be successful might
  /// later be discarded, or it might be determined that it violates a matching
  /// constraint (e.g., wrong line).
  /// \param S The unsuccessful status to assign.
  void markUnsuccessful(StatusTy S) {
    assert(Status == Success && S != Success &&
           "expected to change successful status to unsuccessful");
    Status = S;
  }
  /// Return the match's input range, never \c std::nullopt.
  /// \returns The match's input range.
  std::optional<SMRange> getMatchRange() const override { return MatchRange; }
};

/// \c MatchResultDiag for a pattern that did not match the input.
class MatchNoneDiag : public MatchResultDiag {
public:
  /// Outcome of a pattern that did not match the input.
  enum StatusTy {
    /// Indicates no match for an excluded pattern.
    Success,
    /// Indicates no match due to an expected or excluded pattern that has
    /// proven to be invalid at match time (error).  The exact problems are
    /// usually reported in subsequent \c MatchNoteDiag objects.
    InvalidPattern,
    /// Indicates no match for an expected pattern (error).  In some cases, it
    /// follows good matches (because multiple matches are expected) or
    /// discarded matches for the pattern.
    Expected
  };

private:
  StatusTy Status;

public:
  /// Construct a diagnostic for a pattern that did not match.
  /// \param CheckTy Type of the pattern.
  /// \param CheckLoc Location of the pattern in the check file.
  /// \param Status Whether the lack of match is a success or which failure
  ///        kind.
  /// \param SearchRange Input range that was searched.
  MatchNoneDiag(const Check::FileCheckType &CheckTy, SMLoc CheckLoc,
                StatusTy Status, SMRange SearchRange)
      : MatchResultDiag(FileCheckDiag::MatchNoneDiag, CheckTy, CheckLoc,
                        SearchRange),
        Status(Status) {}
  /// Is \p FCD an instance of \c MatchNoneDiag?
  /// \param FCD Diagnostic to test.
  /// \returns true if \p FCD is a \c MatchNoneDiag.
  static bool classof(const FileCheckDiag *FCD) {
    return FCD->getKind() == FileCheckDiag::MatchNoneDiag;
  }
  /// Does the lack of match represent an error?
  /// \returns true if the lack of match is an error.
  bool isError() const override { return Status != Success; }
  /// Does the lack of a match indicate a success?  If not, why not?
  /// \returns The status describing why there was no match.
  StatusTy getStatus() const { return Status; }
  /// Return \c std::nullopt.
  /// \returns \c std::nullopt; a non-match has no match range.
  std::optional<SMRange> getMatchRange() const override { return std::nullopt; }
};

/// Abstract base class for recording a FileCheck diagnostic that provides an
/// additional note (possibly a new error) about the most recent
/// \c MatchResultDiag.
class MatchNoteDiag : public FileCheckDiag {
private:
  const MatchResultDiag *MRD;

public:
  /// Construct a note diagnostic of the given \p Kind.
  /// \param Kind Discriminator for the concrete MatchNoteDiag subclass.
  MatchNoteDiag(FileCheckDiagKind Kind) : FileCheckDiag(Kind), MRD(nullptr) {}
  /// Destructor is purely virtual to ensure this remains an abstract class.
  virtual ~MatchNoteDiag() = 0;
  /// Is \p FCD an instance of \c MatchNoteDiag?
  /// \param FCD Diagnostic to test.
  /// \returns true if \p FCD is a \c MatchNoteDiag.
  static bool classof(const FileCheckDiag *FCD) {
    FileCheckDiagKind Kind = FCD->getKind();
    return MatchNoteDiag_First <= Kind && Kind <= MatchNoteDiag_Last;
  }
  /// Get the note's associated \c MatchResultDiag.
  /// \returns The associated \c MatchResultDiag.
  const MatchResultDiag &getMatchResultDiag() const override { return *MRD; }
  /// Set the note's associated \c MatchResultDiag.
  /// \param MRDNew The match-result diagnostic this note elaborates.
  void setMatchResultDiag(const MatchResultDiag *MRDNew) {
    assert(!MRD && "expected setMatchResultDiag to be called only once");
    MRD = MRDNew;
  }
};

/// \c MatchNoteDiag for a fuzzy match that serves as a suggestion for the next
/// intended match for an expected pattern with too few or no good matches.
class MatchFuzzyDiag : public MatchNoteDiag {
private:
  SMLoc MatchStart;

public:
  /// Construct a fuzzy-match note at \p MatchStart.
  /// \param MatchStart Start location of the suggested fuzzy match.
  MatchFuzzyDiag(SMLoc MatchStart)
      : MatchNoteDiag(FileCheckDiag::MatchFuzzyDiag), MatchStart(MatchStart) {}
  /// Is \p FCD an instance of \c MatchFuzzyDiag?
  /// \param FCD Diagnostic to test.
  /// \returns true if \p FCD is a \c MatchFuzzyDiag.
  static bool classof(const FileCheckDiag *FCD) {
    return FCD->getKind() == FileCheckDiag::MatchFuzzyDiag;
  }
  /// Always false.  A fuzzy match is not an error even though it is performed
  /// due to an error.
  /// \returns false; a fuzzy match is never itself an error.
  bool isError() const override { return false; }
  /// Return an input range (never \c std::nullopt) starting and ending at the
  /// match start.  The actual match end is not computed.
  /// \returns An input range starting and ending at the match start.
  std::optional<SMRange> getMatchRange() const override {
    return SMRange(MatchStart, MatchStart);
  }
};

/// \c MatchNoteDiag with a custom note not described by any other class derived
/// from \c MatchNoteDiag.
class MatchCustomNoteDiag : public MatchNoteDiag {
private:
  std::string Note;
  bool AddsError;
  std::optional<SMRange> MatchRange;

public:
  /// Construct a custom note about a match result, with an optional match
  /// range.
  ///
  /// If \p MatchRange is specified, it is a range for input text that was
  /// matched in some way (e.g., variable capture) and that is described by
  /// this note.  Either way, as usual, the associated \c MatchResultDiag has
  /// any full match range for the pattern.
  ///
  /// If \p AddsError is true, then this note indicates a \a new error that is
  /// distinct from any error indicated by the associated \c MatchResultDiag.
  /// The error is described by \c Note, which must be worded appropriately for
  /// prepending "error: " when presented later.  For example, the associated
  /// \c MatchResultDiag might indicate a match to either an expected pattern
  /// (success) or an excluded pattern (error), and \c Note might be "unable to
  /// represent numeric value" to indicate the match could not be processed
  /// afterward.
  ///
  /// If \p AddsError is false, then this note merely provides additional
  /// information about the associated \c MatchResultDiag.  That information
  /// might be something harmless (e.g., variable substitution), or it might be
  /// one of potentially many problems summarized as an error by the
  /// \c MatchResultDiag (e.g., one way in which the pattern was invalid).
  /// \param MatchRange Input range described by this note.
  /// \param Note Text of the note.
  /// \param AddsError Whether this note reports an additional error.
  MatchCustomNoteDiag(SMRange MatchRange, StringRef Note,
                      bool AddsError = false)
      : MatchNoteDiag(FileCheckDiag::MatchCustomNoteDiag), Note(Note),
        AddsError(AddsError), MatchRange(MatchRange) {}
  /// Construct a custom note with no match range and no additional error.
  /// \param Note Text of the note.
  MatchCustomNoteDiag(StringRef Note)
      : MatchNoteDiag(FileCheckDiag::MatchCustomNoteDiag), Note(Note),
        AddsError(false) {}
  /// Is \p FCD an instance of \c MatchCustomNoteDiag?
  /// \param FCD Diagnostic to test.
  /// \returns true if \p FCD is a \c MatchCustomNoteDiag.
  static bool classof(const FileCheckDiag *FCD) {
    return FCD->getKind() == FileCheckDiag::MatchCustomNoteDiag;
  }
  /// Return the custom note text.
  /// \returns The custom note text.
  const std::string &getNote() const { return Note; }
  /// Does this note indicate an \a additional error not indicated by the
  /// associated \c MatchResultDiag?
  ///
  /// For details, see the \c MatchCustomNoteDiag::MatchCustomNoteDiag comments
  /// for its \c AddsError parameter.
  /// \returns true if this note reports an additional error.
  bool isError() const override { return AddsError; }
  /// Return the match range described by the note, or \c std::nullopt if none.
  /// \returns The match range described by the note, or \c std::nullopt if
  ///          none.
  std::optional<SMRange> getMatchRange() const override { return MatchRange; }
};

/// A \c FileCheckDiag series emitted by the FileCheck library.
class FileCheckDiagList {
private:
  MatchResultDiag *CurMatchResultDiag = nullptr;
  using vector_type = std::vector<std::unique_ptr<FileCheckDiag>>;
  vector_type DiagList;

public:
  /// Emplace a new \c FileCheckDiag of type \c DiagTy.  If it's a
  /// \c MatchNoteDiag, associate it with its \c MatchResultDiag.
  ///
  /// \c FileCheckTest.cpp calls \c Pattern::printVariableDefs directly, so it
  /// can add a \c MatchNoteDiag without a previous \c MatchResultDiag.
  /// Otherwise, there should always be a previous \c MatchResultDiag.
  /// \param Args Constructor arguments forwarded to \c DiagTy.
  template <typename DiagTy, typename... ArgTys>
  void emplace(ArgTys &&...Args) {
    DiagList.emplace_back(
        std::make_unique<DiagTy>(std::forward<ArgTys>(Args)...));
    FileCheckDiag *Diag = DiagList.back().get();
    if (MatchResultDiag *MRD = dyn_cast<MatchResultDiag>(Diag)) {
      CurMatchResultDiag = MRD;
      return;
    }
    MatchNoteDiag *Note = cast<MatchNoteDiag>(Diag);
    if (!CurMatchResultDiag)
      return;
    Note->setMatchResultDiag(CurMatchResultDiag);
  }
  /// Adjust the previous \c MatchResultDiag, which must be a \c MatchFoundDiag,
  /// from successful status to unsuccessful status.
  /// \param Status The unsuccessful status to assign.
  void adjustPrevMatchFoundDiag(MatchFoundDiag::StatusTy Status) {
    cast<MatchFoundDiag>(CurMatchResultDiag)->markUnsuccessful(Status);
  }
  /// Const forward iterator over diagnostics in the list.
  class const_iterator {
    friend FileCheckDiagList;

  public:
    /// Signed type for distances between iterators.
    using difference_type = std::ptrdiff_t;
    /// Element type referred to by the iterator.
    using value_type = FileCheckDiag;
    /// Pointer to a const diagnostic.
    using pointer = const FileCheckDiag *;
    /// Reference to a const diagnostic.
    using reference = const FileCheckDiag &;
    /// Iterator category tag (forward iterator).
    using iterator_category = std::forward_iterator_tag;

  private:
    vector_type::const_iterator Itr;
    const_iterator(vector_type::const_iterator Itr) : Itr(Itr) {}

  public:
    /// Dereference to the current diagnostic.
    /// \returns A reference to the current diagnostic.
    reference operator*() const { return **Itr; }
    /// Access a member of the current diagnostic.
    /// \returns A pointer to the current diagnostic.
    pointer operator->() const { return &operator*(); }
    /// Advance to the next diagnostic and return this iterator.
    /// \returns A reference to this iterator after advancing.
    const_iterator &operator++() {
      ++Itr;
      return *this;
    }
    /// Advance to the next diagnostic and return the previous iterator.
    /// \param Unused Unused postfix-discriminator parameter.
    /// \returns A copy of the iterator before advancing.
    const_iterator operator++(int Unused) {
      const_iterator Old = *this;
      ++Itr;
      return Old;
    }
    /// Return true if both iterators refer to the same position.
    /// \param Other Iterator to compare against.
    /// \returns true if both iterators refer to the same position.
    bool operator==(const const_iterator &Other) const {
      return Itr == Other.Itr;
    }
    /// Return true if the iterators refer to different positions.
    /// \param Other Iterator to compare against.
    /// \returns true if the iterators refer to different positions.
    bool operator!=(const const_iterator &Other) const {
      return Itr != Other.Itr;
    }
  };

  /// Unsigned type used for sizes and indices.
  using size_type = vector_type::size_type;
  /// Return an iterator to the first diagnostic.
  /// \returns An iterator to the first diagnostic.
  const_iterator begin() const { return const_iterator(DiagList.begin()); }
  /// Return an iterator past the last diagnostic.
  /// \returns An iterator past the last diagnostic.
  const_iterator end() const { return const_iterator(DiagList.end()); }
  /// Return the diagnostic at index \p I.
  /// \param I Zero-based index of the diagnostic.
  /// \returns A const reference to the diagnostic at index \p I.
  const FileCheckDiag &operator[](size_type I) const { return *DiagList[I]; }
  /// Return the number of diagnostics in the list.
  /// \returns The number of diagnostics in the list.
  size_type size() const { return DiagList.size(); }
};

/// Holds pattern-matching context shared while checking an input.
class FileCheckPatternContext;
/// A CHECK string and its associated patterns from the check file.
struct FileCheckString;

/// FileCheck class takes the request and exposes various methods that
/// use information from the request.
class FileCheck {
  FileCheckRequest Req;
  std::unique_ptr<FileCheckPatternContext> PatternContext;
  std::vector<FileCheckString> CheckStrings;

public:
  /// Construct a FileCheck instance for the given request.
  /// \param Req Options controlling FileCheck behavior.
  LLVM_ABI explicit FileCheck(FileCheckRequest Req);
  /// Destroy this FileCheck instance.
  LLVM_ABI ~FileCheck();

  /// Reads the check file from \p Buffer and records the expected strings it
  /// contains. Errors are reported against \p SM.
  ///
  /// If \p ImpPatBufferIDRange, then the range (inclusive start, exclusive end)
  /// of IDs for source buffers added to \p SM for implicit patterns are
  /// recorded in it.  The range is empty if there are none.
  /// \param SM Source manager used to report errors and hold buffers.
  /// \param Buffer Contents of the check file.
  /// \param ImpPatBufferIDRange Optional out-parameter for implicit-pattern
  ///        buffer IDs.
  /// \returns false if the check file could not be read successfully.
  LLVM_ABI bool
  readCheckFile(SourceMgr &SM, StringRef Buffer,
                std::pair<unsigned, unsigned> *ImpPatBufferIDRange = nullptr);

  /// Validate that the configured check prefixes are well-formed.
  /// \returns true if all check prefixes are valid.
  LLVM_ABI bool ValidateCheckPrefixes();

  /// Canonicalizes whitespaces in the file. Line endings are replaced with
  /// UNIX-style '\n'.
  /// \param MB Memory buffer whose contents are canonicalized.
  /// \param OutputBuffer Storage for the canonicalized contents.
  /// \returns A reference to the canonicalized contents in \p OutputBuffer.
  LLVM_ABI StringRef CanonicalizeFile(MemoryBuffer &MB,
                                      SmallVectorImpl<char> &OutputBuffer);

  /// Check \p Buffer against the expected strings from the check file.
  ///
  /// Diagnostics are recorded in \p Diags when provided. Errors are recorded
  /// against \p SM.
  ///
  /// \param SM Source manager used to report errors.
  /// \param Buffer Input text to check.
  /// \param Diags Optional list that receives emitted diagnostics.
  /// \returns false if the input fails to satisfy the checks.
  LLVM_ABI bool checkInput(SourceMgr &SM, StringRef Buffer,
                           FileCheckDiagList *Diags = nullptr);
};

} // namespace llvm

#endif
