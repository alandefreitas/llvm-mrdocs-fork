//===-- SpecialCaseList.h - special case list for sanitizers ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// This file implements a Special Case List for code sanitizers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SPECIALCASELIST_H
#define LLVM_SUPPORT_SPECIALCASELIST_H

#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
class MemoryBuffer;
class StringRef;

namespace vfs {
class FileSystem;
}

/// This is a utility class used to parse user-provided text files with "special case lists" for code sanitizers.
///
/// Such files are used to define an "ABI list" for DataFlowSanitizer and allow/exclusion lists for sanitizers like AddressSanitizer or UndefinedBehaviorSanitizer. Empty lines and lines starting with "#" are ignored. Sections are defined using a '[section_name]' header and can be used to specify sanitizers the entries below it apply to. Section names are globs, and entries without a section header match all sections (e.g. an '[*]' header is assumed.) The remaining lines should have the form: prefix:glob_pattern[=category] If category is not specified, it is assumed to be empty string. Definitions of "prefix" and "category" are sanitizer-specific. For example, sanitizer exclusion support prefixes "src", "mainfile", "fun" and "global". "glob_pattern" defines source files, main files, functions or globals which shouldn't be instrumented. Examples of categories: "functional": used in DFSan to list functions with pure functional semantics. "init": used in ASan exclusion list to disable initialization-order bugs detection for certain globals or source files. Full special case list file example: --- [address] # Excluded items: fun:*_ZN4base6subtle* global:*global_with_bad_access_or_initialization* global:*global_with_initialization_issues*=init type:*Namespace::ClassName*=init src:file_with_tricky_code.cc src:ignore-global-initializers-issues.cc=init mainfile:main_file.cc [dataflow] # Functions with pure functional semantics: fun:cos=functional fun:sin=functional ---
class SpecialCaseList {
public:
  /// Sentinel returned by \c inSectionBlame when no matching entry is found.
  static constexpr std::pair<unsigned, unsigned> NotFound = {0, 0};
  /// Parses the special case list entries from files. On failure, returns
  /// 0 and writes an error message to string.
  ///
  /// \param Paths Paths to special case list files to parse.
  /// \param FS File system used to open \p Paths.
  /// \param Error Receives an error message on failure.
  /// \return A SpecialCaseList on success, or null on failure.
  LLVM_ABI static std::unique_ptr<SpecialCaseList>
  create(const std::vector<std::string> &Paths, llvm::vfs::FileSystem &FS,
         std::string &Error);
  /// Parses the special case list from a memory buffer. On failure, returns
  /// 0 and writes an error message to string.
  ///
  /// \param MB Memory buffer containing the special case list text.
  /// \param Error Receives an error message on failure.
  /// \return A SpecialCaseList on success, or null on failure.
  LLVM_ABI static std::unique_ptr<SpecialCaseList>
  create(const MemoryBuffer *MB, std::string &Error);
  /// Parses the special case list entries from files. On failure, reports a
  /// fatal error.
  ///
  /// \param Paths Paths to special case list files to parse.
  /// \param FS File system used to open \p Paths.
  /// \return A SpecialCaseList owned by the caller.
  LLVM_ABI static std::unique_ptr<SpecialCaseList>
  createOrDie(const std::vector<std::string> &Paths, llvm::vfs::FileSystem &FS);

  /// Destroys the special case list and releases owned section data.
  LLVM_ABI ~SpecialCaseList();

  /// Returns true, if special case list contains a line
  /// \code
  ///   @Prefix:<E>=@Category
  /// \endcode
  /// where @Query satisfies the glob <E> in a given @Section.
  ///
  /// \param Section Section name to match against section headers.
  /// \param Prefix Entry prefix to look up (for example, "src" or "fun").
  /// \param Query String tested against the entry's glob pattern.
  /// \param Category Optional category; empty matches the default category.
  /// \return True if a matching special case list entry exists.
  LLVM_ABI bool inSection(StringRef Section, StringRef Prefix, StringRef Query,
                          StringRef Category = StringRef()) const;

  /// Returns the file index and the line number <FileIdx, LineNo> corresponding
  /// to the special case list entry if the special case list contains a line
  /// \code
  ///   @Prefix:<E>=@Category
  /// \endcode
  /// where @Query satisfies the glob <E> in a given @Section.
  /// Returns (zero, zero) if there is no exclusion entry corresponding to this
  /// expression.
  ///
  /// \param Section Section name to match against section headers.
  /// \param Prefix Entry prefix to look up (for example, "src" or "fun").
  /// \param Query String tested against the entry's glob pattern.
  /// \param Category Optional category; empty matches the default category.
  /// \return A pair of file index and line number, or \c NotFound if no match.
  LLVM_ABI std::pair<unsigned, unsigned>
  inSectionBlame(StringRef Section, StringRef Prefix, StringRef Query,
                 StringRef Category = StringRef()) const;

protected:
  // Implementations of the create*() functions that can also be used by derived
  // classes.
  /// Parse special case list entries from files for use by derived classes.
  ///
  /// \param Paths Paths to special case list files to parse.
  /// \param VFS File system used to open \p Paths.
  /// \param Error Receives an error message on failure.
  /// \return True on success, false on failure.
  LLVM_ABI bool createInternal(const std::vector<std::string> &Paths,
                               vfs::FileSystem &VFS, std::string &Error);
  /// Parse a special case list from a memory buffer for use by derived classes.
  ///
  /// \param MB Memory buffer containing the special case list text.
  /// \param Error Receives an error message on failure.
  /// \return True on success, false on failure.
  LLVM_ABI bool createInternal(const MemoryBuffer *MB, std::string &Error);

  /// Construct an empty special case list.
  SpecialCaseList() = default;
  /// Deleted copy constructor; SpecialCaseList is not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  SpecialCaseList(SpecialCaseList const &Other) = delete;
  /// Deleted copy assignment; SpecialCaseList is not copyable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  SpecialCaseList &operator=(SpecialCaseList const &Other) = delete;

  /// A named section of special case list entries from one input file.
  class Section {
  public:
    /// Construct a section with the given name and originating file index.
    ///
    /// \param Name Section name (the text inside the brackets).
    /// \param FileIdx Index of the file that defined this section.
    /// \param UseGlobs Whether section and entry patterns use globs.
    LLVM_ABI Section(StringRef Name, unsigned FileIdx, bool UseGlobs);
    /// Move-construct a section, taking ownership of its implementation.
    ///
    /// \param Other Section to move from.
    LLVM_ABI Section(Section &&Other);
    /// Destroy the section and release its implementation.
    LLVM_ABI ~Section();

    /// Return the section name (the entire string inside the brackets).
    ///
    /// \return The section name string.
    StringRef name() const { return Name; }

    /// Return true if \p Name matches this section name interpreted as a glob.
    ///
    /// \param Name Candidate section name to match.
    /// \return True if \p Name matches this section's name pattern.
    LLVM_ABI bool matchName(StringRef Name) const;

    /// Return the sequence number of the file where this section is defined.
    ///
    /// \return The zero-based index of the input file that defined this section.
    unsigned fileIndex() const { return FileIdx; }

    /// Search by prefix, query, and category for the defining rule line.
    ///
    /// Returns the 1-based line number on which the matching rule is defined,
    /// or 0 if there is no match.
    ///
    /// \param Prefix Entry prefix to look up (for example, "src" or "fun").
    /// \param Query String tested against the entry's glob pattern.
    /// \param Category Optional category; empty matches the default category.
    /// \return The 1-based line number of the matching rule, or 0 if none.
    LLVM_ABI unsigned getLastMatch(StringRef Prefix, StringRef Query,
                                   StringRef Category) const;

    /// Returns true if the section has any entries for the given prefix.
    ///
    /// \param Prefix Entry prefix to look up (for example, "src" or "fun").
    /// \return True if this section has at least one entry with \p Prefix.
    LLVM_ABI bool hasPrefix(StringRef Prefix) const;

  private:
    friend class SpecialCaseList;
    class SectionImpl;

    StringRef Name;
    unsigned FileIdx;
    std::unique_ptr<SectionImpl> Impl;
  };

  /// Return all parsed sections in this special case list.
  ///
  /// \return An ArrayRef of the sections parsed from the input files.
  ArrayRef<const Section> sections() const { return Sections; }

private:
  BumpPtrAllocator StrAlloc;
  std::vector<Section> Sections;

  LLVM_ABI Expected<Section *> addSection(StringRef SectionStr,
                                          unsigned FileIdx, unsigned LineNo,
                                          bool UseGlobs);

  /// Parses just-constructed SpecialCaseList entries from a memory buffer.
  LLVM_ABI bool parse(unsigned FileIdx, const MemoryBuffer *MB,
                      std::string &Error);
};

} // namespace llvm

#endif // LLVM_SUPPORT_SPECIALCASELIST_H
