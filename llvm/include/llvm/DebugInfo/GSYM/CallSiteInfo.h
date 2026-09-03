//===- CallSiteInfo.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_CALLSITEINFO_H
#define LLVM_DEBUGINFO_GSYM_CALLSITEINFO_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/DebugInfo/GSYM/GsymTypes.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <vector>

namespace llvm {
class raw_ostream;

namespace yaml {
/// YAML root mapping for call-site data loaded from a YAML file.
struct FunctionsYAML;
} // namespace yaml

/// Namespace for the GSYM debug info format.
namespace gsym {
class FileWriter;
class GsymCreator;
class GsymDataExtractor;
struct FunctionInfo;

/// Call site information for a function in a GSYM file.
///
/// Encodes the return offset of a call, optional regex patterns that match
/// called function names, and flags describing the call target scope.
struct CallSiteInfo {
  /// Bit flags describing constraints on the call site target.
  enum Flags : uint8_t {
    /// No call site flags set.
    None = 0,
    /// The call site can only call a function within the same link unit.
    InternalCall = 1 << 0,
    /// The call site can only call a function outside its link unit.
    ExternalCall = 1 << 1,

    LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue*/ ExternalCall),
  };

  /// The return offset of the call site - relative to the function start.
  uint64_t ReturnOffset = 0;

  /// Offsets into the string table for function names regex patterns.
  std::vector<gsym_strp_t> MatchRegex;

  /// Bitwise OR of CallSiteInfo::Flags values
  uint8_t Flags = CallSiteInfo::Flags::None;

  /// Equality comparison operator for CallSiteInfo.
  ///
  /// \param RHS The CallSiteInfo to compare against.
  /// \returns True if both CallSiteInfo objects are equal.
  bool operator==(const CallSiteInfo &RHS) const {
    return ReturnOffset == RHS.ReturnOffset && MatchRegex == RHS.MatchRegex &&
           Flags == RHS.Flags;
  }

  /// Inequality comparison operator for CallSiteInfo.
  ///
  /// \param RHS The CallSiteInfo to compare against.
  /// \returns True if the CallSiteInfo objects are not equal.
  bool operator!=(const CallSiteInfo &RHS) const { return !(*this == RHS); }

  /// Decode a CallSiteInfo object from a binary data stream.
  ///
  /// \param Data The binary stream to read the data from.
  /// \param Offset The current offset within the data stream.
  /// \returns A CallSiteInfo or an error describing the issue.
  LLVM_ABI static llvm::Expected<CallSiteInfo> decode(GsymDataExtractor &Data,
                                                      uint64_t &Offset);

  /// Encode this CallSiteInfo object into a FileWriter stream.
  ///
  /// \param O The binary stream to write the data to.
  /// \returns An error object that indicates success or failure.
  LLVM_ABI llvm::Error encode(FileWriter &O) const;
};

/// Collection of CallSiteInfo entries for a single function.
struct CallSiteInfoCollection {
  /// Call sites belonging to this function.
  std::vector<CallSiteInfo> CallSites;

  /// Equality comparison operator for CallSiteInfoCollection.
  ///
  /// \param RHS The CallSiteInfoCollection to compare against.
  /// \returns True if both CallSiteInfoCollection objects are equal.
  bool operator==(const CallSiteInfoCollection &RHS) const {
    return CallSites == RHS.CallSites;
  }
  /// Inequality comparison operator for CallSiteInfoCollection.
  ///
  /// \param RHS The CallSiteInfoCollection to compare against.
  /// \returns True if the CallSiteInfoCollection objects are not equal.
  bool operator!=(const CallSiteInfoCollection &RHS) const {
    return !(*this == RHS);
  }

  /// Decode a CallSiteInfoCollection object from a binary data stream.
  ///
  /// \param Data The binary stream to read the data from.
  /// \returns A CallSiteInfoCollection or an error describing the issue.
  LLVM_ABI static llvm::Expected<CallSiteInfoCollection>
  decode(GsymDataExtractor &Data);

  /// Encode this CallSiteInfoCollection object into a FileWriter stream.
  ///
  /// \param O The binary stream to write the data to.
  /// \returns An error object that indicates success or failure.
  LLVM_ABI llvm::Error encode(FileWriter &O) const;
};

/// Loads call site information from YAML and attaches it to FunctionInfo
/// objects.
class CallSiteInfoLoader {
public:
  /// Constructor that initializes the CallSiteInfoLoader with necessary data
  /// structures.
  ///
  /// \param GCreator A reference to the GsymCreator.
  /// \param Funcs A reference to the FunctionInfo vector to update with call
  /// site data.
  CallSiteInfoLoader(GsymCreator &GCreator, std::vector<FunctionInfo> &Funcs)
      : GCreator(GCreator), Funcs(Funcs) {}

  /// Reads the specified YAML file and updates call site information.
  ///
  /// Parses the YAML content and updates the `Funcs` vector with call site
  /// information based on the YAML data.
  ///
  /// \param YAMLFile A StringRef representing the path to the YAML file to be
  /// loaded.
  /// \returns An `llvm::Error` indicating success or describing any issues
  /// encountered during the loading process.
  LLVM_ABI llvm::Error loadYAML(StringRef YAMLFile);

private:
  /// Builds a map from function names to FunctionInfo pointers based on the
  /// provided `Funcs` vector.
  ///
  /// \param Funcs A reference to a vector of FunctionInfo objects.
  /// \returns A StringMap mapping function names (StringRef) to their
  /// corresponding FunctionInfo pointers.
  StringMap<FunctionInfo *> buildFunctionMap();

  /// Processes the parsed YAML functions and updates the `FuncMap` accordingly.
  ///
  /// \param FuncYAMLs A constant reference to an llvm::yaml::FunctionsYAML
  /// object containing parsed YAML data.
  /// \param FuncMap A reference to a StringMap mapping function names to
  /// FunctionInfo pointers.
  /// \returns An `llvm::Error` indicating success or describing any issues
  /// encountered during processing.
  llvm::Error processYAMLFunctions(const llvm::yaml::FunctionsYAML &FuncYAMLs,
                                   StringMap<FunctionInfo *> &FuncMap);

  /// Reference to the parent Gsym Creator object.
  GsymCreator &GCreator;

  /// Reference to the vector of FunctionInfo objects to be populated.
  std::vector<FunctionInfo> &Funcs;
};

/// Stream a human-readable representation of \p CSI to \p OS.
///
/// \param OS Destination stream.
/// \param CSI Call site info to print.
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const CallSiteInfo &CSI);
/// Stream a human-readable representation of \p CSIC to \p OS.
///
/// \param OS Destination stream.
/// \param CSIC Call site info collection to print.
/// \returns A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS,
                                 const CallSiteInfoCollection &CSIC);

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_CALLSITEINFO_H
