//===----- llvm/MC/DXContainerInfo.h - DXContainer Info ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_DXCONTAINERINFO_H
#define LLVM_MC_DXCONTAINERINFO_H

#include "llvm/ADT/SmallString.h"
#include "llvm/BinaryFormat/DXContainer.h"

namespace llvm {

class raw_ostream;

/// MC-layer helpers for reading and writing DXContainer (DXBC) parts.
namespace mcdxbc {

/// In-memory representation of a shader debug-name (ILDN) part.
struct DebugName {
  /// On-disk header fields for the debug-name part.
  dxbc::DebugNameHeader Parameters;
  /// Debug file name stored after the header.
  StringRef Filename;

  /// Construct an empty debug-name part with a zeroed header.
  DebugName() : Parameters{0, 0} {}
  /// Construct a debug-name part from an existing header and file name.
  ///
  /// \param Parameters - Debug-name header to store.
  /// \param Filename - Debug file name associated with the part.
  DebugName(dxbc::DebugNameHeader &Parameters, StringRef Filename)
      : Parameters(Parameters), Filename(Filename) {}

  /// Set the debug file name and update the header name length.
  ///
  /// \param DebugFilename - Debug file name to store.
  LLVM_ABI void setFilename(StringRef DebugFilename);
  /// Serialize the debug-name part to \p OS.
  ///
  /// \param OS - Stream to write the part to.
  LLVM_ABI void write(raw_ostream &OS) const;
};

/// In-memory representation of a compiler-version (VERS) part.
struct CompilerVersion {
  /// On-disk header fields for the compiler-version part.
  dxbc::CompilerVersionHeader Parameters;
  /// Git commit SHA of the compiler that produced the object.
  StringRef CommitSha;
  /// Custom compiler version string stored after the commit SHA.
  StringRef CustomVersionString;

  /// Construct a compiler-version part filled from the current LLVM build.
  LLVM_ABI CompilerVersion();

  /// Set the commit SHA and refresh the header content size.
  ///
  /// \param CommitSha - Git commit SHA to store.
  LLVM_ABI void setCommitSha(StringRef CommitSha);
  /// Set the custom version string and refresh the header content size.
  ///
  /// \param VersionString - Custom compiler version string to store.
  LLVM_ABI void setVersionString(StringRef VersionString);
  /// Serialize the compiler-version part to \p OS.
  ///
  /// \param OS - Stream to write the part to.
  LLVM_ABI void write(raw_ostream &OS) const;

private:
  void updateContentSize();
};

/// In-memory representation of a source-info (SRCI) part.
struct SourceInfo {
  /// Common base for one section inside a source-info part.
  struct Section {
    /// Generic section header shared by all source-info sections.
    dxbc::SourceInfo::SectionHeader GenericHeader;

    /// Fill \c GenericHeader from a content size and section type.
    ///
    /// \param ContentSize - Size in bytes of the section body after the
    ///        generic header.
    /// \param Type - Kind of source-info section being described.
    LLVM_ABI void computeGenericHeader(uint32_t ContentSize,
                                       dxbc::SourceInfo::SectionType Type);
  };

  /// Source-file contents section of a source-info part.
  struct SourceContents : public Section {
    /// One uncompressed source-file content entry.
    struct Entry {
      /// On-disk fields for this content entry.
      dxbc::SourceInfo::Contents::Entry Parameters;
      /// Source file text stored after the entry header.
      std::string FileContent;

      /// Compute Parameters based on FileContent.
      LLVM_ABI void compute();
    };

    /// On-disk header for the source-contents section.
    dxbc::SourceInfo::Contents::Header Parameters;
    /// Per-file content entries in the same order as the names section.
    SmallVector<Entry> Entries;

    /// Compute Parameters based on the content of Args.
    /// Sizes are computed assuming CompressionType == None.
    ///
    /// \param Type - Compression type to record in the section header.
    LLVM_ABI void
    computeUncompressed(dxbc::SourceInfo::Contents::CompressionType Type);
    /// Update Parameters based on the compressed size of section content.
    ///
    /// \param CompressedSize - Size in bytes of the (possibly compressed)
    ///        entries payload.
    LLVM_ABI void computeFinalSize(uint32_t CompressedSize);
  };

  /// Source-file names section of a source-info part.
  struct SourceNames : public Section {
    /// Host-endian header for the source-names section.
    struct Header {
      /// Reserved flags; must be zero.
      uint32_t Flags = 0;
      /// Number of name entries that follow the header.
      uint32_t Count = 0;
      /// Total size in bytes of the name entries after this header.
      uint16_t EntriesSizeInBytes = 0;

      /// Construct an empty names-section header.
      Header() = default;
      /// Construct a names-section header from its on-disk layout.
      ///
      /// \param H - Little-endian on-disk header to decode.
      LLVM_ABI Header(const dxbc::SourceInfo::Names::HeaderOnDisk &H);

      /// Byte-swap multi-byte fields of this header.
      void swapBytes() {
        sys::swapByteOrder(Flags);
        sys::swapByteOrder(Count);
        sys::swapByteOrder(EntriesSizeInBytes);
      }
    };

    /// One source-file name entry.
    struct Entry {
      /// On-disk fields for this name entry.
      dxbc::SourceInfo::Names::Entry Parameters;
      /// Source file name stored after the entry header.
      StringRef FileName;

      /// Compute Parameters based on FileName and FileContent.
      ///
      /// \param ContentSize - Size in bytes of the matching file content,
      ///        including the null terminator.
      LLVM_ABI void compute(uint32_t ContentSize);
    };

    /// Host-endian header for the source-names section.
    Header Parameters;
    /// Per-file name entries in the same order as the contents section.
    SmallVector<Entry> Entries;

    /// Compute headers based on the content of entries.
    LLVM_ABI void compute();
  };

  /// Compiler command-line arguments section of a source-info part.
  struct ProgramArgs : public Section {
    /// One argument name/value pair from the compiler invocation.
    using Entry = std::pair<StringRef, StringRef>;

    /// On-disk header for the compiler-arguments section.
    dxbc::SourceInfo::Args::Header Parameters;
    /// Argument name/value pairs in invocation order.
    SmallVector<Entry> Args;

    /// Compute Parameters based on Args.
    LLVM_ABI void compute();
  };

  /// Top-level source-info part header.
  dxbc::SourceInfo::Header Parameters;
  /// Source-file names section.
  SourceNames Names;
  /// Source-file contents section.
  SourceContents Contents;
  /// Compiler command-line arguments section.
  ProgramArgs Args;

  /// Compute Parameters based on the content of sections.
  LLVM_ABI void compute();
};

/// Helper for reading and writing SourceInfo (SRCI) data.
///
/// This structure is used to represent the extracted data in an inspectable and
/// modifiable format, and can be used to serialize the data back into valid
/// SourceInfo.
struct SourceInfoBuilder {
  /// True once \c computeEntries() has populated \c BaseData from inputs.
  bool IsFilled = false;
  /// True once \c finalize() has computed headers and compressed contents.
  bool IsFinalized = false;
  /// Inspectable source-info payload built from added files and arguments.
  SourceInfo BaseData;
  /// Serialized (and possibly compressed) source-contents payload.
  SmallString<128> CompressedContents;

  /// Set the compression type used when finalizing source contents.
  ///
  /// \param Type - Compression type to apply to the contents section.
  void setCompressionType(dxbc::SourceInfo::Contents::CompressionType Type) {
    CompressionType = Type;
  }

  /// Record a source file name and its content for later serialization.
  ///
  /// \param Name - Source file name.
  /// \param Content - Source file text.
  void addFile(StringRef Name, StringRef Content) {
    FileNamesAndContents.emplace_back(Name, Content);
  }
  /// Record a compiler argument name/value pair for later serialization.
  ///
  /// \param Name - Argument name.
  /// \param Value - Argument value.
  void addArg(StringRef Name, StringRef Value) {
    Args.emplace_back(Name, Value);
  }

  /// Populate \c BaseData entries from the recorded files and arguments.
  LLVM_ABI void computeEntries();
  /// Compute section headers, compress contents, and mark the builder final.
  LLVM_ABI void finalize();
  /// Serialize the finalized source-info part to \p OS.
  ///
  /// \param OS - Stream to write the part to.
  LLVM_ABI void write(raw_ostream &OS) const;

private:
  std::optional<dxbc::SourceInfo::Contents::CompressionType> CompressionType;
  SmallVector<std::pair<StringRef, StringRef>> FileNamesAndContents;
  SmallVector<std::pair<StringRef, StringRef>> Args;

  void recomputeAfterCompression(uint32_t CompressedSize);
};

} // namespace mcdxbc
} // namespace llvm

#endif // LLVM_MC_DXCONTAINERINFO_H
