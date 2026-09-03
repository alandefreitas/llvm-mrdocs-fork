//===- StableFunctionMap.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This defines the StableFunctionMap class, to track similar functions.
// It provides a mechanism to map stable hashes of functions to their
// corresponding metadata. It includes structures for storing function details
// and methods for managing and querying these mappings.
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_CGDATA_STABLEFUNCTIONMAP_H
#define LLVM_CGDATA_STABLEFUNCTIONMAP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/StructuralHash.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include <mutex>
#include <unordered_map>

namespace llvm {

/// A pair of an IndexPair and the stable hash of the skipped operand.
using IndexPairHash = std::pair<IndexPair, stable_hash>;
/// A vector of skipped operand IndexPair/hash pairs.
using IndexOperandHashVecType = SmallVector<IndexPairHash>;

/// A stable function is a function with a stable hash while tracking the
/// locations of ignored operands and their hashes.
struct StableFunction {
  /// The combined stable hash of the function.
  stable_hash Hash;
  /// The name of the function.
  std::string FunctionName;
  /// The name of the module the function is in.
  std::string ModuleName;
  /// The number of instructions.
  unsigned InstCount;
  /// A vector of pairs of IndexPair and operand hash which was skipped.
  IndexOperandHashVecType IndexOperandHashes;

  /// Construct a stable function with the given hash and metadata.
  ///
  /// \param Hash Combined stable hash of the function.
  /// \param FunctionName Name of the function.
  /// \param ModuleName Name of the module containing the function.
  /// \param InstCount Number of instructions in the function.
  /// \param IndexOperandHashes Skipped operand locations and their hashes.
  StableFunction(stable_hash Hash, const std::string FunctionName,
                 const std::string ModuleName, unsigned InstCount,
                 IndexOperandHashVecType &&IndexOperandHashes)
      : Hash(Hash), FunctionName(FunctionName), ModuleName(ModuleName),
        InstCount(InstCount),
        IndexOperandHashes(std::move(IndexOperandHashes)) {}
  /// Construct an empty stable function.
  StableFunction() = default;
};

/// Maps stable function hashes to their corresponding metadata entries.
struct StableFunctionMap {
  /// An efficient form of StableFunction for fast look-up
  struct StableFunctionEntry {
    /// The combined stable hash of the function.
    stable_hash Hash;
    /// Id of the function name.
    unsigned FunctionNameId;
    /// Id of the module name.
    unsigned ModuleNameId;
    /// The number of instructions.
    unsigned InstCount;
    /// A map from an IndexPair to a stable_hash which was skipped.
    std::unique_ptr<IndexOperandHashMapType> IndexOperandHashMap;

    /// Construct a stable function entry with the given ids and metadata.
    ///
    /// \param Hash Combined stable hash of the function.
    /// \param FunctionNameId Id of the function name.
    /// \param ModuleNameId Id of the module name.
    /// \param InstCount Number of instructions in the function.
    /// \param IndexOperandHashMap Map of skipped operand locations to hashes.
    StableFunctionEntry(
        stable_hash Hash, unsigned FunctionNameId, unsigned ModuleNameId,
        unsigned InstCount,
        std::unique_ptr<IndexOperandHashMapType> IndexOperandHashMap)
        : Hash(Hash), FunctionNameId(FunctionNameId),
          ModuleNameId(ModuleNameId), InstCount(InstCount),
          IndexOperandHashMap(std::move(IndexOperandHashMap)) {}
  };

  /// A vector of unique pointers to StableFunctionEntry objects.
  using StableFunctionEntries =
      SmallVector<std::unique_ptr<StableFunctionEntry>>;

  /// Storage for stable function entries with lazy-loading support.
  ///
  /// In addition to the deserialized StableFunctionEntry, the struct stores
  /// the offsets of corresponding serialized stable function entries, and a
  /// once flag for safe lazy loading in a multithreaded environment.
  struct EntryStorage {
    /// Deserialized stable function entries for a function hash.
    ///
    /// If the map is lazily loaded, this will be empty until the first access
    /// by the corresponding function hash.
    StableFunctionEntries Entries;

  private:
    /// This is used to deserialize the entry lazily. Each element is the
    /// corresponding serialized stable function entry's offset in the memory
    /// buffer (StableFunctionMap::Buffer).
    /// The offsets are only populated when loading the map lazily, otherwise
    /// it is empty.
    SmallVector<uint64_t> Offsets;
    std::once_flag LazyLoadFlag;
    friend struct StableFunctionMap;
    friend struct StableFunctionMapRecord;
  };

  // Note: EntryStorage contains a std::once_flag, which is neither copyable
  // nor movable, and the lazy-loading design relies on value addresses being
  // stable. DenseMap relocates values on rehash, so use std::unordered_map.
  /// Map from a stable function hash to its entry storage.
  using HashFuncsMapType = std::unordered_map<stable_hash, EntryStorage>;

  /// Get the HashToFuncs map for serialization.
  ///
  /// \return The map from stable function hashes to entry storage.
  LLVM_ABI const HashFuncsMapType &getFunctionMap() const;

  /// Get the NameToId vector for serialization.
  ///
  /// \return The vector of names indexed by id.
  ArrayRef<std::string> getNames() const { return IdToName; }

  /// Get an existing ID associated with the given name or create a new ID if it
  /// doesn't exist.
  ///
  /// \param Name Function or module name to look up or insert.
  /// \return The ID associated with \p Name.
  LLVM_ABI unsigned getIdOrCreateForName(StringRef Name);

  /// Get the name associated with a given ID
  ///
  /// \param Id Name id previously returned by getIdOrCreateForName.
  /// \return The name for \p Id, or std::nullopt if \p Id is invalid.
  LLVM_ABI std::optional<std::string> getNameForId(unsigned Id) const;

  /// Insert a `StableFunction` object into the function map. This method
  /// handles the uniquing of string names and create a `StableFunctionEntry`
  /// for insertion.
  ///
  /// \param Func Stable function to insert into the map.
  LLVM_ABI void insert(const StableFunction &Func);

  /// Merge a \p OtherMap into this function map.
  ///
  /// \param OtherMap Stable function map whose entries are merged into this
  /// one.
  LLVM_ABI void merge(const StableFunctionMap &OtherMap);

  /// Return true if there is no stable function entry.
  ///
  /// \return True if the map has no stable function entries.
  bool empty() const { return size() == 0; }

  /// Return true if there is an entry for the given function hash.
  ///
  /// This does not trigger lazy loading.
  ///
  /// \param FunctionHash Stable function hash to look up.
  /// \return True if an entry exists for \p FunctionHash.
  bool contains(HashFuncsMapType::key_type FunctionHash) const {
    return HashToFuncs.count(FunctionHash) > 0;
  }

  /// Return the stable function entries for the given function hash.
  ///
  /// If the map is lazily loaded, it will deserialize the entries if it is not
  /// already done, other requests to the same hash at the same time will be
  /// blocked until the entries are deserialized.
  ///
  /// \param FunctionHash Stable function hash whose entries are returned.
  /// \return The stable function entries for \p FunctionHash.
  LLVM_ABI const StableFunctionEntries &
  at(HashFuncsMapType::key_type FunctionHash) const;

  /// Kind of size metric for StableFunctionMap::size.
  enum SizeType {
    UniqueHashCount,        ///< The number of unique hashes in HashToFuncs.
    TotalFunctionCount,     ///< The number of total functions in HashToFuncs.
    MergeableFunctionCount, ///< The number of functions that can be merged
                            ///< based on their hash.
  };

  /// Return the size of the stable function map.
  ///
  /// \param Type Which size metric to compute.
  /// \return The size according to \p Type.
  LLVM_ABI size_t size(SizeType Type = UniqueHashCount) const;

  /// Finalize the stable function map by trimming content.
  ///
  /// \param SkipTrim When true, skip trimming the map content.
  LLVM_ABI void finalize(bool SkipTrim = false);

private:
  /// Insert a `StableFunctionEntry` into the function map directly. This
  /// method assumes that string names have already been uniqued and the
  /// `StableFunctionEntry` is ready for insertion.
  void insert(std::unique_ptr<StableFunctionEntry> FuncEntry) {
    assert(!Finalized && "Cannot insert after finalization");
    HashToFuncs[FuncEntry->Hash].Entries.emplace_back(std::move(FuncEntry));
  }

  void deserializeLazyLoadingEntry(HashFuncsMapType::iterator It) const;

  /// Eagerly deserialize all the unloaded entries in the lazy loading map.
  void deserializeLazyLoadingEntries() const;

  bool isLazilyLoaded() const { return (bool)Buffer; }

  /// A map from a stable_hash to a vector of functions with that hash.
  mutable HashFuncsMapType HashToFuncs;
  /// A vector of strings to hold names.
  SmallVector<std::string> IdToName;
  /// A map from StringRef (name) to an ID.
  StringMap<unsigned> NameToId;
  /// True if the function map is finalized with minimal content.
  bool Finalized = false;
  /// The memory buffer that contains the serialized stable function map for
  /// lazy loading.
  /// Non-empty only if this StableFunctionMap is created from a MemoryBuffer
  /// (i.e. by IndexedCodeGenDataReader::read()) and lazily deserialized.
  std::shared_ptr<MemoryBuffer> Buffer;
  /// Whether to read stable function names from the buffer.
  bool ReadStableFunctionMapNames = true;

  friend struct StableFunctionMapRecord;
};

} // namespace llvm

#endif
