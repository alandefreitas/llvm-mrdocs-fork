//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This declares OnDiskGraphDB, an ondisk CAS database with a fixed length
/// hash. This is the class that implements the database storage scheme without
/// exposing the hashing algorithm.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CAS_ONDISKGRAPHDB_H
#define LLVM_CAS_ONDISKGRAPHDB_H

#include "llvm/ADT/PointerUnion.h"
#include "llvm/CAS/OnDiskCASLogger.h"
#include "llvm/CAS/OnDiskDataAllocator.h"
#include "llvm/CAS/OnDiskTrieRawHashMap.h"
#include <atomic>

namespace llvm {
/// In-memory buffer of file or other content (forward declaration).
class MemoryBuffer;
} // namespace llvm

namespace llvm::cas::ondisk {

/// Standard 8 byte reference inside OnDiskGraphDB.
class InternalRef {
public:
  /// \returns the file offset encoded by this reference.
  FileOffset getFileOffset() const { return FileOffset(Data); }
  /// \returns the raw 64-bit encoding of this reference.
  uint64_t getRawData() const { return Data; }

  /// Reconstruct a reference from its raw encoding.
  static InternalRef getFromRawData(uint64_t Data) { return InternalRef(Data); }
  /// Reconstruct a reference from file offset \p Offset.
  static InternalRef getFromOffset(FileOffset Offset) {
    return InternalRef(Offset.get());
  }

  /// Compare two internal references for equality.
  friend bool operator==(InternalRef LHS, InternalRef RHS) {
    return LHS.Data == RHS.Data;
  }

private:
  InternalRef(FileOffset Offset) : Data((uint64_t)Offset.get()) {}
  InternalRef(uint64_t Data) : Data(Data) {}
  uint64_t Data;
};

/// Compact 4 byte reference inside OnDiskGraphDB for smaller references.
class InternalRef4B {
public:
  /// \returns the file offset encoded by this reference.
  FileOffset getFileOffset() const { return FileOffset(Data); }
  /// \returns the raw 32-bit encoding of this reference.
  uint32_t getRawData() const { return Data; }

  /// Shrink to 4B reference.
  static std::optional<InternalRef4B> tryToShrink(InternalRef Ref) {
    uint64_t Offset = Ref.getRawData();
    if (Offset > UINT32_MAX)
      return std::nullopt;
    return InternalRef4B(Offset);
  }

  /// Widen this compact reference to an 8-byte \p InternalRef.
  operator InternalRef() const {
    return InternalRef::getFromOffset(getFileOffset());
  }

private:
  friend class InternalRef;
  InternalRef4B(uint32_t Data) : Data(Data) {}
  uint32_t Data;
};

/// Array of internal node references.
class InternalRefArrayRef {
public:
  /// \returns the number of references in the array.
  size_t size() const { return Size; }
  /// Return whether the array has no references.
  /// \returns true if the array has no references.
  bool empty() const { return !Size; }

  class iterator
      : public iterator_facade_base<iterator, std::random_access_iterator_tag,
                                    const InternalRef> {
  public:
    /// Return true if both iterators refer to the same position.
    bool operator==(const iterator &RHS) const { return I == RHS.I; }
    /// \returns the InternalRef at this iterator position.
    InternalRef operator*() const {
      if (auto *Ref = dyn_cast<const InternalRef *>(I))
        return *Ref;
      return InternalRef(*cast<const InternalRef4B *>(I));
    }
    /// Return true if this iterator is before \p RHS in address order.
    /// @param RHS Iterator to compare with.
    bool operator<(const iterator &RHS) const {
      assert(isa<const InternalRef *>(I) == isa<const InternalRef *>(RHS.I));
      if (auto *Ref = dyn_cast<const InternalRef *>(I))
        return Ref < cast<const InternalRef *>(RHS.I);
      return cast<const InternalRef4B *>(I) -
             cast<const InternalRef4B *>(RHS.I);
    }
    /// \returns the distance between this iterator and \p RHS.
    ptrdiff_t operator-(const iterator &RHS) const {
      assert(isa<const InternalRef *>(I) == isa<const InternalRef *>(RHS.I));
      if (auto *Ref = dyn_cast<const InternalRef *>(I))
        return Ref - cast<const InternalRef *>(RHS.I);
      return cast<const InternalRef4B *>(I) -
             cast<const InternalRef4B *>(RHS.I);
    }
    /// Advance this iterator by \p N positions.
    iterator &operator+=(ptrdiff_t N) {
      if (auto *Ref = dyn_cast<const InternalRef *>(I))
        I = Ref + N;
      else
        I = cast<const InternalRef4B *>(I) + N;
      return *this;
    }
    /// Retreat this iterator by \p N positions.
    iterator &operator-=(ptrdiff_t N) {
      if (auto *Ref = dyn_cast<const InternalRef *>(I))
        I = Ref - N;
      else
        I = cast<const InternalRef4B *>(I) - N;
      return *this;
    }
    /// Return the reference at offset \p N from this iterator.
    /// \returns the reference at offset \p N from this iterator.
    InternalRef operator[](ptrdiff_t N) const { return *(this->operator+(N)); }

    /// Default-construct an empty iterator.
    iterator() = default;

    /// \returns an opaque bit pattern for this iterator.
    uint64_t getOpaqueData() const { return uintptr_t(I.getOpaqueValue()); }

    /// Reconstruct an iterator from an opaque bit pattern.
    static iterator fromOpaqueData(uint64_t Opaque) {
      return iterator(
          PointerUnion<const InternalRef *,
                       const InternalRef4B *>::getFromOpaqueValue((void *)
                                                                      Opaque));
    }

  private:
    friend class InternalRefArrayRef;
    explicit iterator(
        PointerUnion<const InternalRef *, const InternalRef4B *> I)
        : I(I) {}
    PointerUnion<const InternalRef *, const InternalRef4B *> I;
  };

  /// Compare two reference arrays for equality.
  bool operator==(const InternalRefArrayRef &RHS) const {
    return size() == RHS.size() && std::equal(begin(), end(), RHS.begin());
  }

  /// \returns an iterator to the first reference.
  iterator begin() const { return iterator(Begin); }
  /// \returns an iterator past the last reference.
  iterator end() const { return begin() + Size; }

  /// Array accessor.
  InternalRef operator[](ptrdiff_t N) const { return begin()[N]; }

  /// Return whether references are stored as 4-byte values.
  /// \returns true if references are stored as 4-byte \p InternalRef4B values.
  bool is4B() const { return isa<const InternalRef4B *>(Begin); }
  /// Return whether references are stored as 8-byte values.
  /// \returns true if references are stored as 8-byte \p InternalRef values.
  bool is8B() const { return isa<const InternalRef *>(Begin); }

  /// \returns the underlying reference storage as a byte buffer.
  ArrayRef<uint8_t> getBuffer() const {
    if (is4B()) {
      auto *B = cast<const InternalRef4B *>(Begin);
      return ArrayRef((const uint8_t *)B, sizeof(InternalRef4B) * Size);
    }
    auto *B = cast<const InternalRef *>(Begin);
    return ArrayRef((const uint8_t *)B, sizeof(InternalRef) * Size);
  }

  /// Construct an empty reference array with a non-null placeholder begin.
  InternalRefArrayRef(std::nullopt_t = std::nullopt) {
    // This is useful so that all the casts in the \p iterator functions can
    // operate without needing to check for a null value.
    static InternalRef PlaceHolder = InternalRef::getFromRawData(0);
    Begin = &PlaceHolder;
  }

  /// Construct from an array of 8-byte internal references.
  InternalRefArrayRef(ArrayRef<InternalRef> Refs)
      : Begin(Refs.begin()), Size(Refs.size()) {}

  /// Construct from an array of 4-byte internal references.
  InternalRefArrayRef(ArrayRef<InternalRef4B> Refs)
      : Begin(Refs.begin()), Size(Refs.size()) {}

private:
  PointerUnion<const InternalRef *, const InternalRef4B *> Begin;
  size_t Size = 0;
};

/// Reference to a node. The node's data may not be stored in the database.
/// An \p ObjectID instance can only be used with the \p OnDiskGraphDB instance
/// it came from. \p ObjectIDs from different \p OnDiskGraphDB instances are not
/// comparable.
class ObjectID {
public:
  /// \returns an opaque bit pattern identifying this object ID.
  uint64_t getOpaqueData() const { return Opaque; }

  /// Reconstruct an \p ObjectID from an opaque bit pattern.
  static ObjectID fromOpaqueData(uint64_t Opaque) { return ObjectID(Opaque); }

  /// Compare two object IDs for equality.
  friend bool operator==(const ObjectID &LHS, const ObjectID &RHS) {
    return LHS.Opaque == RHS.Opaque;
  }
  /// Compare two object IDs for inequality.
  friend bool operator!=(const ObjectID &LHS, const ObjectID &RHS) {
    return !(LHS == RHS);
  }

private:
  explicit ObjectID(uint64_t Opaque) : Opaque(Opaque) {}
  uint64_t Opaque;
};

/// Handle for a loaded node object.
class ObjectHandle {
public:
  /// Construct a handle from an opaque integer encoding.
  explicit ObjectHandle(uint64_t Opaque) : Opaque(Opaque) {}
  /// \returns the opaque integer encoding of this handle.
  uint64_t getOpaqueData() const { return Opaque; }

  /// Create a handle from file offset \p Offset in the on-disk database.
  LLVM_ABI static ObjectHandle fromFileOffset(FileOffset Offset);
  /// Create a handle from an in-memory object pointer.
  LLVM_ABI static ObjectHandle fromMemory(uintptr_t Ptr);

  /// Compare two object handles for equality of their opaque encodings.
  friend bool operator==(const ObjectHandle &LHS, const ObjectHandle &RHS) {
    return LHS.Opaque == RHS.Opaque;
  }
  /// Compare two object handles for inequality.
  friend bool operator!=(const ObjectHandle &LHS, const ObjectHandle &RHS) {
    return !(LHS == RHS);
  }

private:
  uint64_t Opaque;
};

/// Iterator for ObjectID.
class object_refs_iterator
    : public iterator_facade_base<object_refs_iterator,
                                  std::random_access_iterator_tag, ObjectID> {
public:
  /// \returns true if both iterators point to the same position.
  bool operator==(const object_refs_iterator &RHS) const { return I == RHS.I; }
  /// \returns the ObjectID at this iterator position.
  ObjectID operator*() const {
    return ObjectID::fromOpaqueData((*I).getRawData());
  }
  /// \returns true if this iterator precedes \p RHS.
  bool operator<(const object_refs_iterator &RHS) const { return I < RHS.I; }
  /// \returns the distance between this iterator and \p RHS.
  ptrdiff_t operator-(const object_refs_iterator &RHS) const {
    return I - RHS.I;
  }
  /// Advance this iterator by \p N positions.
  object_refs_iterator &operator+=(ptrdiff_t N) {
    I += N;
    return *this;
  }
  /// Retreat this iterator by \p N positions.
  object_refs_iterator &operator-=(ptrdiff_t N) {
    I -= N;
    return *this;
  }
  /// \returns the \p ObjectID at offset \p N from this iterator.
  ObjectID operator[](ptrdiff_t N) const { return *(this->operator+(N)); }

  /// Default-construct an empty iterator.
  object_refs_iterator() = default;
  /// Construct from an internal reference array iterator.
  object_refs_iterator(InternalRefArrayRef::iterator I) : I(I) {}

  /// \returns an opaque bit pattern for this iterator.
  uint64_t getOpaqueData() const { return I.getOpaqueData(); }

  /// Reconstruct an iterator from an opaque bit pattern.
  static object_refs_iterator fromOpaqueData(uint64_t Opaque) {
    return InternalRefArrayRef::iterator::fromOpaqueData(Opaque);
  }

private:
  InternalRefArrayRef::iterator I;
};

/// Range of ObjectID values over an internal reference array.
using object_refs_range = llvm::iterator_range<object_refs_iterator>;

/// On-disk CAS nodes database, independent of a particular hashing algorithm.
class OnDiskGraphDB {
public:
  /// Associate data & references with a particular object ID. If there is
  /// already a record for this object the operation is a no-op. \param ID the
  /// object ID to associate the data & references with. \param Refs references
  /// \param Data data buffer.
  LLVM_ABI Error store(ObjectID ID, ArrayRef<ObjectID> Refs,
                       ArrayRef<char> Data);

  /// Associates the data of a file with a particular object ID. If there is
  /// already a record for this object the operation is a no-op.
  ///
  /// This is more than a convenience variant of \c store(), \c storeFile() can
  /// perform optimizations that reduce I/O and disk space consumption.
  ///
  /// If there are any concurrent modifications to the file, the contents in the
  /// CAS may be corrupt.
  ///
  /// \param ID the object ID to associate the data with.
  /// \param FilePath the path of the file data.
  LLVM_ABI Error storeFile(ObjectID ID, StringRef FilePath);

  /// \returns \p nullopt if the object associated with \p Ref does not exist.
  LLVM_ABI Expected<std::optional<ObjectHandle>> load(ObjectID Ref);

  /// \returns the hash bytes digest for the object reference.
  ArrayRef<uint8_t> getDigest(ObjectID Ref) const {
    // ObjectID should be valid to fetch Digest.
    return cantFail(getDigest(getInternalRef(Ref)));
  }

  /// Form a reference for the provided hash. The reference can be used as part
  /// of a CAS object even if it's not associated with an object yet.
  LLVM_ABI Expected<ObjectID> getReference(ArrayRef<uint8_t> Hash);

  /// Get an existing reference to the object \p Digest.
  ///
  /// Returns \p nullopt if the object is not stored in this CAS.
  LLVM_ABI std::optional<ObjectID>
  getExistingReference(ArrayRef<uint8_t> Digest, bool CheckUpstream = true);

  /// Check whether the object associated with \p Ref is stored in the CAS.
  /// Note that this function will fault-in according to the policy.
  LLVM_ABI Expected<bool> isMaterialized(ObjectID Ref);

  /// Check whether the object associated with \p Ref is stored in the CAS.
  /// Note that this function does not fault-in.
  bool containsObject(ObjectID Ref, bool CheckUpstream = true) const {
    auto Presence = getObjectPresence(Ref, CheckUpstream);
    if (!Presence) {
      consumeError(Presence.takeError());
      return false;
    }
    switch (*Presence) {
    case ObjectPresence::Missing:
      return false;
    case ObjectPresence::InPrimaryDB:
      return true;
    case ObjectPresence::OnlyInUpstreamDB:
      return true;
    }
    llvm_unreachable("Unknown ObjectPresence enum");
  }

  /// \returns the data part of the provided object handle.
  LLVM_ABI ArrayRef<char> getObjectData(ObjectHandle Node) const;

  /// \returns the object referenced by the provided object handle.
  object_refs_range getObjectRefs(ObjectHandle Node) const {
    InternalRefArrayRef Refs = getInternalRefs(Node);
    return make_range(Refs.begin(), Refs.end());
  }

  /// Encapsulates file info for an underlying object node.
  struct FileBackedData {
    /// The data of the object node.
    ArrayRef<char> Data;

    /// Path and layout details for a file-backed object leaf.
    struct FileInfoTy {
      /// The file path of the object node.
      std::string FilePath;
      /// Whether the file of the object leaf node has an extra nul appended at
      /// the end. If the file is copied the extra nul needs to be removed.
      bool IsFileNulTerminated;
    };
    /// File information for the object, if available.
    std::optional<FileInfoTy> FileInfo;
  };

  /// Provides access to the underlying file path, that represents an object
  /// leaf node, when available.
  ///
  /// This enables reducing I/O and disk space consumption, i.e. instead of
  /// loading the data in memory and then writing it to a file, the client could
  /// clone the underlying file directly. The client *must not* write to or
  /// delete the underlying file, the path is provided only for reading/copying.
  LLVM_ABI FileBackedData
  getInternalFileBackedObjectData(ObjectHandle Node) const;

  /// Get a MemoryBuffer for \p Node's data that stays valid after this
  /// database is destroyed.
  ///
  /// Objects stored in a file of their own are re-read from it rather than
  /// copied out of this database's mapping, which lets the pages be shared and
  /// reclaimed rather than charged to this process. The rest are copied. Never
  /// returns \c nullptr.
  LLVM_ABI std::unique_ptr<MemoryBuffer>
  getStandaloneMemoryBuffer(ObjectHandle Node, StringRef Name,
                            bool RequiresNullTerminator) const;

  /// \returns Total size of stored objects.
  ///
  /// NOTE: There's a possibility that the returned size is not including a
  /// large object if the process crashed right at the point of inserting it.
  LLVM_ABI size_t getStorageSize() const;

  /// \returns The precentage of space utilization of hard space limits.
  ///
  /// Return value is an integer between 0 and 100 for percentage.
  LLVM_ABI unsigned getHardStorageLimitUtilization() const;

  /// Print a debug dump of the database to \p OS.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Hashing function type for validation.
  using HashingFuncT = function_ref<void(
      ArrayRef<ArrayRef<uint8_t>>, ArrayRef<char>, SmallVectorImpl<uint8_t> &)>;

  /// Validate the OnDiskGraphDB.
  ///
  /// \param Deep if true, rehash all the objects to ensure no data
  /// corruption in stored objects, otherwise just validate the structure of
  /// CAS database.
  /// \param Hasher is the hashing function used for objects inside CAS.
  LLVM_ABI Error validate(bool Deep, HashingFuncT Hasher) const;

  /// Checks that \p ID exists in the index. It is allowed to not have data
  /// associated with it.
  LLVM_ABI Error validateObjectID(ObjectID ID) const;

  /// How to fault-in nodes if an upstream database is used.
  enum class FaultInPolicy {
    /// Copy only the requested node.
    SingleNode,
    /// Copy the the entire graph of a node.
    FullTree,
  };

  /// Open the on-disk store from a directory.
  ///
  /// \param Path directory for the on-disk store. The directory will be created
  /// if it doesn't exist.
  /// \param HashName Identifier name for the hashing algorithm that is going to
  /// be used.
  /// \param HashByteSize Size for the object digest hash bytes.
  /// \param UpstreamDB Optional on-disk store to be used for faulting-in nodes
  /// if they don't exist in the primary store. The upstream store is only used
  /// for reading nodes, new nodes are only written to the primary store. User
  /// need to make sure \p UpstreamDB outlives current instance of
  /// OnDiskGraphDB and the common usage is to have an \p UnifiedOnDiskCache to
  /// manage both.
  /// \param Policy If \p UpstreamDB is provided, controls how nodes are copied
  /// to primary store. This is recorded at creation time and subsequent opens
  /// need to pass the same policy otherwise the \p open will fail.
  LLVM_ABI static Expected<std::unique_ptr<OnDiskGraphDB>>
  open(StringRef Path, StringRef HashName, unsigned HashByteSize,
       OnDiskGraphDB *UpstreamDB = nullptr,
       std::shared_ptr<OnDiskCASLogger> Logger = nullptr,
       FaultInPolicy Policy = FaultInPolicy::FullTree);

  /// Destroy the on-disk graph database and release mapped resources.
  LLVM_ABI ~OnDiskGraphDB();

private:
  /// Forward declaration for a proxy for an ondisk index record.
  struct IndexProxy;

  enum class ObjectPresence {
    Missing,
    InPrimaryDB,
    OnlyInUpstreamDB,
  };

  /// Check if object exists and if it is on upstream only.
  LLVM_ABI Expected<ObjectPresence> getObjectPresence(ObjectID Ref,
                                                      bool CheckUpstream) const;

  /// When \p load is called for a node that doesn't exist, this function tries
  /// to load it from the upstream store and copy it to the primary one.
  Expected<std::optional<ObjectHandle>> faultInFromUpstream(ObjectID PrimaryID);

  /// Import the entire tree from upstream with \p UpstreamNode as root.
  Error importFullTree(ObjectID PrimaryID, ObjectHandle UpstreamNode);
  /// Import only the \param UpstreamNode.
  Error importSingleNode(ObjectID PrimaryID, ObjectHandle UpstreamNode);
  Error importUpstreamData(ObjectID PrimaryID, ArrayRef<ObjectID> PrimaryRefs,
                           ObjectHandle UpstreamNode);

  enum class InternalUpstreamImportKind { Leaf, Leaf0 };
  /// Private \c storeFile than optimizes internal upstream database imports.
  Error storeFile(ObjectID ID, StringRef FilePath,
                  std::optional<InternalUpstreamImportKind> ImportKind);

  /// Found the IndexProxy for the hash.
  Expected<IndexProxy> indexHash(ArrayRef<uint8_t> Hash);

  /// Get path for creating standalone data file.
  void getStandalonePath(StringRef FileSuffix, FileOffset IndexOffset,
                         SmallVectorImpl<char> &Path) const;
  /// Create a standalone leaf file.
  Error createStandaloneLeaf(IndexProxy &I, ArrayRef<char> Data);

  /// \name Helper functions for internal data structures.
  /// \{
  static InternalRef getInternalRef(ObjectID Ref) {
    return InternalRef::getFromRawData(Ref.getOpaqueData());
  }

  static ObjectID getExternalReference(InternalRef Ref) {
    return ObjectID::fromOpaqueData(Ref.getRawData());
  }

  static ObjectID getExternalReference(const IndexProxy &I);

  static InternalRef makeInternalRef(FileOffset IndexOffset);

  LLVM_ABI Expected<ArrayRef<uint8_t>> getDigest(InternalRef Ref) const;

  ArrayRef<uint8_t> getDigest(const IndexProxy &I) const;

  Expected<IndexProxy> getIndexProxyFromRef(InternalRef Ref) const;

  IndexProxy
  getIndexProxyFromPointer(OnDiskTrieRawHashMap::ConstOnDiskPtr P) const;

  LLVM_ABI InternalRefArrayRef getInternalRefs(ObjectHandle Node) const;
  /// \}

  /// Get the atomic variable that keeps track of the standalone data storage
  /// size.
  std::atomic<uint64_t> &standaloneStorageSize() const;

  /// Increase the standalone data size.
  void recordStandaloneSizeIncrease(size_t SizeIncrease);
  /// Get the standalone data size.
  uint64_t getStandaloneStorageSize() const;

  // Private constructor.
  OnDiskGraphDB(StringRef RootPath, OnDiskTrieRawHashMap Index,
                OnDiskDataAllocator DataPool, OnDiskGraphDB *UpstreamDB,
                FaultInPolicy Policy, std::shared_ptr<OnDiskCASLogger> Logger);

  /// Mapping from hash to object reference.
  ///
  /// Data type is TrieRecord.
  OnDiskTrieRawHashMap Index;

  /// Storage for most objects.
  ///
  /// Data type is DataRecordHandle.
  OnDiskDataAllocator DataPool;

  /// A StandaloneDataMap.
  void *StandaloneData = nullptr;

  /// Path to the root directory.
  std::string RootPath;

  /// Optional on-disk store to be used for faulting-in nodes.
  OnDiskGraphDB *UpstreamDB = nullptr;

  /// The policy used to fault in data from upstream.
  FaultInPolicy FIPolicy;

  /// Debug Logger.
  std::shared_ptr<OnDiskCASLogger> Logger;
};

} // namespace llvm::cas::ondisk

#endif // LLVM_CAS_ONDISKGRAPHDB_H
