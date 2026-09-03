//===-- MsgPackDocument.h - MsgPack Document --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares a class that exposes a simple in-memory representation
/// of a document of MsgPack objects, that can be read from MsgPack, written to
/// MsgPack, and inspected and modified in memory. This is intended to be a
/// lighter-weight (in terms of memory allocations) replacement for
/// MsgPackTypes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MSGPACKDOCUMENT_H
#define LLVM_BINARYFORMAT_MSGPACKDOCUMENT_H

#include "llvm/BinaryFormat/MsgPackReader.h"
#include "llvm/Support/Compiler.h"
#include <map>

namespace llvm {
namespace msgpack {

class ArrayDocNode;
class Document;
class MapDocNode;

/// The kind of a DocNode and its owning Document.
struct KindAndDocument {
  /// Owning Document for the associated DocNode.
  Document *Doc;
  /// MsgPack type of the associated DocNode.
  Type Kind;
};

/// A node in a MsgPack Document. This is a simple copyable and
/// passable-by-value type that does not own any memory.
class DocNode {
  friend Document;

public:
  /// Map type used for MsgPack map nodes.
  typedef std::map<DocNode, DocNode> MapTy;
  /// Array type used for MsgPack array nodes.
  typedef std::vector<DocNode> ArrayTy;

private:
  // Using KindAndDocument allows us to squeeze Kind and a pointer to the
  // owning Document into the same word. Having a pointer to the owning
  // Document makes the API of DocNode more convenient, and allows its use in
  // YAMLIO.
  const KindAndDocument *KindAndDoc;

protected:
  /// Union of scalar and container payloads for this node.
  union {
    /// Signed integer value for \c Type::Int.
    int64_t Int;
    /// Unsigned integer value for \c Type::UInt.
    uint64_t UInt;
    /// Boolean value for \c Type::Boolean.
    bool Bool;
    /// Floating-point value for \c Type::Float.
    double Float;
    /// String or binary payload for \c Type::String and \c Type::Binary.
    StringRef Raw;
    /// Pointer to array storage for \c Type::Array.
    ArrayTy *Array;
    /// Pointer to map storage for \c Type::Map.
    MapTy *Map;
  };

public:
  /// Construct an empty node with no associated Document.
  ///
  /// All you can do with it is \c isEmpty().
  DocNode() : KindAndDoc(nullptr) {}

  /// Return true if this node is a map.
  /// \returns true if this node is a map.
  bool isMap() const { return getKind() == Type::Map; }
  /// Return true if this node is an array.
  /// \returns true if this node is an array.
  bool isArray() const { return getKind() == Type::Array; }
  /// Return true if this node is a scalar (not a map or array).
  /// \returns true if this node is a scalar.
  bool isScalar() const { return !isMap() && !isArray(); }
  /// Return true if this node is a string.
  /// \returns true if this node is a string.
  bool isString() const { return getKind() == Type::String; }

  /// Return true if this node is empty.
  ///
  /// Returns true for both a default-constructed DocNode that has no
  /// associated Document, and the result of getEmptyNode(), which does have
  /// an associated document.
  /// \returns true if this node is empty.
  bool isEmpty() const { return !KindAndDoc || getKind() == Type::Empty; }
  /// Return the MsgPack type of this node.
  /// \returns the MsgPack type of this node.
  Type getKind() const {
    assert(KindAndDoc);
    return KindAndDoc->Kind;
  }
  /// Return the Document that owns this node.
  /// \returns the Document that owns this node.
  Document *getDocument() const {
    assert(KindAndDoc);
    return KindAndDoc->Doc;
  }

  /// Return a mutable reference to the signed integer value.
  /// \returns a mutable reference to the signed integer value.
  int64_t &getInt() {
    assert(getKind() == Type::Int);
    return Int;
  }

  /// Return a mutable reference to the unsigned integer value.
  /// \returns a mutable reference to the unsigned integer value.
  uint64_t &getUInt() {
    assert(getKind() == Type::UInt);
    return UInt;
  }

  /// Return a mutable reference to the boolean value.
  /// \returns a mutable reference to the boolean value.
  bool &getBool() {
    assert(getKind() == Type::Boolean);
    return Bool;
  }

  /// Return a mutable reference to the floating-point value.
  /// \returns a mutable reference to the floating-point value.
  double &getFloat() {
    assert(getKind() == Type::Float);
    return Float;
  }

  /// Return the signed integer value.
  /// \returns the signed integer value.
  int64_t getInt() const {
    assert(getKind() == Type::Int);
    return Int;
  }

  /// Return the unsigned integer value.
  /// \returns the unsigned integer value.
  uint64_t getUInt() const {
    assert(getKind() == Type::UInt);
    return UInt;
  }

  /// Return the boolean value.
  /// \returns the boolean value.
  bool getBool() const {
    assert(getKind() == Type::Boolean);
    return Bool;
  }

  /// Return the floating-point value.
  /// \returns the floating-point value.
  double getFloat() const {
    assert(getKind() == Type::Float);
    return Float;
  }

  /// Return the string value.
  /// \returns the string value.
  StringRef getString() const {
    assert(getKind() == Type::String);
    return Raw;
  }

  /// Return the binary value as a memory buffer reference.
  /// \returns the binary value as a memory buffer reference.
  MemoryBufferRef getBinary() const {
    assert(getKind() == Type::Binary);
    return MemoryBufferRef(Raw, "");
  }

  /// Get an ArrayDocNode for an array node.
  ///
  /// If Convert, convert the node to an array node if necessary.
  /// \param Convert When true, convert a non-array node to an array.
  /// \returns a reference to this node as an ArrayDocNode.
  ArrayDocNode &getArray(bool Convert = false) {
    if (getKind() != Type::Array) {
      assert(Convert);
      convertToArray();
    }
    // This could be a static_cast, except ArrayDocNode is a forward reference.
    return *reinterpret_cast<ArrayDocNode *>(this);
  }

  /// Get a MapDocNode for a map node.
  ///
  /// If Convert, convert the node to a map node if necessary.
  /// \param Convert When true, convert a non-map node to a map.
  /// \returns a reference to this node as a MapDocNode.
  MapDocNode &getMap(bool Convert = false) {
    if (getKind() != Type::Map) {
      assert(Convert);
      convertToMap();
    }
    // This could be a static_cast, except MapDocNode is a forward reference.
    return *reinterpret_cast<MapDocNode *>(this);
  }

  /// Compare two DocNodes for use as map keys.
  ///
  /// Compares by value, so nodes from different Documents with the same kind
  /// and value are ordered consistently. Only supports scalar types; Array
  /// and Map nodes should not be used as map keys.
  /// \param Lhs Left-hand DocNode to compare.
  /// \param Rhs Right-hand DocNode to compare.
  /// \returns true if \p Lhs is ordered before \p Rhs.
  friend bool operator<(const DocNode &Lhs, const DocNode &Rhs) {
    // Cope with default-constructed nodes where KindAndDoc is not set:
    // isEmpty() returns true both for default-constructed nodes and for
    // nodes returned by getEmptyNode().
    if (Rhs.isEmpty())
      return false;
    if (Lhs.isEmpty())
      return true;
    if (Lhs.getKind() != Rhs.getKind())
      return (unsigned)Lhs.getKind() < (unsigned)Rhs.getKind();
    switch (Lhs.getKind()) {
    case Type::Int:
      return Lhs.Int < Rhs.Int;
    case Type::UInt:
      return Lhs.UInt < Rhs.UInt;
    case Type::Nil:
      return false;
    case Type::Boolean:
      return Lhs.Bool < Rhs.Bool;
    case Type::Float:
      return Lhs.Float < Rhs.Float;
    case Type::String:
    case Type::Binary:
      return Lhs.Raw < Rhs.Raw;
    default:
      assert(false && "bad map key type");
      return false;
    }
  }

  /// Equality operator comparing DocNodes by value.
  ///
  /// Supports all node types including Array and Map, comparing recursively
  /// by value. Works correctly for nodes from different Documents.
  /// \param Lhs Left-hand DocNode to compare.
  /// \param Rhs Right-hand DocNode to compare.
  /// \returns true if \p Lhs and \p Rhs are equal by value.
  LLVM_ABI friend bool operator==(const DocNode &Lhs, const DocNode &Rhs);

  /// Inequality operator comparing DocNodes by value.
  /// \param Lhs Left-hand DocNode to compare.
  /// \param Rhs Right-hand DocNode to compare.
  /// \returns true if \p Lhs and \p Rhs are not equal by value.
  friend bool operator!=(const DocNode &Lhs, const DocNode &Rhs) {
    return !(Lhs == Rhs);
  }

  /// Convert this node to a string, assuming it is scalar.
  /// \returns the string representation of this scalar node.
  LLVM_ABI std::string toString() const;

  /// Set this scalar DocNode from a string representation.
  ///
  /// If it is a string, copy the string into the Document's strings list so
  /// we do not rely on S having a lifetime beyond this call. Tag is "" or a
  /// YAML tag.
  /// \param S String to convert and assign into this node.
  /// \param Tag YAML tag, or empty if none.
  /// \returns the assigned string value as a StringRef.
  LLVM_ABI StringRef fromString(StringRef S, StringRef Tag = "");

  /// Assign a C string value to this DocNode.
  ///
  /// This only works if the destination DocNode has an associated Document,
  /// i.e. it was not constructed using the default constructor. The string
  /// is not copied, so it must remain valid for the lifetime of the Document.
  /// Use fromString to avoid that restriction.
  /// \param Val Null-terminated string to assign (not copied).
  /// \returns a reference to this DocNode.
  DocNode &operator=(const char *Val) { return *this = StringRef(Val); }
  /// Assign a StringRef value to this DocNode.
  ///
  /// Requires an associated Document. The string is not copied, so it must
  /// remain valid for the lifetime of the Document.
  /// \param Val String to assign (not copied).
  /// \returns a reference to this DocNode.
  LLVM_ABI DocNode &operator=(StringRef Val);
  /// Assign a binary MemoryBufferRef value to this DocNode.
  /// \param Val Binary buffer to assign.
  /// \returns a reference to this DocNode.
  LLVM_ABI DocNode &operator=(MemoryBufferRef Val);
  /// Assign a boolean value to this DocNode.
  /// \param Val Boolean value to assign.
  /// \returns a reference to this DocNode.
  LLVM_ABI DocNode &operator=(bool Val);
  /// Assign a signed int value to this DocNode.
  /// \param Val Integer value to assign.
  /// \returns a reference to this DocNode.
  LLVM_ABI DocNode &operator=(int Val);
  /// Assign an unsigned int value to this DocNode.
  /// \param Val Unsigned integer value to assign.
  /// \returns a reference to this DocNode.
  LLVM_ABI DocNode &operator=(unsigned Val);
  /// Assign an int64_t value to this DocNode.
  /// \param Val 64-bit signed integer value to assign.
  /// \returns a reference to this DocNode.
  LLVM_ABI DocNode &operator=(int64_t Val);
  /// Assign a uint64_t value to this DocNode.
  /// \param Val 64-bit unsigned integer value to assign.
  /// \returns a reference to this DocNode.
  LLVM_ABI DocNode &operator=(uint64_t Val);
  /// Assign a double value to this DocNode.
  /// \param Val Floating-point value to assign.
  /// \returns a reference to this DocNode.
  LLVM_ABI DocNode &operator=(double Val);

private:
  // Private constructor setting KindAndDoc, used by methods in Document.
  DocNode(const KindAndDocument *KindAndDoc) : KindAndDoc(KindAndDoc) {}

  LLVM_ABI void convertToArray();
  LLVM_ABI void convertToMap();
};

/// Namespace-scope declaration for the out-of-line friend operator==.
/// \param Lhs Left-hand DocNode to compare.
/// \param Rhs Right-hand DocNode to compare.
/// \returns true if \p Lhs and \p Rhs are equal by value.
LLVM_ABI bool operator==(const DocNode &Lhs, const DocNode &Rhs);

/// A DocNode that is a map.
class MapDocNode : public DocNode {
public:
  /// Construct an empty MapDocNode with no associated Document.
  MapDocNode() = default;
  /// Construct a MapDocNode from an existing map DocNode.
  /// \param N Source DocNode that must already be a map.
  MapDocNode(DocNode &N) : DocNode(N) { assert(getKind() == Type::Map); }

  /// Return the number of entries in the map.
  /// \returns the number of entries in the map.
  size_t size() const { return Map->size(); }
  /// Return true if the map has no entries.
  /// \returns true if the map has no entries.
  bool empty() const { return !size(); }
  /// Return an iterator to the first map entry.
  /// \returns an iterator to the first map entry.
  MapTy::iterator begin() { return Map->begin(); }
  /// Return an iterator past the last map entry.
  /// \returns an iterator past the last map entry.
  MapTy::iterator end() { return Map->end(); }
  /// Find an entry by DocNode key.
  /// \param Key Map key to look up.
  /// \returns an iterator to the entry, or end() if not found.
  MapTy::iterator find(DocNode Key) { return Map->find(Key); }
  /// Find an entry by string key.
  /// \param Key String map key to look up.
  /// \returns an iterator to the entry, or end() if not found.
  LLVM_ABI MapTy::iterator find(StringRef Key);
  /// Erase the map entry at iterator \p I.
  /// \param I Iterator to the entry to erase.
  /// \returns an iterator following the erased entry.
  MapTy::iterator erase(MapTy::const_iterator I) { return Map->erase(I); }
  /// Erase the map entry with key \p Key.
  /// \param Key Map key of the entry to erase.
  /// \returns the number of entries erased.
  size_t erase(DocNode Key) { return Map->erase(Key); }
  /// Erase the map entries in the half-open range [\p First, \p Second).
  /// \param First Iterator to the first entry to erase.
  /// \param Second Iterator past the last entry to erase.
  /// \returns an iterator following the last erased entry.
  MapTy::iterator erase(MapTy::const_iterator First,
                        MapTy::const_iterator Second) {
    return Map->erase(First, Second);
  }
  /// Member access by string key.
  ///
  /// The string data must remain valid for the lifetime of the Document.
  /// \param S String map key; creates an empty entry if missing.
  /// \returns a reference to the value for \p S.
  LLVM_ABI DocNode &operator[](StringRef S);
  /// Member access by DocNode key.
  /// \param Key Map key; creates an empty entry if missing.
  /// \returns a reference to the value for \p Key.
  LLVM_ABI DocNode &operator[](DocNode Key);
  /// Member access by signed int key.
  /// \param Key Integer map key; creates an empty entry if missing.
  /// \returns a reference to the value for \p Key.
  LLVM_ABI DocNode &operator[](int Key);
  /// Member access by unsigned int key.
  /// \param Key Unsigned integer map key; creates an empty entry if missing.
  /// \returns a reference to the value for \p Key.
  LLVM_ABI DocNode &operator[](unsigned Key);
  /// Member access by int64_t key.
  /// \param Key 64-bit signed integer map key; creates an empty entry if
  /// missing.
  /// \returns a reference to the value for \p Key.
  LLVM_ABI DocNode &operator[](int64_t Key);
  /// Member access by uint64_t key.
  /// \param Key 64-bit unsigned integer map key; creates an empty entry if
  /// missing.
  /// \returns a reference to the value for \p Key.
  LLVM_ABI DocNode &operator[](uint64_t Key);
};

/// A DocNode that is an array.
class ArrayDocNode : public DocNode {
public:
  /// Construct an empty ArrayDocNode with no associated Document.
  ArrayDocNode() = default;
  /// Construct an ArrayDocNode from an existing array DocNode.
  /// \param N Source DocNode that must already be an array.
  ArrayDocNode(DocNode &N) : DocNode(N) { assert(getKind() == Type::Array); }

  /// Return the number of elements in the array.
  /// \returns the number of elements in the array.
  size_t size() const { return Array->size(); }
  /// Return true if the array has no elements.
  /// \returns true if the array has no elements.
  bool empty() const { return !size(); }
  /// Return a reference to the last element.
  /// \returns a reference to the last element.
  DocNode &back() const { return Array->back(); }
  /// Return an iterator to the first array element.
  /// \returns an iterator to the first array element.
  ArrayTy::iterator begin() { return Array->begin(); }
  /// Return an iterator past the last array element.
  /// \returns an iterator past the last array element.
  ArrayTy::iterator end() { return Array->end(); }
  /// Append \p N to the end of the array.
  /// \param N Node to append; must be empty or owned by the same Document.
  void push_back(DocNode N) {
    assert(N.isEmpty() || N.getDocument() == getDocument());
    Array->push_back(N);
  }

  /// Element access; extends the array with empty nodes if necessary.
  /// \param Index Zero-based element index to access or create.
  /// \returns a reference to the element at \p Index.
  LLVM_ABI DocNode &operator[](size_t Index);
};

/// In-memory representation of a MsgPack document of findable array and map
/// elements.
///
/// Does not currently cope with any extension types.
class Document {
  // Maps, arrays and strings used by nodes in the document. No attempt is made
  // to free unused ones.
  std::vector<std::unique_ptr<DocNode::MapTy>> Maps;
  std::vector<std::unique_ptr<DocNode::ArrayTy>> Arrays;
  std::vector<std::unique_ptr<char[]>> Strings;

  // The root node of the document.
  DocNode Root;

  // The KindAndDocument structs pointed to by nodes in the document.
  KindAndDocument KindAndDocs[size_t(Type::Empty) + 1];

  // Whether YAML output uses hex for UInt.
  bool HexMode = false;

public:
  /// Construct an empty Document with a nil root and kind tables initialized.
  Document() {
    clear();
    for (unsigned T = 0; T != unsigned(Type::Empty) + 1; ++T)
      KindAndDocs[T] = {this, Type(T)};
  }

  /// Get ref to the document's root element.
  /// \returns a reference to the document's root element.
  DocNode &getRoot() { return Root; }

  /// Restore the Document to an empty state.
  void clear() { getRoot() = getEmptyNode(); }

  /// Create an empty node associated with this Document.
  /// \returns an empty DocNode owned by this Document.
  DocNode getEmptyNode() {
    auto N = DocNode(&KindAndDocs[size_t(Type::Empty)]);
    return N;
  }

  /// Create a nil node associated with this Document.
  /// \returns a nil DocNode owned by this Document.
  DocNode getNode() {
    auto N = DocNode(&KindAndDocs[size_t(Type::Nil)]);
    return N;
  }

  /// Create an Int node associated with this Document.
  /// \param V Signed 64-bit integer value for the node.
  /// \returns an Int DocNode with value \p V.
  DocNode getNode(int64_t V) {
    auto N = DocNode(&KindAndDocs[size_t(Type::Int)]);
    N.Int = V;
    return N;
  }

  /// Create an Int node associated with this Document.
  /// \param V Signed integer value for the node.
  /// \returns an Int DocNode with value \p V.
  DocNode getNode(int V) {
    auto N = DocNode(&KindAndDocs[size_t(Type::Int)]);
    N.Int = V;
    return N;
  }

  /// Create a UInt node associated with this Document.
  /// \param V Unsigned 64-bit integer value for the node.
  /// \returns a UInt DocNode with value \p V.
  DocNode getNode(uint64_t V) {
    auto N = DocNode(&KindAndDocs[size_t(Type::UInt)]);
    N.UInt = V;
    return N;
  }

  /// Create a UInt node associated with this Document.
  /// \param V Unsigned integer value for the node.
  /// \returns a UInt DocNode with value \p V.
  DocNode getNode(unsigned V) {
    auto N = DocNode(&KindAndDocs[size_t(Type::UInt)]);
    N.UInt = V;
    return N;
  }

  /// Create a Boolean node associated with this Document.
  /// \param V Boolean value for the node.
  /// \returns a Boolean DocNode with value \p V.
  DocNode getNode(bool V) {
    auto N = DocNode(&KindAndDocs[size_t(Type::Boolean)]);
    N.Bool = V;
    return N;
  }

  /// Create a Float node associated with this Document.
  /// \param V Floating-point value for the node.
  /// \returns a Float DocNode with value \p V.
  DocNode getNode(double V) {
    auto N = DocNode(&KindAndDocs[size_t(Type::Float)]);
    N.Float = V;
    return N;
  }

  /// Create a String node associated with this Document.
  ///
  /// If !Copy, the passed string must remain valid for the lifetime of the
  /// Document.
  /// \param V String value for the node.
  /// \param Copy When true, copy \p V into the Document's string pool.
  /// \returns a String DocNode with value \p V.
  DocNode getNode(StringRef V, bool Copy = false) {
    if (Copy)
      V = addString(V);
    auto N = DocNode(&KindAndDocs[size_t(Type::String)]);
    N.Raw = V;
    return N;
  }

  /// Create a String node associated with this Document.
  ///
  /// If !Copy, the passed string must remain valid for the lifetime of the
  /// Document.
  /// \param V Null-terminated string value for the node.
  /// \param Copy When true, copy \p V into the Document's string pool.
  /// \returns a String DocNode with value \p V.
  DocNode getNode(const char *V, bool Copy = false) {
    return getNode(StringRef(V), Copy);
  }

  /// Create a Binary node associated with this Document.
  ///
  /// If !Copy, the passed buffer must remain valid for the lifetime of the
  /// Document.
  /// \param V Binary buffer value for the node.
  /// \param Copy When true, copy \p V into the Document's string pool.
  /// \returns a Binary DocNode with value \p V.
  DocNode getNode(MemoryBufferRef V, bool Copy = false) {
    auto Raw = V.getBuffer();
    if (Copy)
      Raw = addString(Raw);
    auto N = DocNode(&KindAndDocs[size_t(Type::Binary)]);
    N.Raw = Raw;
    return N;
  }

  /// Create an empty Map node associated with this Document.
  /// \returns an empty MapDocNode owned by this Document.
  MapDocNode getMapNode() {
    auto N = DocNode(&KindAndDocs[size_t(Type::Map)]);
    Maps.push_back(std::make_unique<DocNode::MapTy>());
    N.Map = Maps.back().get();
    return N.getMap();
  }

  /// Create an empty Array node associated with this Document.
  /// \returns an empty ArrayDocNode owned by this Document.
  ArrayDocNode getArrayNode() {
    auto N = DocNode(&KindAndDocs[size_t(Type::Array)]);
    Arrays.push_back(std::make_unique<DocNode::ArrayTy>());
    N.Array = Arrays.back().get();
    return N.getArray();
  }

  /// Deep-copy a DocNode from any Document into this Document.
  ///
  /// The returned node is owned by this Document and is independent of the
  /// source node's Document. Strings are copied so the source Document's
  /// lifetime does not need to extend beyond this call.
  /// \param Src Source node to deep-copy into this Document.
  /// \returns a deep copy of \p Src owned by this Document.
  LLVM_ABI DocNode copyNode(DocNode Src);

  /// Read a binary msgpack blob into this Document, merging with existing
  /// content.
  ///
  /// The blob data must remain valid for the lifetime of this Document
  /// (because a string object in the document contains a StringRef into the
  /// original blob). If Multi, then this sets root to an array and adds
  /// top-level objects to it. If !Multi, then it only reads a single
  /// top-level object, even if there are more, and sets root to that. Returns
  /// false if failed due to illegal format or merge error.
  ///
  /// The Merger arg is a callback function that is called when the merge has
  /// a conflict, that is, it is trying to set an item that is already set. If
  /// the conflict cannot be resolved, the callback function returns -1. If
  /// the conflict can be resolved, the callback returns a non-negative number
  /// and sets *DestNode to the resolved node. The returned non-negative
  /// number is significant only for an array node; it is then the array index
  /// to start populating at. That allows Merger to choose whether to merge
  /// array elements (returns 0) or append new elements (returns existing
  /// size).
  ///
  /// If SrcNode is an array or map, the resolution must be that *DestNode is
  /// an array or map respectively, although it could be the array or map
  /// (respectively) that was already there. MapKey is the key if *DestNode is
  /// a map entry, a nil node otherwise.
  ///
  /// The default for Merger is to disallow any conflict.
  /// \param Blob Binary msgpack input; must outlive this Document.
  /// \param Multi When true, read all top-level objects into an array root.
  /// \param Merger Conflict-resolution callback; default rejects conflicts.
  /// \returns true on success, false on illegal format or merge error.
  LLVM_ABI bool readFromBlob(
      StringRef Blob, bool Multi,
      function_ref<int(DocNode *DestNode, DocNode SrcNode, DocNode MapKey)>
          Merger = [](DocNode *DestNode, DocNode SrcNode, DocNode MapKey) {
            return -1;
          });

  /// Write a MsgPack document to a binary MsgPack blob.
  /// \param Blob Output string filled with the encoded MsgPack bytes.
  LLVM_ABI void writeToBlob(std::string &Blob);

  /// Copy a string into the Document's strings list, and return the copy that
  /// is owned by the Document.
  /// \param S String to copy into the Document's pool.
  /// \returns a StringRef to the copy owned by this Document.
  StringRef addString(StringRef S) {
    Strings.push_back(std::unique_ptr<char[]>(new char[S.size()]));
    memcpy(&Strings.back()[0], S.data(), S.size());
    return StringRef(&Strings.back()[0], S.size());
  }

  /// Set whether YAML output uses hex for UInt. Default off.
  /// \param Val When true, write UInt values in hexadecimal YAML.
  void setHexMode(bool Val = true) { HexMode = Val; }

  /// Get Hexmode flag.
  /// \returns true if YAML output uses hex for UInt.
  bool getHexMode() const { return HexMode; }

  /// Convert MsgPack Document to YAML text.
  /// \param OS Output stream to write YAML into.
  LLVM_ABI void toYAML(raw_ostream &OS);

  /// Read YAML text into the MsgPack document. Returns false on failure.
  /// \param S YAML text to parse into this Document.
  /// \returns true on success, false on failure.
  LLVM_ABI bool fromYAML(StringRef S);
};

} // namespace msgpack
} // namespace llvm

#endif // LLVM_BINARYFORMAT_MSGPACKDOCUMENT_H
