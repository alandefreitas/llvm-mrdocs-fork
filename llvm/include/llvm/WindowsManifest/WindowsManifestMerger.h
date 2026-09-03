//===-- WindowsManifestMerger.h ---------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file provides a utility for merging Microsoft .manifest files.  These
// files are xml documents which contain meta-information about applications,
// such as whether or not admin access is required, system compatibility,
// versions, etc.  Part of the linking process of an executable may require
// merging several of these .manifest files using a tree-merge following
// specific rules.  Unfortunately, these rules are not documented well
// anywhere.  However, a careful investigation of the behavior of the original
// Microsoft Manifest Tool (mt.exe) revealed the rules of this merge.  As the
// saying goes, code is the best documentation, so please look below if you are
// interested in the exact merging requirements.
//
// Ref:
// https://msdn.microsoft.com/en-us/library/windows/desktop/aa374191(v=vs.85).aspx
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_WINDOWSMANIFEST_WINDOWSMANIFESTMERGER_H
#define LLVM_WINDOWSMANIFEST_WINDOWSMANIFESTMERGER_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {

class MemoryBuffer;
class MemoryBufferRef;

/// Utilities for merging Microsoft Windows application .manifest XML files.
namespace windows_manifest {

/// Return true if Windows manifest merging is available (libxml2 was built in).
///
/// \returns True when libxml2 support was compiled in; otherwise false.
LLVM_ABI bool isAvailable();

/// Error describing a failure while merging Windows .manifest files.
class LLVM_ABI WindowsManifestError
    : public ErrorInfo<WindowsManifestError, ECError> {
public:
  /// RTTI identifier used by ErrorInfo::classID.
  static char ID;
  /// Construct an error with human-readable message \p Msg.
  ///
  /// \param Msg Description of the merge or parse failure.
  WindowsManifestError(const Twine &Msg);
  /// Write this error's message to \p OS.
  ///
  /// \param OS Stream to receive the message.
  void log(raw_ostream &OS) const override;

private:
  std::string Msg;
};

/// Merges multiple Microsoft Windows .manifest XML documents into one.
class WindowsManifestMerger {
public:
  /// Construct an empty merger with no input manifests.
  LLVM_ABI WindowsManifestMerger();
  /// Destroy the merger and release any merged document state.
  LLVM_ABI ~WindowsManifestMerger();
  /// Merge \p Manifest into the combined result using mt.exe-compatible rules.
  ///
  /// \param Manifest Buffer holding one .manifest XML document to merge in.
  /// \returns Success, or a WindowsManifestError on parse or merge failure.
  LLVM_ABI Error merge(MemoryBufferRef Manifest);

  /// Return the merged .manifest as a memory buffer, or nullptr if empty.
  ///
  /// \returns Owned buffer of the merged XML, or nullptr when no manifests
  /// were merged.
  LLVM_ABI std::unique_ptr<MemoryBuffer> getMergedManifest();

private:
  class WindowsManifestMergerImpl;
  std::unique_ptr<WindowsManifestMergerImpl> Impl;
};

} // namespace windows_manifest
} // namespace llvm
#endif
