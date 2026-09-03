//===- OutlinedHashTreeRecord.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This defines the OutlinedHashTreeRecord class. This class holds the outlined
// hash tree for both serialization and deserialization processes. It utilizes
// two data formats for serialization: raw binary data and YAML.
// These two formats can be used interchangeably.
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_CGDATA_OUTLINEDHASHTREERECORD_H
#define LLVM_CGDATA_OUTLINEDHASHTREERECORD_H

#include "llvm/CGData/OutlinedHashTree.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// HashNodeStable is the serialized, stable, and compact representation
/// of a HashNode.
struct HashNodeStable {
  /// Stable hash value of the node.
  llvm::yaml::Hex64 Hash;
  /// Number of terminals in the sequence ending at this node.
  unsigned Terminals;
  /// IDs of the successor nodes in the stable representation.
  std::vector<unsigned> SuccessorIds;
};

/// Map from stable node IDs to their serialized HashNodeStable records.
using IdHashNodeStableMapTy = std::map<unsigned, HashNodeStable>;
/// Map from node IDs to mutable HashNode pointers.
using IdHashNodeMapTy = DenseMap<unsigned, HashNode *>;
/// Map from HashNode pointers to their stable node IDs.
using HashNodeIdMapTy = DenseMap<const HashNode *, unsigned>;

/// Holds an outlined hash tree for serialization and deserialization.
///
/// Supports interchangeable raw binary and YAML data formats.
struct OutlinedHashTreeRecord {
  /// Owned outlined hash tree held by this record.
  std::unique_ptr<OutlinedHashTree> HashTree;

  /// Construct a record with an empty outlined hash tree.
  OutlinedHashTreeRecord() { HashTree = std::make_unique<OutlinedHashTree>(); }
  /// Construct a record that takes ownership of an existing hash tree.
  ///
  /// \param HashTree Outlined hash tree to take ownership of.
  OutlinedHashTreeRecord(std::unique_ptr<OutlinedHashTree> HashTree)
      : HashTree(std::move(HashTree)) {};

  /// Serialize the outlined hash tree to a raw_ostream.
  ///
  /// \param OS Stream that receives the serialized binary data.
  LLVM_ABI void serialize(raw_ostream &OS) const;
  /// Deserialize the outlined hash tree from a raw_ostream.
  ///
  /// \param Ptr Pointer into the binary buffer; advanced past the data read.
  LLVM_ABI void deserialize(const unsigned char *&Ptr);
  /// Serialize the outlined hash tree to a YAML stream.
  ///
  /// \param YOS YAML output stream that receives the serialized tree.
  LLVM_ABI void serializeYAML(yaml::Output &YOS) const;
  /// Deserialize the outlined hash tree from a YAML stream.
  ///
  /// \param YIS YAML input stream to read the tree from.
  LLVM_ABI void deserializeYAML(yaml::Input &YIS);

  /// Merge the other outlined hash tree into this one.
  ///
  /// \param Other Record whose outlined hash tree is merged into this one.
  void merge(const OutlinedHashTreeRecord &Other) {
    HashTree->merge(Other.HashTree.get());
  }

  /// Check whether the outlined hash tree is empty.
  ///
  /// \returns true if the outlined hash tree is empty.
  bool empty() const { return HashTree->empty(); }

  /// Print the outlined hash tree in a YAML format.
  ///
  /// \param OS Stream that receives the YAML representation.
  void print(raw_ostream &OS = llvm::errs()) const {
    yaml::Output YOS(OS);
    serializeYAML(YOS);
  }

private:
  /// Convert the outlined hash tree to stable data.
  void convertToStableData(IdHashNodeStableMapTy &IdNodeStableMap) const;

  /// Convert the stable data back to the outlined hash tree.
  void convertFromStableData(const IdHashNodeStableMapTy &IdNodeStableMap);
};

} // end namespace llvm

#endif // LLVM_CGDATA_OUTLINEDHASHTREERECORD_H
