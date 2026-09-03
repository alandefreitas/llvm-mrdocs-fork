//===-- FileCollector.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FILECOLLECTOR_H
#define LLVM_SUPPORT_FILECOLLECTOR_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <mutex>
#include <string>

namespace llvm {
/// Virtual file system that records accessed paths in a FileCollector.
class FileCollectorFileSystem;
class Twine;

/// Base class for collecting unique file and directory paths.
class LLVM_ABI FileCollectorBase {
public:
  /// Construct an empty file collector base.
  FileCollectorBase();
  /// Destroy the file collector base.
  virtual ~FileCollectorBase();

  /// Record \p file if it has not already been seen.
  ///
  /// \param file Path of the file to collect.
  void addFile(const Twine &file);
  /// Record \p Dir and the files it contains.
  ///
  /// \param Dir Path of the directory to collect.
  void addDirectory(const Twine &Dir);

protected:
  /// Record \p Path so later lookups can skip it.
  ///
  /// \param Path Path to record as already processed.
  /// \return True if \p Path was newly recorded; false if empty or already seen.
  bool markAsSeen(StringRef Path) {
    if (Path.empty())
      return false;
    return Seen.insert(Path).second;
  }

  /// Collect a newly seen file at \p SrcPath.
  ///
  /// \param SrcPath Path of the file to collect.
  virtual void addFileImpl(StringRef SrcPath) = 0;

  /// Collect files in \p Dir using \p FS and return a directory iterator.
  ///
  /// \param Dir Directory whose entries should be collected.
  /// \param FS File system used to iterate the directory.
  /// \param EC Set if directory iteration fails.
  /// \return A directory iterator positioned at the first entry of \p Dir.
  virtual llvm::vfs::directory_iterator
  addDirectoryImpl(const llvm::Twine &Dir,
                   IntrusiveRefCntPtr<vfs::FileSystem> FS,
                   std::error_code &EC) = 0;

  /// Synchronizes access to internal data structures.
  std::mutex Mutex;

  /// Tracks already seen files so they can be skipped.
  StringSet<> Seen;
};

/// Captures file system interaction and generates data to be later replayed
/// with the RedirectingFileSystem.
///
/// For any file that gets accessed we eventually create:
/// - a copy of the file inside Root
/// - a record in RedirectingFileSystem mapping that maps:
///   current real path -> path to the copy in Root
///
/// That intent is that later when the mapping is used by RedirectingFileSystem
/// it simulates the state of FS that we collected.
///
/// We generate file copies and mapping lazily - see writeMapping and copyFiles.
/// We don't try to capture the state of the file at the exact time when it's
/// accessed. Files might get changed, deleted ... we record only the "final"
/// state.
///
/// In order to preserve the relative topology of files we use their real paths
/// as relative paths inside of the Root.
class LLVM_ABI FileCollector : public FileCollectorBase {
public:
  /// Helper utility that encapsulates the logic for canonicalizing a virtual
  /// path and a path to copy from.
  class PathCanonicalizer {
  public:
    /// Pair of a real source path and its canonical virtual path.
    struct PathStorage {
      /// Real path used as the copy source.
      SmallString<256> CopyFrom;
      /// Canonical virtual path used in the VFS overlay.
      SmallString<256> VirtualPath;
    };

    /// Canonicalize a pair of virtual and real paths.
    ///
    /// \param SrcPath Path to convert into overlay and copy-from forms.
    /// \return The virtual path and corresponding real copy-from path.
    LLVM_ABI PathStorage canonicalize(StringRef SrcPath);

    /// Return the underlying file system.
    ///
    /// \return The file system used to resolve paths.
    vfs::FileSystem &getFileSystem() const { return *VFS; };

    /// Construct a canonicalizer that resolves paths through \p VFS.
    ///
    /// \param VFS File system used to make paths absolute and resolve
    ///        directories.
    explicit PathCanonicalizer(IntrusiveRefCntPtr<vfs::FileSystem> VFS)
        : VFS(std::move(VFS)) {}

  private:
    /// Replace with a (mostly) real path, or don't modify. Resolves symlinks
    /// in the directory, using \a CachedDirs to avoid redundant lookups, but
    /// leaves the filename as a possible symlink.
    void updateWithRealPath(SmallVectorImpl<char> &Path);

    IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS;

    StringMap<std::string> CachedDirs;
  };

  /// Construct a collector that copies accessed files into a replayable overlay.
  ///
  /// The root directory is created in copyFiles unless it already exists.
  ///
  /// \param Root Directory where collected files will be stored.
  /// \param OverlayRoot VFS mapping root used when writing the overlay.
  /// \param VFS File system used to canonicalize collected paths.
  FileCollector(std::string Root, std::string OverlayRoot,
                IntrusiveRefCntPtr<vfs::FileSystem> VFS);

  /// Write the yaml mapping (for the VFS) to the given file.
  ///
  /// \param MappingFile Path of the YAML file to write.
  /// \return An error code if writing fails; a success code otherwise.
  std::error_code writeMapping(StringRef MappingFile);

  /// Copy the files into the root directory.
  ///
  /// When StopOnError is true (the default) we abort as soon as one file
  /// cannot be copied. This is relatively common, for example when a file was
  /// removed after it was added to the mapping.
  ///
  /// \param StopOnError If true, return on the first copy failure.
  /// \return An error code if copying fails; a success code otherwise.
  std::error_code copyFiles(bool StopOnError = true);

  /// Create a VFS that uses \p Collector to collect files accessed via \p
  /// BaseFS.
  ///
  /// \param BaseFS Underlying file system whose accesses are recorded.
  /// \param Collector Collector that records each accessed path.
  /// \return A VFS that records accesses through \p Collector.
  static IntrusiveRefCntPtr<vfs::FileSystem>
  createCollectorVFS(IntrusiveRefCntPtr<vfs::FileSystem> BaseFS,
                     std::shared_ptr<FileCollector> Collector);

private:
  friend FileCollectorFileSystem;

  void addFileToMapping(StringRef VirtualPath, StringRef RealPath) {
    if (sys::fs::is_directory(VirtualPath))
      VFSWriter.addDirectoryMapping(VirtualPath, RealPath);
    else
      VFSWriter.addFileMapping(VirtualPath, RealPath);
  }

protected:
  /// Add a VFS overlay mapping for the canonicalized \p SrcPath.
  ///
  /// \param SrcPath Accessed path to record in the overlay mapping.
  void addFileImpl(StringRef SrcPath) override;

  /// Collect \p Dir and its entries from \p FS, returning a directory iterator.
  ///
  /// \param Dir Directory to collect.
  /// \param FS File system used to iterate \p Dir.
  /// \param EC Set if directory iteration fails.
  /// \return A directory iterator positioned at the first entry of \p Dir.
  llvm::vfs::directory_iterator
  addDirectoryImpl(const llvm::Twine &Dir,
                   IntrusiveRefCntPtr<vfs::FileSystem> FS,
                   std::error_code &EC) override;

  /// The directory where collected files are copied to in copyFiles().
  const std::string Root;

  /// The root directory where the VFS overlay lives.
  const std::string OverlayRoot;

  /// The yaml mapping writer.
  vfs::YAMLVFSWriter VFSWriter;

  /// Helper utility for canonicalizing paths.
  PathCanonicalizer Canonicalizer;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_FILECOLLECTOR_H
