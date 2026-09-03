//===- VirtualFileSystem.h - Virtual File System Layer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the virtual file system interface vfs::FileSystem.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_VIRTUALFILESYSTEM_H
#define LLVM_SUPPORT_VIRTUALFILESYSTEM_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/ExtensibleRTTI.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace llvm {

class MemoryBuffer;
class MemoryBufferRef;
class Twine;

namespace vfs {

/// The result of a \p status operation.
class Status {
  std::string Name;
  llvm::sys::fs::UniqueID UID;
  llvm::sys::TimePoint<> MTime;
  uint32_t User;
  uint32_t Group;
  uint64_t Size;
  llvm::sys::fs::file_type Type = llvm::sys::fs::file_type::status_error;
  llvm::sys::fs::perms Perms;

public:
  /// Whether this entity exposes a distinct external VFS path.
  ///
  /// Whether this entity has an external path different from the virtual path,
  /// and the external path is exposed by leaking it through the abstraction.
  /// For example, a RedirectingFileSystem will set this for paths where
  /// UseExternalName is true.
  ///
  /// FIXME: Currently the external path is exposed by replacing the virtual
  /// path in this Status object. Instead, we should leave the path in the
  /// Status intact (matching the requested virtual path) - see
  /// FileManager::getFileRef for how we plan to fix this.
  bool ExposesExternalVFSPath = false;

  /// Construct an empty status.
  Status() = default;
  /// Construct a status from a real filesystem file_status.
  ///
  /// \param Status Status from llvm::sys::fs.
  LLVM_ABI Status(const llvm::sys::fs::file_status &Status);
  /// Construct a status with the given attributes.
  ///
  /// \param Name Name that should be used for this file or directory.
  /// \param UID Unique identifier for this entity.
  /// \param MTime Last modification time.
  /// \param User Owner user id.
  /// \param Group Owner group id.
  /// \param Size Size in bytes.
  /// \param Type File type.
  /// \param Perms Permission bits.
  LLVM_ABI Status(const Twine &Name, llvm::sys::fs::UniqueID UID,
                  llvm::sys::TimePoint<> MTime, uint32_t User, uint32_t Group,
                  uint64_t Size, llvm::sys::fs::file_type Type,
                  llvm::sys::fs::perms Perms);

  /// Get a copy of a Status with a different size.
  ///
  /// \param In Status to copy.
  /// \param NewSize Replacement size.
  /// \returns A Status copied from \p In with size \p NewSize.
  LLVM_ABI static Status copyWithNewSize(const Status &In, uint64_t NewSize);
  /// Get a copy of a Status with a different name.
  ///
  /// \param In Status to copy.
  /// \param NewName Replacement name.
  /// \returns A Status copied from \p In with name \p NewName.
  LLVM_ABI static Status copyWithNewName(const Status &In,
                                         const Twine &NewName);
  /// Get a copy of a file_status with a different name.
  ///
  /// \param In Real filesystem status to copy.
  /// \param NewName Replacement name.
  /// \returns A Status copied from \p In with name \p NewName.
  LLVM_ABI static Status copyWithNewName(const llvm::sys::fs::file_status &In,
                                         const Twine &NewName);

  /// Returns the name that should be used for this file or directory.
  ///
  /// \returns The name that should be used for this file or directory.
  StringRef getName() const { return Name; }

  /// @name Status interface from llvm::sys::fs
  /// @{
  /// Return the file type of this status.
  ///
  /// \returns The file type of this status.
  llvm::sys::fs::file_type getType() const { return Type; }
  /// Return the permission bits of this status.
  ///
  /// \returns The permission bits of this status.
  llvm::sys::fs::perms getPermissions() const { return Perms; }
  /// Return the last modification time.
  ///
  /// \returns The last modification time.
  llvm::sys::TimePoint<> getLastModificationTime() const { return MTime; }
  /// Return the unique identifier for this entity.
  ///
  /// \returns The unique identifier for this entity.
  llvm::sys::fs::UniqueID getUniqueID() const { return UID; }
  /// Return the owner user id.
  ///
  /// \returns The owner user id.
  uint32_t getUser() const { return User; }
  /// Return the owner group id.
  ///
  /// \returns The owner group id.
  uint32_t getGroup() const { return Group; }
  /// Return the size in bytes.
  ///
  /// \returns The size in bytes.
  uint64_t getSize() const { return Size; }
  /// @}
  /// @name Status queries
  /// These are static queries in llvm::sys::fs.
  /// @{

  /// Check whether this status refers to the same entity as \p Other.
  ///
  /// \param Other Status to compare against.
  /// \returns True if both statuses refer to the same entity.
  LLVM_ABI bool equivalent(const Status &Other) const;
  /// Return true if this status represents a directory.
  ///
  /// \returns True if this status represents a directory.
  LLVM_ABI bool isDirectory() const;
  /// Return true if this status represents a regular file.
  ///
  /// \returns True if this status represents a regular file.
  LLVM_ABI bool isRegularFile() const;
  /// Return true if this status represents an "other" file type.
  ///
  /// \returns True if this status represents an "other" file type.
  LLVM_ABI bool isOther() const;
  /// Return true if this status represents a symbolic link.
  ///
  /// \returns True if this status represents a symbolic link.
  LLVM_ABI bool isSymlink() const;
  /// Return true if the status is known (not an error status).
  ///
  /// \returns True if the status is known (not an error status).
  LLVM_ABI bool isStatusKnown() const;
  /// Return true if the entity exists.
  ///
  /// \returns True if the entity exists.
  LLVM_ABI bool exists() const;
  /// @}
};

/// Represents an open file.
class LLVM_ABI File {
public:
  /// Destroy the file after closing it if still open.
  ///
  /// Sub-classes should generally call close() inside their destructors.  We
  /// cannot do that from the base class, since close is virtual.
  virtual ~File();

  /// Get the status of the file.
  ///
  /// \returns The status of the file, or an error.
  virtual llvm::ErrorOr<Status> status() = 0;

  /// Get the name of the file.
  ///
  /// \returns The name of the file, or an error.
  virtual llvm::ErrorOr<std::string> getName() {
    if (auto Status = status())
      return Status->getName().str();
    else
      return Status.getError();
  }

  /// Get the contents of the file as a \p MemoryBuffer.
  ///
  /// \param Name Identifier for the resulting buffer.
  /// \param FileSize Known size, or -1 if unknown.
  /// \param RequiresNullTerminator Whether the buffer must be null-terminated.
  /// \param IsVolatile Whether the file contents may change concurrently.
  /// \returns The file contents as a MemoryBuffer, or an error.
  virtual llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBuffer(const Twine &Name, int64_t FileSize = -1,
            bool RequiresNullTerminator = true, bool IsVolatile = false) = 0;

  /// Closes the file.
  ///
  /// \returns A success or error code for the close operation.
  virtual std::error_code close() = 0;

  /// Get the same file with a different path.
  ///
  /// \param Result File to rewrap, or an error to propagate.
  /// \param P Replacement path for the returned file.
  /// \returns The rewrapped file, or the propagated error.
  static ErrorOr<std::unique_ptr<File>>
  getWithPath(ErrorOr<std::unique_ptr<File>> Result, const Twine &P);

protected:
  /// Set the file's underlying path.
  ///
  /// \param Path New path for this file.
  virtual void setPath(const Twine &Path) {}
};

/// A member of a directory, yielded by a directory_iterator.
/// Only information available on most platforms is included.
class directory_entry {
  std::string Path;
  llvm::sys::fs::file_type Type = llvm::sys::fs::file_type::type_unknown;

public:
  /// Construct an empty directory entry.
  directory_entry() = default;
  /// Construct a directory entry with \p Path and \p Type.
  ///
  /// \param Path Full path of the directory member.
  /// \param Type File type of the directory member.
  directory_entry(std::string Path, llvm::sys::fs::file_type Type)
      : Path(std::move(Path)), Type(Type) {}

  /// Return the path of this directory entry.
  ///
  /// \returns The path of this directory entry.
  llvm::StringRef path() const { return Path; }
  /// Return the file type of this directory entry.
  ///
  /// \returns The file type of this directory entry.
  llvm::sys::fs::file_type type() const { return Type; }
};

namespace detail {

/// An interface for virtual file systems to provide an iterator over the
/// (non-recursive) contents of a directory.
struct LLVM_ABI DirIterImpl {
  virtual ~DirIterImpl();

  /// Sets \c CurrentEntry to the next entry in the directory on success,
  /// to directory_entry() at end,  or returns a system-defined \c error_code.
  virtual std::error_code increment() = 0;

  directory_entry CurrentEntry;
};

} // namespace detail

/// An input iterator over the entries in a virtual path, similar to
/// llvm::sys::fs::directory_iterator.
class directory_iterator {
  std::shared_ptr<detail::DirIterImpl> Impl; // Input iterator semantics on copy

public:
  /// Construct an iterator from implementation \p I.
  ///
  /// \param I Non-null directory iterator implementation.
  directory_iterator(std::shared_ptr<detail::DirIterImpl> I)
      : Impl(std::move(I)) {
    assert(Impl.get() != nullptr && "requires non-null implementation");
    if (Impl->CurrentEntry.path().empty())
      Impl.reset(); // Normalize the end iterator to Impl == nullptr.
  }

  /// Construct an 'end' iterator.
  directory_iterator() = default;

  /// Equivalent to operator++, with an error code.
  ///
  /// \param EC Receives an error code on failure.
  /// \returns A reference to this iterator.
  directory_iterator &increment(std::error_code &EC) {
    assert(Impl && "attempting to increment past end");
    EC = Impl->increment();
    if (Impl->CurrentEntry.path().empty())
      Impl.reset(); // Normalize the end iterator to Impl == nullptr.
    return *this;
  }

  /// Return a reference to the current directory entry.
  ///
  /// \returns A reference to the current directory entry.
  const directory_entry &operator*() const { return Impl->CurrentEntry; }
  /// Return a pointer to the current directory entry.
  ///
  /// \returns A pointer to the current directory entry.
  const directory_entry *operator->() const { return &Impl->CurrentEntry; }

  /// Return true if this iterator equals \p RHS.
  ///
  /// \param RHS Other iterator to compare.
  /// \returns True if this iterator equals \p RHS.
  bool operator==(const directory_iterator &RHS) const {
    if (Impl && RHS.Impl)
      return Impl->CurrentEntry.path() == RHS.Impl->CurrentEntry.path();
    return !Impl && !RHS.Impl;
  }
  /// Return true if this iterator differs from \p RHS.
  ///
  /// \param RHS Other iterator to compare.
  /// \returns True if this iterator differs from \p RHS.
  bool operator!=(const directory_iterator &RHS) const {
    return !(*this == RHS);
  }
};

class FileSystem;

namespace detail {

/// Keeps state for the recursive_directory_iterator.
struct RecDirIterState {
  std::vector<directory_iterator> Stack;
  bool HasNoPushRequest = false;
};

} // end namespace detail

/// An input iterator over the recursive contents of a virtual path,
/// similar to llvm::sys::fs::recursive_directory_iterator.
class recursive_directory_iterator {
  FileSystem *FS;
  std::shared_ptr<detail::RecDirIterState>
      State; // Input iterator semantics on copy.

public:
  /// Construct a recursive iterator over \p Path in \p FS.
  ///
  /// \param FS File system to iterate.
  /// \param Path Starting directory path.
  /// \param EC Receives an error code on failure.
  LLVM_ABI recursive_directory_iterator(FileSystem &FS, const Twine &Path,
                                        std::error_code &EC);

  /// Construct an 'end' iterator.
  recursive_directory_iterator() = default;

  /// Equivalent to operator++, with an error code.
  ///
  /// \param EC Receives an error code on failure.
  /// \returns A reference to this iterator.
  LLVM_ABI recursive_directory_iterator &increment(std::error_code &EC);

  /// Return a reference to the current directory entry.
  ///
  /// \returns A reference to the current directory entry.
  const directory_entry &operator*() const { return *State->Stack.back(); }
  /// Return a pointer to the current directory entry.
  ///
  /// \returns A pointer to the current directory entry.
  const directory_entry *operator->() const { return &*State->Stack.back(); }

  /// Return true if this iterator equals \p Other.
  ///
  /// \param Other Other iterator to compare.
  /// \returns True if this iterator equals \p Other.
  bool operator==(const recursive_directory_iterator &Other) const {
    return State == Other.State; // identity
  }
  /// Return true if this iterator differs from \p RHS.
  ///
  /// \param RHS Other iterator to compare.
  /// \returns True if this iterator differs from \p RHS.
  bool operator!=(const recursive_directory_iterator &RHS) const {
    return !(*this == RHS);
  }

  /// Gets the current level. Starting path is at level 0.
  ///
  /// \returns The current recursion depth, with the starting path at level 0.
  int level() const {
    assert(!State->Stack.empty() &&
           "Cannot get level without any iteration state");
    return State->Stack.size() - 1;
  }

  /// Do not recurse into the directory of the current entry.
  void no_push() { State->HasNoPushRequest = true; }
};

/// The virtual file system interface.
class LLVM_ABI FileSystem : public llvm::ThreadSafeRefCountedBase<FileSystem>,
                            public RTTIExtends<FileSystem, RTTIRoot> {
public:
  /// RTTI type identifier for this class.
  static const char ID;
  /// Virtual destructor for polymorphic file systems.
  ~FileSystem() override;

  /// Get the status of the entry at \p Path, if one exists.
  ///
  /// \param Path Path to query.
  /// \returns The status of the entry, or an error if it does not exist.
  virtual llvm::ErrorOr<Status> status(const Twine &Path) = 0;

  /// Get a \p File object for the text file at \p Path, if one exists.
  ///
  /// \param Path Path of the file to open.
  /// \returns A File for the text file at \p Path, or an error.
  virtual llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) = 0;

  /// Get a File object for the binary file at \p Path.
  ///
  /// Some non-ascii based file systems perform encoding conversions
  /// when reading as a text file, and this function should be used if
  /// a file's bytes should be read as-is. On most filesystems, this
  /// is the same behaviour as openFileForRead.
  ///
  /// \param Path Path of the file to open.
  /// \returns A File for the binary file at \p Path, or an error.
  virtual llvm::ErrorOr<std::unique_ptr<File>>
  openFileForReadBinary(const Twine &Path) {
    return openFileForRead(Path);
  }

  /// Open a file, read its contents, and close it.
  ///
  /// The IsText parameter is used to distinguish whether the file should be
  /// opened as a binary or text file.
  ///
  /// \param Name Path of the file to read.
  /// \param FileSize Known size, or -1 if unknown.
  /// \param RequiresNullTerminator Whether the buffer must be null-terminated.
  /// \param IsVolatile Whether the file contents may change concurrently.
  /// \param IsText True to open as text; false for binary.
  /// \returns The file contents as a MemoryBuffer, or an error.
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBufferForFile(const Twine &Name, int64_t FileSize = -1,
                   bool RequiresNullTerminator = true, bool IsVolatile = false,
                   bool IsText = true);

  /// Get a directory_iterator for \p Dir.
  /// \note The 'end' iterator is directory_iterator().
  ///
  /// \param Dir Directory path to iterate.
  /// \param EC Receives an error code on failure.
  /// \returns An iterator over the directory entries, or the end iterator on error.
  virtual directory_iterator dir_begin(const Twine &Dir,
                                       std::error_code &EC) = 0;

  /// Set the working directory. This will affect all following operations on
  /// this file system and may propagate down for nested file systems.
  ///
  /// \param Path New working directory.
  /// \returns A success or error code for setting the working directory.
  virtual std::error_code setCurrentWorkingDirectory(const Twine &Path) = 0;

  /// Get the working directory of this file system.
  ///
  /// \returns The working directory, or an error.
  virtual llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const = 0;

  /// Get the real path of \p Path.
  ///
  /// Collapses all . and .. patterns and resolves symlinks. For real file
  /// system, this uses `llvm::sys::fs::real_path`.
  /// This returns errc::operation_not_permitted if not implemented by subclass.
  ///
  /// \param Path Path to resolve.
  /// \param Output Receives the resolved path on success.
  /// \returns A success or error code for resolving the path.
  virtual std::error_code getRealPath(const Twine &Path,
                                      SmallVectorImpl<char> &Output);

  /// Check whether \p Path exists. By default this uses \c status(), but
  /// filesystems may provide a more efficient implementation if available.
  ///
  /// \param Path Path to test for existence.
  /// \returns True if \p Path exists, false otherwise.
  virtual bool exists(const Twine &Path);

  /// Is the file mounted on a local filesystem?
  ///
  /// \param Path Path to query.
  /// \param Result Set to true if the path is local.
  /// \returns A success or error code for the locality query.
  virtual std::error_code isLocal(const Twine &Path, bool &Result);

  /// Make \a Path an absolute path.
  ///
  /// Makes \a Path absolute using the current directory if it is not already.
  /// An empty \a Path will result in the current directory.
  ///
  /// /absolute/path   => /absolute/path
  /// relative/../path => <current-directory>/relative/../path
  ///
  /// \param Path A path that is modified to be an absolute path.
  /// \returns success if \a path has been made absolute, otherwise a
  ///          platform-specific error_code.
  virtual std::error_code makeAbsolute(SmallVectorImpl<char> &Path) const;

  /// Check whether \p A and \p B refer to the same file.
  ///
  /// \param A First path to compare.
  /// \param B Second path to compare.
  /// \returns true if \p A and \p B represent the same file, or an error or
  /// false if they do not.
  llvm::ErrorOr<bool> equivalent(const Twine &A, const Twine &B);

  /// How much detail to include when printing a file system.
  enum class PrintType {
    Summary,           ///< Print only a one-line summary.
    Contents,          ///< Print this node and a summary of children.
    RecursiveContents  ///< Print this node and recurse into children.
  };
  /// Print a description of this file system to \p OS.
  ///
  /// \param OS Stream to write to.
  /// \param Type How much detail to print.
  /// \param IndentLevel Indentation depth for this node.
  void print(raw_ostream &OS, PrintType Type = PrintType::Contents,
             unsigned IndentLevel = 0) const {
    printImpl(OS, Type, IndentLevel);
  }

  /// Callback type used when visiting nested file systems.
  using VisitCallbackTy = llvm::function_ref<void(FileSystem &)>;
  /// Visit nested child file systems.
  ///
  /// \param Callback Invoked for each child file system.
  virtual void visitChildFileSystems(VisitCallbackTy Callback) {}
  /// Visit this file system and then its children.
  ///
  /// \param Callback Invoked for this file system and each child.
  void visit(VisitCallbackTy Callback) {
    Callback(*this);
    visitChildFileSystems(Callback);
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this file system to stderr for debugging.
  LLVM_DUMP_METHOD void dump() const;
#endif

protected:
  /// Print this file system implementation.
  ///
  /// \param OS Stream to write to.
  /// \param Type How much detail to print.
  /// \param IndentLevel Indentation depth for this node.
  virtual void printImpl(raw_ostream &OS, PrintType Type,
                         unsigned IndentLevel) const {
    printIndent(OS, IndentLevel);
    OS << "FileSystem\n";
  }

  /// Write indentation for file system printing.
  ///
  /// \param OS Stream to write to.
  /// \param IndentLevel Number of indentation steps to write.
  void printIndent(raw_ostream &OS, unsigned IndentLevel) const {
    for (unsigned i = 0; i < IndentLevel; ++i)
      OS << "  ";
  }
};

/// Get an vfs::FileSystem for the process's real file system.
///
/// Gets an \p vfs::FileSystem for the 'real' file system, as seen by
/// the operating system.
/// The working directory is linked to the process's working directory.
/// (This is usually thread-hostile).
/// This may only be called outside the IO sandbox.
///
/// \returns A FileSystem for the process's real file system.
LLVM_ABI IntrusiveRefCntPtr<FileSystem> getRealFileSystem();

/// Create an vfs::FileSystem for the OS real file system.
///
/// Create an \p vfs::FileSystem for the 'real' file system, as seen by
/// the operating system.
/// It has its own working directory, independent of (but initially equal to)
/// that of the process.
/// This may only be called outside the IO sandbox.
///
/// \returns A FileSystem for the OS real file system with its own working directory.
LLVM_ABI std::unique_ptr<FileSystem> createPhysicalFileSystem();

/// A file system that allows overlaying one \p AbstractFileSystem on top
/// of another.
///
/// Consists of a stack of >=1 \p FileSystem objects, which are treated as being
/// one merged file system. When there is a directory that exists in more than
/// one file system, the \p OverlayFileSystem contains a directory containing
/// the union of their contents.  The attributes (permissions, etc.) of the
/// top-most (most recently added) directory are used.  When there is a file
/// that exists in more than one file system, the file in the top-most file
/// system overrides the other(s).
class LLVM_ABI OverlayFileSystem
    : public RTTIExtends<OverlayFileSystem, FileSystem> {
  using FileSystemList = SmallVector<IntrusiveRefCntPtr<FileSystem>, 1>;

  /// The stack of file systems, implemented as a list in order of
  /// their addition.
  FileSystemList FSList;

public:
  /// RTTI type identifier for this class.
  static const char ID;
  /// Construct an overlay with \p Base as the bottom-most file system.
  ///
  /// \param Base Initial underlying file system.
  OverlayFileSystem(IntrusiveRefCntPtr<FileSystem> Base);

  /// Pushes a file system on top of the stack.
  ///
  /// \param FS File system to add above existing overlays.
  void pushOverlay(IntrusiveRefCntPtr<FileSystem> FS);

  /// Get the status of the entry at \p Path, if one exists.
  ///
  /// \param Path Path to query.
  /// \returns The status of the entry, or an error if it does not exist.
  llvm::ErrorOr<Status> status(const Twine &Path) override;
  /// Check whether \p Path exists in any overlay.
  ///
  /// \param Path Path to test for existence.
  /// \returns True if \p Path exists in any overlay.
  bool exists(const Twine &Path) override;
  /// Open the file at \p Path for reading.
  ///
  /// \param Path Path of the file to open.
  /// \returns A File for \p Path, or an error.
  llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) override;
  /// Begin iterating the directory at \p Dir.
  ///
  /// \param Dir Directory path to iterate.
  /// \param EC Receives an error code on failure.
  /// \returns An iterator over the directory entries, or the end iterator on error.
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override;
  /// Get the working directory of this file system.
  ///
  /// \returns The working directory, or an error.
  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override;
  /// Set the working directory to \p Path.
  ///
  /// \param Path New working directory.
  /// \returns A success or error code for setting the working directory.
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;
  /// Check whether \p Path is on a local filesystem.
  ///
  /// \param Path Path to query.
  /// \param Result Set to true if the path is local.
  /// \returns A success or error code for the locality query.
  std::error_code isLocal(const Twine &Path, bool &Result) override;
  /// Resolve the real path of \p Path.
  ///
  /// \param Path Path to resolve.
  /// \param Output Receives the resolved path on success.
  /// \returns A success or error code for resolving the path.
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) override;

  /// Iterator over overlays from most to least recently added.
  using iterator = FileSystemList::reverse_iterator;
  /// Const iterator over overlays from most to least recently added.
  using const_iterator = FileSystemList::const_reverse_iterator;
  /// Iterator over overlays from least to most recently added.
  using reverse_iterator = FileSystemList::iterator;
  /// Const iterator over overlays from least to most recently added.
  using const_reverse_iterator = FileSystemList::const_iterator;
  /// Range of overlays from most to least recently added.
  using range = iterator_range<iterator>;
  /// Const range of overlays from most to least recently added.
  using const_range = iterator_range<const_iterator>;

  /// Get an iterator pointing to the most recently added file system.
  ///
  /// \returns An iterator to the most recently added file system.
  iterator overlays_begin() { return FSList.rbegin(); }
  /// Get a const iterator pointing to the most recently added file system.
  ///
  /// \returns A const iterator to the most recently added file system.
  const_iterator overlays_begin() const { return FSList.rbegin(); }

  /// Get an iterator pointing one-past the least recently added file system.
  ///
  /// \returns An iterator one-past the least recently added file system.
  iterator overlays_end() { return FSList.rend(); }
  /// Get a const iterator pointing one-past the least recently added file system.
  ///
  /// \returns A const iterator one-past the least recently added file system.
  const_iterator overlays_end() const { return FSList.rend(); }

  /// Get an iterator pointing to the least recently added file system.
  ///
  /// \returns An iterator to the least recently added file system.
  reverse_iterator overlays_rbegin() { return FSList.begin(); }
  /// Get a const iterator pointing to the least recently added file system.
  ///
  /// \returns A const iterator to the least recently added file system.
  const_reverse_iterator overlays_rbegin() const { return FSList.begin(); }

  /// Get an iterator pointing one-past the most recently added file system.
  ///
  /// \returns An iterator one-past the most recently added file system.
  reverse_iterator overlays_rend() { return FSList.end(); }
  /// Get a const iterator pointing one-past the most recently added file system.
  ///
  /// \returns A const iterator one-past the most recently added file system.
  const_reverse_iterator overlays_rend() const { return FSList.end(); }

  /// Return a range of overlays from most to least recently added.
  ///
  /// \returns A range of overlays from most to least recently added.
  range overlays_range() { return llvm::reverse(FSList); }
  /// Return a const range of overlays from most to least recently added.
  ///
  /// \returns A const range of overlays from most to least recently added.
  const_range overlays_range() const { return llvm::reverse(FSList); }

protected:
  /// Print this overlay file system.
  ///
  /// \param OS Stream to write to.
  /// \param Type How much detail to print.
  /// \param IndentLevel Indentation depth for this node.
  void printImpl(raw_ostream &OS, PrintType Type,
                 unsigned IndentLevel) const override;
  /// Visit each overlaid file system.
  ///
  /// \param Callback Invoked for each child file system.
  void visitChildFileSystems(VisitCallbackTy Callback) override;
};

/// File system adapter that proxies calls to an underlying file system.
///
/// By default, this delegates all calls to the underlying file system. This is
/// useful when derived file systems want to override some calls and still proxy
/// other calls.
class LLVM_ABI ProxyFileSystem
    : public RTTIExtends<ProxyFileSystem, FileSystem> {
public:
  /// RTTI type identifier for this class.
  static const char ID;
  /// Construct a proxy around \p FS.
  ///
  /// \param FS Underlying file system to forward calls to.
  explicit ProxyFileSystem(IntrusiveRefCntPtr<FileSystem> FS)
      : FS(std::move(FS)) {}

  /// Get the status of the entry at \p Path, if one exists.
  ///
  /// \param Path Path to query.
  /// \returns The status of the entry, or an error if it does not exist.
  llvm::ErrorOr<Status> status(const Twine &Path) override {
    return FS->status(Path);
  }
  /// Check whether \p Path exists in the underlying file system.
  ///
  /// \param Path Path to test for existence.
  /// \returns True if \p Path exists in the underlying file system.
  bool exists(const Twine &Path) override { return FS->exists(Path); }
  /// Open the file at \p Path for reading.
  ///
  /// \param Path Path of the file to open.
  /// \returns A File for \p Path, or an error.
  llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) override {
    return FS->openFileForRead(Path);
  }
  /// Begin iterating the directory at \p Dir.
  ///
  /// \param Dir Directory path to iterate.
  /// \param EC Receives an error code on failure.
  /// \returns An iterator over the directory entries, or the end iterator on error.
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override {
    return FS->dir_begin(Dir, EC);
  }
  /// Get the working directory of the underlying file system.
  ///
  /// \returns The working directory, or an error.
  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override {
    return FS->getCurrentWorkingDirectory();
  }
  /// Set the working directory on the underlying file system.
  ///
  /// \param Path New working directory.
  /// \returns A success or error code for setting the working directory.
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override {
    return FS->setCurrentWorkingDirectory(Path);
  }
  /// Resolve the real path of \p Path via the underlying file system.
  ///
  /// \param Path Path to resolve.
  /// \param Output Receives the resolved path on success.
  /// \returns A success or error code for resolving the path.
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) override {
    return FS->getRealPath(Path, Output);
  }
  /// Check whether \p Path is on a local filesystem.
  ///
  /// \param Path Path to query.
  /// \param Result Set to true if the path is local.
  /// \returns A success or error code for the locality query.
  std::error_code isLocal(const Twine &Path, bool &Result) override {
    return FS->isLocal(Path, Result);
  }

protected:
  /// Return the underlying file system being proxied.
  ///
  /// \returns The underlying file system being proxied.
  FileSystem &getUnderlyingFS() const { return *FS; }
  /// Visit the underlying file system and its children.
  ///
  /// \param Callback Invoked for each child file system.
  void visitChildFileSystems(VisitCallbackTy Callback) override {
    if (FS) {
      Callback(*FS);
      FS->visitChildFileSystems(Callback);
    }
  }

private:
  IntrusiveRefCntPtr<FileSystem> FS;

  void anchor() override;
};

namespace detail {

class InMemoryDirectory;
class InMemoryNode;

struct NewInMemoryNodeInfo {
  llvm::sys::fs::UniqueID DirUID;
  StringRef Path;
  StringRef Name;
  time_t ModificationTime;
  std::unique_ptr<llvm::MemoryBuffer> Buffer;
  uint32_t User;
  uint32_t Group;
  llvm::sys::fs::file_type Type;
  llvm::sys::fs::perms Perms;

  LLVM_ABI Status makeStatus() const;
};

class NamedNodeOrError {
  ErrorOr<std::pair<llvm::SmallString<128>, const detail::InMemoryNode *>>
      Value;

public:
  NamedNodeOrError(llvm::SmallString<128> Name,
                   const detail::InMemoryNode *Node)
      : Value(std::make_pair(Name, Node)) {}
  NamedNodeOrError(std::error_code EC) : Value(EC) {}
  NamedNodeOrError(llvm::errc EC) : Value(EC) {}

  StringRef getName() const { return (*Value).first; }
  explicit operator bool() const { return static_cast<bool>(Value); }
  operator std::error_code() const { return Value.getError(); }
  std::error_code getError() const { return Value.getError(); }
  const detail::InMemoryNode *operator*() const { return (*Value).second; }
};

} // namespace detail

/// An in-memory file system.
class LLVM_ABI InMemoryFileSystem
    : public RTTIExtends<InMemoryFileSystem, FileSystem> {
  std::unique_ptr<detail::InMemoryDirectory> Root;
  std::string WorkingDirectory;
  bool UseNormalizedPaths = true;

public:
  /// RTTI type identifier for this class.
  static const char ID;

private:
  using MakeNodeFn = llvm::function_ref<std::unique_ptr<detail::InMemoryNode>(
      detail::NewInMemoryNodeInfo)>;

  /// Create node with \p MakeNode and add it into this filesystem at \p Path.
  bool addFile(const Twine &Path, time_t ModificationTime,
               std::unique_ptr<llvm::MemoryBuffer> Buffer,
               std::optional<uint32_t> User, std::optional<uint32_t> Group,
               std::optional<llvm::sys::fs::file_type> Type,
               std::optional<llvm::sys::fs::perms> Perms, MakeNodeFn MakeNode);

  /// Looks up the in-memory node for the path \p P.
  /// If \p FollowFinalSymlink is true, the returned node is guaranteed to
  /// not be a symlink and its path may differ from \p P.
  detail::NamedNodeOrError lookupNode(const Twine &P, bool FollowFinalSymlink,
                                      size_t SymlinkDepth = 0) const;

  class DirIterator;

public:
  /// Construct an in-memory file system.
  ///
  /// \param UseNormalizedPaths Whether to normalize \c . and \c .. in paths.
  explicit InMemoryFileSystem(bool UseNormalizedPaths = true);
  /// Destroy this in-memory file system.
  ~InMemoryFileSystem() override;

  /// Add a file or directory containing \p Buffer at \p Path.
  ///
  /// The VFS owns the buffer. If present, User, Group, Type and Perms apply to
  /// the newly-created file or directory.
  /// \return true if the file or directory was successfully added,
  /// false if the file or directory already exists in the file system with
  /// different contents.
  ///
  /// \param Path Path at which to add the node.
  /// \param ModificationTime Last-modification time for the new node.
  /// \param Buffer Contents of the file; ignored for directories.
  /// \param User Optional owner user id.
  /// \param Group Optional owner group id.
  /// \param Type Optional file type; inferred when omitted.
  /// \param Perms Optional permission bits.
  bool addFile(const Twine &Path, time_t ModificationTime,
               std::unique_ptr<llvm::MemoryBuffer> Buffer,
               std::optional<uint32_t> User = std::nullopt,
               std::optional<uint32_t> Group = std::nullopt,
               std::optional<llvm::sys::fs::file_type> Type = std::nullopt,
               std::optional<llvm::sys::fs::perms> Perms = std::nullopt);

  /// Add a hard link to a file.
  ///
  /// Here hard links are not intended to be fully equivalent to the classical
  /// filesystem. Both the hard link and the file share the same buffer and
  /// status (and thus have the same UniqueID). Because of this there is no way
  /// to distinguish between the link and the file after the link has been
  /// added.
  ///
  /// The \p Target path must be an existing file or a hardlink. The
  /// \p NewLink file must not have been added before. The \p Target
  /// path must not be a directory. The \p NewLink node is added as a hard
  /// link which points to the resolved file of \p Target node.
  /// \return true if the above condition is satisfied and hardlink was
  /// successfully created, false otherwise.
  ///
  /// \param NewLink Path of the hard link to create.
  /// \param Target Existing file path to link to.
  bool addHardLink(const Twine &NewLink, const Twine &Target);

  /// Arbitrary max depth to search through symlinks. We can get into problems
  /// if a link links to a link that links back to the link, for example.
  static constexpr size_t MaxSymlinkDepth = 16;

  /// Add a symbolic link from \p NewLink to \p Target.
  ///
  /// Unlike a HardLink, \p Target doesn't need to refer to a file (or refer to
  /// anything, as it happens). Also, an in-memory directory for \p Target isn't
  /// automatically created.
  ///
  /// \param NewLink Path of the symbolic link to create.
  /// \param Target Path stored as the link target.
  /// \param ModificationTime Last-modification time for the link.
  /// \param User Optional owner user id.
  /// \param Group Optional owner group id.
  /// \param Perms Optional permission bits.
  /// \returns True if the symbolic link was successfully created.
  bool
  addSymbolicLink(const Twine &NewLink, const Twine &Target,
                  time_t ModificationTime,
                  std::optional<uint32_t> User = std::nullopt,
                  std::optional<uint32_t> Group = std::nullopt,
                  std::optional<llvm::sys::fs::perms> Perms = std::nullopt);

  /// Add a buffer to the VFS with a path. The VFS does not own the buffer.
  /// If present, User, Group, Type and Perms apply to the newly-created file
  /// or directory.
  /// \return true if the file or directory was successfully added,
  /// false if the file or directory already exists in the file system with
  /// different contents.
  ///
  /// \param Path Path at which to add the node.
  /// \param ModificationTime Last-modification time for the new node.
  /// \param Buffer Contents of the file; not owned by the VFS.
  /// \param User Optional owner user id.
  /// \param Group Optional owner group id.
  /// \param Type Optional file type; inferred when omitted.
  /// \param Perms Optional permission bits.
  bool addFileNoOwn(const Twine &Path, time_t ModificationTime,
                    const llvm::MemoryBufferRef &Buffer,
                    std::optional<uint32_t> User = std::nullopt,
                    std::optional<uint32_t> Group = std::nullopt,
                    std::optional<llvm::sys::fs::file_type> Type = std::nullopt,
                    std::optional<llvm::sys::fs::perms> Perms = std::nullopt);

  /// Return a string dump of this in-memory file system.
  ///
  /// \returns A string dump of this in-memory file system.
  std::string toString() const;

  /// Return true if this file system normalizes . and .. in paths.
  ///
  /// \returns True if this file system normalizes . and .. in paths.
  bool useNormalizedPaths() const { return UseNormalizedPaths; }

  /// Get the status of the entry at \p Path, if one exists.
  ///
  /// \param Path Path to query.
  /// \returns The status of the entry, or an error if it does not exist.
  llvm::ErrorOr<Status> status(const Twine &Path) override;
  /// Open the file at \p Path for reading.
  ///
  /// \param Path Path of the file to open.
  /// \returns A File for \p Path, or an error.
  llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) override;
  /// Begin iterating the directory at \p Dir.
  ///
  /// \param Dir Directory path to iterate.
  /// \param EC Receives an error code on failure.
  /// \returns An iterator over the directory entries, or the end iterator on error.
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override;

  /// Get the working directory of this file system.
  ///
  /// \returns The working directory, or an error.
  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override {
    return WorkingDirectory;
  }
  /// Canonicalize \p Path against the working directory.
  ///
  /// Combines with the current working directory and normalizes the path (e.g.
  /// remove dots). If the current working directory is not set, this returns
  /// errc::operation_not_permitted.
  ///
  /// This doesn't resolve symlinks as they are not supported in in-memory file
  /// system.
  ///
  /// \param Path Path to canonicalize.
  /// \param Output Receives the canonicalized path on success.
  /// \returns A success or error code for canonicalizing the path.
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) override;
  /// Check whether \p Path is on a local filesystem.
  ///
  /// \param Path Path to query.
  /// \param Result Set to true if the path is local.
  /// \returns A success or error code for the locality query.
  std::error_code isLocal(const Twine &Path, bool &Result) override;
  /// Set the working directory to \p Path.
  ///
  /// \param Path New working directory.
  /// \returns A success or error code for setting the working directory.
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;

protected:
  /// Print this in-memory file system.
  ///
  /// \param OS Stream to write to.
  /// \param Type How much detail to print.
  /// \param IndentLevel Indentation depth for this node.
  void printImpl(raw_ostream &OS, PrintType Type,
                 unsigned IndentLevel) const override;
};

/// Get a globally unique ID for a virtual file or directory.
///
/// \returns A globally unique ID for a virtual file or directory.
LLVM_ABI llvm::sys::fs::UniqueID getNextVirtualUniqueID();

/// Gets a \p FileSystem for a virtual file system described in YAML
/// format.
///
/// \param Buffer YAML description of the virtual file system.
/// \param DiagHandler Handler invoked for YAML diagnostics.
/// \param YAMLFilePath Path used when reporting diagnostics.
/// \param DiagContext Opaque context passed to \p DiagHandler.
/// \param ExternalFS File system used for remapped external contents.
/// \returns A FileSystem parsed from the YAML description.
LLVM_ABI std::unique_ptr<FileSystem>
getVFSFromYAML(std::unique_ptr<llvm::MemoryBuffer> Buffer,
               llvm::SourceMgr::DiagHandlerTy DiagHandler,
               StringRef YAMLFilePath, void *DiagContext = nullptr,
               IntrusiveRefCntPtr<FileSystem> ExternalFS = getRealFileSystem());

/// A virtual-to-real path mapping collected from a YAML VFS.
struct YAMLVFSEntry {
  /// Construct a mapping from \p VPath to \p RPath.
  ///
  /// \param VPath Virtual path as seen by clients of the VFS.
  /// \param RPath Real path in the external file system.
  /// \param IsDirectory True if this mapping refers to a directory.
  template <typename T1, typename T2>
  YAMLVFSEntry(T1 &&VPath, T2 &&RPath, bool IsDirectory = false)
      : VPath(std::forward<T1>(VPath)), RPath(std::forward<T2>(RPath)),
        IsDirectory(IsDirectory) {}
  /// Virtual path as seen by clients of the VFS.
  std::string VPath;
  /// Real path in the external file system.
  std::string RPath;
  /// True if this mapping refers to a directory.
  bool IsDirectory = false;
};

/// Directory iterator implementation for RedirectingFileSystem.
class RedirectingFSDirIterImpl;
/// Parser for RedirectingFileSystem YAML descriptions.
class RedirectingFileSystemParser;

/// A virtual file system parsed from a YAML file.
///
/// Currently, this class allows creating virtual files and directories. Virtual
/// files map to existing external files in \c ExternalFS, and virtual
/// directories may either map to existing directories in \c ExternalFS or list
/// their contents in the form of other virtual directories and/or files.
///
/// The basic structure of the parsed file is:
/// \verbatim
/// {
///   'version': <version number>,
///   <optional configuration>
///   'roots': [
///              <directory entries>
///            ]
/// }
/// \endverbatim
/// The roots may be absolute or relative. If relative they will be made
/// absolute against either current working directory or the directory where
/// the Overlay YAML file is located, depending on the 'root-relative'
/// configuration.
///
/// All configuration options are optional.
///   'case-sensitive': <boolean, default=(true for Posix, false for Windows)>
///   'use-external-names': <boolean, default=true>
///   'root-relative': <string, one of 'cwd' or 'overlay-dir', default='cwd'>
///   'overlay-relative': <boolean, default=false>
///   'fallthrough': <boolean, default=true, deprecated - use 'redirecting-with'
///                   instead>
///   'redirecting-with': <string, one of 'fallthrough', 'fallback', or
///                        'redirect-only', default='fallthrough'>
///
/// To clarify, 'root-relative' option will prepend the current working
/// directory, or the overlay directory to the 'roots->name' field only if
/// 'roots->name' is a relative path. On the other hand, when 'overlay-relative'
/// is set to 'true', external paths will always be prepended with the overlay
/// directory, even if external paths are not relative paths. The
/// 'root-relative' option has no interaction with the 'overlay-relative'
/// option.
///
/// Virtual directories that list their contents are represented as
/// \verbatim
/// {
///   'type': 'directory',
///   'name': <string>,
///   'contents': [ <file or directory entries> ]
/// }
/// \endverbatim
/// The default attributes for such virtual directories are:
/// \verbatim
/// MTime = now() when created
/// Perms = 0777
/// User = Group = 0
/// Size = 0
/// UniqueID = unspecified unique value
/// \endverbatim
/// When a path prefix matches such a directory, the next component in the path
/// is matched against the entries in the 'contents' array.
///
/// Re-mapped directories, on the other hand, are represented as
/// /// \verbatim
/// {
///   'type': 'directory-remap',
///   'name': <string>,
///   'use-external-name': <boolean>, # Optional
///   'external-contents': <path to external directory>
/// }
/// \endverbatim
/// and inherit their attributes from the external directory. When a path
/// prefix matches such an entry, the unmatched components are appended to the
/// 'external-contents' path, and the resulting path is looked up in the
/// external file system instead.
///
/// Re-mapped files are represented as
/// \verbatim
/// {
///   'type': 'file',
///   'name': <string>,
///   'use-external-name': <boolean>, # Optional
///   'external-contents': <path to external file>
/// }
/// \endverbatim
/// Their attributes and file contents are determined by looking up the file at
/// their 'external-contents' path in the external file system.
///
/// For 'file', 'directory' and 'directory-remap' entries the 'name' field may
/// contain multiple path components (e.g. /path/to/file). However, any
/// directory in such a path that contains more than one child must be uniquely
/// represented by a 'directory' entry.
///
/// When the 'use-external-name' field is set, calls to \a vfs::File::status()
/// give the external (remapped) filesystem name instead of the name the file
/// was accessed by. This is an intentional leak through the \a
/// RedirectingFileSystem abstraction layer. It enables clients to discover
/// (and use) the external file location when communicating with users or tools
/// that don't use the same VFS overlay.
///
/// FIXME: 'use-external-name' causes behaviour that's inconsistent with how
/// "real" filesystems behave. Maybe there should be a separate channel for
/// this information.
class LLVM_ABI RedirectingFileSystem
    : public RTTIExtends<RedirectingFileSystem, vfs::FileSystem> {
public:
  /// RTTI type identifier for this class.
  static const char ID;
  /// Kind of VFS tree entry.
  enum EntryKind {
    EK_Directory,      ///< Directory with explicit child contents.
    EK_DirectoryRemap, ///< Directory remapped to an external directory.
    EK_File            ///< File remapped to an external file.
  };
  /// How a remapped entry chooses its reported name.
  enum NameKind {
    NK_NotSet,   ///< Use the file system's global use-external-names setting.
    NK_External, ///< Always report the external path as the name.
    NK_Virtual   ///< Always report the virtual path as the name.
  };

  /// The type of redirection to perform.
  enum class RedirectKind {
    /// Lookup the redirected path first (ie. the one specified in
    /// 'external-contents') and if that fails "fallthrough" to a lookup of the
    /// originally provided path.
    Fallthrough,
    /// Lookup the provided path first and if that fails, "fallback" to a
    /// lookup of the redirected path.
    Fallback,
    /// Only lookup the redirected path, do not lookup the originally provided
    /// path.
    RedirectOnly
  };

  /// The type of relative path used by Roots.
  enum class RootRelativeKind {
    /// The roots are relative to the current working directory.
    CWD,
    /// The roots are relative to the directory where the Overlay YAML file
    // locates.
    OverlayDir
  };

  /// A single file or directory in the VFS.
  class Entry {
    EntryKind Kind;
    std::string Name;

  public:
    /// Construct an entry of kind \p K named \p Name.
    ///
    /// \param K Kind of VFS entry.
    /// \param Name Name of this entry within its parent.
    Entry(EntryKind K, StringRef Name) : Kind(K), Name(Name) {}
    /// Virtual destructor for polymorphic VFS entries.
    virtual ~Entry() = default;

    /// Return the name of this entry.
    ///
    /// \returns The name of this entry.
    StringRef getName() const { return Name; }
    /// Return the kind of this entry.
    ///
    /// \returns The kind of this entry.
    EntryKind getKind() const { return Kind; }
  };

  /// A directory in the vfs with explicitly specified contents.
  class DirectoryEntry : public Entry {
    std::vector<std::unique_ptr<Entry>> Contents;
    Status S;

  public:
    /// Constructs a directory entry with explicitly specified contents.
    ///
    /// \param Name Name of this directory within its parent.
    /// \param Contents Child entries of this directory.
    /// \param S Status attributes for this directory.
    DirectoryEntry(StringRef Name, std::vector<std::unique_ptr<Entry>> Contents,
                   Status S)
        : Entry(EK_Directory, Name), Contents(std::move(Contents)),
          S(std::move(S)) {}

    /// Constructs an empty directory entry.
    ///
    /// \param Name Name of this directory within its parent.
    /// \param S Status attributes for this directory.
    DirectoryEntry(StringRef Name, Status S)
        : Entry(EK_Directory, Name), S(std::move(S)) {}

    /// Return the status of this directory.
    ///
    /// \returns The status of this directory.
    Status getStatus() { return S; }

    /// Append a child entry to this directory.
    ///
    /// \param Content Child entry to take ownership of.
    void addContent(std::unique_ptr<Entry> Content) {
      Contents.push_back(std::move(Content));
    }

    /// Return the most recently added child entry.
    ///
    /// \returns The most recently added child entry.
    Entry *getLastContent() const { return Contents.back().get(); }

    /// Iterator over the child entries of this directory.
    using iterator = decltype(Contents)::iterator;

    /// Return an iterator to the first child entry.
    ///
    /// \returns An iterator to the first child entry.
    iterator contents_begin() { return Contents.begin(); }
    /// Return an iterator past the last child entry.
    ///
    /// \returns An iterator past the last child entry.
    iterator contents_end() { return Contents.end(); }

    /// Check whether \p E is a DirectoryEntry.
    ///
    /// \param E Entry to test.
    /// \returns True if \p E is a DirectoryEntry.
    static bool classof(const Entry *E) { return E->getKind() == EK_Directory; }
  };

  /// A file or directory in the vfs that is mapped to a file or directory in
  /// the external filesystem.
  class RemapEntry : public Entry {
    std::string ExternalContentsPath;
    NameKind UseName;

  protected:
    /// Construct a remapping entry.
    ///
    /// \param K Kind of remapping entry.
    /// \param Name Name of this entry within its parent.
    /// \param ExternalContentsPath Path in the external file system.
    /// \param UseName Whether to expose the external or virtual name.
    RemapEntry(EntryKind K, StringRef Name, StringRef ExternalContentsPath,
               NameKind UseName)
        : Entry(K, Name), ExternalContentsPath(ExternalContentsPath),
          UseName(UseName) {}

  public:
    /// Return the path this entry maps to in the external file system.
    ///
    /// \returns The path this entry maps to in the external file system.
    StringRef getExternalContentsPath() const { return ExternalContentsPath; }

    /// Whether to use the external path as the name for this file or directory.
    ///
    /// \param GlobalUseExternalName Default when this entry does not override.
    /// \returns True if the external path should be used as the name.
    bool useExternalName(bool GlobalUseExternalName) const {
      return UseName == NK_NotSet ? GlobalUseExternalName
                                  : (UseName == NK_External);
    }

    /// Return how this entry chooses between external and virtual names.
    ///
    /// \returns How this entry chooses between external and virtual names.
    NameKind getUseName() const { return UseName; }

    /// Check whether \p E is a remapping entry.
    ///
    /// \param E Entry to test.
    /// \returns True if \p E is a FileEntry or DirectoryRemapEntry.
    static bool classof(const Entry *E) {
      switch (E->getKind()) {
      case EK_DirectoryRemap:
        [[fallthrough]];
      case EK_File:
        return true;
      case EK_Directory:
        return false;
      }
      llvm_unreachable("invalid entry kind");
    }
  };

  /// A directory in the vfs that maps to a directory in the external file
  /// system.
  class DirectoryRemapEntry : public RemapEntry {
  public:
    /// Construct a directory remapping entry.
    ///
    /// \param Name Name of this directory within its parent.
    /// \param ExternalContentsPath External directory this entry maps to.
    /// \param UseName Whether to expose the external or virtual name.
    DirectoryRemapEntry(StringRef Name, StringRef ExternalContentsPath,
                        NameKind UseName)
        : RemapEntry(EK_DirectoryRemap, Name, ExternalContentsPath, UseName) {}

    /// Check whether \p E is a DirectoryRemapEntry.
    ///
    /// \param E Entry to test.
    /// \returns True if \p E is a DirectoryRemapEntry.
    static bool classof(const Entry *E) {
      return E->getKind() == EK_DirectoryRemap;
    }
  };

  /// A file in the vfs that maps to a file in the external file system.
  class FileEntry : public RemapEntry {
  public:
    /// Construct a file remapping entry.
    ///
    /// \param Name Name of this file within its parent.
    /// \param ExternalContentsPath External file this entry maps to.
    /// \param UseName Whether to expose the external or virtual name.
    FileEntry(StringRef Name, StringRef ExternalContentsPath, NameKind UseName)
        : RemapEntry(EK_File, Name, ExternalContentsPath, UseName) {}

    /// Check whether \p E is a FileEntry.
    ///
    /// \param E Entry to test.
    /// \returns True if \p E is a FileEntry.
    static bool classof(const Entry *E) { return E->getKind() == EK_File; }
  };

  /// Represents the result of a path lookup into the RedirectingFileSystem.
  struct LookupResult {
    /// Chain of parent directory entries for \c E.
    llvm::SmallVector<Entry *, 32> Parents;

    /// The entry the looked-up path corresponds to.
    Entry *E;

  private:
    /// When the found Entry is a DirectoryRemapEntry, stores the path in the
    /// external file system that the looked-up path in the virtual file system
    //  corresponds to.
    std::optional<std::string> ExternalRedirect;

  public:
    /// Construct a lookup result for entry \p E over path components [\p Start, \p End).
    ///
    /// \param E Matched VFS entry.
    /// \param Start Beginning of the matched path component range.
    /// \param End End of the matched path component range.
    LLVM_ABI LookupResult(Entry *E, sys::path::const_iterator Start,
                          sys::path::const_iterator End);

    /// If the found Entry maps the input path to a path in the external
    /// file system (i.e. it is a FileEntry or DirectoryRemapEntry), returns
    /// that path.
    ///
    /// \returns The external redirect path, or std::nullopt if none.
    std::optional<StringRef> getExternalRedirect() const {
      if (isa<DirectoryRemapEntry>(E))
        return StringRef(*ExternalRedirect);
      if (auto *FE = dyn_cast<FileEntry>(E))
        return FE->getExternalContentsPath();
      return std::nullopt;
    }

    /// Get the (canonical) path of the found entry. This uses the as-written
    /// path components from the VFS specification.
    ///
    /// \param Path Receives the canonical path of the found entry.
    LLVM_ABI void getPath(llvm::SmallVectorImpl<char> &Path) const;
  };

private:
  friend class RedirectingFSDirIterImpl;
  friend class RedirectingFileSystemParser;

  /// Canonicalize path by removing ".", "..", "./", components. This is
  /// a VFS request, do not bother about symlinks in the path components
  /// but canonicalize in order to perform the correct entry search.
  std::error_code makeCanonicalForLookup(SmallVectorImpl<char> &Path) const;

  /// Get the File status, or error, from the underlying external file system.
  /// This returns the status with the originally requested name, while looking
  /// up the entry using a potentially different path.
  ErrorOr<Status> getExternalStatus(const Twine &LookupPath,
                                    const Twine &OriginalPath) const;

  /// Make \a Path an absolute path.
  ///
  /// Makes \a Path absolute using the \a WorkingDir if it is not already.
  ///
  /// /absolute/path   => /absolute/path
  /// relative/../path => <WorkingDir>/relative/../path
  ///
  /// \param WorkingDir  A path that will be used as the base Dir if \a Path
  ///                    is not already absolute.
  /// \param Path A path that is modified to be an absolute path.
  /// \returns success if \a path has been made absolute, otherwise a
  ///          platform-specific error_code.
  std::error_code makeAbsolute(StringRef WorkingDir,
                               SmallVectorImpl<char> &Path) const;

  // In a RedirectingFileSystem, keys can be specified in Posix or Windows
  // style (or even a mixture of both), so this comparison helper allows
  // slashes (representing a root) to match backslashes (and vice versa).  Note
  // that, other than the root, path components should not contain slashes or
  // backslashes.
  bool pathComponentMatches(llvm::StringRef lhs, llvm::StringRef rhs) const {
    if ((CaseSensitive ? lhs == rhs : lhs.equals_insensitive(rhs)))
      return true;
    return (lhs == "/" && rhs == "\\") || (lhs == "\\" && rhs == "/");
  }

  /// The root(s) of the virtual file system.
  std::vector<std::unique_ptr<Entry>> Roots;

  /// The current working directory of the file system.
  std::string WorkingDirectory;

  /// The file system to use for external references.
  IntrusiveRefCntPtr<FileSystem> ExternalFS;

  /// This represents the directory path that the YAML file is located.
  /// This will be prefixed to each 'external-contents' if IsRelativeOverlay
  /// is set. This will also be prefixed to each 'roots->name' if RootRelative
  /// is set to RootRelativeKind::OverlayDir and the path is relative.
  std::string OverlayFileDir;

  /// @name Configuration
  /// @{

  /// Whether to perform case-sensitive comparisons.
  ///
  /// Currently, case-insensitive matching only works correctly with ASCII.
  bool CaseSensitive = is_style_posix(sys::path::Style::native);

  /// IsRelativeOverlay marks whether a OverlayFileDir path must
  /// be prefixed in every 'external-contents' when reading from YAML files.
  bool IsRelativeOverlay = false;

  /// Whether to use to use the value of 'external-contents' for the
  /// names of files.  This global value is overridable on a per-file basis.
  bool UseExternalNames = true;

  /// True if this FS has redirected a lookup. This does not include
  /// fallthrough.
  mutable bool HasBeenUsed = false;

  /// Used to enable or disable updating `HasBeenUsed`.
  bool UsageTrackingActive = false;

  /// Determines the lookups to perform, as well as their order. See
  /// \c RedirectKind for details.
  RedirectKind Redirection = RedirectKind::Fallthrough;

  /// Determine the prefix directory if the roots are relative paths. See
  /// \c RootRelativeKind for details.
  RootRelativeKind RootRelative = RootRelativeKind::CWD;
  /// @}

  RedirectingFileSystem(IntrusiveRefCntPtr<FileSystem> ExternalFS);

  // Explicitly non-copyable.
  RedirectingFileSystem(RedirectingFileSystem const &) = delete;
  RedirectingFileSystem &operator=(RedirectingFileSystem const &) = delete;

  /// Looks up the path <tt>[Start, End)</tt> in \p From, possibly recursing
  /// into the contents of \p From if it is a directory. Returns a LookupResult
  /// giving the matched entry and, if that entry is a FileEntry or
  /// DirectoryRemapEntry, the path it redirects to in the external file system.
  ErrorOr<LookupResult>
  lookupPathImpl(llvm::sys::path::const_iterator Start,
                 llvm::sys::path::const_iterator End, Entry *From,
                 llvm::SmallVectorImpl<Entry *> &Entries) const;

  /// Get the status for a path with the provided \c LookupResult.
  ErrorOr<Status> status(const Twine &LookupPath, const Twine &OriginalPath,
                         const LookupResult &Result);

public:
  /// Look up \p Path in the VFS roots.
  ///
  /// Returns a LookupResult giving the matched entry and, if the entry was a
  /// FileEntry or DirectoryRemapEntry, the path it redirects to in the external
  /// file system.
  ///
  /// \param Path Virtual path to look up.
  /// \returns The matched entry and any external redirect, or an error.
  ErrorOr<LookupResult> lookupPath(StringRef Path) const;

  /// Parse a YAML buffer into a redirecting file system.
  ///
  /// \param Buffer YAML description of the virtual file system.
  /// \param DiagHandler Handler invoked for YAML diagnostics.
  /// \param YAMLFilePath Path used when reporting diagnostics.
  /// \param DiagContext Opaque context passed to \p DiagHandler.
  /// \param ExternalFS File system used for remapped external contents.
  /// \returns A RedirectingFileSystem parsed from the YAML buffer.
  static std::unique_ptr<RedirectingFileSystem>
  create(std::unique_ptr<MemoryBuffer> Buffer,
         SourceMgr::DiagHandlerTy DiagHandler, StringRef YAMLFilePath,
         void *DiagContext, IntrusiveRefCntPtr<FileSystem> ExternalFS);

  /// Create a redirecting file system from remapped path pairs.
  ///
  /// \param RemappedFiles Pairs of (virtual path, real path) to redirect.
  /// \param UseExternalNames Whether remapped entries expose external names.
  /// \param ExternalFS File system used for remapped external contents.
  /// \returns A RedirectingFileSystem built from the remapped path pairs.
  static std::unique_ptr<RedirectingFileSystem>
  create(ArrayRef<std::pair<std::string, std::string>> RemappedFiles,
         bool UseExternalNames, IntrusiveRefCntPtr<FileSystem> ExternalFS);

  /// Get the status of the entry at \p Path, if one exists.
  ///
  /// \param Path Path to query.
  /// \returns The status of the entry, or an error if it does not exist.
  ErrorOr<Status> status(const Twine &Path) override;
  /// Check whether \p Path exists in this file system.
  ///
  /// \param Path Path to test for existence.
  /// \returns True if \p Path exists in this file system.
  bool exists(const Twine &Path) override;
  /// Open the file at \p Path for reading.
  ///
  /// \param Path Path of the file to open.
  /// \returns A File for \p Path, or an error.
  ErrorOr<std::unique_ptr<File>> openFileForRead(const Twine &Path) override;

  /// Resolve the real path of \p Path.
  ///
  /// \param Path Path to resolve.
  /// \param Output Receives the resolved path on success.
  /// \returns A success or error code for resolving the path.
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) override;

  /// Get the working directory of this file system.
  ///
  /// \returns The working directory, or an error.
  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override;

  /// Set the working directory to \p Path.
  ///
  /// \param Path New working directory.
  /// \returns A success or error code for setting the working directory.
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;

  /// Check whether \p Path is on a local filesystem.
  ///
  /// \param Path Path to query.
  /// \param Result Set to true if the path is local.
  /// \returns A success or error code for the locality query.
  std::error_code isLocal(const Twine &Path, bool &Result) override;

  /// Make \p Path absolute using the current working directory.
  ///
  /// \param Path Path that is modified to be absolute.
  /// \returns A success or error code for making the path absolute.
  std::error_code makeAbsolute(SmallVectorImpl<char> &Path) const override;

  /// Begin iterating the directory at \p Dir.
  ///
  /// \param Dir Directory path to iterate.
  /// \param EC Receives an error code on failure.
  /// \returns An iterator over the directory entries, or the end iterator on error.
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override;

  /// Set the overlay file directory used for relative remappings.
  ///
  /// \param PrefixDir Directory that external paths may be relative to.
  void setOverlayFileDir(StringRef PrefixDir);

  /// Return the overlay file directory.
  ///
  /// \returns The overlay file directory used for relative remappings.
  StringRef getOverlayFileDir() const;

  /// Sets the redirection kind to \c Fallthrough if true or \c RedirectOnly
  /// otherwise. Will removed in the future, use \c setRedirection instead.
  ///
  /// \param Fallthrough True to fall through to the original path on miss.
  void setFallthrough(bool Fallthrough);

  /// Set how lookups are redirected.
  ///
  /// \param Kind Redirection strategy to use.
  void setRedirection(RedirectingFileSystem::RedirectKind Kind);

  /// Return the root entry names of this file system.
  ///
  /// \returns The root entry names of this file system.
  std::vector<llvm::StringRef> getRoots() const;

  /// Return whether any lookup has been redirected.
  ///
  /// \returns True if any lookup has been redirected.
  bool hasBeenUsed() const { return HasBeenUsed; };
  /// Clear the has-been-used flag.
  void clearHasBeenUsed() { HasBeenUsed = false; }

  /// Enable or disable usage tracking.
  ///
  /// \param Active True to record redirected lookups in \c HasBeenUsed.
  void setUsageTrackingActive(bool Active) { UsageTrackingActive = Active; }

  /// Print a VFS entry tree rooted at \p E.
  ///
  /// \param OS Stream to write to.
  /// \param E Entry to print.
  /// \param IndentLevel Indentation depth for this node.
  void printEntry(raw_ostream &OS, Entry *E, unsigned IndentLevel = 0) const;

protected:
  /// Print this redirecting file system.
  ///
  /// \param OS Stream to write to.
  /// \param Type How much detail to print.
  /// \param IndentLevel Indentation depth for this node.
  void printImpl(raw_ostream &OS, PrintType Type,
                 unsigned IndentLevel) const override;
  /// Visit the external file system used for remapped contents.
  ///
  /// \param Callback Invoked for each child file system.
  void visitChildFileSystems(VisitCallbackTy Callback) override;
};

/// Collect all virtual-to-real path pairs from \p VFS.
///
/// This is used by the module dependency collector to forward the entries into
/// the reproducer output VFS YAML file.
///
/// \param VFS Redirecting file system to walk.
/// \param CollectedEntries Receives the collected path mapping entries.
LLVM_ABI void
collectVFSEntries(RedirectingFileSystem &VFS,
                  SmallVectorImpl<YAMLVFSEntry> &CollectedEntries);

/// Helper that builds a YAML VFS overlay description.
class YAMLVFSWriter {
  std::vector<YAMLVFSEntry> Mappings;
  std::optional<bool> IsCaseSensitive;
  std::optional<bool> IsOverlayRelative;
  std::optional<bool> UseExternalNames;
  std::string OverlayDir;

  void addEntry(StringRef VirtualPath, StringRef RealPath, bool IsDirectory);

public:
  /// Construct an empty YAML VFS writer.
  YAMLVFSWriter() = default;

  /// Map \p VirtualPath to the real file at \p RealPath.
  ///
  /// \param VirtualPath Path as seen in the virtual file system.
  /// \param RealPath Path in the external file system.
  LLVM_ABI void addFileMapping(StringRef VirtualPath, StringRef RealPath);
  /// Map \p VirtualPath to the real directory at \p RealPath.
  ///
  /// \param VirtualPath Path as seen in the virtual file system.
  /// \param RealPath Path in the external file system.
  LLVM_ABI void addDirectoryMapping(StringRef VirtualPath, StringRef RealPath);

  /// Set whether path lookups are case-sensitive.
  ///
  /// \param CaseSensitive True for case-sensitive matching.
  void setCaseSensitivity(bool CaseSensitive) {
    IsCaseSensitive = CaseSensitive;
  }

  /// Set whether to expose external names for remapped entries.
  ///
  /// \param UseExtNames True to use external names by default.
  void setUseExternalNames(bool UseExtNames) { UseExternalNames = UseExtNames; }

  /// Set the overlay directory and mark mappings as overlay-relative.
  ///
  /// \param OverlayDirectory Directory that external paths are relative to.
  void setOverlayDir(StringRef OverlayDirectory) {
    IsOverlayRelative = true;
    OverlayDir.assign(OverlayDirectory.str());
  }

  /// Return the accumulated virtual-to-real path mappings.
  ///
  /// \returns The accumulated virtual-to-real path mappings.
  const std::vector<YAMLVFSEntry> &getMappings() const { return Mappings; }

  /// Write the YAML VFS description to \p OS.
  ///
  /// \param OS Output stream to receive the YAML.
  LLVM_ABI void write(llvm::raw_ostream &OS);
};

/// File system that counts calls to the underlying file system.
///
/// This is particularly useful when wrapped around \c RealFileSystem to add
/// lightweight tracking of expensive syscalls.
///
/// Templated on the counter type so callers can choose between non-atomic
/// counters (suitable for single-threaded tracing) and atomic counters
/// (suitable for tracing under concurrent access). Use the
/// \c TracingFileSystem and \c AtomicTracingFileSystem aliases below.
template <typename CounterT>
class TracingFileSystemImpl
    : public llvm::RTTIExtends<TracingFileSystemImpl<CounterT>,
                               ProxyFileSystem> {
public:
  /// RTTI type identifier for this class.
  inline static const char ID = 0;

  /// Number of calls to \c status.
  CounterT NumStatusCalls = 0;
  /// Number of calls to \c openFileForRead.
  CounterT NumOpenFileForReadCalls = 0;
  /// Number of calls to \c dir_begin.
  CounterT NumDirBeginCalls = 0;
  /// Number of calls to \c getRealPath.
  CounterT NumGetRealPathCalls = 0;
  /// Number of calls to \c exists.
  CounterT NumExistsCalls = 0;
  /// Number of calls to \c isLocal.
  CounterT NumIsLocalCalls = 0;

  /// Construct a tracing wrapper around \p FS.
  ///
  /// \param FS Underlying file system to forward calls to.
  TracingFileSystemImpl(llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS)
      : llvm::RTTIExtends<TracingFileSystemImpl<CounterT>, ProxyFileSystem>(
            std::move(FS)) {}

  /// Get the status of the entry at \p Path, counting the call.
  ///
  /// \param Path Path to query.
  /// \returns The status of the entry, or an error if it does not exist.
  ErrorOr<Status> status(const Twine &Path) override {
    ++NumStatusCalls;
    return ProxyFileSystem::status(Path);
  }

  /// Open the file at \p Path for reading, counting the call.
  ///
  /// \param Path Path of the file to open.
  /// \returns A File for \p Path, or an error.
  ErrorOr<std::unique_ptr<File>> openFileForRead(const Twine &Path) override {
    ++NumOpenFileForReadCalls;
    return ProxyFileSystem::openFileForRead(Path);
  }

  /// Begin iterating the directory at \p Dir, counting the call.
  ///
  /// \param Dir Directory path to iterate.
  /// \param EC Receives an error code on failure.
  /// \returns An iterator over the directory entries, or the end iterator on error.
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override {
    ++NumDirBeginCalls;
    return ProxyFileSystem::dir_begin(Dir, EC);
  }

  /// Resolve the real path of \p Path, counting the call.
  ///
  /// \param Path Path to resolve.
  /// \param Output Receives the resolved path on success.
  /// \returns A success or error code for resolving the path.
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) override {
    ++NumGetRealPathCalls;
    return ProxyFileSystem::getRealPath(Path, Output);
  }

  /// Check whether \p Path exists, counting the call.
  ///
  /// \param Path Path to test for existence.
  /// \returns True if \p Path exists.
  bool exists(const Twine &Path) override {
    ++NumExistsCalls;
    return ProxyFileSystem::exists(Path);
  }

  /// Check whether \p Path is on a local filesystem, counting the call.
  ///
  /// \param Path Path to query.
  /// \param Result Set to true if the path is local.
  /// \returns A success or error code for the locality query.
  std::error_code isLocal(const Twine &Path, bool &Result) override {
    ++NumIsLocalCalls;
    return ProxyFileSystem::isLocal(Path, Result);
  }

protected:
  /// Print this tracing file system and its call counts.
  ///
  /// \param OS Stream to write to.
  /// \param Type How much detail to print.
  /// \param IndentLevel Indentation depth for this node.
  void printImpl(raw_ostream &OS, FileSystem::PrintType Type,
                 unsigned IndentLevel) const override {
    FileSystem::printIndent(OS, IndentLevel);
    OS << "TracingFileSystem\n";
    if (Type == FileSystem::PrintType::Summary)
      return;

    FileSystem::printIndent(OS, IndentLevel);
    OS << "NumStatusCalls=" << static_cast<std::size_t>(NumStatusCalls) << "\n";
    FileSystem::printIndent(OS, IndentLevel);
    OS << "NumOpenFileForReadCalls="
       << static_cast<std::size_t>(NumOpenFileForReadCalls) << "\n";
    FileSystem::printIndent(OS, IndentLevel);
    OS << "NumDirBeginCalls=" << static_cast<std::size_t>(NumDirBeginCalls)
       << "\n";
    FileSystem::printIndent(OS, IndentLevel);
    OS << "NumGetRealPathCalls="
       << static_cast<std::size_t>(NumGetRealPathCalls) << "\n";
    FileSystem::printIndent(OS, IndentLevel);
    OS << "NumExistsCalls=" << static_cast<std::size_t>(NumExistsCalls) << "\n";
    FileSystem::printIndent(OS, IndentLevel);
    OS << "NumIsLocalCalls=" << static_cast<std::size_t>(NumIsLocalCalls)
       << "\n";

    if (Type == FileSystem::PrintType::Contents)
      Type = FileSystem::PrintType::Summary;
    this->getUnderlyingFS().print(OS, Type, IndentLevel + 1);
  }
};

/// Single-threaded tracing filesystem. Counters are plain \c std::size_t and
/// must not be incremented concurrently.
using TracingFileSystem = TracingFileSystemImpl<std::size_t>;

/// Concurrent-safe tracing filesystem. Counters are \c std::atomic<std::size_t>
/// so the proxy can be shared across threads.
using AtomicTracingFileSystem = TracingFileSystemImpl<std::atomic<std::size_t>>;

} // namespace vfs
} // namespace llvm

#endif // LLVM_SUPPORT_VIRTUALFILESYSTEM_H
