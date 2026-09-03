//===- YAMLParser.h - Simple YAML parser ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This is a YAML 1.2 parser.
//
//  See http://www.yaml.org/spec/1.2/spec.html for the full standard.
//
//  This currently does not implement the following:
//    * Tag resolution.
//    * UTF-16.
//    * BOMs anywhere other than the first Unicode scalar value in the file.
//
//  The most important class here is Stream. This represents a YAML stream with
//  0, 1, or many documents.
//
//  SourceMgr sm;
//  StringRef input = getInput();
//  yaml::Stream stream(input, sm);
//
//  for (yaml::document_iterator di = stream.begin(), de = stream.end();
//       di != de; ++di) {
//    yaml::Node *n = di->getRoot();
//    if (n) {
//      // Do something with n...
//    } else {
//      break;
//    }
//  }
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_YAMLPARSER_H
#define LLVM_SUPPORT_YAMLPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace llvm {

class MemoryBufferRef;
class raw_ostream;
class Twine;

namespace yaml {

class Document;
class document_iterator;
class Node;
/// Tokenizer that produces a stream of YAML tokens from an input buffer.
class Scanner;
/// A single lexical token produced by the YAML scanner.
struct Token;

/// Dump all the tokens in this stream to OS.
/// \param Input YAML text to tokenize.
/// \param OS Stream that receives the token dump.
/// \returns true if there was an error, false otherwise.
LLVM_ABI bool dumpTokens(StringRef Input, raw_ostream &OS);

/// Scans all tokens in input without outputting anything. This is used
///        for benchmarking the tokenizer.
/// \param Input YAML text to tokenize.
/// \returns true if there was an error, false otherwise.
LLVM_ABI bool scanTokens(StringRef Input);

/// Escape \p Input for use inside a double-quoted YAML scalar.
///
/// If \p EscapePrintable is true, all UTF8 sequences will be escaped; if
/// \p EscapePrintable is false, those UTF8 sequences encoding printable
/// unicode scalars will not be escaped, but emitted verbatim.
/// \param Input Text to escape.
/// \param EscapePrintable Whether to escape printable Unicode as well.
/// \returns The escaped string suitable for a double-quoted scalar.
LLVM_ABI std::string escape(StringRef Input, bool EscapePrintable = true);

/// Parse \p S as a bool according to https://yaml.org/type/bool.html.
/// \param S Scalar text to interpret as a YAML bool.
/// \returns The parsed bool, or std::nullopt if \p S is not a YAML bool.
LLVM_ABI std::optional<bool> parseBool(StringRef S);

/// This class represents a YAML stream potentially containing multiple
///        documents.
class Stream {
public:
  /// This keeps a reference to the string referenced by \p Input.
  /// \param Input YAML text to parse; kept by reference.
  /// \param SM Source manager used for diagnostics.
  /// \param ShowColors Whether diagnostics may use color.
  /// \param EC Optional out-parameter set if stream setup fails.
  LLVM_ABI Stream(StringRef Input, SourceMgr &SM, bool ShowColors = true,
                  std::error_code *EC = nullptr);

  /// Construct a stream over the contents of \p InputBuffer.
  /// \param InputBuffer Memory buffer whose contents are parsed.
  /// \param SM Source manager used for diagnostics.
  /// \param ShowColors Whether diagnostics may use color.
  /// \param EC Optional out-parameter set if stream setup fails.
  LLVM_ABI Stream(MemoryBufferRef InputBuffer, SourceMgr &SM,
                  bool ShowColors = true, std::error_code *EC = nullptr);
  /// Destroy the stream and release owned scanner state.
  LLVM_ABI ~Stream();

  /// Return an iterator to the first document in the stream.
  /// \returns Begin iterator for the first document.
  LLVM_ABI document_iterator begin();
  /// Return an end iterator past the last document.
  /// \returns An end document iterator.
  LLVM_ABI document_iterator end();
  /// Skip remaining unread documents in the stream.
  LLVM_ABI void skip();
  /// Return true if a parse error has been reported.
  /// \returns True if a parse error has been reported.
  LLVM_ABI bool failed();

  /// Validate the stream by skipping all documents; return true on success.
  /// \returns True if no parse error was reported.
  bool validate() {
    skip();
    return !failed();
  }

  /// Print a diagnostic for node \p N with message \p Msg.
  /// \param N Node whose source range anchors the diagnostic.
  /// \param Msg Diagnostic message text.
  /// \param Kind Severity of the diagnostic.
  LLVM_ABI void printError(Node *N, const Twine &Msg,
                           SourceMgr::DiagKind Kind = SourceMgr::DK_Error);
  /// Print a diagnostic for source range \p Range with message \p Msg.
  /// \param Range Source range that anchors the diagnostic.
  /// \param Msg Diagnostic message text.
  /// \param Kind Severity of the diagnostic.
  LLVM_ABI void printError(const SMRange &Range, const Twine &Msg,
                           SourceMgr::DiagKind Kind = SourceMgr::DK_Error);

private:
  friend class Document;

  std::unique_ptr<Scanner> scanner;
  std::unique_ptr<Document> CurrentDoc;
};

/// Abstract base class for all Nodes.
class LLVM_ABI Node {
  virtual void anchor();

public:
  /// Discriminator for LLVM-style RTTI of YAML AST nodes.
  enum NodeKind {
    NK_Null,       ///< YAML null value.
    NK_Scalar,     ///< Flow or plain scalar.
    NK_BlockScalar,///< Block scalar (| or >).
    NK_KeyValue,   ///< Single key/value pair in a mapping.
    NK_Mapping,    ///< Mapping (block, flow, or inline).
    NK_Sequence,   ///< Sequence (block, flow, or indentless).
    NK_Alias       ///< Alias to an anchored node.
  };

  /// Construct a node of kind \p Type owned by document \p D.
  /// \param Type NodeKind discriminator stored as TypeID.
  /// \param D Document that owns this node.
  /// \param Anchor Anchor name, or empty if none.
  /// \param Tag Raw tag as written, or empty if none.
  Node(unsigned int Type, std::unique_ptr<Document> &D, StringRef Anchor,
       StringRef Tag);

  // It's not safe to copy YAML nodes; the document is streamed and the position
  // is part of the state.
  /// Deleted copy constructor; YAML nodes are not copyable.
  /// \param Other Unused; this overload is deleted.
  Node(const Node &Other) = delete;
  /// Deleted copy assignment; YAML nodes are not copyable.
  /// \param Other Unused; this overload is deleted.
  void operator=(const Node &Other) = delete;

  /// Allocate a node from bump allocator \p Alloc.
  /// \param Size Number of bytes to allocate.
  /// \param Alloc Bump allocator that owns the storage.
  /// \param Alignment Allocation alignment in bytes.
  /// \returns Pointer to the allocated storage.
  void *operator new(size_t Size, BumpPtrAllocator &Alloc,
                     size_t Alignment = 16) noexcept {
    return Alloc.Allocate(Size, Alignment);
  }

  /// Deallocate a node previously allocated from \p Alloc.
  /// \param Ptr Pointer returned by the matching operator new.
  /// \param Alloc Bump allocator that owns the storage.
  /// \param Size Size previously passed to Allocate.
  void operator delete(void *Ptr, BumpPtrAllocator &Alloc,
                       size_t Size) noexcept {
    Alloc.Deallocate(Ptr, Size, 0);
  }

  /// Deleted sized deallocation; nodes are freed with the document allocator.
  /// \param Ptr Unused; this overload is deleted.
  void operator delete(void *Ptr) noexcept = delete;

  /// Get the value of the anchor attached to this node. If it does not
  ///        have one, getAnchor().size() will be 0.
  /// \returns The anchor name, or empty if none.
  StringRef getAnchor() const { return Anchor; }

  /// Get the tag as it was written in the document. This does not
  ///   perform tag resolution.
  /// \returns The raw tag text, or empty if none.
  StringRef getRawTag() const { return Tag; }

  /// Get the verbatium tag for a given Node. This performs tag resoluton
  ///   and substitution.
  /// \returns The resolved verbatim tag string.
  std::string getVerbatimTag() const;

  /// Return the source range covering this node.
  /// \returns The source range of this node.
  SMRange getSourceRange() const { return SourceRange; }
  /// Set the source range covering this node.
  /// \param SR New source range for this node.
  void setSourceRange(SMRange SR) { SourceRange = SR; }

  // These functions forward to Document and Scanner.
  /// Peek at the next token without consuming it.
  /// \returns Reference to the next token.
  Token &peekNext();
  /// Consume and return the next token.
  /// \returns The next token, which is consumed.
  Token getNext();
  /// Parse the next block-level node from the stream.
  /// \returns The parsed node, or nullptr on failure.
  Node *parseBlockNode();
  /// Return the bump allocator used for nodes in this document.
  /// \returns The document's bump allocator.
  BumpPtrAllocator &getAllocator();
  /// Report a parse error at \p Location with \p Message.
  /// \param Message Diagnostic text.
  /// \param Location Token that anchors the diagnostic.
  void setError(const Twine &Message, Token &Location) const;
  /// Return true if a parse error has been reported for this node.
  /// \returns True if a parse error has been reported.
  bool failed() const;

  /// Skip any remaining unparsed content under this node.
  virtual void skip() {}

  /// Return the NodeKind discriminator for this node.
  /// \returns The NodeKind stored as TypeID.
  unsigned int getType() const { return TypeID; }

protected:
  /// Document that owns this node and provides token access.
  std::unique_ptr<Document> &Doc;
  /// Source range of this node's text in the input.
  SMRange SourceRange;

  /// Destroy the node; subclasses are not deleted polymorphically.
  ~Node() = default;

private:
  unsigned int TypeID;
  StringRef Anchor;
  /// The tag as typed in the document.
  StringRef Tag;
};

/// A null value.
///
/// Example:
///   !!null null
class LLVM_ABI NullNode final : public Node {
  void anchor() override;

public:
  /// Construct a null node owned by document \p D.
  /// \param D Document that owns this node.
  NullNode(std::unique_ptr<Document> &D)
      : Node(NK_Null, D, StringRef(), StringRef()) {}

  /// Return true if \p N is a NullNode.
  /// \param N Node to test.
  /// \returns True if \p N is a NullNode.
  static bool classof(const Node *N) { return N->getType() == NK_Null; }
};

/// A scalar node is an opaque datum that can be presented as a
///        series of zero or more Unicode scalar values.
///
/// Example:
///   Adena
class LLVM_ABI ScalarNode final : public Node {
  void anchor() override;

public:
  /// Construct a scalar with raw value \p Val.
  /// \param D Document that owns this node.
  /// \param Anchor Anchor name, or empty if none.
  /// \param Tag Raw tag as written, or empty if none.
  /// \param Val Scalar text as it appears in the input.
  ScalarNode(std::unique_ptr<Document> &D, StringRef Anchor, StringRef Tag,
             StringRef Val)
      : Node(NK_Scalar, D, Anchor, Tag), Value(Val) {
    SMLoc Start = SMLoc::getFromPointer(Val.begin());
    SMLoc End = SMLoc::getFromPointer(Val.end());
    SourceRange = SMRange(Start, End);
  }

  /// Return the scalar bytes exactly as they appear in the file (after UTF-8).
  ///
  /// This omits escaping, folding, or other YAML scalar processing.
  /// \returns The raw scalar text from the input.
  StringRef getRawValue() const { return Value; }

  /// Gets the value of this node as a StringRef.
  ///
  /// \param Storage is used to store the content of the returned StringRef if
  ///        it requires any modification from how it appeared in the source.
  ///        This happens with escaped characters and multi-line literals.
  /// \returns The unescaped scalar value, possibly backed by \p Storage.
  StringRef getValue(SmallVectorImpl<char> &Storage) const;

  /// Return true if \p N is a ScalarNode.
  /// \param N Node to test.
  /// \returns True if \p N is a ScalarNode.
  static bool classof(const Node *N) {
    return N->getType() == NK_Scalar;
  }

private:
  StringRef Value;

  StringRef getDoubleQuotedValue(StringRef UnquotedValue,
                                 SmallVectorImpl<char> &Storage) const;

  static StringRef getSingleQuotedValue(StringRef RawValue,
                                        SmallVectorImpl<char> &Storage);

  static StringRef getPlainValue(StringRef RawValue,
                                 SmallVectorImpl<char> &Storage);
};

/// A block scalar node is an opaque datum that can be presented as a
///        series of zero or more Unicode scalar values.
///
/// Example:
///   |
///     Hello
///     World
class LLVM_ABI BlockScalarNode final : public Node {
  void anchor() override;

public:
  /// Construct a block scalar with decoded value \p Value.
  /// \param D Document that owns this node.
  /// \param Anchor Anchor name, or empty if none.
  /// \param Tag Raw tag as written, or empty if none.
  /// \param Value Decoded block scalar content.
  /// \param RawVal Raw block scalar text used for the source range.
  BlockScalarNode(std::unique_ptr<Document> &D, StringRef Anchor, StringRef Tag,
                  StringRef Value, StringRef RawVal)
      : Node(NK_BlockScalar, D, Anchor, Tag), Value(Value) {
    SMLoc Start = SMLoc::getFromPointer(RawVal.begin());
    SMLoc End = SMLoc::getFromPointer(RawVal.end());
    SourceRange = SMRange(Start, End);
  }

  /// Gets the value of this node as a StringRef.
  /// \returns The decoded block scalar content.
  StringRef getValue() const { return Value; }

  /// Return true if \p N is a BlockScalarNode.
  /// \param N Node to test.
  /// \returns True if \p N is a BlockScalarNode.
  static bool classof(const Node *N) {
    return N->getType() == NK_BlockScalar;
  }

private:
  StringRef Value;
};

/// A key and value pair. While not technically a Node under the YAML
///        representation graph, it is easier to treat them this way.
///
/// TODO: Consider making this not a child of Node.
///
/// Example:
///   Section: .text
class LLVM_ABI KeyValueNode final : public Node {
  void anchor() override;

public:
  /// Construct a key/value pair node owned by document \p D.
  /// \param D Document that owns this node.
  KeyValueNode(std::unique_ptr<Document> &D)
      : Node(NK_KeyValue, D, StringRef(), StringRef()) {}

  /// Parse and return the key.
  ///
  /// This may be called multiple times.
  ///
  /// \returns The key, or nullptr if failed() == true.
  Node *getKey();

  /// Parse and return the value.
  ///
  /// This may be called multiple times.
  ///
  /// \returns The value, or nullptr if failed() == true.
  Node *getValue();

  /// Skip the key and value under this pairing.
  void skip() override {
    if (Node *Key = getKey()) {
      Key->skip();
      if (Node *Val = getValue())
        Val->skip();
    }
  }

  /// Return true if \p N is a KeyValueNode.
  /// \param N Node to test.
  /// \returns True if \p N is a KeyValueNode.
  static bool classof(const Node *N) {
    return N->getType() == NK_KeyValue;
  }

private:
  Node *Key = nullptr;
  Node *Value = nullptr;
};

/// This is an iterator abstraction over YAML collections shared by both
///        sequences and maps.
///
/// BaseT must have a ValueT* member named CurrentEntry and a member function
/// increment() which must set CurrentEntry to 0 to create an end iterator.
template <class BaseT, class ValueT> class basic_collection_iterator {
public:
  /// Iterator category tag for input iterators.
  using iterator_category = std::input_iterator_tag;
  /// Type of the element pointed to by this iterator.
  using value_type = ValueT;
  /// Type used for distances between iterators.
  using difference_type = std::ptrdiff_t;
  /// Pointer to the element type.
  using pointer = value_type *;
  /// Reference to the element type.
  using reference = value_type &;

  /// Construct an end (singular) iterator.
  basic_collection_iterator() = default;
  /// Construct an iterator over collection \p B.
  /// \param B Collection being iterated; must outlive the iterator.
  basic_collection_iterator(BaseT *B) : Base(B) {}

  /// Return a pointer to the current entry.
  /// \returns Pointer to the current entry.
  ValueT *operator->() const {
    assert(Base && Base->CurrentEntry && "Attempted to access end iterator!");
    return Base->CurrentEntry;
  }

  /// Return a reference to the current entry.
  /// \returns Reference to the current entry.
  ValueT &operator*() const {
    assert(Base && Base->CurrentEntry &&
           "Attempted to dereference end iterator!");
    return *Base->CurrentEntry;
  }

  /// Convert to a pointer to the current entry.
  /// \returns Pointer to the current entry.
  operator ValueT *() const {
    assert(Base && Base->CurrentEntry && "Attempted to access end iterator!");
    return Base->CurrentEntry;
  }

  /// Return true if this iterator refers to the same collection as \p Other.
  ///
  /// Note on EqualityComparable:
  ///
  /// The iterator is not re-entrant,
  /// it is meant to be used for parsing YAML on-demand
  /// Once iteration started - it can point only to one entry at a time
  /// hence Base.CurrentEntry and Other.Base.CurrentEntry are equal
  /// iff Base and Other.Base are equal.
  /// \param Other Iterator to compare against.
  /// \returns True if both iterators refer to the same collection.
  bool operator==(const basic_collection_iterator &Other) const {
    if (Base && (Base == Other.Base)) {
      assert((Base->CurrentEntry == Other.Base->CurrentEntry)
             && "equal Bases expected to point to equal Entries");
    }

    return Base == Other.Base;
  }

  /// Return true if this iterator does not refer to the same collection as \p Other.
  /// \param Other Iterator to compare against.
  /// \returns True if the iterators refer to different collections.
  bool operator!=(const basic_collection_iterator &Other) const {
    return !(Base == Other.Base);
  }

  /// Advance to the next entry in the collection.
  /// \returns Reference to this iterator after advancing.
  basic_collection_iterator &operator++() {
    assert(Base && "Attempted to advance iterator past end!");
    Base->increment();
    // Create an end iterator.
    if (!Base->CurrentEntry)
      Base = nullptr;
    return *this;
  }

private:
  BaseT *Base = nullptr;
};

// The following two templates are used for both MappingNode and Sequence Node.
/// Return a begin iterator for collection \p C; may be used only once.
/// \param C Mapping or sequence node to iterate.
/// \returns Begin iterator positioned at the first entry of \p C.
template <class CollectionType>
typename CollectionType::iterator begin(CollectionType &C) {
  assert(C.IsAtBeginning && "You may only iterate over a collection once!");
  C.IsAtBeginning = false;
  typename CollectionType::iterator ret(&C);
  ++ret;
  return ret;
}

/// Skip all remaining entries in collection \p C.
/// \param C Mapping or sequence node to skip; must be at begin or end.
template <class CollectionType> void skip(CollectionType &C) {
  // TODO: support skipping from the middle of a parsed collection ;/
  assert((C.IsAtBeginning || C.IsAtEnd) && "Cannot skip mid parse!");
  if (C.IsAtBeginning)
    for (typename CollectionType::iterator i = begin(C), e = C.end(); i != e;
         ++i)
      i->skip();
}

/// Represents a YAML map created from either a block map for a flow map.
///
/// This parses the YAML stream as increment() is called.
///
/// Example:
///   Name: _main
///   Scope: Global
class LLVM_ABI MappingNode final : public Node {
  void anchor() override;

public:
  /// How a mapping was written in the input.
  enum MappingType {
    MT_Block,  ///< Block-style mapping (`key: value` under indentation).
    MT_Flow,   ///< Flow-style mapping (`{key: value}`).
    MT_Inline  ///< An inline mapping node is used for "[key: value]".
  };

  /// Construct a mapping node of style \p MT.
  /// \param D Document that owns this node.
  /// \param Anchor Anchor name, or empty if none.
  /// \param Tag Raw tag as written, or empty if none.
  /// \param MT Whether this is a block, flow, or inline mapping.
  MappingNode(std::unique_ptr<Document> &D, StringRef Anchor, StringRef Tag,
              MappingType MT)
      : Node(NK_Mapping, D, Anchor, Tag), Type(MT) {}

  friend class basic_collection_iterator<MappingNode, KeyValueNode>;

  /// Input iterator over key/value entries in this mapping.
  using iterator = basic_collection_iterator<MappingNode, KeyValueNode>;

  template <class T> friend typename T::iterator yaml::begin(T &);
  template <class T> friend void yaml::skip(T &);

  /// Return an iterator to the first key/value entry.
  /// \returns Begin iterator for this mapping.
  iterator begin() { return yaml::begin(*this); }

  /// Return an end iterator past the last key/value entry.
  /// \returns An end iterator for this mapping.
  iterator end() { return iterator(); }

  /// Skip any remaining unparsed key/value entries.
  void skip() override { yaml::skip(*this); }

  /// Return true if \p N is a MappingNode.
  /// \param N Node to test.
  /// \returns True if \p N is a MappingNode.
  static bool classof(const Node *N) {
    return N->getType() == NK_Mapping;
  }

private:
  MappingType Type;
  bool IsAtBeginning = true;
  bool IsAtEnd = false;
  KeyValueNode *CurrentEntry = nullptr;

  void increment();
};

/// Represents a YAML sequence created from either a block sequence for a
///        flow sequence.
///
/// This parses the YAML stream as increment() is called.
///
/// Example:
///   - Hello
///   - World
class LLVM_ABI SequenceNode final : public Node {
  void anchor() override;

public:
  /// How a sequence was written in the input.
  enum SequenceType {
    ST_Block, ///< Block-style sequence (`- item` under indentation).
    ST_Flow,  ///< Flow-style sequence (`[item, item]`).
    /// Indentless sequence used when entries follow a mapping key without a
    /// nested block sequence start, so BlockMappingEntry/BlockEnd are omitted:
    ///
    /// key:
    /// - val1
    /// - val2
    ST_Indentless
  };

  /// Construct a sequence node of style \p ST.
  /// \param D Document that owns this node.
  /// \param Anchor Anchor name, or empty if none.
  /// \param Tag Raw tag as written, or empty if none.
  /// \param ST Whether this is a block, flow, or indentless sequence.
  SequenceNode(std::unique_ptr<Document> &D, StringRef Anchor, StringRef Tag,
               SequenceType ST)
      : Node(NK_Sequence, D, Anchor, Tag), SeqType(ST) {}

  friend class basic_collection_iterator<SequenceNode, Node>;

  /// Input iterator over element nodes in this sequence.
  using iterator = basic_collection_iterator<SequenceNode, Node>;

  template <class T> friend typename T::iterator yaml::begin(T &);
  template <class T> friend void yaml::skip(T &);

  /// Advance the current entry to the next sequence element.
  void increment();

  /// Return an iterator to the first sequence element.
  /// \returns Begin iterator for this sequence.
  iterator begin() { return yaml::begin(*this); }

  /// Return an end iterator past the last sequence element.
  /// \returns An end iterator for this sequence.
  iterator end() { return iterator(); }

  /// Skip any remaining unparsed sequence elements.
  void skip() override { yaml::skip(*this); }

  /// Return true if \p N is a SequenceNode.
  /// \param N Node to test.
  /// \returns True if \p N is a SequenceNode.
  static bool classof(const Node *N) {
    return N->getType() == NK_Sequence;
  }

private:
  SequenceType SeqType;
  bool IsAtBeginning = true;
  bool IsAtEnd = false;
  bool WasPreviousTokenFlowEntry = true; // Start with an imaginary ','.
  Node *CurrentEntry = nullptr;
};

/// Represents an alias to a Node with an anchor.
///
/// Example:
///   *AnchorName
class LLVM_ABI AliasNode final : public Node {
  void anchor() override;

public:
  /// Construct an alias referring to anchor name \p Val.
  /// \param D Document that owns this node.
  /// \param Val Anchor name this alias references.
  AliasNode(std::unique_ptr<Document> &D, StringRef Val)
      : Node(NK_Alias, D, StringRef(), StringRef()), Name(Val) {}

  /// Return the anchor name referenced by this alias.
  /// \returns The referenced anchor name.
  StringRef getName() const { return Name; }

  /// Return true if \p N is an AliasNode.
  /// \param N Node to test.
  /// \returns True if \p N is an AliasNode.
  static bool classof(const Node *N) { return N->getType() == NK_Alias; }

private:
  StringRef Name;
};

/// A YAML Stream is a sequence of Documents. A document contains a root
///        node.
class Document {
public:
  /// Construct a document reading tokens from \p ParentStream.
  /// \param ParentStream Stream that owns the scanner for this document.
  LLVM_ABI Document(Stream &ParentStream);

  /// Root for parsing a node. Returns a single node.
  /// \returns The parsed node, or nullptr on failure.
  LLVM_ABI Node *parseBlockNode();

  /// Finish parsing the current document and return true if there are
  ///        more. Return false otherwise.
  /// \returns True if another document follows; false otherwise.
  LLVM_ABI bool skip();

  /// Parse and return the root level node.
  /// \returns The document root node, parsing it if needed.
  Node *getRoot() {
    if (Root)
      return Root;
    return Root = parseBlockNode();
  }

  /// Return the map from tag handles to their expanded prefixes.
  /// \returns Map from tag handles to expanded prefixes.
  const std::map<StringRef, StringRef> &getTagMap() const { return TagMap; }

private:
  friend class Node;
  friend class document_iterator;

  /// Stream to read tokens from.
  Stream &stream;

  /// Used to allocate nodes to. All are destroyed without calling their
  ///        destructor when the document is destroyed.
  BumpPtrAllocator NodeAllocator;

  /// The root node. Used to support skipping a partially parsed
  ///        document.
  Node *Root;

  /// Maps tag prefixes to their expansion.
  std::map<StringRef, StringRef> TagMap;

  Token &peekNext();
  Token getNext();
  void setError(const Twine &Message, Token &Location) const;
  bool failed() const;

  /// Parse %BLAH directives and return true if any were encountered.
  bool parseDirectives();

  /// Parse %YAML
  void parseYAMLDirective();

  /// Parse %TAG
  void parseTAGDirective();

  /// Consume the next token and error if it is not \a TK.
  bool expectToken(int TK);
};

/// Iterator abstraction for Documents over a Stream.
class document_iterator {
public:
  /// Construct an end (singular) document iterator.
  document_iterator() = default;
  /// Construct an iterator referring to document \p D.
  /// \param D Unique pointer to the current document; may be reset to null.
  document_iterator(std::unique_ptr<Document> &D) : Doc(&D) {}

  /// Return true if this iterator refers to the same document as \p Other.
  /// \param Other Iterator to compare against.
  /// \returns True if both iterators refer to the same document.
  bool operator==(const document_iterator &Other) const {
    if (isAtEnd() || Other.isAtEnd())
      return isAtEnd() && Other.isAtEnd();

    return Doc == Other.Doc;
  }
  /// Return true if this iterator does not refer to the same document as \p Other.
  /// \param Other Iterator to compare against.
  /// \returns True if the iterators refer to different documents.
  bool operator!=(const document_iterator &Other) const {
    return !(*this == Other);
  }

  /// Advance to the next document in the stream.
  /// \returns Reference to this iterator after advancing.
  document_iterator operator++() {
    assert(Doc && "incrementing iterator past the end.");
    if (!(*Doc)->skip()) {
      Doc->reset(nullptr);
    } else {
      Stream &S = (*Doc)->stream;
      Doc->reset(new Document(S));
    }
    return *this;
  }

  /// Return a reference to the current document.
  /// \returns Reference to the current document.
  Document &operator*() { return **Doc; }

  /// Return a reference to the unique_ptr owning the current document.
  /// \returns Reference to the unique_ptr for the current document.
  std::unique_ptr<Document> &operator->() { return *Doc; }

private:
  bool isAtEnd() const { return !Doc || !*Doc; }

  std::unique_ptr<Document> *Doc = nullptr;
};

} // end namespace yaml

} // end namespace llvm

#endif // LLVM_SUPPORT_YAMLPARSER_H
