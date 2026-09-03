//===--- ItaniumDemangle.h -----------*- mode:c++;eval:(read-only-mode) -*-===//
//       Do not edit! See README.txt.
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic itanium demangler library.
// There are two copies of this file in the source tree.  The one under
// libcxxabi is the original and the one under llvm is the copy.  Use
// cp-to-llvm.sh to update the copy.  See README.txt for more details.
//
//===----------------------------------------------------------------------===//

#ifndef DEMANGLE_ITANIUMDEMANGLE_H
#define DEMANGLE_ITANIUMDEMANGLE_H

#include "DemangleConfig.h"
#include "StringViewExtras.h"
#include "Utility.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-template"
#endif

namespace llvm {
/// Itanium C++ ABI demangler AST nodes, utilities, and parser.
namespace itanium_demangle {

/// Small vector of POD elements with inline storage of capacity N.
template <class T, size_t N> class PODSmallVector {
  static_assert(std::is_trivially_copyable<T>::value,
                "T is required to be a trivially copyable type");
  static_assert(std::is_trivially_default_constructible<T>::value,
                "T is required to be trivially default constructible");
  static_assert(N > 0, "PODSmallVector requires a non-zero inline capacity");
  T *First = nullptr;
  T *Last = nullptr;
  T *Cap = nullptr;
  T Inline[N] = {};

  bool isInline() const { return First == Inline; }

  void clearInline() {
    First = Inline;
    Last = Inline;
    Cap = Inline + N;
  }

  void reserve(size_t NewCap) {
    size_t S = size();
    if (isInline()) {
      auto *Tmp = static_cast<T *>(std::malloc(NewCap * sizeof(T)));
      if (Tmp == nullptr)
        std::abort();
      std::copy(First, Last, Tmp);
      First = Tmp;
    } else {
      First = static_cast<T *>(std::realloc(First, NewCap * sizeof(T)));
      if (First == nullptr)
        std::abort();
    }
    Last = First + S;
    Cap = First + NewCap;
  }

public:
  /// Construct an empty vector using inline storage.
  PODSmallVector() : First(Inline), Last(First), Cap(Inline + N) {}

  /// Copy construction is deleted.
  /// \param Other Unused; copy construction is deleted.
  PODSmallVector(const PODSmallVector &Other) = delete;
  /// Copy assignment is deleted.
  /// \param Other Unused; copy assignment is deleted.
  PODSmallVector &operator=(const PODSmallVector &Other) = delete;

  /// Move-construct from \p Other, leaving it empty.
  /// \param Other Source vector whose elements are taken or copied.
  PODSmallVector(PODSmallVector &&Other) : PODSmallVector() {
    if (Other.isInline()) {
      std::copy(Other.begin(), Other.end(), First);
      Last = First + Other.size();
      Other.clear();
      return;
    }

    First = Other.First;
    Last = Other.Last;
    Cap = Other.Cap;
    Other.clearInline();
  }

  /// Move-assign from \p Other, leaving it empty.
  /// \param Other Source vector whose elements are taken or copied.
  /// \return A reference to this vector.
  PODSmallVector &operator=(PODSmallVector &&Other) {
    if (Other.isInline()) {
      if (!isInline()) {
        std::free(First);
        clearInline();
      }
      std::copy(Other.begin(), Other.end(), First);
      Last = First + Other.size();
      Other.clear();
      return *this;
    }

    if (isInline()) {
      First = Other.First;
      Last = Other.Last;
      Cap = Other.Cap;
      Other.clearInline();
      return *this;
    }

    std::swap(First, Other.First);
    std::swap(Last, Other.Last);
    std::swap(Cap, Other.Cap);
    Other.clear();
    return *this;
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  /// Append \p Elem, growing storage if needed.
  /// \param Elem Element to append.
  void push_back(const T &Elem) {
    if (Last == Cap)
      reserve(size() * 2);
    *Last++ = Elem;
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  /// Remove the last element.
  void pop_back() {
    DEMANGLE_ASSERT(Last != First, "Popping empty vector!");
    --Last;
  }

  /// Shrink the vector to the first \p Index elements.
  /// \param Index New size; must be <= size().
  void shrinkToSize(size_t Index) {
    DEMANGLE_ASSERT(Index <= size(), "shrinkToSize() can't expand!");
    Last = First + Index;
  }

  /// Return a pointer to the first element.
  /// \return A pointer to the first element.
  T *begin() { return First; }
  /// Return a pointer one past the last element.
  /// \return A pointer one past the last element.
  T *end() { return Last; }

  /// Return true if the vector has no elements.
  /// \return True if there are no elements.
  bool empty() const { return First == Last; }
  /// Return the number of elements.
  /// \return The number of elements.
  size_t size() const { return static_cast<size_t>(Last - First); }
  /// Return a reference to the last element.
  /// \return A reference to the last element.
  T &back() {
    DEMANGLE_ASSERT(Last != First, "Calling back() on empty vector!");
    return *(Last - 1);
  }
  /// Return a reference to the element at \p Index.
  /// \param Index Zero-based element index.
  /// \return A reference to the element at the given index.
  T &operator[](size_t Index) {
    DEMANGLE_ASSERT(Index < size(), "Invalid access!");
    return *(begin() + Index);
  }
  /// Remove all elements without freeing capacity.
  void clear() { Last = First; }

  /// Destroy the vector and free any heap storage.
  ~PODSmallVector() {
    if (!isInline())
      std::free(First);
  }
};

class NodeArray;

/// Base class of all Itanium demangler AST nodes.
///
/// The AST is built by the parser, then traversed by the printLeft/Right
/// functions to produce a demangled string.
class Node {
public:
  /// Discriminator for the concrete derived node type.
  enum Kind : uint8_t {
#define NODE(NodeKind) K##NodeKind,
#include "ItaniumNodes.def"
  };

  /// Three-way bool to track a cached value.
  ///
  /// Unknown is possible if this node has an unexpanded parameter pack below
  /// it that may affect this cache.
  enum class Cache : uint8_t {
    /// Cached result is true.
    Yes,
    /// Cached result is false.
    No,
    /// Result depends on unexpanded packs and is not yet known.
    Unknown,
  };

  /// Operator precedence for expression nodes.
  ///
  /// Used to determine required parentheses in expression emission.
  enum class Prec : uint8_t {
    /// Primary expression (highest precedence).
    Primary,
    /// Postfix operators (++, --, calls, subscripts).
    Postfix,
    /// Prefix unary operators.
    Unary,
    /// C-style and named casts.
    Cast,
    /// Pointer-to-member operators .* and ->*.
    PtrMem,
    /// Multiplicative operators *, /, %.
    Multiplicative,
    /// Additive operators + and -.
    Additive,
    /// Shift operators << and >>.
    Shift,
    /// Three-way comparison operator <=>.
    Spaceship,
    /// Relational operators <, >, <=, >=.
    Relational,
    /// Equality operators == and !=.
    Equality,
    /// Bitwise AND operator &.
    And,
    /// Bitwise XOR operator ^.
    Xor,
    /// Bitwise OR operator |.
    Ior,
    /// Logical AND operator &&.
    AndIf,
    /// Logical OR operator ||.
    OrIf,
    /// Conditional operator ?: .
    Conditional,
    /// Assignment and compound assignment operators.
    Assign,
    /// Comma operator.
    Comma,
    /// Default / sentinel precedence used when none is specified.
    Default,
  };

private:
  Kind K;

  Prec Precedence : 6;

protected:
  /// Tracks if this node has a component on its right side, in which case we
  /// need to call printRight.
  Cache RHSComponentCache : 2;

  /// Track if this node is a (possibly qualified) array type. This can affect
  /// how we format the output string.
  Cache ArrayCache : 2;

  /// Track if this node is a (possibly qualified) function type. This can
  /// affect how we format the output string.
  Cache FunctionCache : 2;

public:
  /// Construct a node with kind, precedence, and cache hints.
  /// \param K_ Concrete node kind.
  /// \param Precedence_ Expression precedence for this node.
  /// \param RHSComponentCache_ Whether this node has a right-hand component.
  /// \param ArrayCache_ Whether this node is an array type.
  /// \param FunctionCache_ Whether this node is a function type.
  Node(Kind K_, Prec Precedence_ = Prec::Primary,
       Cache RHSComponentCache_ = Cache::No, Cache ArrayCache_ = Cache::No,
       Cache FunctionCache_ = Cache::No)
      : K(K_), Precedence(Precedence_), RHSComponentCache(RHSComponentCache_),
        ArrayCache(ArrayCache_), FunctionCache(FunctionCache_) {}
  /// Construct a node with kind and cache hints, using primary precedence.
  /// \param K_ Concrete node kind.
  /// \param RHSComponentCache_ Whether this node has a right-hand component.
  /// \param ArrayCache_ Whether this node is an array type.
  /// \param FunctionCache_ Whether this node is a function type.
  Node(Kind K_, Cache RHSComponentCache_, Cache ArrayCache_ = Cache::No,
       Cache FunctionCache_ = Cache::No)
      : Node(K_, Prec::Primary, RHSComponentCache_, ArrayCache_,
             FunctionCache_) {}

  /// Visit the most-derived object corresponding to this object.
  /// \param F Callable invoked with the node cast to its derived type.
  template<typename Fn> void visit(Fn F) const;

  // The following function is provided by all derived classes:
  //
  // Call F with arguments that, when passed to the constructor of this node,
  // would construct an equivalent node.
  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  //template<typename Fn> void match(Fn F) const;

  /// Return true if this node has a right-hand print component.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return True if this node has a right-hand print component.
  bool hasRHSComponent(OutputBuffer &OB) const {
    if (RHSComponentCache != Cache::Unknown)
      return RHSComponentCache == Cache::Yes;
    return hasRHSComponentSlow(OB);
  }

  /// Return true if this node is a (possibly qualified) array type.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return True if this node is a (possibly qualified) array type.
  bool hasArray(OutputBuffer &OB) const {
    if (ArrayCache != Cache::Unknown)
      return ArrayCache == Cache::Yes;
    return hasArraySlow(OB);
  }

  /// Return true if this node is a (possibly qualified) function type.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return True if this node is a (possibly qualified) function type.
  bool hasFunction(OutputBuffer &OB) const {
    if (FunctionCache != Cache::Unknown)
      return FunctionCache == Cache::Yes;
    return hasFunctionSlow(OB);
  }

  /// Return the concrete kind of this node.
  /// \return The concrete kind of this node.
  Kind getKind() const { return K; }

  /// Return the expression precedence of this node.
  /// \return The expression precedence of this node.
  Prec getPrecedence() const { return Precedence; }
  /// Return the cached right-hand-component flag.
  /// \return The cached right-hand-component flag.
  Cache getRHSComponentCache() const { return RHSComponentCache; }
  /// Return the cached array-type flag.
  /// \return The cached array-type flag.
  Cache getArrayCache() const { return ArrayCache; }
  /// Return the cached function-type flag.
  /// \return The cached function-type flag.
  Cache getFunctionCache() const { return FunctionCache; }

  /// Slow path for hasRHSComponent when the cache is Unknown.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return True if this node has a right-hand print component.
  virtual bool hasRHSComponentSlow(OutputBuffer &OB) const { return false; }
  /// Slow path for hasArray when the cache is Unknown.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return True if this node is a (possibly qualified) array type.
  virtual bool hasArraySlow(OutputBuffer &OB) const { return false; }
  /// Slow path for hasFunction when the cache is Unknown.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return True if this node is a (possibly qualified) function type.
  virtual bool hasFunctionSlow(OutputBuffer &OB) const { return false; }

  /// Dig through glue nodes to the concrete syntax node.
  ///
  /// Skips ParameterPack and ForwardTemplateReference wrappers.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return The concrete syntax node under glue wrappers.
  virtual const Node *getSyntaxNode(OutputBuffer &OB) const { return this; }

  /// Print this node as an expression operand.
  ///
  /// Surrounds the node in parentheses if its precedence is weaker than \p P
  /// (or strictly weaker when \p StrictlyWorse is true).
  /// \param OB Destination demangle output buffer.
  /// \param P Surrounding operator precedence.
  /// \param StrictlyWorse Require strictly weaker precedence for parentheses.
  void printAsOperand(OutputBuffer &OB, Prec P = Prec::Default,
                      bool StrictlyWorse = false) const {
    bool Paren =
        unsigned(getPrecedence()) >= unsigned(P) + unsigned(StrictlyWorse);
    if (Paren)
      OB.printOpen();
    print(OB);
    if (Paren)
      OB.printClose();
  }

  /// Print this node by emitting its left and optional right components.
  /// \param OB Destination demangle output buffer.
  void print(OutputBuffer &OB) const {
    OB.printLeft(*this);
    if (RHSComponentCache != Cache::No)
      OB.printRight(*this);
  }

  /// Print an initializer list of this type.
  ///
  /// \return true if a custom representation was printed; false to use the
  /// default representation.
  /// \param OB Destination demangle output buffer.
  /// \param Elements Initializer-list element nodes.
  virtual bool printInitListAsType(OutputBuffer &OB,
                                   const NodeArray &Elements) const {
    return false;
  }

  /// Return the base identifier spelling for this name node, if any.
  /// \return The base identifier spelling for this name node.
  virtual std::string_view getBaseName() const { return {}; }

  /// Virtual destructor; nodes are not destroyed through this hierarchy.
  virtual ~Node() = default;

#ifndef NDEBUG
  /// Dump this node for debugging.
  DEMANGLE_DUMP_METHOD void dump() const;
#endif

private:
  friend class OutputBuffer;

  /// Print the left-hand portion of this node into \p OB.
  ///
  /// Only OutputBuffer implementations should call this; clients use
  /// \ref OutputBuffer::printLeft instead.
  /// \param OB Destination demangle output buffer.
  virtual void printLeft(OutputBuffer &OB) const = 0;

  /// Print the right-hand portion of this node into \p OB.
  ///
  /// Needed for C++ types that appear to the right of their subtype, such as
  /// arrays or functions. Most nodes have no right-hand component.
  ///
  /// Only OutputBuffer implementations should call this; clients use
  /// \ref OutputBuffer::printRight instead.
  /// \param OB Destination demangle output buffer.
  virtual void printRight(OutputBuffer &OB) const {}
};

/// Non-owning array of AST node pointers.
class NodeArray {
  Node **Elements;
  size_t NumElements;

public:
  /// Construct an empty node array.
  NodeArray() : Elements(nullptr), NumElements(0) {}
  /// Construct a node array over \p Elements_ of length \p NumElements_.
  /// \param Elements_ Pointer to the first node pointer.
  /// \param NumElements_ Number of elements.
  NodeArray(Node **Elements_, size_t NumElements_)
      : Elements(Elements_), NumElements(NumElements_) {}

  /// Return true if the array has no elements.
  /// \return True if there are no elements.
  bool empty() const { return NumElements == 0; }
  /// Return the number of elements.
  /// \return The number of elements.
  size_t size() const { return NumElements; }

  /// Return a pointer to the first element.
  /// \return A pointer to the first element.
  Node **begin() const { return Elements; }
  /// Return a pointer one past the last element.
  /// \return A pointer one past the last element.
  Node **end() const { return Elements + NumElements; }

  /// Return the element at \p Idx.
  /// \param Idx Zero-based element index.
  /// \return A reference to the element at the given index.
  Node *operator[](size_t Idx) const { return Elements[Idx]; }

  /// Print elements as a comma-separated operand list into \p OB.
  /// \param OB Destination demangle output buffer.
  void printWithComma(OutputBuffer &OB) const {
    bool FirstElement = true;
    for (size_t Idx = 0; Idx != NumElements; ++Idx) {
      size_t BeforeComma = OB.getCurrentPosition();
      if (!FirstElement)
        OB += ", ";
      size_t AfterComma = OB.getCurrentPosition();
      Elements[Idx]->printAsOperand(OB, Node::Prec::Comma);

      // Elements[Idx] is an empty parameter pack expansion, we should erase the
      // comma we just printed.
      if (AfterComma == OB.getCurrentPosition()) {
        OB.setCurrentPosition(BeforeComma);
        continue;
      }

      FirstElement = false;
    }
  }

  /// Print an array of integer literals as a string literal.
  /// \param OB Destination demangle output buffer.
  /// \return true if a string literal was printed.
  bool printAsString(OutputBuffer &OB) const;
};

/// AST node wrapping a NodeArray for printing.
struct NodeArrayNode : Node {
  /// Contained node array.
  NodeArray Array;
  /// Construct a NodeArrayNode node.
  /// \param Array_ The array.
  NodeArrayNode(NodeArray Array_) : Node(KNodeArrayNode), Array(Array_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Array); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override { Array.printWithComma(OB); }
};

/// Vendor or extension suffix after a mangled encoding.
class DotSuffix final : public Node {
  const Node *Prefix;
  const std::string_view Suffix;

public:
  /// Construct a DotSuffix node.
  /// \param Prefix_ The prefix.
  /// \param Suffix_ The suffix.
  DotSuffix(const Node *Prefix_, std::string_view Suffix_)
      : Node(KDotSuffix), Prefix(Prefix_), Suffix(Suffix_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Prefix, Suffix); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Prefix->print(OB);
    OB += " (";
    OB += Suffix;
    OB += ")";
  }
};

/// Type with a vendor-extended qualifier.
class VendorExtQualType final : public Node {
  const Node *Ty;
  std::string_view Ext;
  const Node *TA;

public:
  /// Construct a VendorExtQualType node.
  /// \param Ty_ Pointee or underlying type node.
  /// \param Ext_ The ext.
  /// \param TA_ The ta.
  VendorExtQualType(const Node *Ty_, std::string_view Ext_, const Node *TA_)
      : Node(KVendorExtQualType), Ty(Ty_), Ext(Ext_), TA(TA_) {}

  /// Return the underlying type.
  /// \return The underlying type.
  const Node *getTy() const { return Ty; }
  /// Return the ext.
  /// \return The vendor extension qualifier spelling.
  std::string_view getExt() const { return Ext; }
  /// Return the template arguments for the vendor qualifier.
  /// \return The template arguments for the vendor qualifier, or null if none.
  const Node *getTA() const { return TA; }

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const { F(Ty, Ext, TA); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Ty->print(OB);
    OB += " ";
    OB += Ext;
    if (TA != nullptr)
      TA->print(OB);
  }
};

/// Ref-qualifier on a non-static member function type.
enum FunctionRefQual : unsigned char {
  /// No ref-qualifier.
  FrefQualNone,
  /// Lvalue ref-qualifier (&).
  FrefQualLValue,
  /// Rvalue ref-qualifier (&&).
  FrefQualRValue,
};

/// CV and restrict qualifiers bitfield.
enum Qualifiers {
  /// No qualifiers.
  QualNone = 0,
  /// const qualifier.
  QualConst = 0x1,
  /// volatile qualifier.
  QualVolatile = 0x2,
  /// restrict qualifier.
  QualRestrict = 0x4,
};

/// Or-assign \p Q2 into \p Q1 and return the result.
/// \param Q1 Qualifiers updated in place.
/// \param Q2 Qualifiers to merge.
/// \return The updated qualifiers value.
inline Qualifiers operator|=(Qualifiers &Q1, Qualifiers Q2) {
  return Q1 = static_cast<Qualifiers>(Q1 | Q2);
}

/// Type with const/volatile/restrict qualifiers.
class QualType final : public Node {
protected:
  /// Applied CV/restrict qualifiers.
  const Qualifiers Quals;
  /// Qualified child type.
  const Node *Child;

  /// Print CV/restrict qualifiers into \p OB.
  /// \param OB Destination demangle output buffer.
  void printQuals(OutputBuffer &OB) const {
    if (Quals & QualConst)
      OB += " const";
    if (Quals & QualVolatile)
      OB += " volatile";
    if (Quals & QualRestrict)
      OB += " restrict";
  }

public:
  /// Construct a QualType node.
  /// \param Child_ Child AST node.
  /// \param Quals_ Type qualifiers.
  QualType(const Node *Child_, Qualifiers Quals_)
      : Node(KQualType, Child_->getRHSComponentCache(), Child_->getArrayCache(),
             Child_->getFunctionCache()),
        Quals(Quals_), Child(Child_) {}

  /// Return the quals.
  /// \return The applied CV/restrict qualifiers.
  Qualifiers getQuals() const { return Quals; }
  /// Return the qualified child type.
  /// \return The qualified child type.
  const Node *getChild() const { return Child; }

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Child, Quals); }

  /// Slow path for hasRHSComponent when the cache is Unknown.
  /// \param OB Destination demangle output buffer.
  /// \return True if this node has a right-hand print component.
  bool hasRHSComponentSlow(OutputBuffer &OB) const override {
    return Child->hasRHSComponent(OB);
  }
  /// Slow path for hasArray when the cache is Unknown.
  /// \param OB Destination demangle output buffer.
  /// \return True if this node is a (possibly qualified) array type.
  bool hasArraySlow(OutputBuffer &OB) const override {
    return Child->hasArray(OB);
  }
  /// Slow path for hasFunction when the cache is Unknown.
  /// \param OB Destination demangle output buffer.
  /// \return True if this node is a (possibly qualified) function type.
  bool hasFunctionSlow(OutputBuffer &OB) const override {
    return Child->hasFunction(OB);
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB.printLeft(*Child);
    /// Print CV/restrict qualifiers into \p OB.
    /// \param OB Destination demangle output buffer.
    printQuals(OB);
  }

  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override { OB.printRight(*Child); }
};

/// Conversion operator target type.
class ConversionOperatorType final : public Node {
  const Node *Ty;

public:
  /// Construct a ConversionOperatorType node.
  /// \param Ty_ Pointee or underlying type node.
  ConversionOperatorType(const Node *Ty_)
      : Node(KConversionOperatorType), Ty(Ty_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Ty); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "operator ";
    Ty->print(OB);
  }
};

/// Type with a vendor postfix qualifier.
class PostfixQualifiedType final : public Node {
  const Node *Ty;
  const std::string_view Postfix;

public:
  /// Construct a PostfixQualifiedType node.
  /// \param Ty_ Pointee or underlying type node.
  /// \param Postfix_ The postfix.
  PostfixQualifiedType(const Node *Ty_, std::string_view Postfix_)
      : Node(KPostfixQualifiedType), Ty(Ty_), Postfix(Postfix_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Ty, Postfix); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB.printLeft(*Ty);
    OB += Postfix;
  }
};

/// Simple named type or identifier spelling.
class NameType final : public Node {
  const std::string_view Name;

public:
  /// Construct a NameType node.
  /// \param Name_ Name node.
  NameType(std::string_view Name_) : Node(KNameType), Name(Name_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Name); }

  /// Return the name.
  /// \return The name spelling.
  std::string_view getName() const { return Name; }
  /// Return the base name.
  /// \return The base identifier spelling for this name node.
  std::string_view getBaseName() const override { return Name; }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override { OB += Name; }
};

/// _BitInt(N) / unsigned _BitInt(N) type.
class BitIntType final : public Node {
  const Node *Size;
  bool Signed;

public:
  /// Construct a BitIntType node.
  /// \param Size_ The size.
  /// \param Signed_ The signed.
  BitIntType(const Node *Size_, bool Signed_)
      : Node(KBitIntType), Size(Size_), Signed(Signed_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const { F(Size, Signed); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    if (!Signed)
      OB += "unsigned ";
    OB += "_BitInt";
    OB.printOpen();
    Size->printAsOperand(OB);
    OB.printClose();
  }
};

/// Elaborated type specifier (class/struct/union/enum).
class ElaboratedTypeSpefType : public Node {
  std::string_view Kind;
  Node *Child;
public:
  /// Construct a ElaboratedTypeSpefType node.
  /// \param Kind_ The kind.
  /// \param Child_ Child AST node.
  ElaboratedTypeSpefType(std::string_view Kind_, Node *Child_)
      : Node(KElaboratedTypeSpefType), Kind(Kind_), Child(Child_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Kind, Child); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += Kind;
    OB += ' ';
    Child->print(OB);
  }
};

/// Type transformed by a vendor type trait.
class TransformedType : public Node {
  std::string_view Transform;
  Node *BaseType;
public:
  /// Construct a TransformedType node.
  /// \param Transform_ The transform.
  /// \param BaseType_ The base type.
  TransformedType(std::string_view Transform_, Node *BaseType_)
      : Node(KTransformedType), Transform(Transform_), BaseType(BaseType_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Transform, BaseType); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += Transform;
    OB += '(';
    BaseType->print(OB);
    OB += ')';
  }
};

/// GNU abi_tag attribute applied to a name.
struct AbiTagAttr : Node {
  /// Node to which the abi_tag is applied.
  Node *Base;
  /// ABI tag spelling.
  std::string_view Tag;

  /// Construct a AbiTagAttr node.
  /// \param Base_ Base node.
  /// \param Tag_ ABI tag spelling.
  AbiTagAttr(Node *Base_, std::string_view Tag_)
      : Node(KAbiTagAttr, Base_->getRHSComponentCache(), Base_->getArrayCache(),
             Base_->getFunctionCache()),
        Base(Base_), Tag(Tag_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Base, Tag); }

  /// Return the base name.
  /// \return The base identifier spelling for this name node.
  std::string_view getBaseName() const override { return Base->getBaseName(); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB.printLeft(*Base);
    OB += "[abi:";
    OB += Tag;
    OB += "]";
  }
};

/// enable_if attribute with condition expressions.
class EnableIfAttr : public Node {
  NodeArray Conditions;
public:
  /// Construct a EnableIfAttr node.
  /// \param Conditions_ The conditions.
  EnableIfAttr(NodeArray Conditions_)
      : Node(KEnableIfAttr), Conditions(Conditions_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Conditions); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += " [enable_if:";
    Conditions.printWithComma(OB);
    OB += ']';
  }
};

/// Objective-C protocol-qualified type name.
class ObjCProtoName : public Node {
  const Node *Ty;
  std::string_view Protocol;

public:
  /// Construct a ObjCProtoName node.
  /// \param Ty_ Pointee or underlying type node.
  /// \param Protocol_ The protocol.
  ObjCProtoName(const Node *Ty_, std::string_view Protocol_)
      : Node(KObjCProtoName), Ty(Ty_), Protocol(Protocol_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Ty, Protocol); }

  /// Return true if obj cobject.
  /// \return True if the underlying type is objc_object.
  bool isObjCObject() const {
    return Ty->getKind() == KNameType &&
           static_cast<const NameType *>(Ty)->getName() == "objc_object";
  }

  /// Return the protocol.
  /// \return The Objective-C protocol name.
  std::string_view getProtocol() const { return Protocol; }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Ty->print(OB);
    OB += "<";
    OB += Protocol;
    OB += ">";
  }
};

/// Pointer type.
class PointerType final : public Node {
  const Node *Pointee;

public:
  /// Construct a PointerType node.
  /// \param Pointee_ The pointee.
  PointerType(const Node *Pointee_)
      : Node(KPointerType, Pointee_->getRHSComponentCache()),
        Pointee(Pointee_) {}

  /// Return the pointee type.
  /// \return The pointee type.
  const Node *getPointee() const { return Pointee; }

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Pointee); }

  /// Slow path for hasRHSComponent when the cache is Unknown.
  /// \param OB Destination demangle output buffer.
  /// \return True if this node has a right-hand print component.
  bool hasRHSComponentSlow(OutputBuffer &OB) const override {
    return Pointee->hasRHSComponent(OB);
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    // We rewrite objc_object<SomeProtocol>* into id<SomeProtocol>.
    if (Pointee->getKind() != KObjCProtoName ||
        !static_cast<const ObjCProtoName *>(Pointee)->isObjCObject()) {
      OB.printLeft(*Pointee);
      if (Pointee->hasArray(OB))
        OB += " ";
      if (Pointee->hasArray(OB) || Pointee->hasFunction(OB))
        OB += "(";
      OB += "*";
    } else {
      const auto *objcProto = static_cast<const ObjCProtoName *>(Pointee);
      OB += "id<";
      OB += objcProto->getProtocol();
      OB += ">";
    }
  }

  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override {
    if (Pointee->getKind() != KObjCProtoName ||
        !static_cast<const ObjCProtoName *>(Pointee)->isObjCObject()) {
      if (Pointee->hasArray(OB) || Pointee->hasFunction(OB))
        OB += ")";
      OB.printRight(*Pointee);
    }
  }
};

/// Kind of C++ reference.
enum class ReferenceKind {
  /// Lvalue reference (&).
  LValue,
  /// Rvalue reference (&&).
  RValue,
};

// Represents either a LValue or an RValue reference type.
/// Lvalue or rvalue reference type.
class ReferenceType : public Node {
  const Node *Pointee;
  ReferenceKind RK;

  mutable bool Printing = false;

  // Dig through any refs to refs, collapsing the ReferenceTypes as we go. The
  // rule here is rvalue ref to rvalue ref collapses to a rvalue ref, and any
  // other combination collapses to a lvalue ref.
  //
  // A combination of a TemplateForwardReference and a back-ref Substitution
  // from an ill-formed string may have created a cycle; use cycle detection to
  // avoid looping forever.
  std::pair<ReferenceKind, const Node *> collapse(OutputBuffer &OB) const {
    auto SoFar = std::make_pair(RK, Pointee);
    // Track the chain of nodes for the Floyd's 'tortoise and hare'
    /// Return the concrete syntax node under glue wrappers.
    /// \param S The s.
    // cycle-detection algorithm, since getSyntaxNode(S) is impure
    PODSmallVector<const Node *, 8> Prev;
    for (;;) {
      /// Return the concrete syntax node under glue wrappers.
      /// \param OB Destination demangle output buffer.
      const Node *SN = SoFar.second->getSyntaxNode(OB);
      if (SN->getKind() != KReferenceType)
        break;
      auto *RT = static_cast<const ReferenceType *>(SN);
      SoFar.second = RT->Pointee;
      SoFar.first = std::min(SoFar.first, RT->RK);

      // The middle of Prev is the 'slow' pointer moving at half speed
      Prev.push_back(SoFar.second);
      if (Prev.size() > 1 && SoFar.second == Prev[(Prev.size() - 1) / 2]) {
        // Cycle detected
        SoFar.second = nullptr;
        break;
      }
    }
    return SoFar;
  }

public:
  /// Construct a ReferenceType node.
  /// \param Pointee_ The pointee.
  /// \param RK_ The rk.
  ReferenceType(const Node *Pointee_, ReferenceKind RK_)
      : Node(KReferenceType, Pointee_->getRHSComponentCache()),
        Pointee(Pointee_), RK(RK_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Pointee, RK); }

  /// Slow path for hasRHSComponent when the cache is Unknown.
  /// \param OB Destination demangle output buffer.
  /// \return True if this node has a right-hand print component.
  bool hasRHSComponentSlow(OutputBuffer &OB) const override {
    return Pointee->hasRHSComponent(OB);
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    if (Printing)
      return;
    ScopedOverride<bool> SavePrinting(Printing, true);
    std::pair<ReferenceKind, const Node *> Collapsed = collapse(OB);
    if (!Collapsed.second)
      return;
    OB.printLeft(*Collapsed.second);
    if (Collapsed.second->hasArray(OB))
      OB += " ";
    if (Collapsed.second->hasArray(OB) || Collapsed.second->hasFunction(OB))
      OB += "(";

    OB += (Collapsed.first == ReferenceKind::LValue ? "&" : "&&");
  }
  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override {
    if (Printing)
      return;
    ScopedOverride<bool> SavePrinting(Printing, true);
    std::pair<ReferenceKind, const Node *> Collapsed = collapse(OB);
    if (!Collapsed.second)
      return;
    if (Collapsed.second->hasArray(OB) || Collapsed.second->hasFunction(OB))
      OB += ")";
    OB.printRight(*Collapsed.second);
  }
};

/// Pointer-to-member type.
class PointerToMemberType final : public Node {
  const Node *ClassType;
  const Node *MemberType;

public:
  /// Construct a PointerToMemberType node.
  /// \param ClassType_ The class type.
  /// \param MemberType_ The member type.
  PointerToMemberType(const Node *ClassType_, const Node *MemberType_)
      : Node(KPointerToMemberType, MemberType_->getRHSComponentCache()),
        ClassType(ClassType_), MemberType(MemberType_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(ClassType, MemberType); }

  /// Slow path for hasRHSComponent when the cache is Unknown.
  /// \param OB Destination demangle output buffer.
  /// \return True if this node has a right-hand print component.
  bool hasRHSComponentSlow(OutputBuffer &OB) const override {
    return MemberType->hasRHSComponent(OB);
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB.printLeft(*MemberType);
    if (MemberType->hasArray(OB) || MemberType->hasFunction(OB))
      OB += "(";
    else
      OB += " ";
    ClassType->print(OB);
    OB += "::*";
  }

  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override {
    if (MemberType->hasArray(OB) || MemberType->hasFunction(OB))
      OB += ")";
    OB.printRight(*MemberType);
  }
};

/// Array type with optional bound.
class ArrayType final : public Node {
  const Node *Base;
  Node *Dimension;

public:
  /// Construct a ArrayType node.
  /// \param Base_ Base node.
  /// \param Dimension_ The dimension.
  ArrayType(const Node *Base_, Node *Dimension_)
      : Node(KArrayType,
             /*RHSComponentCache=*/Cache::Yes,
             /*ArrayCache=*/Cache::Yes),
        Base(Base_), Dimension(Dimension_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Base, Dimension); }

  /// Slow path for hasRHSComponent when the cache is Unknown.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return True if this node has a right-hand print component.
  bool hasRHSComponentSlow(OutputBuffer &OB) const override { return true; }
  /// Slow path for hasArray when the cache is Unknown.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return True if this node is a (possibly qualified) array type.
  bool hasArraySlow(OutputBuffer &OB) const override { return true; }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override { OB.printLeft(*Base); }

  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override {
    if (OB.back() != ']')
      OB += " ";
    OB += "[";
    if (Dimension)
      Dimension->print(OB);
    OB += "]";
    OB.printRight(*Base);
  }

  /// Print an initializer list of this array type.
  /// \param OB Destination demangle output buffer.
  /// \param Elements Initializer-list element nodes.
  /// \return True if a custom representation was printed.
  bool printInitListAsType(OutputBuffer &OB,
                           const NodeArray &Elements) const override {
    if (Base->getKind() == KNameType &&
        static_cast<const NameType *>(Base)->getName() == "char") {
      return Elements.printAsString(OB);
    }
    return false;
  }
};

/// Function type including exception spec and ref-qualifiers.
class FunctionType final : public Node {
  const Node *Ret;
  NodeArray Params;
  Qualifiers CVQuals;
  FunctionRefQual RefQual;
  const Node *ExceptionSpec;

public:
  /// Construct a FunctionType node.
  /// \param Ret_ The ret.
  /// \param Params_ The params.
  /// \param CVQuals_ The cvquals.
  /// \param RefQual_ The ref qual.
  /// \param ExceptionSpec_ The exception spec.
  FunctionType(const Node *Ret_, NodeArray Params_, Qualifiers CVQuals_,
               FunctionRefQual RefQual_, const Node *ExceptionSpec_)
      : Node(KFunctionType,
             /*RHSComponentCache=*/Cache::Yes, /*ArrayCache=*/Cache::No,
             /*FunctionCache=*/Cache::Yes),
        Ret(Ret_), Params(Params_), CVQuals(CVQuals_), RefQual(RefQual_),
        ExceptionSpec(ExceptionSpec_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const {
    F(Ret, Params, CVQuals, RefQual, ExceptionSpec);
  }

  /// Slow path for hasRHSComponent when the cache is Unknown.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return True if this node has a right-hand print component.
  bool hasRHSComponentSlow(OutputBuffer &OB) const override { return true; }
  /// Slow path for hasFunction when the cache is Unknown.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return True if this node is a (possibly qualified) function type.
  bool hasFunctionSlow(OutputBuffer &OB) const override { return true; }

  // Handle C++'s ... quirky decl grammar by using the left & right
  // distinction. Consider:
  //   int (*f(float))(char) {}
  // f is a function that takes a float and returns a pointer to a function
  // that takes a char and returns an int. If we're trying to print f, start
  // by printing out the return types's left, then print our parameters, then
  // finally print right of the return type.
  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB.printLeft(*Ret);
    OB += " ";
  }

  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override {
    OB.printOpen();
    Params.printWithComma(OB);
    OB.printClose();
    OB.printRight(*Ret);

    if (CVQuals & QualConst)
      OB += " const";
    if (CVQuals & QualVolatile)
      OB += " volatile";
    if (CVQuals & QualRestrict)
      OB += " restrict";

    if (RefQual == FrefQualLValue)
      OB += " &";
    else if (RefQual == FrefQualRValue)
      OB += " &&";

    if (ExceptionSpec != nullptr) {
      OB += ' ';
      ExceptionSpec->print(OB);
    }
  }
};

/// noexcept exception specification.
class NoexceptSpec : public Node {
  const Node *E;
public:
  /// Construct a NoexceptSpec node.
  /// \param E_ Two-character operator encoding.
  NoexceptSpec(const Node *E_) : Node(KNoexceptSpec), E(E_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(E); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "noexcept";
    OB.printOpen();
    E->printAsOperand(OB);
    OB.printClose();
  }
};

/// Dynamic exception specification throw(...).
class DynamicExceptionSpec : public Node {
  NodeArray Types;
public:
  /// Construct a DynamicExceptionSpec node.
  /// \param Types_ The types.
  DynamicExceptionSpec(NodeArray Types_)
      : Node(KDynamicExceptionSpec), Types(Types_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Types); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "throw";
    OB.printOpen();
    Types.printWithComma(OB);
    OB.printClose();
  }
};

/// Represents the explicitly named object parameter.
/// E.g.,
/// \code{.cpp}
///   struct Foo {
///     void bar(this Foo && self);
///   };
/// \endcode
class ExplicitObjectParameter final : public Node {
  Node *Base;

public:
  /// Construct a ExplicitObjectParameter node.
  /// \param Base_ Base node.
  ExplicitObjectParameter(Node *Base_)
      : Node(KExplicitObjectParameter), Base(Base_) {
    DEMANGLE_ASSERT(
        Base != nullptr,
        "Creating an ExplicitObjectParameter without a valid Base Node.");
  }

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const { F(Base); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "this ";
    Base->print(OB);
  }
};

/// Encoded function name with type and constraints.
class FunctionEncoding final : public Node {
  const Node *Ret;
  const Node *Name;
  NodeArray Params;
  const Node *Attrs;
  const Node *Requires;
  Qualifiers CVQuals;
  FunctionRefQual RefQual;

public:
  /// Construct a FunctionEncoding node.
  /// \param Ret_ The ret.
  /// \param Name_ Name node.
  /// \param Params_ The params.
  /// \param Attrs_ The attrs.
  /// \param Requires_ Requires clause node.
  /// \param CVQuals_ The cvquals.
  /// \param RefQual_ The ref qual.
  FunctionEncoding(const Node *Ret_, const Node *Name_, NodeArray Params_,
                   const Node *Attrs_, const Node *Requires_,
                   Qualifiers CVQuals_, FunctionRefQual RefQual_)
      : Node(KFunctionEncoding,
             /*RHSComponentCache=*/Cache::Yes, /*ArrayCache=*/Cache::No,
             /*FunctionCache=*/Cache::Yes),
        Ret(Ret_), Name(Name_), Params(Params_), Attrs(Attrs_),
        Requires(Requires_), CVQuals(CVQuals_), RefQual(RefQual_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const {
    F(Ret, Name, Params, Attrs, Requires, CVQuals, RefQual);
  }

  /// Return the cvquals.
  /// \return The function cv-qualifiers.
  Qualifiers getCVQuals() const { return CVQuals; }
  /// Return the ref qual.
  /// \return The function ref-qualifier.
  FunctionRefQual getRefQual() const { return RefQual; }
  /// Return the function parameter types.
  /// \return The function parameter types.
  NodeArray getParams() const { return Params; }
  /// Return the function return type.
  /// \return The function return type, or null if none.
  const Node *getReturnType() const { return Ret; }
  /// Return the function attributes node.
  /// \return The function attributes node, or null if none.
  const Node *getAttrs() const { return Attrs; }
  /// Return the requires-clause node.
  /// \return The requires-clause node, or null if none.
  const Node *getRequires() const { return Requires; }

  /// Slow path for hasRHSComponent when the cache is Unknown.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return True if this node has a right-hand print component.
  bool hasRHSComponentSlow(OutputBuffer &OB) const override { return true; }
  /// Slow path for hasFunction when the cache is Unknown.
  /// \param OB Output buffer providing pack-expansion printing state.
  /// \return True if this node is a (possibly qualified) function type.
  bool hasFunctionSlow(OutputBuffer &OB) const override { return true; }

  /// Return the function name node.
  /// \return The function name node.
  const Node *getName() const { return Name; }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    if (Ret) {
      OB.printLeft(*Ret);
      if (!Ret->hasRHSComponent(OB))
        OB += " ";
    }

    Name->print(OB);
  }

  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override {
    OB.printOpen();
    Params.printWithComma(OB);
    OB.printClose();

    if (Ret)
      OB.printRight(*Ret);

    if (CVQuals & QualConst)
      OB += " const";
    if (CVQuals & QualVolatile)
      OB += " volatile";
    if (CVQuals & QualRestrict)
      OB += " restrict";

    if (RefQual == FrefQualLValue)
      OB += " &";
    else if (RefQual == FrefQualRValue)
      OB += " &&";

    if (Attrs != nullptr)
      Attrs->print(OB);

    if (Requires != nullptr) {
      OB += " requires ";
      Requires->print(OB);
    }
  }
};

/// User-defined literal operator name.
class LiteralOperator : public Node {
  const Node *OpName;

public:
  /// Construct a LiteralOperator node.
  /// \param OpName_ The op name.
  LiteralOperator(const Node *OpName_)
      : Node(KLiteralOperator), OpName(OpName_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(OpName); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "operator\"\" ";
    OpName->print(OB);
  }
};

/// Special mangled name with a fixed prefix string.
class SpecialName final : public Node {
  const std::string_view Special;
  const Node *Child;

public:
  /// Construct a SpecialName node.
  /// \param Special_ The special.
  /// \param Child_ Child AST node.
  SpecialName(std::string_view Special_, const Node *Child_)
      : Node(KSpecialName), Special(Special_), Child(Child_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Special, Child); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += Special;
    Child->print(OB);
  }
};

/// Construction-vtable special name.
class CtorVtableSpecialName final : public Node {
  const Node *FirstType;
  const Node *SecondType;

public:
  /// Construct a CtorVtableSpecialName node.
  /// \param FirstType_ The first type.
  /// \param SecondType_ The second type.
  CtorVtableSpecialName(const Node *FirstType_, const Node *SecondType_)
      : Node(KCtorVtableSpecialName),
        FirstType(FirstType_), SecondType(SecondType_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(FirstType, SecondType); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "construction vtable for ";
    FirstType->print(OB);
    OB += "-in-";
    SecondType->print(OB);
  }
};

/// Qualified nested name (N...E).
struct NestedName : Node {
  /// Qualifying scope.
  Node *Qual;
  /// Nested name component.
  Node *Name;

  /// Construct a NestedName node.
  /// \param Qual_ The qual.
  /// \param Name_ Name node.
  NestedName(Node *Qual_, Node *Name_)
      : Node(KNestedName), Qual(Qual_), Name(Name_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Qual, Name); }

  /// Return the base name.
  /// \return The base identifier spelling for this name node.
  std::string_view getBaseName() const override { return Name->getBaseName(); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Qual->print(OB);
    OB += "::";
    Name->print(OB);
  }
};

/// Member-like friend function name.
struct MemberLikeFriendName : Node {
  /// Qualifying scope.
  Node *Qual;
  /// Friend function name.
  Node *Name;

  /// Construct a MemberLikeFriendName node.
  /// \param Qual_ The qual.
  /// \param Name_ Name node.
  MemberLikeFriendName(Node *Qual_, Node *Name_)
      : Node(KMemberLikeFriendName), Qual(Qual_), Name(Name_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Qual, Name); }

  /// Return the base name.
  /// \return The base identifier spelling for this name node.
  std::string_view getBaseName() const override { return Name->getBaseName(); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Qual->print(OB);
    OB += "::friend ";
    Name->print(OB);
  }
};

/// C++20 module or module-partition name.
struct ModuleName : Node {
  /// Parent module name, if any.
  ModuleName *Parent;
  /// Module component name.
  Node *Name;
  /// True if this component is a partition.
  bool IsPartition;

  /// Construct a ModuleName node.
  /// \param Parent_ The parent.
  /// \param Name_ Name node.
  /// \param IsPartition_ The is partition.
  ModuleName(ModuleName *Parent_, Node *Name_, bool IsPartition_ = false)
      : Node(KModuleName), Parent(Parent_), Name(Name_),
        IsPartition(IsPartition_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(Parent, Name, IsPartition);
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    if (Parent)
      Parent->print(OB);
    if (Parent || IsPartition)
      OB += IsPartition ? ':' : '.';
    Name->print(OB);
  }
};

/// Entity attached to a module name.
struct ModuleEntity : Node {
  /// Owning module.
  ModuleName *Module;
  /// Entity name within the module.
  Node *Name;

  /// Construct a ModuleEntity node.
  /// \param Module_ Module name being built.
  /// \param Name_ Name node.
  ModuleEntity(ModuleName *Module_, Node *Name_)
      : Node(KModuleEntity), Module(Module_), Name(Name_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const { F(Module, Name); }

  /// Return the base name.
  /// \return The base identifier spelling for this name node.
  std::string_view getBaseName() const override { return Name->getBaseName(); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Name->print(OB);
    OB += '@';
    Module->print(OB);
  }
};

/// Local name encoded relative to a function (Z...E).
struct LocalName : Node {
  /// Enclosing function encoding.
  Node *Encoding;
  /// Local entity name.
  Node *Entity;

  /// Construct a LocalName node.
  /// \param Encoding_ The encoding.
  /// \param Entity_ The entity.
  LocalName(Node *Encoding_, Node *Entity_)
      : Node(KLocalName), Encoding(Encoding_), Entity(Entity_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Encoding, Entity); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Encoding->print(OB);
    OB += "::";
    Entity->print(OB);
  }
};

/// Name qualified by a scope node.
class QualifiedName final : public Node {
  // qualifier::name
  const Node *Qualifier;
  const Node *Name;

public:
  /// Construct a QualifiedName node.
  /// \param Qualifier_ The qualifier.
  /// \param Name_ Name node.
  QualifiedName(const Node *Qualifier_, const Node *Name_)
      : Node(KQualifiedName), Qualifier(Qualifier_), Name(Name_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Qualifier, Name); }

  /// Return the base name.
  /// \return The base identifier spelling for this name node.
  std::string_view getBaseName() const override { return Name->getBaseName(); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Qualifier->print(OB);
    OB += "::";
    Name->print(OB);
  }
};

/// GNU vector type.
class VectorType final : public Node {
  const Node *BaseType;
  const Node *Dimension;

public:
  /// Construct a VectorType node.
  /// \param BaseType_ The base type.
  /// \param Dimension_ The dimension.
  VectorType(const Node *BaseType_, const Node *Dimension_)
      : Node(KVectorType), BaseType(BaseType_), Dimension(Dimension_) {}

  /// Return the vector element type.
  /// \return The vector element type.
  const Node *getBaseType() const { return BaseType; }
  /// Return the vector dimension expression.
  /// \return The vector dimension expression.
  const Node *getDimension() const { return Dimension; }

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(BaseType, Dimension); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    BaseType->print(OB);
    OB += " vector[";
    if (Dimension)
      Dimension->print(OB);
    OB += "]";
  }
};

/// AltiVec pixel vector type.
class PixelVectorType final : public Node {
  const Node *Dimension;

public:
  /// Construct a PixelVectorType node.
  /// \param Dimension_ The dimension.
  PixelVectorType(const Node *Dimension_)
      : Node(KPixelVectorType), Dimension(Dimension_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Dimension); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    // FIXME: This should demangle as "vector pixel".
    OB += "pixel vector[";
    Dimension->print(OB);
    OB += "]";
  }
};

/// Binary floating-point type of given bit width.
class BinaryFPType final : public Node {
  const Node *Dimension;

public:
  /// Construct a BinaryFPType node.
  /// \param Dimension_ The dimension.
  BinaryFPType(const Node *Dimension_)
      : Node(KBinaryFPType), Dimension(Dimension_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Dimension); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "_Float";
    Dimension->print(OB);
  }
};

/// Kind of synthetic / invented template parameter.
enum class TemplateParamKind {
  /// Type template parameter.
  Type,
  /// Non-type template parameter.
  NonType,
  /// Template template parameter.
  Template,
};

/// An invented name for a template parameter for which we don't have a
/// corresponding template argument.
///
/// This node is created when parsing the <lambda-sig> for a lambda with
/// explicit template arguments, which might be referenced in the parameter
/// types appearing later in the <lambda-sig>.
class SyntheticTemplateParamName final : public Node {
  TemplateParamKind Kind;
  unsigned Index;

public:
  /// Construct a SyntheticTemplateParamName node.
  /// \param Kind_ The kind.
  /// \param Index_ Index expression node.
  SyntheticTemplateParamName(TemplateParamKind Kind_, unsigned Index_)
      : Node(KSyntheticTemplateParamName), Kind(Kind_), Index(Index_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Kind, Index); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    switch (Kind) {
    case TemplateParamKind::Type:
      OB += "$T";
      break;
    case TemplateParamKind::NonType:
      OB += "$N";
      break;
    case TemplateParamKind::Template:
      OB += "$TT";
      break;
    }
    if (Index > 0)
      OB << Index - 1;
  }
};

/// Template argument with an explicit parameter.
class TemplateParamQualifiedArg final : public Node {
  Node *Param;
  Node *Arg;

public:
  /// Construct a TemplateParamQualifiedArg node.
  /// \param Param_ The param.
  /// \param Arg_ The arg.
  TemplateParamQualifiedArg(Node *Param_, Node *Arg_)
      : Node(KTemplateParamQualifiedArg), Param(Param_), Arg(Arg_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const { F(Param, Arg); }

  /// Return the template argument node.
  /// \return The template argument node.
  Node *getArg() { return Arg; }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    // Don't print Param to keep the output consistent.
    Arg->print(OB);
  }
};

/// A template type parameter declaration, 'typename T'.
class TypeTemplateParamDecl final : public Node {
  Node *Name;

public:
  /// Construct a TypeTemplateParamDecl node.
  /// \param Name_ Name node.
  TypeTemplateParamDecl(Node *Name_)
      : Node(KTypeTemplateParamDecl, Cache::Yes), Name(Name_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Name); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override { OB += "typename "; }

  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override { Name->print(OB); }
};

/// A constrained template type parameter declaration, 'C<U> T'.
class ConstrainedTypeTemplateParamDecl final : public Node {
  Node *Constraint;
  Node *Name;

public:
  /// Construct a ConstrainedTypeTemplateParamDecl node.
  /// \param Constraint_ Constraint expression node.
  /// \param Name_ Name node.
  ConstrainedTypeTemplateParamDecl(Node *Constraint_, Node *Name_)
      : Node(KConstrainedTypeTemplateParamDecl, Cache::Yes),
        Constraint(Constraint_), Name(Name_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Constraint, Name); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Constraint->print(OB);
    OB += " ";
  }

  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override { Name->print(OB); }
};

/// A non-type template parameter declaration, 'int N'.
class NonTypeTemplateParamDecl final : public Node {
  Node *Name;
  Node *Type;

public:
  /// Construct a NonTypeTemplateParamDecl node.
  /// \param Name_ Name node.
  /// \param Type_ Parameter type node.
  NonTypeTemplateParamDecl(Node *Name_, Node *Type_)
      : Node(KNonTypeTemplateParamDecl, Cache::Yes), Name(Name_), Type(Type_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Name, Type); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB.printLeft(*Type);
    if (!Type->hasRHSComponent(OB))
      OB += " ";
  }

  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override {
    Name->print(OB);
    OB.printRight(*Type);
  }
};

/// A template template parameter declaration,
/// 'template<typename T> typename N'.
class TemplateTemplateParamDecl final : public Node {
  Node *Name;
  NodeArray Params;
  Node *Requires;

public:
  /// Construct a TemplateTemplateParamDecl node.
  /// \param Name_ Name node.
  /// \param Params_ The params.
  /// \param Requires_ Requires clause node.
  TemplateTemplateParamDecl(Node *Name_, NodeArray Params_, Node *Requires_)
      : Node(KTemplateTemplateParamDecl, Cache::Yes), Name(Name_),
        Params(Params_), Requires(Requires_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const { F(Name, Params, Requires); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    ScopedOverride<bool> LT(OB.TemplateTracker.InsideTemplate, true);
    OB += "template<";
    Params.printWithComma(OB);
    OB += "> typename ";
  }

  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override {
    Name->print(OB);
    if (Requires != nullptr) {
      OB += " requires ";
      Requires->print(OB);
    }
  }
};

/// A template parameter pack declaration, 'typename ...T'.
class TemplateParamPackDecl final : public Node {
  Node *Param;

public:
  /// Construct a TemplateParamPackDecl node.
  /// \param Param_ Underlying template parameter declaration.
  TemplateParamPackDecl(Node *Param_)
      : Node(KTemplateParamPackDecl, Cache::Yes), Param(Param_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Param); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB.printLeft(*Param);
    OB += "...";
  }

  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override { OB.printRight(*Param); }
};

/// An unexpanded parameter pack (either in the expression or type context). If
/// this AST is correct, this node will have a ParameterPackExpansion node above
/// it.
///
/// This node is created when some <template-args> are found that apply to an
/// <encoding>, and is stored in the TemplateParams table. In order for this to
/// appear in the final AST, it has to referenced via a <template-param> (ie,
/// T_).
class ParameterPack final : public Node {
  NodeArray Data;

  // Setup OutputBuffer for a pack expansion, unless we're already expanding
  // one.
  void initializePackExpansion(OutputBuffer &OB) const {
    if (OB.CurrentPackMax == std::numeric_limits<unsigned>::max()) {
      OB.CurrentPackMax = static_cast<unsigned>(Data.size());
      OB.CurrentPackIndex = 0;
    }
  }

public:
  /// Construct a ParameterPack node.
  /// \param Data_ The data.
  ParameterPack(NodeArray Data_) : Node(KParameterPack), Data(Data_) {
    ArrayCache = FunctionCache = RHSComponentCache = Cache::Unknown;
    if (std::all_of(Data.begin(), Data.end(),
                    [](Node *P) { return P->getArrayCache() == Cache::No; }))
      ArrayCache = Cache::No;
    if (std::all_of(Data.begin(), Data.end(),
                    [](Node *P) { return P->getFunctionCache() == Cache::No; }))
      FunctionCache = Cache::No;
    if (std::all_of(Data.begin(), Data.end(), [](Node *P) {
          return P->getRHSComponentCache() == Cache::No;
        }))
      RHSComponentCache = Cache::No;
  }

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Data); }

  /// Slow path for hasRHSComponent when the cache is Unknown.
  /// \param OB Destination demangle output buffer.
  /// \return True if this node has a right-hand print component.
  bool hasRHSComponentSlow(OutputBuffer &OB) const override {
    initializePackExpansion(OB);
    size_t Idx = OB.CurrentPackIndex;
    return Idx < Data.size() && Data[Idx]->hasRHSComponent(OB);
  }
  /// Slow path for hasArray when the cache is Unknown.
  /// \param OB Destination demangle output buffer.
  /// \return True if this node is a (possibly qualified) array type.
  bool hasArraySlow(OutputBuffer &OB) const override {
    initializePackExpansion(OB);
    size_t Idx = OB.CurrentPackIndex;
    return Idx < Data.size() && Data[Idx]->hasArray(OB);
  }
  /// Slow path for hasFunction when the cache is Unknown.
  /// \param OB Destination demangle output buffer.
  /// \return True if this node is a (possibly qualified) function type.
  bool hasFunctionSlow(OutputBuffer &OB) const override {
    initializePackExpansion(OB);
    size_t Idx = OB.CurrentPackIndex;
    return Idx < Data.size() && Data[Idx]->hasFunction(OB);
  }
  /// Return the concrete syntax node under glue wrappers.
  /// \param OB Destination demangle output buffer.
  /// \return The concrete syntax node under glue wrappers.
  const Node *getSyntaxNode(OutputBuffer &OB) const override {
    initializePackExpansion(OB);
    size_t Idx = OB.CurrentPackIndex;
    return Idx < Data.size() ? Data[Idx]->getSyntaxNode(OB) : this;
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    initializePackExpansion(OB);
    size_t Idx = OB.CurrentPackIndex;
    if (Idx < Data.size())
      OB.printLeft(*Data[Idx]);
  }
  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override {
    initializePackExpansion(OB);
    size_t Idx = OB.CurrentPackIndex;
    if (Idx < Data.size())
      OB.printRight(*Data[Idx]);
  }
};

/// Variadic template argument pack from a J...E encoding.
///
/// This node represents an occurrence of J<something>E in some
/// <template-args>. It isn't itself unexpanded, unless one of its Elements is.
/// The parser inserts a ParameterPack into the TemplateParams table if the
/// <template-args> this pack belongs to apply to an <encoding>.
class TemplateArgumentPack final : public Node {
  NodeArray Elements;
public:
  /// Construct a TemplateArgumentPack node.
  /// \param Elements_ Pack or array elements.
  TemplateArgumentPack(NodeArray Elements_)
      : Node(KTemplateArgumentPack), Elements(Elements_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Elements); }

  /// Return the elements.
  /// \return The elements of the template argument pack.
  NodeArray getElements() const { return Elements; }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Elements.printWithComma(OB);
  }
};

/// A pack expansion. Below this node, there are some unexpanded ParameterPacks
/// which each have Child->ParameterPackSize elements.
class ParameterPackExpansion final : public Node {
  const Node *Child;

public:
  /// Construct a ParameterPackExpansion node.
  /// \param Child_ Child AST node.
  ParameterPackExpansion(const Node *Child_)
      : Node(KParameterPackExpansion), Child(Child_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Child); }

  /// Return the pattern being expanded.
  /// \return The child pattern that contains unexpanded parameter packs.
  const Node *getChild() const { return Child; }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    constexpr unsigned Max = std::numeric_limits<unsigned>::max();
    ScopedOverride<unsigned> SavePackIdx(OB.CurrentPackIndex, Max);
    ScopedOverride<unsigned> SavePackMax(OB.CurrentPackMax, Max);
    size_t StreamPos = OB.getCurrentPosition();

    // Print the first element in the pack. If Child contains a ParameterPack,
    // it will set up S.CurrentPackMax and print the first element.
    Child->print(OB);

    // No ParameterPack was found in Child. This can occur if we've found a pack
    // expansion on a <function-param>.
    if (OB.CurrentPackMax == Max) {
      OB += "...";
      return;
    }

    // We found a ParameterPack, but it has no elements. Erase whatever we may
    // of printed.
    if (OB.CurrentPackMax == 0) {
      OB.setCurrentPosition(StreamPos);
      return;
    }

    // Else, iterate through the rest of the elements in the pack.
    for (unsigned I = 1, E = OB.CurrentPackMax; I < E; ++I) {
      OB += ", ";
      OB.CurrentPackIndex = I;
      Child->print(OB);
    }
  }
};

/// C++26 pack indexing expression.
class PackIndexing final : public Node {
  const Node *Pattern;
  const Node *Index;

public:
  /// Construct a PackIndexing node.
  /// \param Pattern_ Pack pattern node.
  /// \param Index_ Index expression node.
  PackIndexing(const Node *Pattern_, const Node *Index_)
      : Node(KPackIndexing), Pattern(Pattern_), Index(Index_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const { F(Pattern, Index); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB.printOpen('(');
    ParameterPackExpansion PPE(Pattern);
    PPE.printLeft(OB);
    OB.printClose(')');
    OB.printOpen('[');
    OB.printLeft(*Index);
    OB.printClose(']');
  }
};

/// List of template arguments (I...E).
class TemplateArgs final : public Node {
  NodeArray Params;
  Node *Requires;

public:
  /// Construct a TemplateArgs node.
  /// \param Params_ The params.
  /// \param Requires_ Requires clause node.
  TemplateArgs(NodeArray Params_, Node *Requires_)
      : Node(KTemplateArgs), Params(Params_), Requires(Requires_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  /// \return The function parameter types.
  template<typename Fn> void match(Fn F) const { F(Params, Requires); }

  /// Return the template argument nodes.
  /// \return The template argument nodes.
  NodeArray getParams() { return Params; }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    ScopedOverride<bool> LT(OB.TemplateTracker.InsideTemplate, true);
    OB += "<";
    Params.printWithComma(OB);
    OB += ">";
    // Don't print the requires clause to keep the output simple.
  }
};

/// A forward-reference to a template argument that was not known at the point
/// where the template parameter name was parsed in a mangling.
///
/// This is created when demangling the name of a specialization of a
/// conversion function template:
///
/// \code
/// struct A {
///   template<typename T> operator T*();
/// };
/// \endcode
///
/// When demangling a specialization of the conversion function template, we
/// encounter the name of the template (including the \c T) before we reach
/// the template argument list, so we cannot substitute the parameter name
/// for the corresponding argument while parsing. Instead, we create a
/// \c ForwardTemplateReference node that is resolved after we parse the
/// template arguments.
struct ForwardTemplateReference : Node {
  /// Template parameter index being referenced.
  size_t Index;
  /// Resolved referent once known.
  Node *Ref = nullptr;

  // If we're currently printing this node. It is possible (though invalid) for
  // a forward template reference to refer to itself via a substitution. This
  // creates a cyclic AST, which will stack overflow printing. To fix this, bail
  // out if more than one print* function is active.
  /// True while this node is being printed (cycle guard).
  mutable bool Printing = false;

  /// Construct a ForwardTemplateReference node.
  /// \param Index_ Index expression node.
  ForwardTemplateReference(size_t Index_)
      : Node(KForwardTemplateReference, Cache::Unknown, Cache::Unknown,
             Cache::Unknown),
        Index(Index_) {}

  // We don't provide a matcher for these, because the value of the node is
  // not determined by its construction parameters, and it generally needs
  // special handling.
  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const = delete;

  /// Slow path for hasRHSComponent when the cache is Unknown.
  /// \param OB Destination demangle output buffer.
  /// \return True if this node has a right-hand print component.
  bool hasRHSComponentSlow(OutputBuffer &OB) const override {
    if (Printing)
      return false;
    ScopedOverride<bool> SavePrinting(Printing, true);
    return Ref->hasRHSComponent(OB);
  }
  /// Slow path for hasArray when the cache is Unknown.
  /// \param OB Destination demangle output buffer.
  /// \return True if this node is a (possibly qualified) array type.
  bool hasArraySlow(OutputBuffer &OB) const override {
    if (Printing)
      return false;
    ScopedOverride<bool> SavePrinting(Printing, true);
    return Ref->hasArray(OB);
  }
  /// Slow path for hasFunction when the cache is Unknown.
  /// \param OB Destination demangle output buffer.
  /// \return True if this node is a (possibly qualified) function type.
  bool hasFunctionSlow(OutputBuffer &OB) const override {
    if (Printing)
      return false;
    ScopedOverride<bool> SavePrinting(Printing, true);
    return Ref->hasFunction(OB);
  }
  /// Return the concrete syntax node under glue wrappers.
  /// \param OB Destination demangle output buffer.
  /// \return The concrete syntax node under glue wrappers.
  const Node *getSyntaxNode(OutputBuffer &OB) const override {
    if (Printing)
      return this;
    ScopedOverride<bool> SavePrinting(Printing, true);
    return Ref->getSyntaxNode(OB);
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    if (Printing)
      return;
    ScopedOverride<bool> SavePrinting(Printing, true);
    OB.printLeft(*Ref);
  }
  /// Print the right-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printRight(OutputBuffer &OB) const override {
    if (Printing)
      return;
    ScopedOverride<bool> SavePrinting(Printing, true);
    OB.printRight(*Ref);
  }
};

/// Name with attached template arguments.
struct NameWithTemplateArgs : Node {
  // name<template_args>
  /// Base name before template arguments.
  Node *Name;
  /// Template argument list node.
  Node *TemplateArgs;

  /// Construct a NameWithTemplateArgs node.
  /// \param Name_ Name node.
  /// \param TemplateArgs_ The template args.
  NameWithTemplateArgs(Node *Name_, Node *TemplateArgs_)
      : Node(KNameWithTemplateArgs), Name(Name_), TemplateArgs(TemplateArgs_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Name, TemplateArgs); }

  /// Return the base name.
  /// \return The base identifier spelling for this name node.
  std::string_view getBaseName() const override { return Name->getBaseName(); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Name->print(OB);
    TemplateArgs->print(OB);
  }
};

/// Name explicitly rooted in the global namespace.
class GlobalQualifiedName final : public Node {
  Node *Child;

public:
  /// Construct a GlobalQualifiedName node.
  /// \param Child_ Child AST node.
  GlobalQualifiedName(Node* Child_)
      : Node(KGlobalQualifiedName), Child(Child_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Child); }

  /// Return the base name.
  /// \return The base identifier spelling for this name node.
  std::string_view getBaseName() const override { return Child->getBaseName(); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "::";
    Child->print(OB);
  }
};

/// Kind of Itanium special substitution for common std entities.
enum class SpecialSubKind {
  /// std::allocator.
  allocator,
  /// std::basic_string.
  basic_string,
  /// std::string.
  string,
  /// std::istream.
  istream,
  /// std::ostream.
  ostream,
  /// std::iostream.
  iostream,
};

class SpecialSubstitution;
/// Expanded spelling of a special substitution.
class ExpandedSpecialSubstitution : public Node {
protected:
  /// Which special substitution is expanded.
  SpecialSubKind SSK;

  /// Construct a ExpandedSpecialSubstitution node.
  /// \param SSK_ Which special substitution this node represents.
  /// \param K_ Concrete AST node kind.
  ExpandedSpecialSubstitution(SpecialSubKind SSK_, Kind K_)
      : Node(K_), SSK(SSK_) {}
public:
  /// Construct a ExpandedSpecialSubstitution node.
  /// \param SSK_ Which special substitution this node represents.
  ExpandedSpecialSubstitution(SpecialSubKind SSK_)
      : ExpandedSpecialSubstitution(SSK_, KExpandedSpecialSubstitution) {}
  inline ExpandedSpecialSubstitution(SpecialSubstitution const *);

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(SSK); }

protected:
  /// Return true if this special substitution is a class-template instantiation.
  /// \return True if this special substitution is a class-template instantiation.
  bool isInstantiation() const {
    return unsigned(SSK) >= unsigned(SpecialSubKind::string);
  }

  /// Return the base name.
  /// \return The base identifier spelling for this name node.
  std::string_view getBaseName() const override {
    switch (SSK) {
    case SpecialSubKind::allocator:
      return {"allocator"};
    case SpecialSubKind::basic_string:
      return {"basic_string"};
    case SpecialSubKind::string:
      return {"basic_string"};
    case SpecialSubKind::istream:
      return {"basic_istream"};
    case SpecialSubKind::ostream:
      return {"basic_ostream"};
    case SpecialSubKind::iostream:
      return {"basic_iostream"};
    }
    DEMANGLE_UNREACHABLE;
  }

private:
  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB << "std::" << getBaseName();
    if (isInstantiation()) {
      OB << "<char, std::char_traits<char>";
      if (SSK == SpecialSubKind::string)
        OB << ", std::allocator<char>";
      OB << ">";
    }
  }
};

/// Compressed special substitution (std::string, etc.).
class SpecialSubstitution final : public ExpandedSpecialSubstitution {
public:
  /// Construct a SpecialSubstitution node.
  /// \param SSK_ Which special substitution this node represents.
  SpecialSubstitution(SpecialSubKind SSK_)
      : ExpandedSpecialSubstitution(SSK_, KSpecialSubstitution) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(SSK); }

  /// Return the base name.
  /// \return The base identifier spelling for this name node.
  std::string_view getBaseName() const override {
    std::string_view SV = ExpandedSpecialSubstitution::getBaseName();
    if (isInstantiation()) {
      // The instantiations are typedefs that drop the "basic_" prefix.
      DEMANGLE_ASSERT(starts_with(SV, "basic_"), "");
      SV.remove_prefix(sizeof("basic_") - 1);
    }
    return SV;
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB << "std::" << getBaseName();
  }
};

/// Construct an ExpandedSpecialSubstitution from a SpecialSubstitution.
/// \param SS The special substitution whose kind is expanded.
inline ExpandedSpecialSubstitution::ExpandedSpecialSubstitution(
    SpecialSubstitution const *SS)
    : ExpandedSpecialSubstitution(SS->SSK) {}

/// Constructor or destructor name.
class CtorDtorName final : public Node {
  const Node *Basename;
  const bool IsDtor;
  const int Variant;

public:
  /// Construct a CtorDtorName node.
  /// \param Basename_ The basename.
  /// \param IsDtor_ The is dtor.
  /// \param Variant_ The variant.
  CtorDtorName(const Node *Basename_, bool IsDtor_, int Variant_)
      : Node(KCtorDtorName), Basename(Basename_), IsDtor(IsDtor_),
        Variant(Variant_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Basename, IsDtor, Variant); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    if (IsDtor)
      OB += "~";
    OB += Basename->getBaseName();
  }
};

/// Destructor name derived from a type.
class DtorName : public Node {
  const Node *Base;

public:
  /// Construct a DtorName node.
  /// \param Base_ Base node.
  DtorName(const Node *Base_) : Node(KDtorName), Base(Base_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Base); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "~";
    OB.printLeft(*Base);
  }
};

/// Unnamed local type with a discriminator.
class UnnamedTypeName : public Node {
  const std::string_view Count;

public:
  /// Construct a UnnamedTypeName node.
  /// \param Count_ The count.
  UnnamedTypeName(std::string_view Count_)
      : Node(KUnnamedTypeName), Count(Count_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Count); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "'unnamed";
    OB += Count;
    OB += "\'";
  }
};

/// Closure / lambda type name.
class ClosureTypeName : public Node {
  NodeArray TemplateParams;
  const Node *Requires1;
  NodeArray Params;
  const Node *Requires2;
  std::string_view Count;

public:
  /// Construct a ClosureTypeName node.
  /// \param TemplateParams_ The template params.
  /// \param Requires1_ The requires1.
  /// \param Params_ The params.
  /// \param Requires2_ The requires2.
  /// \param Count_ The count.
  ClosureTypeName(NodeArray TemplateParams_, const Node *Requires1_,
                  NodeArray Params_, const Node *Requires2_,
                  std::string_view Count_)
      : Node(KClosureTypeName), TemplateParams(TemplateParams_),
        Requires1(Requires1_), Params(Params_), Requires2(Requires2_),
        Count(Count_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const {
    F(TemplateParams, Requires1, Params, Requires2, Count);
  }

  /// Print the lambda declarator portion into \p OB.
  /// \param OB Destination demangle output buffer.
  void printDeclarator(OutputBuffer &OB) const {
    if (!TemplateParams.empty()) {
      ScopedOverride<bool> LT(OB.TemplateTracker.InsideTemplate, true);
      OB += "<";
      TemplateParams.printWithComma(OB);
      OB += ">";
    }
    if (Requires1 != nullptr) {
      OB += " requires ";
      Requires1->print(OB);
      OB += " ";
    }
    OB.printOpen();
    Params.printWithComma(OB);
    OB.printClose();
    if (Requires2 != nullptr) {
      OB += " requires ";
      Requires2->print(OB);
    }
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    // FIXME: This demangling is not particularly readable.
    OB += "\'lambda";
    OB += Count;
    OB += "\'";
    /// Print the lambda declarator portion into \p OB.
    /// \param OB Destination demangle output buffer.
    printDeclarator(OB);
  }
};

/// Structured binding declaration name.
class StructuredBindingName : public Node {
  NodeArray Bindings;
public:
  /// Construct a StructuredBindingName node.
  /// \param Bindings_ The bindings.
  StructuredBindingName(NodeArray Bindings_)
      : Node(KStructuredBindingName), Bindings(Bindings_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Bindings); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB.printOpen('[');
    Bindings.printWithComma(OB);
    OB.printClose(']');
  }
};

// -- Expression Nodes --

/// Binary operator expression.
class BinaryExpr : public Node {
  const Node *LHS;
  const std::string_view InfixOperator;
  const Node *RHS;

public:
  /// Construct a BinaryExpr node.
  /// \param LHS_ The lhs.
  /// \param InfixOperator_ The infix operator.
  /// \param RHS_ The rhs.
  /// \param Prec_ The prec.
  BinaryExpr(const Node *LHS_, std::string_view InfixOperator_,
             const Node *RHS_, Prec Prec_)
      : Node(KBinaryExpr, Prec_), LHS(LHS_), InfixOperator(InfixOperator_),
        RHS(RHS_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(LHS, InfixOperator, RHS, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    // If we're printing a '<' inside of a template argument, and we haven't
    // yet parenthesized the expression, do so now.
    bool ParenAll = !OB.isInParensInTemplateArgs() &&
                    (InfixOperator == ">" || InfixOperator == ">>");
    if (ParenAll)
      OB.printOpen();
    // Assignment is right associative, with special LHS precedence.
    bool IsAssign = getPrecedence() == Prec::Assign;
    LHS->printAsOperand(OB, IsAssign ? Prec::OrIf : getPrecedence(), !IsAssign);
    // No space before comma operator
    if (!(InfixOperator == ","))
      OB += " ";
    OB += InfixOperator;
    OB += " ";
    RHS->printAsOperand(OB, getPrecedence(), IsAssign);
    if (ParenAll)
      OB.printClose();
  }
};

/// Array subscript expression.
class ArraySubscriptExpr : public Node {
  const Node *Op1;
  const Node *Op2;

public:
  /// Construct a ArraySubscriptExpr node.
  /// \param Op1_ Left-hand subscript operand.
  /// \param Op2_ Index expression.
  /// \param Prec_ Expression precedence.
  ArraySubscriptExpr(const Node *Op1_, const Node *Op2_, Prec Prec_)
      : Node(KArraySubscriptExpr, Prec_), Op1(Op1_), Op2(Op2_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(Op1, Op2, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Op1->printAsOperand(OB, getPrecedence());
    OB.printOpen('[');
    Op2->printAsOperand(OB);
    OB.printClose(']');
  }
};

/// Postfix unary operator expression.
class PostfixExpr : public Node {
  const Node *Child;
  const std::string_view Operator;

public:
  /// Construct a PostfixExpr node.
  /// \param Child_ Child AST node.
  /// \param Operator_ Postfix operator spelling.
  /// \param Prec_ Expression precedence.
  PostfixExpr(const Node *Child_, std::string_view Operator_, Prec Prec_)
      : Node(KPostfixExpr, Prec_), Child(Child_), Operator(Operator_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(Child, Operator, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Child->printAsOperand(OB, getPrecedence(), true);
    OB += Operator;
  }
};

/// Conditional (ternary) expression.
class ConditionalExpr : public Node {
  const Node *Cond;
  const Node *Then;
  const Node *Else;

public:
  /// Construct a ConditionalExpr node.
  /// \param Cond_ Condition expression.
  /// \param Then_ Then-branch expression.
  /// \param Else_ Else-branch expression.
  /// \param Prec_ Expression precedence.
  ConditionalExpr(const Node *Cond_, const Node *Then_, const Node *Else_,
                  Prec Prec_)
      : Node(KConditionalExpr, Prec_), Cond(Cond_), Then(Then_), Else(Else_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(Cond, Then, Else, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    Cond->printAsOperand(OB, getPrecedence());
    OB += " ? ";
    Then->printAsOperand(OB);
    OB += " : ";
    Else->printAsOperand(OB, Prec::Assign, true);
  }
};

/// Member access expression.
class MemberExpr : public Node {
  const Node *LHS;
  const std::string_view Kind;
  const Node *RHS;

public:
  /// Construct a MemberExpr node.
  /// \param LHS_ Left-hand object expression.
  /// \param Kind_ Member-access operator spelling.
  /// \param RHS_ Right-hand member name.
  /// \param Prec_ Expression precedence.
  MemberExpr(const Node *LHS_, std::string_view Kind_, const Node *RHS_,
             Prec Prec_)
      : Node(KMemberExpr, Prec_), LHS(LHS_), Kind(Kind_), RHS(RHS_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(LHS, Kind, RHS, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    LHS->printAsOperand(OB, getPrecedence(), true);
    OB += Kind;
    RHS->printAsOperand(OB, getPrecedence(), false);
  }
};

/// Subobject expression with optional union selectors.
class SubobjectExpr : public Node {
  const Node *Type;
  const Node *SubExpr;
  std::string_view Offset;
  NodeArray UnionSelectors;
  bool OnePastTheEnd;

public:
  /// Construct a SubobjectExpr node.
  /// \param Type_ The type.
  /// \param SubExpr_ The sub expr.
  /// \param Offset_ The offset.
  /// \param UnionSelectors_ The union selectors.
  /// \param OnePastTheEnd_ The one past the end.
  SubobjectExpr(const Node *Type_, const Node *SubExpr_,
                std::string_view Offset_, NodeArray UnionSelectors_,
                bool OnePastTheEnd_)
      : Node(KSubobjectExpr), Type(Type_), SubExpr(SubExpr_), Offset(Offset_),
        UnionSelectors(UnionSelectors_), OnePastTheEnd(OnePastTheEnd_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const {
    F(Type, SubExpr, Offset, UnionSelectors, OnePastTheEnd);
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    SubExpr->print(OB);
    OB += ".<";
    Type->print(OB);
    OB += " at offset ";
    if (Offset.empty()) {
      OB += "0";
    } else if (Offset[0] == 'n') {
      OB += "-";
      OB += std::string_view(Offset.data() + 1, Offset.size() - 1);
    } else {
      OB += Offset;
    }
    OB += ">";
  }
};

/// Expression wrapped in a named enclosing construct.
class EnclosingExpr : public Node {
  const std::string_view Prefix;
  const Node *Infix;
  const std::string_view Postfix;

public:
  /// Construct a EnclosingExpr node.
  /// \param Prefix_ Text before the enclosed expression.
  /// \param Infix_ Enclosed expression node.
  /// \param Prec_ Expression precedence.
  EnclosingExpr(std::string_view Prefix_, const Node *Infix_,
                Prec Prec_ = Prec::Primary)
      : Node(KEnclosingExpr, Prec_), Prefix(Prefix_), Infix(Infix_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(Prefix, Infix, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += Prefix;
    OB.printOpen();
    Infix->print(OB);
    OB.printClose();
    OB += Postfix;
  }
};

/// Named cast expression (static_cast, etc.).
class CastExpr : public Node {
  // cast_kind<to>(from)
  const std::string_view CastKind;
  const Node *To;
  const Node *From;

public:
  /// Construct a CastExpr node.
  /// \param CastKind_ Cast keyword spelling.
  /// \param To_ Destination type node.
  /// \param From_ Source expression node.
  /// \param Prec_ Expression precedence.
  CastExpr(std::string_view CastKind_, const Node *To_, const Node *From_,
           Prec Prec_)
      : Node(KCastExpr, Prec_), CastKind(CastKind_), To(To_), From(From_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(CastKind, To, From, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += CastKind;
    {
      ScopedOverride<bool> LT(OB.TemplateTracker.InsideTemplate, true);
      OB += "<";
      OB.printLeft(*To);
      OB += ">";
    }
    OB.printOpen();
    From->printAsOperand(OB);
    OB.printClose();
  }
};

/// sizeof...(pack) expression.
class SizeofParamPackExpr : public Node {
  const Node *Pack;

public:
  /// Construct a SizeofParamPackExpr node.
  /// \param Pack_ The pack.
  SizeofParamPackExpr(const Node *Pack_)
      : Node(KSizeofParamPackExpr), Pack(Pack_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Pack); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "sizeof...";
    OB.printOpen();
    ParameterPackExpansion PPE(Pack);
    PPE.printLeft(OB);
    OB.printClose();
  }
};

/// Function call expression.
class CallExpr : public Node {
  const Node *Callee;
  NodeArray Args;
  bool IsParen; // (func)(args ...) ?

public:
  /// Construct a CallExpr node.
  /// \param Callee_ The callee.
  /// \param Args_ The args.
  /// \param IsParen_ The is paren.
  /// \param Prec_ The prec.
  CallExpr(const Node *Callee_, NodeArray Args_, bool IsParen_, Prec Prec_)
      : Node(KCallExpr, Prec_), Callee(Callee_), Args(Args_),
        IsParen(IsParen_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(Callee, Args, IsParen, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    if (IsParen)
      OB.printOpen();
    Callee->print(OB);
    if (IsParen)
      OB.printClose();
    OB.printOpen();
    Args.printWithComma(OB);
    OB.printClose();
  }
};

/// new-expression.
class NewExpr : public Node {
  // new (expr_list) type(init_list)
  NodeArray ExprList;
  Node *Type;
  NodeArray InitList;
  bool IsGlobal; // ::operator new ?
  bool IsArray;  // new[] ?
public:
  /// Construct a NewExpr node.
  /// \param ExprList_ The expr list.
  /// \param Type_ The type.
  /// \param InitList_ The init list.
  /// \param IsGlobal_ The is global.
  /// \param IsArray_ The is array.
  /// \param Prec_ The prec.
  NewExpr(NodeArray ExprList_, Node *Type_, NodeArray InitList_, bool IsGlobal_,
          bool IsArray_, Prec Prec_)
      : Node(KNewExpr, Prec_), ExprList(ExprList_), Type(Type_),
        InitList(InitList_), IsGlobal(IsGlobal_), IsArray(IsArray_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const {
    F(ExprList, Type, InitList, IsGlobal, IsArray, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    if (IsGlobal)
      OB += "::";
    OB += "new";
    if (IsArray)
      OB += "[]";
    if (!ExprList.empty()) {
      OB.printOpen();
      ExprList.printWithComma(OB);
      OB.printClose();
    }
    OB += " ";
    Type->print(OB);
    if (!InitList.empty()) {
      OB.printOpen();
      InitList.printWithComma(OB);
      OB.printClose();
    }
  }
};

/// delete-expression.
class DeleteExpr : public Node {
  Node *Op;
  bool IsGlobal;
  bool IsArray;

public:
  /// Construct a DeleteExpr node.
  /// \param Op_ The op.
  /// \param IsGlobal_ The is global.
  /// \param IsArray_ The is array.
  /// \param Prec_ The prec.
  DeleteExpr(Node *Op_, bool IsGlobal_, bool IsArray_, Prec Prec_)
      : Node(KDeleteExpr, Prec_), Op(Op_), IsGlobal(IsGlobal_),
        IsArray(IsArray_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(Op, IsGlobal, IsArray, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    if (IsGlobal)
      OB += "::";
    OB += "delete";
    if (IsArray)
      OB += "[]";
    OB += ' ';
    Op->print(OB);
  }
};

/// Prefix unary operator expression.
class PrefixExpr : public Node {
  std::string_view Prefix;
  Node *Child;

public:
  /// Construct a PrefixExpr node.
  /// \param Prefix_ Prefix operator spelling.
  /// \param Child_ Child AST node.
  /// \param Prec_ Expression precedence.
  PrefixExpr(std::string_view Prefix_, Node *Child_, Prec Prec_)
      : Node(KPrefixExpr, Prec_), Prefix(Prefix_), Child(Child_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(Prefix, Child, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += Prefix;
    Child->printAsOperand(OB, getPrecedence());
  }
};

/// Function parameter expression (fpT / fp_).
class FunctionParam : public Node {
  std::string_view Number;

public:
  /// Construct a FunctionParam node.
  /// \param Number_ The number.
  FunctionParam(std::string_view Number_)
      : Node(KFunctionParam), Number(Number_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Number); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "fp";
    OB += Number;
  }
};

/// Conversion expression with an operand list.
class ConversionExpr : public Node {
  const Node *Type;
  NodeArray Expressions;

public:
  /// Construct a ConversionExpr node.
  /// \param Type_ Destination type node.
  /// \param Expressions_ Conversion operand expressions.
  /// \param Prec_ Expression precedence.
  ConversionExpr(const Node *Type_, NodeArray Expressions_, Prec Prec_)
      : Node(KConversionExpr, Prec_), Type(Type_), Expressions(Expressions_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(Type, Expressions, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB.printOpen();
    Type->print(OB);
    OB.printClose();
    OB.printOpen();
    Expressions.printWithComma(OB);
    OB.printClose();
  }
};

/// Pointer-to-member conversion expression.
class PointerToMemberConversionExpr : public Node {
  const Node *Type;
  const Node *SubExpr;
  std::string_view Offset;

public:
  /// Construct a PointerToMemberConversionExpr node.
  /// \param Type_ The type.
  /// \param SubExpr_ The sub expr.
  /// \param Offset_ The offset.
  /// \param Prec_ The prec.
  PointerToMemberConversionExpr(const Node *Type_, const Node *SubExpr_,
                                std::string_view Offset_, Prec Prec_)
      : Node(KPointerToMemberConversionExpr, Prec_), Type(Type_),
        SubExpr(SubExpr_), Offset(Offset_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(Type, SubExpr, Offset, getPrecedence());
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB.printOpen();
    Type->print(OB);
    OB.printClose();
    OB.printOpen();
    SubExpr->print(OB);
    OB.printClose();
  }
};

/// Initializer-list expression.
class InitListExpr : public Node {
  const Node *Ty;
  NodeArray Inits;
public:
  /// Construct a InitListExpr node.
  /// \param Ty_ Pointee or underlying type node.
  /// \param Inits_ The inits.
  InitListExpr(const Node *Ty_, NodeArray Inits_)
      : Node(KInitListExpr), Ty(Ty_), Inits(Inits_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Ty, Inits); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    if (Ty) {
      if (Ty->printInitListAsType(OB, Inits))
        return;
      Ty->print(OB);
    }
    OB += '{';
    Inits.printWithComma(OB);
    OB += '}';
  }
};

/// Braced field initializer expression.
class BracedExpr : public Node {
  const Node *Elem;
  const Node *Init;
  bool IsArray;
public:
  /// Construct a BracedExpr node.
  /// \param Elem_ The elem.
  /// \param Init_ The init.
  /// \param IsArray_ The is array.
  BracedExpr(const Node *Elem_, const Node *Init_, bool IsArray_)
      : Node(KBracedExpr), Elem(Elem_), Init(Init_), IsArray(IsArray_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Elem, Init, IsArray); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    if (IsArray) {
      OB += '[';
      Elem->print(OB);
      OB += ']';
    } else {
      OB += '.';
      Elem->print(OB);
    }
    if (Init->getKind() != KBracedExpr && Init->getKind() != KBracedRangeExpr)
      OB += " = ";
    Init->print(OB);
  }
};

/// Braced range (.field = [a ... b] = x) expression.
class BracedRangeExpr : public Node {
  const Node *First;
  const Node *Last;
  const Node *Init;
public:
  /// Construct a BracedRangeExpr node.
  /// \param First_ Start of mangled input.
  /// \param Last_ End of mangled input.
  /// \param Init_ The init.
  BracedRangeExpr(const Node *First_, const Node *Last_, const Node *Init_)
      : Node(KBracedRangeExpr), First(First_), Last(Last_), Init(Init_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(First, Last, Init); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += '[';
    First->print(OB);
    OB += " ... ";
    Last->print(OB);
    OB += ']';
    if (Init->getKind() != KBracedExpr && Init->getKind() != KBracedRangeExpr)
      OB += " = ";
    Init->print(OB);
  }
};

/// C++17 fold expression.
class FoldExpr : public Node {
  const Node *Pack, *Init;
  std::string_view OperatorName;
  bool IsLeftFold;

public:
  /// Construct a FoldExpr node.
  /// \param IsLeftFold_ The is left fold.
  /// \param OperatorName_ The operator name.
  /// \param Pack_ The pack.
  /// \param Init_ The init.
  FoldExpr(bool IsLeftFold_, std::string_view OperatorName_, const Node *Pack_,
           const Node *Init_)
      : Node(KFoldExpr), Pack(Pack_), Init(Init_), OperatorName(OperatorName_),
        IsLeftFold(IsLeftFold_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const {
    F(IsLeftFold, OperatorName, Pack, Init);
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    auto PrintPack = [&] {
      OB.printOpen();
      ParameterPackExpansion(Pack).print(OB);
      OB.printClose();
    };

    OB.printOpen();
    // Either '[init op ]... op pack' or 'pack op ...[ op init]'
    // Refactored to '[(init|pack) op ]...[ op (pack|init)]'
    // Fold expr operands are cast-expressions
    if (!IsLeftFold || Init != nullptr) {
      // '(init|pack) op '
      if (IsLeftFold)
        Init->printAsOperand(OB, Prec::Cast, true);
      else
        PrintPack();
      OB << " " << OperatorName << " ";
    }
    OB << "...";
    if (IsLeftFold || Init != nullptr) {
      // ' op (init|pack)'
      OB << " " << OperatorName << " ";
      if (IsLeftFold)
        PrintPack();
      else
        Init->printAsOperand(OB, Prec::Cast, true);
    }
    OB.printClose();
  }
};

/// throw-expression.
class ThrowExpr : public Node {
  const Node *Op;

public:
  /// Construct a ThrowExpr node.
  /// \param Op_ The op.
  ThrowExpr(const Node *Op_) : Node(KThrowExpr), Op(Op_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Op); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "throw ";
    Op->print(OB);
  }
};

/// Boolean literal expression.
class BoolExpr : public Node {
  bool Value;

public:
  /// Construct a BoolExpr node.
  /// \param Value_ The value.
  BoolExpr(bool Value_) : Node(KBoolExpr), Value(Value_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Value); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += Value ? std::string_view("true") : std::string_view("false");
  }
};

/// String literal expression.
class StringLiteral : public Node {
  const Node *Type;

public:
  /// Construct a StringLiteral node.
  /// \param Type_ The type.
  StringLiteral(const Node *Type_) : Node(KStringLiteral), Type(Type_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Type); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "\"<";
    Type->print(OB);
    OB += ">\"";
  }
};

/// Lambda expression type sugar.
class LambdaExpr : public Node {
  const Node *Type;

public:
  /// Construct a LambdaExpr node.
  /// \param Type_ The type.
  LambdaExpr(const Node *Type_) : Node(KLambdaExpr), Type(Type_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Type); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "[]";
    if (Type->getKind() == KClosureTypeName)
      /// Print the lambda declarator portion into \p OB.
      /// \param Type The type.
      static_cast<const ClosureTypeName *>(Type)->printDeclarator(OB);
    OB += "{...}";
  }
};

/// Enumerated literal expression.
class EnumLiteral : public Node {
  // ty(integer)
  const Node *Ty;
  std::string_view Integer;

public:
  /// Construct a EnumLiteral node.
  /// \param Ty_ Pointee or underlying type node.
  /// \param Integer_ The integer.
  EnumLiteral(const Node *Ty_, std::string_view Integer_)
      : Node(KEnumLiteral), Ty(Ty_), Integer(Integer_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Ty, Integer); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB.printOpen();
    Ty->print(OB);
    OB.printClose();

    if (Integer[0] == 'n')
      OB << '-' << std::string_view(Integer.data() + 1, Integer.size() - 1);
    else
      OB << Integer;
  }
};

/// Integer literal expression.
class IntegerLiteral : public Node {
  std::string_view Type;
  std::string_view Value;

public:
  /// Construct a IntegerLiteral node.
  /// \param Type_ The type.
  /// \param Value_ The value.
  IntegerLiteral(std::string_view Type_, std::string_view Value_)
      : Node(KIntegerLiteral), Type(Type_), Value(Value_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Type, Value); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    if (Type.size() > 3) {
      OB.printOpen();
      OB += Type;
      OB.printClose();
    }

    if (Value[0] == 'n')
      OB << '-' << std::string_view(Value.data() + 1, Value.size() - 1);
    else
      OB += Value;

    if (Type.size() <= 3)
      OB += Type;
  }

  /// Return the integer literal spelling.
  /// \return The integer literal spelling.
  std::string_view value() const { return Value; }
};

/// requires-expression.
class RequiresExpr : public Node {
  NodeArray Parameters;
  NodeArray Requirements;
public:
  /// Construct a RequiresExpr node.
  /// \param Parameters_ The parameters.
  /// \param Requirements_ The requirements.
  RequiresExpr(NodeArray Parameters_, NodeArray Requirements_)
      : Node(KRequiresExpr), Parameters(Parameters_),
        Requirements(Requirements_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Parameters, Requirements); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += "requires";
    if (!Parameters.empty()) {
      OB += ' ';
      OB.printOpen();
      Parameters.printWithComma(OB);
      OB.printClose();
    }
    OB += ' ';
    OB.printOpen('{');
    for (const Node *Req : Requirements) {
      Req->print(OB);
    }
    OB += ' ';
    OB.printClose('}');
  }
};

/// Expression requirement in a requires-expression.
class ExprRequirement : public Node {
  const Node *Expr;
  bool IsNoexcept;
  const Node *TypeConstraint;
public:
  /// Construct a ExprRequirement node.
  /// \param Expr_ The expr.
  /// \param IsNoexcept_ The is noexcept.
  /// \param TypeConstraint_ The type constraint.
  ExprRequirement(const Node *Expr_, bool IsNoexcept_,
                  const Node *TypeConstraint_)
      : Node(KExprRequirement), Expr(Expr_), IsNoexcept(IsNoexcept_),
        TypeConstraint(TypeConstraint_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const {
    F(Expr, IsNoexcept, TypeConstraint);
  }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += " ";
    if (IsNoexcept || TypeConstraint)
      OB.printOpen('{');
    Expr->print(OB);
    if (IsNoexcept || TypeConstraint)
      OB.printClose('}');
    if (IsNoexcept)
      OB += " noexcept";
    if (TypeConstraint) {
      OB += " -> ";
      TypeConstraint->print(OB);
    }
    OB += ';';
  }
};

/// Type requirement in a requires-expression.
class TypeRequirement : public Node {
  const Node *Type;
public:
  /// Construct a TypeRequirement node.
  /// \param Type_ The type.
  TypeRequirement(const Node *Type_)
      : Node(KTypeRequirement), Type(Type_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const { F(Type); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += " typename ";
    Type->print(OB);
    OB += ';';
  }
};

/// Nested requirement in a requires-expression.
class NestedRequirement : public Node {
  const Node *Constraint;
public:
  /// Construct a NestedRequirement node.
  /// \param Constraint_ Constraint expression node.
  NestedRequirement(const Node *Constraint_)
      : Node(KNestedRequirement), Constraint(Constraint_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template <typename Fn> void match(Fn F) const { F(Constraint); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    OB += " requires ";
    Constraint->print(OB);
    OB += ';';
  }
};

/// Traits describing how a floating literal of type Float is mangled.
template <class Float> struct FloatData;

/// Helpers selecting the AST node kind for a floating literal type.
namespace float_literal_impl {
/// Return the node kind for a float literal.
/// \param unused Tag pointer selecting the float overload.
/// \return The AST node kind for the floating literal type.
constexpr Node::Kind getFloatLiteralKind(float *unused) {
  return Node::KFloatLiteral;
}
/// Return the node kind for a double literal.
/// \param unused Tag pointer selecting the double overload.
/// \return The AST node kind for the floating literal type.
constexpr Node::Kind getFloatLiteralKind(double *unused) {
  return Node::KDoubleLiteral;
}
/// Return the node kind for a long double literal.
/// \param unused Tag pointer selecting the long double overload.
/// \return The AST node kind for the floating literal type.
constexpr Node::Kind getFloatLiteralKind(long double *unused) {
  return Node::KLongDoubleLiteral;
}
}

/// Floating-point literal AST node for type \p Float.
template <class Float> class FloatLiteralImpl : public Node {
  const std::string_view Contents;

  static constexpr Kind KindForClass =
      float_literal_impl::getFloatLiteralKind((Float *)nullptr);

public:
  /// Construct a FloatLiteralImpl node.
  /// \param Contents_ Mangled literal contents.
  FloatLiteralImpl(std::string_view Contents_)
      : Node(KindForClass), Contents(Contents_) {}

  /// Invoke \p F with the constructor arguments that rebuild this node.
  /// \param F Callable receiving the matched constructor arguments.
  template<typename Fn> void match(Fn F) const { F(Contents); }

  /// Print the left-hand portion of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  void printLeft(OutputBuffer &OB) const override {
    const size_t N = FloatData<Float>::mangled_size;
    if (Contents.size() >= N) {
      union {
        Float value;
        char buf[sizeof(Float)];
      };
      const char *t = Contents.data();
      const char *last = t + N;
      char *e = buf;
      for (; t != last; ++t, ++e) {
        unsigned d1 = isdigit(*t) ? static_cast<unsigned>(*t - '0')
                                  : static_cast<unsigned>(*t - 'a' + 10);
        ++t;
        unsigned d0 = isdigit(*t) ? static_cast<unsigned>(*t - '0')
                                  : static_cast<unsigned>(*t - 'a' + 10);
        *e = static_cast<char>((d1 << 4) + d0);
      }
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      std::reverse(buf, e);
#endif
      char num[FloatData<Float>::max_demangled_size] = {0};
      int n = snprintf(num, sizeof(num), FloatData<Float>::spec, value);
      OB += std::string_view(num, n);
    }
  }
};

/// float floating literal node.
using FloatLiteral = FloatLiteralImpl<float>;
/// double floating literal node.
using DoubleLiteral = FloatLiteralImpl<double>;
/// long double floating literal node.
using LongDoubleLiteral = FloatLiteralImpl<long double>;

/// Visit the node. Calls \c F(P), where \c P is the node cast to the
/// appropriate derived class.
/// \param F Callable invoked with the node cast to its derived type.
template<typename Fn>
void Node::visit(Fn F) const {
  switch (K) {
#define NODE(X)                                                                \
  case K##X:                                                                   \
    return F(static_cast<const X *>(this));
#include "ItaniumNodes.def"
  }
  DEMANGLE_ASSERT(0, "unknown mangling node kind");
}

/// Determine the kind of a node from its type.
template<typename NodeT> struct NodeKind;
#define NODE(X)                                                                \
  template <> struct NodeKind<X> {                                             \
    static constexpr Node::Kind Kind = Node::K##X;                             \
    static constexpr const char *name() { return #X; }                         \
  };
#include "ItaniumNodes.def"

/// Print an array of integer literals as a string literal.
/// \param OB Destination demangle output buffer.
/// \return true if a string literal was printed.
inline bool NodeArray::printAsString(OutputBuffer &OB) const {
  auto StartPos = OB.getCurrentPosition();
  auto Fail = [&OB, StartPos] {
    OB.setCurrentPosition(StartPos);
    return false;
  };

  OB += '"';
  bool LastWasNumericEscape = false;
  for (const Node *Element : *this) {
    if (Element->getKind() != Node::KIntegerLiteral)
      return Fail();
    int integer_value = 0;
    for (char c : static_cast<const IntegerLiteral *>(Element)->value()) {
      if (c < '0' || c > '9' || integer_value > 25)
        return Fail();
      integer_value *= 10;
      integer_value += c - '0';
    }
    if (integer_value > 255)
      return Fail();

    // Insert a `""` to avoid accidentally extending a numeric escape.
    if (LastWasNumericEscape) {
      if ((integer_value >= '0' && integer_value <= '9') ||
          (integer_value >= 'a' && integer_value <= 'f') ||
          (integer_value >= 'A' && integer_value <= 'F')) {
        OB += "\"\"";
      }
    }

    LastWasNumericEscape = false;

    // Determine how to print this character.
    switch (integer_value) {
    case '\a':
      OB += "\\a";
      break;
    case '\b':
      OB += "\\b";
      break;
    case '\f':
      OB += "\\f";
      break;
    case '\n':
      OB += "\\n";
      break;
    case '\r':
      OB += "\\r";
      break;
    case '\t':
      OB += "\\t";
      break;
    case '\v':
      OB += "\\v";
      break;

    case '"':
      OB += "\\\"";
      break;
    case '\\':
      OB += "\\\\";
      break;

    default:
      // We assume that the character is ASCII, and use a numeric escape for all
      // remaining non-printable ASCII characters.
      if (integer_value < 32 || integer_value == 127) {
        constexpr char Hex[] = "0123456789ABCDEF";
        OB += '\\';
        if (integer_value > 7)
          OB += 'x';
        if (integer_value >= 16)
          OB += Hex[integer_value >> 4];
        OB += Hex[integer_value & 0xF];
        LastWasNumericEscape = true;
        break;
      }

      // Assume all remaining characters are directly printable.
      OB += (char)integer_value;
      break;
    }
  }
  OB += '"';
  return true;
}

/// Recursive-descent parser for Itanium mangled names.
template <typename Derived, typename Alloc> struct AbstractManglingParser {
  /// Start of the remaining mangled input.
  const char *First;
  /// End of the mangled input.
  const char *Last;

  /// Temporary name stack used while building the AST.
  ///
  /// The parser collapses multiple names into new nodes to construct the AST.
  /// Once the parser is finished, Names.size() == 1.
  PODSmallVector<Node *, 32> Names;

  /// Substitution table for compressed Itanium name references.
  ///
  /// The string "S42_" refers to the 44th entry (base-36) in this table.
  PODSmallVector<Node *, 32> Subs;

  /// List of template argument values for a template parameter list.
  using TemplateParamList = PODSmallVector<Node *, 8>;

  /// RAII helper that pushes a nested template parameter list.
  class ScopedTemplateParamList {
    AbstractManglingParser *Parser;
    size_t OldNumTemplateParamLists;
    TemplateParamList Params;

  public:
    /// Push a new template parameter list onto \p TheParser.
    /// \param TheParser Parser whose TemplateParams stack is extended.
    ScopedTemplateParamList(AbstractManglingParser *TheParser)
        : Parser(TheParser),
          OldNumTemplateParamLists(TheParser->TemplateParams.size()) {
      Parser->TemplateParams.push_back(&Params);
    }
    /// Pop the template parameter list pushed by the constructor.
    ~ScopedTemplateParamList() {
      DEMANGLE_ASSERT(Parser->TemplateParams.size() >= OldNumTemplateParamLists,
                      "");
      Parser->TemplateParams.shrinkToSize(OldNumTemplateParamLists);
    }
    /// Return the scoped template parameter list.
    /// \return Pointer to the scoped template parameter list.
    TemplateParamList *params() { return &Params; }
  };

  /// Innermost / outer template parameter argument table (T_, T0_, ...).
  ///
  /// Referenced like "T42_". Smaller than Subs/Names so it can live on the
  /// stack.
  TemplateParamList OuterTemplateParams;

  /// Template parameter lists indexed by template parameter depth (TL2_4_).
  ///
  /// If nonempty, element 0 is always OuterTemplateParams; inner elements are
  /// always template parameter lists of lambda expressions. For a generic
  /// lambda with no explicit template parameter list, the corresponding
  /// parameter list pointer will be null.
  PODSmallVector<TemplateParamList *, 4> TemplateParams;

  /// RAII helper that saves and clears template parameter tables.
  class SaveTemplateParams {
    AbstractManglingParser *Parser;
    decltype(TemplateParams) OldParams;
    decltype(OuterTemplateParams) OldOuterParams;

  public:
    /// Save \p TheParser's template parameter tables and clear them.
    /// \param TheParser Parser whose template parameter state is saved.
    SaveTemplateParams(AbstractManglingParser *TheParser) : Parser(TheParser) {
      OldParams = std::move(Parser->TemplateParams);
      OldOuterParams = std::move(Parser->OuterTemplateParams);
      Parser->TemplateParams.clear();
      Parser->OuterTemplateParams.clear();
    }
    /// Restore the saved template parameter tables.
    ~SaveTemplateParams() {
      Parser->TemplateParams = std::move(OldParams);
      Parser->OuterTemplateParams = std::move(OldOuterParams);
    }
  };

  /// Unresolved forward <template-param> references.
  ///
  /// These can occur in a conversion operator's type, and are resolved in the
  /// enclosing <encoding>.
  PODSmallVector<ForwardTemplateReference *, 4> ForwardTemplateRefs;

  /// When true, attempt to parse <template-args> after names.
  bool TryToParseTemplateArgs = true;
  /// When true, allow forward references to template parameters.
  bool PermitForwardTemplateReferences = false;
  /// When true, template parameter tracking may be incomplete.
  bool HasIncompleteTemplateParameterTracking = false;
  /// Depth at which lambda parameters are currently being parsed, or -1.
  size_t ParsingLambdaParamsAtLevel = (size_t)-1;

  /// Counts of invented template parameters by TemplateParamKind.
  unsigned NumSyntheticTemplateParameters[3] = {};

  /// Allocator used for AST nodes.
  Alloc ASTAllocator;

  /// Construct a parser over mangled input [\p First_, \p Last_).
  /// \param First_ Start of mangled input.
  /// \param Last_ End of mangled input.
  AbstractManglingParser(const char *First_, const char *Last_)
      : First(First_), Last(Last_) {}

  /// Return this parser cast to the derived CRTP type.
  /// \return This parser cast to the derived CRTP type.
  Derived &getDerived() { return static_cast<Derived &>(*this); }

  /// Reset the parser to mangled input [\p First_, \p Last_).
  /// \param First_ Start of mangled input.
  /// \param Last_ End of mangled input.
  void reset(const char *First_, const char *Last_) {
    First = First_;
    Last = Last_;
    Names.clear();
    Subs.clear();
    TemplateParams.clear();
    ParsingLambdaParamsAtLevel = (size_t)-1;
    TryToParseTemplateArgs = true;
    PermitForwardTemplateReferences = false;
    for (unsigned int & NumSyntheticTemplateParameter : NumSyntheticTemplateParameters)
      NumSyntheticTemplateParameter = 0;
    ASTAllocator.reset();
  }

  /// Allocate a node of type \p T with constructor arguments \p args.
  /// \param args Constructor arguments forwarded to T.
  /// \return Pointer to the newly allocated node.
  template <class T, class... Args> Node *make(Args &&... args) {
    return ASTAllocator.template makeNode<T>(std::forward<Args>(args)...);
  }

  /// Copy the iterator range \p begin..\p end into allocator-owned storage.
  /// \param begin Start iterator over Node*.
  /// \param end End iterator over Node*.
  /// \return A NodeArray owning a copy of the range.
  template <class It> NodeArray makeNodeArray(It begin, It end) {
    size_t sz = static_cast<size_t>(end - begin);
    void *mem = ASTAllocator.allocateNodeArray(sz);
    Node **data = new (mem) Node *[sz];
    std::copy(begin, end, data);
    return NodeArray(data, sz);
  }

  /// Pop names pushed since \p FromPosition into a NodeArray.
  /// \param FromPosition Baseline Names size before the trailing nodes.
  /// \return A NodeArray of the popped trailing names.
  NodeArray popTrailingNodeArray(size_t FromPosition) {
    DEMANGLE_ASSERT(FromPosition <= Names.size(), "");
    NodeArray res =
        makeNodeArray(Names.begin() + (long)FromPosition, Names.end());
    Names.shrinkToSize(FromPosition);
    return res;
  }

  /// If the next input equals \p S, consume it and return true.
  /// \param S Literal prefix to match.
  /// \return True if the prefix or character was consumed.
  bool consumeIf(std::string_view S) {
    if (starts_with(std::string_view(First, Last - First), S)) {
      First += S.size();
      return true;
    }
    return false;
  }

  /// If the next character equals \p C, consume it and return true.
  /// \param C Character to match.
  /// \return True if the prefix or character was consumed.
  bool consumeIf(char C) {
    if (First != Last && *First == C) {
      ++First;
      return true;
    }
    return false;
  }

  /// Consume and return the next input character.
  /// \return The consumed character, or '\0' if no input remains.
  char consume() { return First != Last ? *First++ : '\0'; }

  /// Peek at the character \p Lookahead positions ahead without consuming.
  /// \param Lookahead Zero-based offset from the current parse position.
  /// \return The character at that position, or '\0' if past the end.
  char look(unsigned Lookahead = 0) const {
    if (static_cast<size_t>(Last - First) <= Lookahead)
      return '\0';
    return First[Lookahead];
  }

  /// Return the number of unparsed input characters remaining.
  /// \return The number of unparsed input characters remaining.
  size_t numLeft() const { return static_cast<size_t>(Last - First); }

  /// Parse an optionally signed number from the input.
  /// \param AllowNegative When true, accept a leading 'n' for negative values.
  /// \return The parsed number spelling as a string view.
  std::string_view parseNumber(bool AllowNegative = false);
  /// Parse CV and restrict qualifiers.
  /// \return The parsed CV and restrict qualifiers.
  Qualifiers parseCVQualifiers();
  /// Parse a positive integer into Out.
  /// \param Out Destination for the parsed integer or sequence id.
  /// \return True on success.
  bool parsePositiveInteger(size_t *Out);
  /// Parse a bare <source-name> spelling.
  /// \return The bare source-name spelling, or empty on failure.
  std::string_view parseBareSourceName();

  /// Parse a <seq-id> production into Out.
  /// \param Out Destination for the parsed integer or sequence id.
  /// \return True on success.
  bool parseSeqId(size_t *Out);
  /// Parse a <substitution> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseSubstitution();
  /// Parse a <template-param> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseTemplateParam();
  /// Parse a <template-param-decl> production.
  /// \param Params Template parameter list being declared into.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseTemplateParamDecl(TemplateParamList *Params);
  /// Parse a <template-args> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseTemplateArgs(bool TagTemplates = false);
  /// Parse a <template-arg> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseTemplateArg();

  /// Return true if the next input begins a <template-param-decl>.
  /// \return True if the next input begins a <template-param-decl>.
  bool isTemplateParamDecl() {
    return look() == 'T' &&
           std::string_view("yptnk").find(look(1)) != std::string_view::npos;
  }

  /// Parse the <expression> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseExpr();
  /// Parse a prefix unary expression.
  /// \param Kind Operator spelling for the expression.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parsePrefixExpr(std::string_view Kind, Node::Prec Prec);
  /// Parse a binary operator expression.
  /// \param Kind Operator spelling for the expression.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseBinaryExpr(std::string_view Kind, Node::Prec Prec);
  /// Parse an integer literal with the given type spelling.
  /// \param Lit Type spelling to emit with the integer literal.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseIntegerLiteral(std::string_view Lit);
  /// Parse an <expr-primary> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseExprPrimary();
  /// Parse a floating-point literal of type Float.
  /// \return The parsed AST node, or nullptr on failure.
  template <class Float> Node *parseFloatingLiteral();
  /// Parse a function parameter expression.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseFunctionParam();
  /// Parse a conversion expression.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseConversionExpr();
  /// Parse a braced initializer expression.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseBracedExpr();
  /// Parse a fold expression.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseFoldExpr();
  /// Parse a pointer-to-member conversion expression.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parsePointerToMemberConversionExpr(Node::Prec Prec);
  /// Parse a subobject expression.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseSubobjectExpr();
  /// Parse a constraint expression.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseConstraintExpr();
  /// Parse a requires-expression.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseRequiresExpr();

  /// Parse the <type> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseType();
  /// Parse a function type.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseFunctionType();
  /// Parse a vector type.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseVectorType();
  /// Parse a decltype type.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseDecltype();
  /// Parse an array type.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseArrayType();
  /// Parse a pointer-to-member type.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parsePointerToMemberType();
  /// Parse a class or enum type.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseClassEnumType();
  /// Parse a qualified type.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseQualifiedType();

  /// Parse an <encoding> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseEncoding(bool ParseParams = true);
  /// Parse a <call-offset> production.
  /// \return True if a <call-offset> was successfully parsed.
  bool parseCallOffset();
  /// Parse a <special-name> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseSpecialName();

  /// Mutable state collected while parsing a <name>.
  ///
  /// Holds extra information about a <name> that is being parsed. This
  /// information is only pertinent if the <name> refers to an <encoding>.
  struct NameState {
    /// True if the name is a ctor, dtor, or conversion operator.
    bool CtorDtorConversion = false;
    /// True if the name ends with template arguments.
    bool EndsWithTemplateArgs = false;
    /// CV-qualifiers accumulated for an encoding name.
    Qualifiers CVQualifiers = QualNone;
    /// Ref-qualifier accumulated for an encoding name.
    FunctionRefQual ReferenceQualifier = FrefQualNone;
    /// Index into ForwardTemplateRefs at the start of this name.
    size_t ForwardTemplateRefsBegin;
    /// True if an explicit object parameter (C++23) was seen.
    bool HasExplicitObjectParameter = false;

    /// Snapshot forward-template-ref baseline from \p Enclosing.
    /// \param Enclosing Parser whose ForwardTemplateRefs size is recorded.
    NameState(AbstractManglingParser *Enclosing)
        : ForwardTemplateRefsBegin(Enclosing->ForwardTemplateRefs.size()) {}
  };

  /// Resolve forward template refs recorded since \p State was constructed.
  /// \param State Name state holding the ForwardTemplateRefs baseline.
  /// \return true on success.
  bool resolveForwardTemplateRefs(NameState &State) {
    size_t I = State.ForwardTemplateRefsBegin;
    size_t E = ForwardTemplateRefs.size();
    for (; I < E; ++I) {
      size_t Idx = ForwardTemplateRefs[I]->Index;
      if (TemplateParams.empty() || !TemplateParams[0] ||
          Idx >= TemplateParams[0]->size())
        return true;
      ForwardTemplateRefs[I]->Ref = (*TemplateParams[0])[Idx];
    }
    ForwardTemplateRefs.shrinkToSize(State.ForwardTemplateRefsBegin);
    return false;
  }

  /// Parse the <name> production.
  /// \param State Optional name-parsing state to update.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseName(NameState *State = nullptr);
  /// Parse a <local-name> production.
  /// \param State Name-parsing state to update.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseLocalName(NameState *State);
  /// Parse an <operator-name> production.
  /// \param State Name-parsing state to update.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseOperatorName(NameState *State);
  /// Parse an optional module name prefix.
  /// \param Module Module name being built.
  /// \return True on success.
  bool parseModuleNameOpt(ModuleName *&Module);
  /// Parse an <unqualified-name> production.
  /// \param State Name-parsing state to update.
  /// \param Scope Enclosing scope node.
  /// \param Module Module name being built.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseUnqualifiedName(NameState *State, Node *Scope, ModuleName *Module);
  /// Parse an <unnamed-type-name> production.
  /// \param State Name-parsing state to update.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseUnnamedTypeName(NameState *State);
  /// Parse a <source-name> production.
  /// \param State Name-parsing state to update.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseSourceName(NameState *State);
  /// Parse an <unscoped-name> production.
  /// \param State Name-parsing state to update.
  /// \param IsSubst Optional out-flag set when a substitution was parsed.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseUnscopedName(NameState *State, bool *IsSubst);
  /// Parse a <nested-name> production.
  /// \param State Name-parsing state to update.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseNestedName(NameState *State);
  /// Parse a constructor or destructor name.
  /// \param SoFar Name parsed so far.
  /// \param State Name-parsing state to update.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseCtorDtorName(Node *&SoFar, NameState *State);

  /// Parse and attach ABI tags to \p N.
  /// \param N Node to extend with ABI tags.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseAbiTags(Node *N);

  /// Table entry describing a mangled operator encoding.
  struct OperatorInfo {
    /// Classification of how the operator is parsed and printed.
    enum OIKind : unsigned char {
      /// Prefix unary: @ expr.
      Prefix,
      /// Postfix unary: expr @.
      Postfix,
      /// Binary: lhs @ rhs.
      Binary,
      /// Array index: lhs [ rhs ].
      Array,
      /// Member access: lhs @ rhs.
      Member,
      /// New-expression operator.
      New,
      /// Delete-expression operator.
      Del,
      /// Function call: expr (expr*).
      Call,
      /// C cast: (type)expr.
      CCast,
      /// Conditional: expr ? expr : expr.
      Conditional,
      /// Overload-only name; not allowed in an expression.
      NameOnly,
      /// Named cast: @<type>(expr).
      NamedCast,
      /// alignof, sizeof, or typeid operator.
      OfIdOp,

      /// First kind without a printable "operator" spelling.
      Unnameable = NamedCast,
    };
    /// Two-character Itanium operator encoding.
    char Enc[2];
    /// Kind of operator.
    OIKind Kind;
    /// Entry-specific flag.
    bool Flag : 1;
    /// Expression precedence.
    Node::Prec Prec : 7;
    /// Operator spelling, including any leading "operator" text.
    const char *Name;

  public:
    /// Construct an operator info table entry.
    /// \param E Two-character encoding plus NUL.
    /// \param K Operator kind.
    /// \param F Entry-specific flag.
    /// \param P Expression precedence.
    /// \param N Operator spelling.
    constexpr OperatorInfo(const char (&E)[3], OIKind K, bool F, Node::Prec P,
                           const char *N)
        : Enc{E[0], E[1]}, Kind{K}, Flag{F}, Prec{P}, Name{N} {}

  public:
    /// Compare encodings so this entry sorts before \p Other.
    /// \param Other Other operator info.
    /// \return True if this entry sorts before the other encoding.
    bool operator<(const OperatorInfo &Other) const {
      return *this < Other.Enc;
    }
    /// Compare this encoding with the two characters at \p Peek.
    /// \param Peek Pointer to a two-character encoding candidate.
    /// \return True if this entry sorts before the other encoding.
    bool operator<(const char *Peek) const {
      return Enc[0] < Peek[0] || (Enc[0] == Peek[0] && Enc[1] < Peek[1]);
    }
    /// Return true if this encoding equals the two characters at \p Peek.
    /// \param Peek Pointer to a two-character encoding candidate.
    /// \return True if the encodings are equal.
    bool operator==(const char *Peek) const {
      return Enc[0] == Peek[0] && Enc[1] == Peek[1];
    }
    /// Return true if this encoding differs from the two characters at \p Peek.
    /// \param Peek Pointer to a two-character encoding candidate.
    /// \return True if the encodings differ.
    bool operator!=(const char *Peek) const { return !this->operator==(Peek); }

  public:
    /// Return the operator symbol without a leading "operator" prefix.
    /// \return The operator symbol without a leading "operator" prefix.
    std::string_view getSymbol() const {
      std::string_view Res = Name;
      if (Kind < Unnameable) {
        DEMANGLE_ASSERT(starts_with(Res, "operator"),
                        "operator name does not start with 'operator'");
        Res.remove_prefix(sizeof("operator") - 1);
        if (starts_with(Res, ' '))
          Res.remove_prefix(1);
      }
      return Res;
    }
    /// Return the full operator spelling.
    /// \return The full operator spelling.
    std::string_view getName() const { return Name; }
    /// Return the operator kind.
    /// \return The operator kind.
    OIKind getKind() const { return Kind; }
    /// Return the entry-specific flag.
    /// \return The entry-specific flag.
    bool getFlag() const { return Flag; }
    /// Return the expression precedence.
    /// \return The expression precedence.
    Node::Prec getPrecedence() const { return Prec; }
  };
  /// Sorted table of operator encodings.
  static const OperatorInfo Ops[];
  /// Number of entries in Ops.
  static const size_t NumOps;
  /// Parse an operator encoding from the input, if present.
  /// \return Pointer to the matching OperatorInfo, or nullptr.
  const OperatorInfo *parseOperatorEncoding();

  /// Parse the <unresolved-name> production.
  /// \param Global True when the name is rooted with a leading GS.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseUnresolvedName(bool Global);
  /// Parse a <simple-id> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseSimpleId();
  /// Parse a <base-unresolved-name> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseBaseUnresolvedName();
  /// Parse an <unresolved-type> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseUnresolvedType();
  /// Parse a <destructor-name> production.
  /// \return The parsed AST node, or nullptr on failure.
  Node *parseDestructorName();

  /// Top-level entry point into the parser.
  /// \param ParseParams When false, stop before function parameter types.
  /// \return The root AST node, or nullptr on failure.
  Node *parse(bool ParseParams = true);
};

/// Skip a trailing discriminator in [\p first, \p last).
/// \param first Start of the remaining mangled input.
/// \param last End of the mangled input.
/// \return Pointer past the discriminator, or \p first on failure.
DEMANGLE_ABI const char *parse_discriminator(const char *first,
                                             const char *last);

// <name> ::= <nested-name> // N
//        ::= <local-name> # See Scope Encoding below  // Z
//        ::= <unscoped-template-name> <template-args>
//        ::= <unscoped-name>
//
// <unscoped-template-name> ::= <unscoped-name>
//                          ::= <substitution>
/// Parse a <name> production.
/// \param State Name-parsing state to update.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseName(NameState *State) {
  if (look() == 'N')
    return getDerived().parseNestedName(State);
  if (look() == 'Z')
    return getDerived().parseLocalName(State);

  Node *Result = nullptr;
  bool IsSubst = false;

  Result = getDerived().parseUnscopedName(State, &IsSubst);
  if (!Result)
    return nullptr;

  if (look() == 'I') {
    //        ::= <unscoped-template-name> <template-args>
    if (!IsSubst)
      // An unscoped-template-name is substitutable.
      Subs.push_back(Result);
    Node *TA = getDerived().parseTemplateArgs(State != nullptr);
    if (TA == nullptr)
      return nullptr;
    if (State)
      State->EndsWithTemplateArgs = true;
    Result = make<NameWithTemplateArgs>(Result, TA);
  } else if (IsSubst) {
    // The substitution case must be followed by <template-args>.
    return nullptr;
  }

  return Result;
}

// <local-name> := Z <function encoding> E <entity name> [<discriminator>]
//              := Z <function encoding> E s [<discriminator>]
//              := Z <function encoding> Ed [ <parameter number> ] _ <entity name>
/// Parse a <local-name> production.
/// \param State Name-parsing state to update.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseLocalName(NameState *State) {
  if (!consumeIf('Z'))
    return nullptr;
  Node *Encoding = getDerived().parseEncoding();
  if (Encoding == nullptr || !consumeIf('E'))
    return nullptr;

  if (consumeIf('s')) {
    First = parse_discriminator(First, Last);
    auto *StringLitName = make<NameType>("string literal");
    if (!StringLitName)
      return nullptr;
    return make<LocalName>(Encoding, StringLitName);
  }

  // The template parameters of the inner name are unrelated to those of the
  // enclosing context.
  SaveTemplateParams SaveTemplateParamsScope(this);

  if (consumeIf('d')) {
    parseNumber(true);
    if (!consumeIf('_'))
      return nullptr;
    Node *N = getDerived().parseName(State);
    if (N == nullptr)
      return nullptr;
    return make<LocalName>(Encoding, N);
  }

  Node *Entity = getDerived().parseName(State);
  if (Entity == nullptr)
    return nullptr;
  First = parse_discriminator(First, Last);
  return make<LocalName>(Encoding, Entity);
}

// <unscoped-name> ::= <unqualified-name>
//                 ::= St <unqualified-name>   # ::std::
// [*] extension
/// Parse an <unscoped-name> production.
/// \param State Name-parsing state to update.
/// \param IsSubst Optional out-flag set when a substitution was parsed.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseUnscopedName(NameState *State,
                                                          bool *IsSubst) {

  Node *Std = nullptr;
  if (consumeIf("St")) {
    Std = make<NameType>("std");
    if (Std == nullptr)
      return nullptr;
  }

  Node *Res = nullptr;
  ModuleName *Module = nullptr;
  if (look() == 'S') {
    Node *S = getDerived().parseSubstitution();
    if (!S)
      return nullptr;
    if (S->getKind() == Node::KModuleName)
      Module = static_cast<ModuleName *>(S);
    else if (IsSubst && Std == nullptr) {
      Res = S;
      *IsSubst = true;
    } else {
      return nullptr;
    }
  }

  if (Res == nullptr || Std != nullptr) {
    Res = getDerived().parseUnqualifiedName(State, Std, Module);
  }

  return Res;
}

// <unqualified-name> ::= [<module-name>] F? L? <operator-name> [<abi-tags>]
//                    ::= [<module-name>] <ctor-dtor-name> [<abi-tags>]
//                    ::= [<module-name>] F? L? <source-name> [<abi-tags>]
//                    ::= [<module-name>] L? <unnamed-type-name> [<abi-tags>]
//			# structured binding declaration
//                    ::= [<module-name>] L? DC <source-name>+ E
/// Parse an <unqualified-name> production.
/// \param State Name-parsing state to update.
/// \param Scope Enclosing scope node.
/// \param Module Module name being built.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseUnqualifiedName(
    NameState *State, Node *Scope, ModuleName *Module) {
  if (getDerived().parseModuleNameOpt(Module))
    return nullptr;

  bool IsMemberLikeFriend = Scope && consumeIf('F');

  consumeIf('L');

  Node *Result;
  if (look() >= '1' && look() <= '9') {
    Result = getDerived().parseSourceName(State);
  } else if (look() == 'U') {
    Result = getDerived().parseUnnamedTypeName(State);
  } else if (consumeIf("DC")) {
    // Structured binding
    size_t BindingsBegin = Names.size();
    do {
      Node *Binding = getDerived().parseSourceName(State);
      if (Binding == nullptr)
        return nullptr;
      Names.push_back(Binding);
    } while (!consumeIf('E'));
    Result = make<StructuredBindingName>(popTrailingNodeArray(BindingsBegin));
  } else if (look() == 'C' || look() == 'D') {
    // A <ctor-dtor-name>.
    if (Scope == nullptr || Module != nullptr)
      return nullptr;
    Result = getDerived().parseCtorDtorName(Scope, State);
  } else {
    Result = getDerived().parseOperatorName(State);
  }

  if (Result != nullptr && Module != nullptr)
    Result = make<ModuleEntity>(Module, Result);
  if (Result != nullptr)
    Result = getDerived().parseAbiTags(Result);
  if (Result != nullptr && IsMemberLikeFriend)
    Result = make<MemberLikeFriendName>(Scope, Result);
  else if (Result != nullptr && Scope != nullptr)
    Result = make<NestedName>(Scope, Result);

  return Result;
}

// <module-name> ::= <module-subname>
// 	 	 ::= <module-name> <module-subname>
//		 ::= <substitution>  # passed in by caller
// <module-subname> ::= W <source-name>
//		    ::= W P <source-name>
/// Parse an optional module name prefix.
/// \param Module Module name being built.
/// \return True on success.
template <typename Derived, typename Alloc>
bool AbstractManglingParser<Derived, Alloc>::parseModuleNameOpt(
    ModuleName *&Module) {
  while (consumeIf('W')) {
    bool IsPartition = consumeIf('P');
    Node *Sub = getDerived().parseSourceName(nullptr);
    if (!Sub)
      return true;
    Module =
        static_cast<ModuleName *>(make<ModuleName>(Module, Sub, IsPartition));
    Subs.push_back(Module);
  }

  return false;
}

// <unnamed-type-name> ::= Ut [<nonnegative number>] _
//                     ::= <closure-type-name>
//
// <closure-type-name> ::= Ul <lambda-sig> E [ <nonnegative number> ] _
//
// <lambda-sig> ::= <template-param-decl>* [Q <requires-clause expression>]
//                  <parameter type>+  # or "v" if the lambda has no parameters
/// Parse an <unnamed-type-name> production.
/// \param State Name-parsing state to update.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseUnnamedTypeName(NameState *State) {
  // <template-params> refer to the innermost <template-args>. Clear out any
  // outer args that we may have inserted into TemplateParams.
  if (State != nullptr)
    TemplateParams.clear();

  if (consumeIf("Ut")) {
    std::string_view Count = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<UnnamedTypeName>(Count);
  }
  if (consumeIf("Ul")) {
    ScopedOverride<size_t> SwapParams(ParsingLambdaParamsAtLevel,
                                      TemplateParams.size());
    ScopedTemplateParamList LambdaTemplateParams(this);

    size_t ParamsBegin = Names.size();
    while (getDerived().isTemplateParamDecl()) {
      Node *T =
          getDerived().parseTemplateParamDecl(LambdaTemplateParams.params());
      if (T == nullptr)
        return nullptr;
      Names.push_back(T);
    }
    NodeArray TempParams = popTrailingNodeArray(ParamsBegin);

    // FIXME: If TempParams is empty and none of the function parameters
    // includes 'auto', we should remove LambdaTemplateParams from the
    // TemplateParams list. Unfortunately, we don't find out whether there are
    // any 'auto' parameters until too late in an example such as:
    //
    //   template<typename T> void f(
    //       decltype([](decltype([]<typename T>(T v) {}),
    //                   auto) {})) {}
    //   template<typename T> void f(
    //       decltype([](decltype([]<typename T>(T w) {}),
    //                   int) {})) {}
    //
    // Here, the type of v is at level 2 but the type of w is at level 1. We
    // don't find this out until we encounter the type of the next parameter.
    //
    // However, compilers can't actually cope with the former example in
    // practice, and it's likely to be made ill-formed in future, so we don't
    // need to support it here.
    //
    // If we encounter an 'auto' in the function parameter types, we will
    // recreate a template parameter scope for it, but any intervening lambdas
    // will be parsed in the 'wrong' template parameter depth.
    if (TempParams.empty())
      TemplateParams.pop_back();

    Node *Requires1 = nullptr;
    if (consumeIf('Q')) {
      Requires1 = getDerived().parseConstraintExpr();
      if (Requires1 == nullptr)
        return nullptr;
    }

    if (!consumeIf("v")) {
      do {
        Node *P = getDerived().parseType();
        if (P == nullptr)
          return nullptr;
        Names.push_back(P);
      } while (look() != 'E' && look() != 'Q');
    }
    NodeArray Params = popTrailingNodeArray(ParamsBegin);

    Node *Requires2 = nullptr;
    if (consumeIf('Q')) {
      Requires2 = getDerived().parseConstraintExpr();
      if (Requires2 == nullptr)
        return nullptr;
    }

    if (!consumeIf('E'))
      return nullptr;

    std::string_view Count = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<ClosureTypeName>(TempParams, Requires1, Params, Requires2,
                                 Count);
  }
  if (consumeIf("Ub")) {
    (void)parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<NameType>("'block-literal'");
  }
  return nullptr;
}

// <source-name> ::= <positive length number> <identifier>
/// Parse a <source-name> production.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSourceName(NameState *) {
  size_t Length = 0;
  if (parsePositiveInteger(&Length))
    return nullptr;
  if (numLeft() < Length || Length == 0)
    return nullptr;
  std::string_view Name(First, Length);
  First += Length;
  if (starts_with(Name, "_GLOBAL__N"))
    return make<NameType>("(anonymous namespace)");
  return make<NameType>(Name);
}

// Operator encodings
template <typename Derived, typename Alloc>
const typename AbstractManglingParser<
    Derived, Alloc>::OperatorInfo AbstractManglingParser<Derived,
                                                         Alloc>::Ops[] = {
    // Keep ordered by encoding
    {"aN", OperatorInfo::Binary, false, Node::Prec::Assign, "operator&="},
    {"aS", OperatorInfo::Binary, false, Node::Prec::Assign, "operator="},
    {"aa", OperatorInfo::Binary, false, Node::Prec::AndIf, "operator&&"},
    {"ad", OperatorInfo::Prefix, false, Node::Prec::Unary, "operator&"},
    {"an", OperatorInfo::Binary, false, Node::Prec::And, "operator&"},
    {"at", OperatorInfo::OfIdOp, /*Type*/ true, Node::Prec::Unary, "alignof "},
    {"aw", OperatorInfo::NameOnly, false, Node::Prec::Primary,
     "operator co_await"},
    {"az", OperatorInfo::OfIdOp, /*Type*/ false, Node::Prec::Unary, "alignof "},
    {"cc", OperatorInfo::NamedCast, false, Node::Prec::Postfix, "const_cast"},
    {"cl", OperatorInfo::Call, /*Paren*/ false, Node::Prec::Postfix,
     "operator()"},
    {"cm", OperatorInfo::Binary, false, Node::Prec::Comma, "operator,"},
    {"co", OperatorInfo::Prefix, false, Node::Prec::Unary, "operator~"},
    {"cp", OperatorInfo::Call, /*Paren*/ true, Node::Prec::Postfix,
     "operator()"},
    {"cv", OperatorInfo::CCast, false, Node::Prec::Cast, "operator"}, // C Cast
    {"dV", OperatorInfo::Binary, false, Node::Prec::Assign, "operator/="},
    {"da", OperatorInfo::Del, /*Ary*/ true, Node::Prec::Unary,
     "operator delete[]"},
    {"dc", OperatorInfo::NamedCast, false, Node::Prec::Postfix, "dynamic_cast"},
    {"de", OperatorInfo::Prefix, false, Node::Prec::Unary, "operator*"},
    {"dl", OperatorInfo::Del, /*Ary*/ false, Node::Prec::Unary,
     "operator delete"},
    {"ds", OperatorInfo::Member, /*Named*/ false, Node::Prec::PtrMem,
     "operator.*"},
    {"dt", OperatorInfo::Member, /*Named*/ false, Node::Prec::Postfix,
     "operator."},
    {"dv", OperatorInfo::Binary, false, Node::Prec::Assign, "operator/"},
    {"eO", OperatorInfo::Binary, false, Node::Prec::Assign, "operator^="},
    {"eo", OperatorInfo::Binary, false, Node::Prec::Xor, "operator^"},
    {"eq", OperatorInfo::Binary, false, Node::Prec::Equality, "operator=="},
    {"ge", OperatorInfo::Binary, false, Node::Prec::Relational, "operator>="},
    {"gt", OperatorInfo::Binary, false, Node::Prec::Relational, "operator>"},
    {"ix", OperatorInfo::Array, false, Node::Prec::Postfix, "operator[]"},
    {"lS", OperatorInfo::Binary, false, Node::Prec::Assign, "operator<<="},
    {"le", OperatorInfo::Binary, false, Node::Prec::Relational, "operator<="},
    {"ls", OperatorInfo::Binary, false, Node::Prec::Shift, "operator<<"},
    {"lt", OperatorInfo::Binary, false, Node::Prec::Relational, "operator<"},
    {"mI", OperatorInfo::Binary, false, Node::Prec::Assign, "operator-="},
    {"mL", OperatorInfo::Binary, false, Node::Prec::Assign, "operator*="},
    {"mi", OperatorInfo::Binary, false, Node::Prec::Additive, "operator-"},
    {"ml", OperatorInfo::Binary, false, Node::Prec::Multiplicative,
     "operator*"},
    {"mm", OperatorInfo::Postfix, false, Node::Prec::Postfix, "operator--"},
    {"na", OperatorInfo::New, /*Ary*/ true, Node::Prec::Unary,
     "operator new[]"},
    {"ne", OperatorInfo::Binary, false, Node::Prec::Equality, "operator!="},
    {"ng", OperatorInfo::Prefix, false, Node::Prec::Unary, "operator-"},
    {"nt", OperatorInfo::Prefix, false, Node::Prec::Unary, "operator!"},
    {"nw", OperatorInfo::New, /*Ary*/ false, Node::Prec::Unary, "operator new"},
    {"oR", OperatorInfo::Binary, false, Node::Prec::Assign, "operator|="},
    {"oo", OperatorInfo::Binary, false, Node::Prec::OrIf, "operator||"},
    {"or", OperatorInfo::Binary, false, Node::Prec::Ior, "operator|"},
    {"pL", OperatorInfo::Binary, false, Node::Prec::Assign, "operator+="},
    {"pl", OperatorInfo::Binary, false, Node::Prec::Additive, "operator+"},
    {"pm", OperatorInfo::Member, /*Named*/ true, Node::Prec::PtrMem,
     "operator->*"},
    {"pp", OperatorInfo::Postfix, false, Node::Prec::Postfix, "operator++"},
    {"ps", OperatorInfo::Prefix, false, Node::Prec::Unary, "operator+"},
    {"pt", OperatorInfo::Member, /*Named*/ true, Node::Prec::Postfix,
     "operator->"},
    {"qu", OperatorInfo::Conditional, false, Node::Prec::Conditional,
     "operator?"},
    {"rM", OperatorInfo::Binary, false, Node::Prec::Assign, "operator%="},
    {"rS", OperatorInfo::Binary, false, Node::Prec::Assign, "operator>>="},
    {"rc", OperatorInfo::NamedCast, false, Node::Prec::Postfix,
     "reinterpret_cast"},
    {"rm", OperatorInfo::Binary, false, Node::Prec::Multiplicative,
     "operator%"},
    {"rs", OperatorInfo::Binary, false, Node::Prec::Shift, "operator>>"},
    {"sc", OperatorInfo::NamedCast, false, Node::Prec::Postfix, "static_cast"},
    {"ss", OperatorInfo::Binary, false, Node::Prec::Spaceship, "operator<=>"},
    {"st", OperatorInfo::OfIdOp, /*Type*/ true, Node::Prec::Unary, "sizeof "},
    {"sz", OperatorInfo::OfIdOp, /*Type*/ false, Node::Prec::Unary, "sizeof "},
    {"te", OperatorInfo::OfIdOp, /*Type*/ false, Node::Prec::Postfix,
     "typeid "},
    {"ti", OperatorInfo::OfIdOp, /*Type*/ true, Node::Prec::Postfix, "typeid "},
};
template <typename Derived, typename Alloc>
const size_t AbstractManglingParser<Derived, Alloc>::NumOps = sizeof(Ops) /
                                                              sizeof(Ops[0]);

// If the next 2 chars are an operator encoding, consume them and return their
// OperatorInfo.  Otherwise return nullptr.
/// Parse an operator encoding from the input, if present.
/// \return Pointer to the matching OperatorInfo, or nullptr.
template <typename Derived, typename Alloc>
const typename AbstractManglingParser<Derived, Alloc>::OperatorInfo *
AbstractManglingParser<Derived, Alloc>::parseOperatorEncoding() {
  if (numLeft() < 2)
    return nullptr;

  // We can't use lower_bound as that can link to symbols in the C++ library,
  // and this must remain independent of that.
  size_t lower = 0u, upper = NumOps - 1; // Inclusive bounds.
  while (upper != lower) {
    size_t middle = (upper + lower) / 2;
    if (Ops[middle] < First)
      lower = middle + 1;
    else
      upper = middle;
  }
  if (Ops[lower] != First)
    return nullptr;

  First += 2;
  return &Ops[lower];
}

//   <operator-name> ::= See parseOperatorEncoding()
//                   ::= li <source-name>  # operator ""
//                   ::= v <digit> <source-name>  # vendor extended operator
/// Parse an <operator-name> production.
/// \param State Name-parsing state to update.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseOperatorName(NameState *State) {
  if (const auto *Op = parseOperatorEncoding()) {
    if (Op->getKind() == OperatorInfo::CCast) {
      //              ::= cv <type>    # (cast)
      ScopedOverride<bool> SaveTemplate(TryToParseTemplateArgs, false);
      // If we're parsing an encoding, State != nullptr and the conversion
      // operators' <type> could have a <template-param> that refers to some
      // <template-arg>s further ahead in the mangled name.
      ScopedOverride<bool> SavePermit(PermitForwardTemplateReferences,
                                      PermitForwardTemplateReferences ||
                                          State != nullptr);
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      if (State) State->CtorDtorConversion = true;
      return make<ConversionOperatorType>(Ty);
    }

    if (Op->getKind() >= OperatorInfo::Unnameable)
      /* Not a nameable operator.  */
      return nullptr;
    if (Op->getKind() == OperatorInfo::Member && !Op->getFlag())
      /* Not a nameable MemberExpr */
      return nullptr;

    return make<NameType>(Op->getName());
  }

  if (consumeIf("li")) {
    //                   ::= li <source-name>  # operator ""
    Node *SN = getDerived().parseSourceName(State);
    if (SN == nullptr)
      return nullptr;
    return make<LiteralOperator>(SN);
  }

  if (consumeIf('v')) {
    // ::= v <digit> <source-name>        # vendor extended operator
    if (look() >= '0' && look() <= '9') {
      First++;
      Node *SN = getDerived().parseSourceName(State);
      if (SN == nullptr)
        return nullptr;
      return make<ConversionOperatorType>(SN);
    }
    return nullptr;
  }

  return nullptr;
}

// <ctor-dtor-name> ::= C1  # complete object constructor
//                  ::= C2  # base object constructor
//                  ::= C3  # complete object allocating constructor
//   extension      ::= C4  # gcc old-style "[unified]" constructor
//   extension      ::= C5  # the COMDAT used for ctors
//                  ::= D0  # deleting destructor
//                  ::= D1  # complete object destructor
//                  ::= D2  # base object destructor
//   extension      ::= D4  # gcc old-style "[unified]" destructor
//   extension      ::= D5  # the COMDAT used for dtors
/// Parse a constructor or destructor name.
/// \param SoFar Name parsed so far.
/// \param State Name-parsing state to update.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseCtorDtorName(Node *&SoFar,
                                                          NameState *State) {
  if (SoFar->getKind() == Node::KSpecialSubstitution) {
    // Expand the special substitution.
    SoFar = make<ExpandedSpecialSubstitution>(
        static_cast<SpecialSubstitution *>(SoFar));
    if (!SoFar)
      return nullptr;
  }

  if (consumeIf('C')) {
    bool IsInherited = consumeIf('I');
    if (look() != '1' && look() != '2' && look() != '3' && look() != '4' &&
        look() != '5')
      return nullptr;
    int Variant = look() - '0';
    ++First;
    if (State) State->CtorDtorConversion = true;
    if (IsInherited) {
      if (getDerived().parseName(State) == nullptr)
        return nullptr;
    }
    return make<CtorDtorName>(SoFar, /*IsDtor=*/false, Variant);
  }

  if (look() == 'D' && (look(1) == '0' || look(1) == '1' || look(1) == '2' ||
                        look(1) == '4' || look(1) == '5')) {
    int Variant = look(1) - '0';
    First += 2;
    if (State) State->CtorDtorConversion = true;
    return make<CtorDtorName>(SoFar, /*IsDtor=*/true, Variant);
  }

  return nullptr;
}

// <nested-name> ::= N [<CV-Qualifiers>] [<ref-qualifier>] <prefix>
// 			<unqualified-name> E
//               ::= N [<CV-Qualifiers>] [<ref-qualifier>] <template-prefix>
//               	<template-args> E
//
// <prefix> ::= <prefix> <unqualified-name>
//          ::= <template-prefix> <template-args>
//          ::= <template-param>
//          ::= <decltype>
//          ::= # empty
//          ::= <substitution>
//          ::= <prefix> <data-member-prefix>
// [*] extension
//
// <data-member-prefix> := <member source-name> [<template-args>] M
//
// <template-prefix> ::= <prefix> <template unqualified-name>
//                   ::= <template-param>
//                   ::= <substitution>
/// Parse a <nested-name> production.
/// \param State Name-parsing state to update.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseNestedName(NameState *State) {
  if (!consumeIf('N'))
    return nullptr;

  // 'H' specifies that the encoding that follows
  // has an explicit object parameter.
  if (!consumeIf('H')) {
    Qualifiers CVTmp = parseCVQualifiers();
    if (State)
      State->CVQualifiers = CVTmp;

    if (consumeIf('O')) {
      if (State)
        State->ReferenceQualifier = FrefQualRValue;
    } else if (consumeIf('R')) {
      if (State)
        State->ReferenceQualifier = FrefQualLValue;
    } else {
      if (State)
        State->ReferenceQualifier = FrefQualNone;
    }
  } else if (State) {
    State->HasExplicitObjectParameter = true;
  }

  Node *SoFar = nullptr;
  while (!consumeIf('E')) {
    if (State)
      // Only set end-with-template on the case that does that.
      State->EndsWithTemplateArgs = false;

    if (look() == 'T') {
      //          ::= <template-param>
      if (SoFar != nullptr)
        return nullptr; // Cannot have a prefix.
      SoFar = getDerived().parseTemplateParam();
    } else if (look() == 'I') {
      //          ::= <template-prefix> <template-args>
      if (SoFar == nullptr)
        return nullptr; // Must have a prefix.
      Node *TA = getDerived().parseTemplateArgs(State != nullptr);
      if (TA == nullptr)
        return nullptr;
      if (SoFar->getKind() == Node::KNameWithTemplateArgs)
        // Semantically <template-args> <template-args> cannot be generated by a
        // C++ entity.  There will always be [something like] a name between
        // them.
        return nullptr;
      if (State)
        State->EndsWithTemplateArgs = true;
      SoFar = make<NameWithTemplateArgs>(SoFar, TA);
    } else if (look() == 'D' && (look(1) == 't' || look(1) == 'T')) {
      //          ::= <decltype>
      if (SoFar != nullptr)
        return nullptr; // Cannot have a prefix.
      SoFar = getDerived().parseDecltype();
    } else {
      ModuleName *Module = nullptr;

      if (look() == 'S') {
        //          ::= <substitution>
        Node *S = nullptr;
        if (look(1) == 't') {
          First += 2;
          S = make<NameType>("std");
        } else {
          S = getDerived().parseSubstitution();
        }
        if (!S)
          return nullptr;
        if (S->getKind() == Node::KModuleName) {
          Module = static_cast<ModuleName *>(S);
        } else if (SoFar != nullptr) {
          return nullptr; // Cannot have a prefix.
        } else {
          SoFar = S;
          continue; // Do not push a new substitution.
        }
      }

      //          ::= [<prefix>] <unqualified-name>
      SoFar = getDerived().parseUnqualifiedName(State, SoFar, Module);
    }

    if (SoFar == nullptr)
      return nullptr;
    Subs.push_back(SoFar);

    // No longer used.
    // <data-member-prefix> := <member source-name> [<template-args>] M
    consumeIf('M');
  }

  if (SoFar == nullptr || Subs.empty())
    return nullptr;

  Subs.pop_back();
  return SoFar;
}

// <simple-id> ::= <source-name> [ <template-args> ]
/// Parse a <simple-id> production.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSimpleId() {
  Node *SN = getDerived().parseSourceName(/*NameState=*/nullptr);
  if (SN == nullptr)
    return nullptr;
  if (look() == 'I') {
    Node *TA = getDerived().parseTemplateArgs();
    if (TA == nullptr)
      return nullptr;
    return make<NameWithTemplateArgs>(SN, TA);
  }
  return SN;
}

// <destructor-name> ::= <unresolved-type>  # e.g., ~T or ~decltype(f())
//                   ::= <simple-id>        # e.g., ~A<2*N>
/// Parse a <destructor-name> production.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseDestructorName() {
  Node *Result;
  if (std::isdigit(look()))
    Result = getDerived().parseSimpleId();
  else
    Result = getDerived().parseUnresolvedType();
  if (Result == nullptr)
    return nullptr;
  return make<DtorName>(Result);
}

// <unresolved-type> ::= <template-param>
//                   ::= <decltype>
//                   ::= <substitution>
/// Parse an <unresolved-type> production.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseUnresolvedType() {
  if (look() == 'T') {
    Node *TP = getDerived().parseTemplateParam();
    if (TP == nullptr)
      return nullptr;
    Subs.push_back(TP);
    return TP;
  }
  if (look() == 'D') {
    Node *DT = getDerived().parseDecltype();
    if (DT == nullptr)
      return nullptr;
    Subs.push_back(DT);
    return DT;
  }
  return getDerived().parseSubstitution();
}

// <base-unresolved-name> ::= <simple-id>                                # unresolved name
//          extension     ::= <operator-name>                            # unresolved operator-function-id
//          extension     ::= <operator-name> <template-args>            # unresolved operator template-id
//                        ::= on <operator-name>                         # unresolved operator-function-id
//                        ::= on <operator-name> <template-args>         # unresolved operator template-id
//                        ::= dn <destructor-name>                       # destructor or pseudo-destructor;
//                                                                         # e.g. ~X or ~X<N-1>
/// Parse a <base-unresolved-name> production.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseBaseUnresolvedName() {
  if (std::isdigit(look()))
    return getDerived().parseSimpleId();

  if (consumeIf("dn"))
    return getDerived().parseDestructorName();

  consumeIf("on");

  Node *Oper = getDerived().parseOperatorName(/*NameState=*/nullptr);
  if (Oper == nullptr)
    return nullptr;
  if (look() == 'I') {
    Node *TA = getDerived().parseTemplateArgs();
    if (TA == nullptr)
      return nullptr;
    return make<NameWithTemplateArgs>(Oper, TA);
  }
  return Oper;
}

// <unresolved-name>
//  extension        ::= srN <unresolved-type> [<template-args>] <unresolved-qualifier-level>* E <base-unresolved-name>
//                   ::= [gs] <base-unresolved-name>                     # x or (with "gs") ::x
//                   ::= [gs] sr <unresolved-qualifier-level>+ E <base-unresolved-name>
//                                                                       # A::x, N::y, A<T>::z; "gs" means leading "::"
// [gs] has been parsed by caller.
//                   ::= sr <unresolved-type> <base-unresolved-name>     # T::x / decltype(p)::x
//  extension        ::= sr <unresolved-type> <template-args> <base-unresolved-name>
//                                                                       # T::N::x /decltype(p)::N::x
//  (ignored)        ::= srN <unresolved-type>  <unresolved-qualifier-level>+ E <base-unresolved-name>
//
// <unresolved-qualifier-level> ::= <simple-id>
/// Parse an <unresolved-name> production.
/// \param Global True when the name is globally qualified.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseUnresolvedName(bool Global) {
  Node *SoFar = nullptr;

  // srN <unresolved-type> [<template-args>] <unresolved-qualifier-level>* E <base-unresolved-name>
  // srN <unresolved-type>                   <unresolved-qualifier-level>+ E <base-unresolved-name>
  if (consumeIf("srN")) {
    SoFar = getDerived().parseUnresolvedType();
    if (SoFar == nullptr)
      return nullptr;

    if (look() == 'I') {
      Node *TA = getDerived().parseTemplateArgs();
      if (TA == nullptr)
        return nullptr;
      SoFar = make<NameWithTemplateArgs>(SoFar, TA);
      if (!SoFar)
        return nullptr;
    }

    while (!consumeIf('E')) {
      Node *Qual = getDerived().parseSimpleId();
      if (Qual == nullptr)
        return nullptr;
      SoFar = make<QualifiedName>(SoFar, Qual);
      if (!SoFar)
        return nullptr;
    }

    Node *Base = getDerived().parseBaseUnresolvedName();
    if (Base == nullptr)
      return nullptr;
    return make<QualifiedName>(SoFar, Base);
  }

  // [gs] <base-unresolved-name>                     # x or (with "gs") ::x
  if (!consumeIf("sr")) {
    SoFar = getDerived().parseBaseUnresolvedName();
    if (SoFar == nullptr)
      return nullptr;
    if (Global)
      SoFar = make<GlobalQualifiedName>(SoFar);
    return SoFar;
  }

  // [gs] sr <unresolved-qualifier-level>+ E   <base-unresolved-name>
  if (std::isdigit(look())) {
    do {
      Node *Qual = getDerived().parseSimpleId();
      if (Qual == nullptr)
        return nullptr;
      if (SoFar)
        SoFar = make<QualifiedName>(SoFar, Qual);
      else if (Global)
        SoFar = make<GlobalQualifiedName>(Qual);
      else
        SoFar = Qual;
      if (!SoFar)
        return nullptr;
    } while (!consumeIf('E'));
  }
  //      sr <unresolved-type>                 <base-unresolved-name>
  //      sr <unresolved-type> <template-args> <base-unresolved-name>
  else {
    SoFar = getDerived().parseUnresolvedType();
    if (SoFar == nullptr)
      return nullptr;

    if (look() == 'I') {
      Node *TA = getDerived().parseTemplateArgs();
      if (TA == nullptr)
        return nullptr;
      SoFar = make<NameWithTemplateArgs>(SoFar, TA);
      if (!SoFar)
        return nullptr;
    }
  }

  DEMANGLE_ASSERT(SoFar != nullptr, "");

  Node *Base = getDerived().parseBaseUnresolvedName();
  if (Base == nullptr)
    return nullptr;
  return make<QualifiedName>(SoFar, Base);
}

// <abi-tags> ::= <abi-tag> [<abi-tags>]
// <abi-tag> ::= B <source-name>
/// Parse and attach ABI tags to \p N.
/// \param N Node to extend with ABI tags.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseAbiTags(Node *N) {
  while (consumeIf('B')) {
    std::string_view SN = parseBareSourceName();
    if (SN.empty())
      return nullptr;
    N = make<AbiTagAttr>(N, SN);
    if (!N)
      return nullptr;
  }
  return N;
}

// <number> ::= [n] <non-negative decimal integer>
/// Parse an optionally signed number from the input.
/// \param AllowNegative When true, accept a leading 'n' for negative values.
/// \return The parsed number spelling as a string view.
template <typename Alloc, typename Derived>
std::string_view
AbstractManglingParser<Alloc, Derived>::parseNumber(bool AllowNegative) {
  const char *Tmp = First;
  if (AllowNegative)
    consumeIf('n');
  if (numLeft() == 0 || !std::isdigit(*First))
    return std::string_view();
  while (numLeft() != 0 && std::isdigit(*First))
    ++First;
  return std::string_view(Tmp, First - Tmp);
}

// <positive length number> ::= [0-9]*
/// Parse a positive integer into Out.
/// \param Out Destination for the parsed integer or sequence id.
/// \return True on success.
template <typename Alloc, typename Derived>
bool AbstractManglingParser<Alloc, Derived>::parsePositiveInteger(size_t *Out) {
  *Out = 0;
  if (look() < '0' || look() > '9')
    return true;
  while (look() >= '0' && look() <= '9') {
    *Out *= 10;
    *Out += static_cast<size_t>(consume() - '0');
  }
  return false;
}

/// Parse a bare <source-name> spelling.
/// \return The bare source-name spelling, or empty on failure.
template <typename Alloc, typename Derived>
std::string_view AbstractManglingParser<Alloc, Derived>::parseBareSourceName() {
  size_t Int = 0;
  if (parsePositiveInteger(&Int) || numLeft() < Int)
    return {};
  std::string_view R(First, Int);
  First += Int;
  return R;
}

// <function-type> ::= [<CV-qualifiers>] [<exception-spec>] [Dx] F [Y] <bare-function-type> [<ref-qualifier>] E
//
// <exception-spec> ::= Do                # non-throwing exception-specification (e.g., noexcept, throw())
//                  ::= DO <expression> E # computed (instantiation-dependent) noexcept
//                  ::= Dw <type>+ E      # dynamic exception specification with instantiation-dependent types
//
// <ref-qualifier> ::= R                   # & ref-qualifier
// <ref-qualifier> ::= O                   # && ref-qualifier
/// Parse a function type.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseFunctionType() {
  Qualifiers CVQuals = parseCVQualifiers();

  Node *ExceptionSpec = nullptr;
  if (consumeIf("Do")) {
    ExceptionSpec = make<NameType>("noexcept");
    if (!ExceptionSpec)
      return nullptr;
  } else if (consumeIf("DO")) {
    Node *E = getDerived().parseExpr();
    if (E == nullptr || !consumeIf('E'))
      return nullptr;
    ExceptionSpec = make<NoexceptSpec>(E);
    if (!ExceptionSpec)
      return nullptr;
  } else if (consumeIf("Dw")) {
    size_t SpecsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *T = getDerived().parseType();
      if (T == nullptr)
        return nullptr;
      Names.push_back(T);
    }
    ExceptionSpec =
      make<DynamicExceptionSpec>(popTrailingNodeArray(SpecsBegin));
    if (!ExceptionSpec)
      return nullptr;
  }

  consumeIf("Dx"); // transaction safe

  if (!consumeIf('F'))
    return nullptr;
  consumeIf('Y'); // extern "C"
  Node *ReturnType = getDerived().parseType();
  if (ReturnType == nullptr)
    return nullptr;

  FunctionRefQual ReferenceQualifier = FrefQualNone;
  size_t ParamsBegin = Names.size();
  while (true) {
    if (consumeIf('E'))
      break;
    if (consumeIf('v'))
      continue;
    if (consumeIf("RE")) {
      ReferenceQualifier = FrefQualLValue;
      break;
    }
    if (consumeIf("OE")) {
      ReferenceQualifier = FrefQualRValue;
      break;
    }
    Node *T = getDerived().parseType();
    if (T == nullptr)
      return nullptr;
    Names.push_back(T);
  }

  NodeArray Params = popTrailingNodeArray(ParamsBegin);
  return make<FunctionType>(ReturnType, Params, CVQuals,
                            ReferenceQualifier, ExceptionSpec);
}

// extension:
// <vector-type>           ::= Dv <positive dimension number> _ <extended element type>
//                         ::= Dv [<dimension expression>] _ <element type>
// <extended element type> ::= <element type>
//                         ::= p # AltiVec vector pixel
/// Parse a vector type.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseVectorType() {
  if (!consumeIf("Dv"))
    return nullptr;
  if (look() >= '1' && look() <= '9') {
    Node *DimensionNumber = make<NameType>(parseNumber());
    if (!DimensionNumber)
      return nullptr;
    if (!consumeIf('_'))
      return nullptr;
    if (consumeIf('p'))
      return make<PixelVectorType>(DimensionNumber);
    Node *ElemType = getDerived().parseType();
    if (ElemType == nullptr)
      return nullptr;
    return make<VectorType>(ElemType, DimensionNumber);
  }

  if (!consumeIf('_')) {
    Node *DimExpr = getDerived().parseExpr();
    if (!DimExpr)
      return nullptr;
    if (!consumeIf('_'))
      return nullptr;
    Node *ElemType = getDerived().parseType();
    if (!ElemType)
      return nullptr;
    return make<VectorType>(ElemType, DimExpr);
  }
  Node *ElemType = getDerived().parseType();
  if (!ElemType)
    return nullptr;
  return make<VectorType>(ElemType, /*Dimension=*/nullptr);
}

// <decltype>  ::= Dt <expression> E  # decltype of an id-expression or class member access (C++0x)
//             ::= DT <expression> E  # decltype of an expression (C++0x)
/// Parse a decltype type.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseDecltype() {
  if (!consumeIf('D'))
    return nullptr;
  if (!consumeIf('t') && !consumeIf('T'))
    return nullptr;
  Node *E = getDerived().parseExpr();
  if (E == nullptr)
    return nullptr;
  if (!consumeIf('E'))
    return nullptr;
  return make<EnclosingExpr>("decltype", E);
}

// <array-type> ::= A <positive dimension number> _ <element type>
//              ::= A [<dimension expression>] _ <element type>
/// Parse an array type.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseArrayType() {
  if (!consumeIf('A'))
    return nullptr;

  Node *Dimension = nullptr;

  if (std::isdigit(look())) {
    Dimension = make<NameType>(parseNumber());
    if (!Dimension)
      return nullptr;
    if (!consumeIf('_'))
      return nullptr;
  } else if (!consumeIf('_')) {
    Node *DimExpr = getDerived().parseExpr();
    if (DimExpr == nullptr)
      return nullptr;
    if (!consumeIf('_'))
      return nullptr;
    Dimension = DimExpr;
  }

  Node *Ty = getDerived().parseType();
  if (Ty == nullptr)
    return nullptr;
  return make<ArrayType>(Ty, Dimension);
}

// <pointer-to-member-type> ::= M <class type> <member type>
/// Parse a pointer-to-member type.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parsePointerToMemberType() {
  if (!consumeIf('M'))
    return nullptr;
  Node *ClassType = getDerived().parseType();
  if (ClassType == nullptr)
    return nullptr;
  Node *MemberType = getDerived().parseType();
  if (MemberType == nullptr)
    return nullptr;
  return make<PointerToMemberType>(ClassType, MemberType);
}

// <class-enum-type> ::= <name>     # non-dependent type name, dependent type name, or dependent typename-specifier
//                   ::= Ts <name>  # dependent elaborated type specifier using 'struct' or 'class'
//                   ::= Tu <name>  # dependent elaborated type specifier using 'union'
//                   ::= Te <name>  # dependent elaborated type specifier using 'enum'
/// Parse a class/enum type.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseClassEnumType() {
  std::string_view ElabSpef;
  if (consumeIf("Ts"))
    ElabSpef = "struct";
  else if (consumeIf("Tu"))
    ElabSpef = "union";
  else if (consumeIf("Te"))
    ElabSpef = "enum";

  Node *Name = getDerived().parseName();
  if (Name == nullptr)
    return nullptr;

  if (!ElabSpef.empty())
    return make<ElaboratedTypeSpefType>(ElabSpef, Name);

  return Name;
}

// <qualified-type>     ::= <qualifiers> <type>
// <qualifiers> ::= <extended-qualifier>* <CV-qualifiers>
// <extended-qualifier> ::= U <source-name> [<template-args>] # vendor extended type qualifier
/// Parse a qualified type.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseQualifiedType() {
  if (consumeIf('U')) {
    std::string_view Qual = parseBareSourceName();
    if (Qual.empty())
      return nullptr;

    // extension            ::= U <objc-name> <objc-type>  # objc-type<identifier>
    if (starts_with(Qual, "objcproto")) {
      constexpr size_t Len = sizeof("objcproto") - 1;
      std::string_view ProtoSourceName(Qual.data() + Len, Qual.size() - Len);
      std::string_view Proto;
      {
        ScopedOverride<const char *> SaveFirst(First, ProtoSourceName.data()),
            SaveLast(Last, &*ProtoSourceName.rbegin() + 1);
        Proto = parseBareSourceName();
      }
      if (Proto.empty())
        return nullptr;
      Node *Child = getDerived().parseQualifiedType();
      if (Child == nullptr)
        return nullptr;
      return make<ObjCProtoName>(Child, Proto);
    }

    Node *TA = nullptr;
    if (look() == 'I') {
      TA = getDerived().parseTemplateArgs();
      if (TA == nullptr)
        return nullptr;
    }

    Node *Child = getDerived().parseQualifiedType();
    if (Child == nullptr)
      return nullptr;
    return make<VendorExtQualType>(Child, Qual, TA);
  }

  Qualifiers Quals = parseCVQualifiers();
  Node *Ty = getDerived().parseType();
  if (Ty == nullptr)
    return nullptr;
  if (Quals != QualNone)
    Ty = make<QualType>(Ty, Quals);
  return Ty;
}

// <type>      ::= <builtin-type>
//             ::= <qualified-type>
//             ::= <function-type>
//             ::= <class-enum-type>
//             ::= <array-type>
//             ::= <pointer-to-member-type>
//             ::= <template-param>
//             ::= <template-template-param> <template-args>
//             ::= <decltype>
//             ::= P <type>        # pointer
//             ::= R <type>        # l-value reference
//             ::= O <type>        # r-value reference (C++11)
//             ::= C <type>        # complex pair (C99)
//             ::= G <type>        # imaginary (C99)
//             ::= <substitution>  # See Compression below
// extension   ::= U <objc-name> <objc-type>  # objc-type<identifier>
// extension   ::= <vector-type> # <vector-type> starts with Dv
//
// <objc-name> ::= <k0 number> objcproto <k1 number> <identifier>  # k0 = 9 + <number of digits in k1> + k1
// <objc-type> ::= <source-name>  # PU<11+>objcproto 11objc_object<source-name> 11objc_object -> id<source-name>
/// Parse a <type> production.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseType() {
  Node *Result = nullptr;

  switch (look()) {
  //             ::= <qualified-type>
  case 'r':
  case 'V':
  case 'K': {
    unsigned AfterQuals = 0;
    if (look(AfterQuals) == 'r') ++AfterQuals;
    if (look(AfterQuals) == 'V') ++AfterQuals;
    if (look(AfterQuals) == 'K') ++AfterQuals;

    if (look(AfterQuals) == 'F' ||
        (look(AfterQuals) == 'D' &&
         (look(AfterQuals + 1) == 'o' || look(AfterQuals + 1) == 'O' ||
          look(AfterQuals + 1) == 'w' || look(AfterQuals + 1) == 'x'))) {
      Result = getDerived().parseFunctionType();
      break;
    }
    DEMANGLE_FALLTHROUGH;
  }
  case 'U': {
    Result = getDerived().parseQualifiedType();
    break;
  }
  // <builtin-type> ::= v    # void
  case 'v':
    ++First;
    return make<NameType>("void");
  //                ::= w    # wchar_t
  case 'w':
    ++First;
    return make<NameType>("wchar_t");
  //                ::= b    # bool
  case 'b':
    ++First;
    return make<NameType>("bool");
  //                ::= c    # char
  case 'c':
    ++First;
    return make<NameType>("char");
  //                ::= a    # signed char
  case 'a':
    ++First;
    return make<NameType>("signed char");
  //                ::= h    # unsigned char
  case 'h':
    ++First;
    return make<NameType>("unsigned char");
  //                ::= s    # short
  case 's':
    ++First;
    return make<NameType>("short");
  //                ::= t    # unsigned short
  case 't':
    ++First;
    return make<NameType>("unsigned short");
  //                ::= i    # int
  case 'i':
    ++First;
    return make<NameType>("int");
  //                ::= j    # unsigned int
  case 'j':
    ++First;
    return make<NameType>("unsigned int");
  //                ::= l    # long
  case 'l':
    ++First;
    return make<NameType>("long");
  //                ::= m    # unsigned long
  case 'm':
    ++First;
    return make<NameType>("unsigned long");
  //                ::= x    # long long, __int64
  case 'x':
    ++First;
    return make<NameType>("long long");
  //                ::= y    # unsigned long long, __int64
  case 'y':
    ++First;
    return make<NameType>("unsigned long long");
  //                ::= n    # __int128
  case 'n':
    ++First;
    return make<NameType>("__int128");
  //                ::= o    # unsigned __int128
  case 'o':
    ++First;
    return make<NameType>("unsigned __int128");
  //                ::= f    # float
  case 'f':
    ++First;
    return make<NameType>("float");
  //                ::= d    # double
  case 'd':
    ++First;
    return make<NameType>("double");
  //                ::= e    # long double, __float80
  case 'e':
    ++First;
    return make<NameType>("long double");
  //                ::= g    # __float128
  case 'g':
    ++First;
    return make<NameType>("__float128");
  //                ::= z    # ellipsis
  case 'z':
    ++First;
    return make<NameType>("...");

  // <builtin-type> ::= u <source-name>    # vendor extended type
  case 'u': {
    ++First;
    std::string_view Res = parseBareSourceName();
    if (Res.empty())
      return nullptr;
    // Typically, <builtin-type>s are not considered substitution candidates,
    // but the exception to that exception is vendor extended types (Itanium C++
    // ABI 5.9.1).
    if (consumeIf('I')) {
      Node *BaseType = parseType();
      if (BaseType == nullptr)
        return nullptr;
      if (!consumeIf('E'))
        return nullptr;
      Result = make<TransformedType>(Res, BaseType);
    } else
      Result = make<NameType>(Res);
    break;
  }
  case 'D':
    switch (look(1)) {
    //                ::= Dd   # IEEE 754r decimal floating point (64 bits)
    case 'd':
      First += 2;
      return make<NameType>("decimal64");
    //                ::= De   # IEEE 754r decimal floating point (128 bits)
    case 'e':
      First += 2;
      return make<NameType>("decimal128");
    //                ::= Df   # IEEE 754r decimal floating point (32 bits)
    case 'f':
      First += 2;
      return make<NameType>("decimal32");
    //                ::= Dh   # IEEE 754r half-precision floating point (16 bits)
    case 'h':
      First += 2;
      return make<NameType>("half");
    //       ::= DF16b         # C++23 std::bfloat16_t
    //       ::= DF <number> _ # ISO/IEC TS 18661 binary floating point (N bits)
    case 'F': {
      First += 2;
      if (consumeIf("16b"))
        return make<NameType>("std::bfloat16_t");
      Node *DimensionNumber = make<NameType>(parseNumber());
      if (!DimensionNumber)
        return nullptr;
      if (!consumeIf('_'))
        return nullptr;
      return make<BinaryFPType>(DimensionNumber);
    }
    //                ::= [DS] DA  # N1169 fixed-point [_Sat] T _Accum
    //                ::= [DS] DR  # N1169 fixed-point [_Sat] T _Frac
    // <fixed-point-size>
    //                ::= s # short
    //                ::= t # unsigned short
    //                ::= i # plain
    //                ::= j # unsigned
    //                ::= l # long
    //                ::= m # unsigned long
    case 'A': {
      char c = look(2);
      First += 3;
      switch (c) {
      case 's':
        return make<NameType>("short _Accum");
      case 't':
        return make<NameType>("unsigned short _Accum");
      case 'i':
        return make<NameType>("_Accum");
      case 'j':
        return make<NameType>("unsigned _Accum");
      case 'l':
        return make<NameType>("long _Accum");
      case 'm':
        return make<NameType>("unsigned long _Accum");
      default:
        return nullptr;
      }
    }
    case 'R': {
      char c = look(2);
      First += 3;
      switch (c) {
      case 's':
        return make<NameType>("short _Fract");
      case 't':
        return make<NameType>("unsigned short _Fract");
      case 'i':
        return make<NameType>("_Fract");
      case 'j':
        return make<NameType>("unsigned _Fract");
      case 'l':
        return make<NameType>("long _Fract");
      case 'm':
        return make<NameType>("unsigned long _Fract");
      default:
        return nullptr;
      }
    }
    case 'S': {
      First += 2;
      if (look() != 'D')
        return nullptr;
      if (look(1) == 'A') {
        char c = look(2);
        First += 3;
        switch (c) {
        case 's':
          return make<NameType>("_Sat short _Accum");
        case 't':
          return make<NameType>("_Sat unsigned short _Accum");
        case 'i':
          return make<NameType>("_Sat _Accum");
        case 'j':
          return make<NameType>("_Sat unsigned _Accum");
        case 'l':
          return make<NameType>("_Sat long _Accum");
        case 'm':
          return make<NameType>("_Sat unsigned long _Accum");
        default:
          return nullptr;
        }
      }
      if (look(1) == 'R') {
        char c = look(2);
        First += 3;
        switch (c) {
        case 's':
          return make<NameType>("_Sat short _Fract");
        case 't':
          return make<NameType>("_Sat unsigned short _Fract");
        case 'i':
          return make<NameType>("_Sat _Fract");
        case 'j':
          return make<NameType>("_Sat unsigned _Fract");
        case 'l':
          return make<NameType>("_Sat long _Fract");
        case 'm':
          return make<NameType>("_Sat unsigned long _Fract");
        default:
          return nullptr;
        }
      }
      return nullptr;
    }
    //                ::= DB <number> _                             # C23 signed _BitInt(N)
    //                ::= DB <instantiation-dependent expression> _ # C23 signed _BitInt(N)
    //                ::= DU <number> _                             # C23 unsigned _BitInt(N)
    //                ::= DU <instantiation-dependent expression> _ # C23 unsigned _BitInt(N)
    case 'B':
    case 'U': {
      bool Signed = look(1) == 'B';
      First += 2;
      Node *Size = std::isdigit(look()) ? make<NameType>(parseNumber())
                                        : getDerived().parseExpr();
      if (!Size)
        return nullptr;
      if (!consumeIf('_'))
        return nullptr;
      // The front end expects this to be available for Substitution
      Result = make<BitIntType>(Size, Signed);
      break;
    }
    //                ::= Di   # char32_t
    case 'i':
      First += 2;
      return make<NameType>("char32_t");
    //                ::= Ds   # char16_t
    case 's':
      First += 2;
      return make<NameType>("char16_t");
    //                ::= Du   # char8_t (C++2a, not yet in the Itanium spec)
    case 'u':
      First += 2;
      return make<NameType>("char8_t");
    //                ::= Da   # auto (in dependent new-expressions)
    case 'a':
      First += 2;
      return make<NameType>("auto");
    //                ::= Dc   # decltype(auto)
    case 'c':
      First += 2;
      return make<NameType>("decltype(auto)");
    //                ::= Dk <type-constraint> # constrained auto
    //                ::= DK <type-constraint> # constrained decltype(auto)
    case 'k':
    case 'K': {
      std::string_view Kind = look(1) == 'k' ? " auto" : " decltype(auto)";
      First += 2;
      Node *Constraint = getDerived().parseName();
      if (!Constraint)
        return nullptr;
      return make<PostfixQualifiedType>(Constraint, Kind);
    }
    //                ::= Dn   # std::nullptr_t (i.e., decltype(nullptr))
    case 'n':
      First += 2;
      return make<NameType>("std::nullptr_t");

    //             ::= <decltype>
    case 't':
    case 'T': {
      Result = getDerived().parseDecltype();
      break;
    }
    // extension   ::= <vector-type> # <vector-type> starts with Dv
    case 'v': {
      Result = getDerived().parseVectorType();
      break;
    }
    //           ::= Dp <type>       # pack expansion (C++0x)
    case 'p': {
      First += 2;
      Node *Child = getDerived().parseType();
      if (!Child)
        return nullptr;
      Result = make<ParameterPackExpansion>(Child);
      break;
    }
    //           ::= Dy <type> <expression> # pack indexing (C++26)
    case 'y': {
      First += 2;
      Node *Pattern = getDerived().parseType();
      if (!Pattern)
        return nullptr;
      Node *Index = getDerived().parseExpr();
      if (!Index)
        return nullptr;
      Result = make<PackIndexing>(Pattern, Index);
      break;
    }
    // Exception specifier on a function type.
    case 'o':
    case 'O':
    case 'w':
    // Transaction safe function type.
    case 'x':
      Result = getDerived().parseFunctionType();
      break;
    }
    break;
  //             ::= <function-type>
  case 'F': {
    Result = getDerived().parseFunctionType();
    break;
  }
  //             ::= <array-type>
  case 'A': {
    Result = getDerived().parseArrayType();
    break;
  }
  //             ::= <pointer-to-member-type>
  case 'M': {
    Result = getDerived().parsePointerToMemberType();
    break;
  }
  //             ::= <template-param>
  case 'T': {
    // This could be an elaborate type specifier on a <class-enum-type>.
    if (look(1) == 's' || look(1) == 'u' || look(1) == 'e') {
      Result = getDerived().parseClassEnumType();
      break;
    }

    Result = getDerived().parseTemplateParam();
    if (Result == nullptr)
      return nullptr;

    // Result could be either of:
    //   <type>        ::= <template-param>
    //   <type>        ::= <template-template-param> <template-args>
    //
    //   <template-template-param> ::= <template-param>
    //                             ::= <substitution>
    //
    // If this is followed by some <template-args>, and we're permitted to
    // parse them, take the second production.

    if (TryToParseTemplateArgs && look() == 'I') {
      Subs.push_back(Result);
      Node *TA = getDerived().parseTemplateArgs();
      if (TA == nullptr)
        return nullptr;
      Result = make<NameWithTemplateArgs>(Result, TA);
    }
    break;
  }
  //             ::= P <type>        # pointer
  case 'P': {
    ++First;
    Node *Ptr = getDerived().parseType();
    if (Ptr == nullptr)
      return nullptr;
    Result = make<PointerType>(Ptr);
    break;
  }
  //             ::= R <type>        # l-value reference
  case 'R': {
    ++First;
    Node *Ref = getDerived().parseType();
    if (Ref == nullptr)
      return nullptr;
    Result = make<ReferenceType>(Ref, ReferenceKind::LValue);
    break;
  }
  //             ::= O <type>        # r-value reference (C++11)
  case 'O': {
    ++First;
    Node *Ref = getDerived().parseType();
    if (Ref == nullptr)
      return nullptr;
    Result = make<ReferenceType>(Ref, ReferenceKind::RValue);
    break;
  }
  //             ::= C <type>        # complex pair (C99)
  case 'C': {
    ++First;
    Node *P = getDerived().parseType();
    if (P == nullptr)
      return nullptr;
    Result = make<PostfixQualifiedType>(P, " complex");
    break;
  }
  //             ::= G <type>        # imaginary (C99)
  case 'G': {
    ++First;
    Node *P = getDerived().parseType();
    if (P == nullptr)
      return P;
    Result = make<PostfixQualifiedType>(P, " imaginary");
    break;
  }
  //             ::= <substitution>  # See Compression below
  case 'S': {
    if (look(1) != 't') {
      bool IsSubst = false;
      Result = getDerived().parseUnscopedName(nullptr, &IsSubst);
      if (!Result)
        return nullptr;

      // Sub could be either of:
      //   <type>        ::= <substitution>
      //   <type>        ::= <template-template-param> <template-args>
      //
      //   <template-template-param> ::= <template-param>
      //                             ::= <substitution>
      //
      // If this is followed by some <template-args>, and we're permitted to
      // parse them, take the second production.

      if (look() == 'I' && (!IsSubst || TryToParseTemplateArgs)) {
        if (!IsSubst)
          Subs.push_back(Result);
        Node *TA = getDerived().parseTemplateArgs();
        if (TA == nullptr)
          return nullptr;
        Result = make<NameWithTemplateArgs>(Result, TA);
      } else if (IsSubst) {
        // If all we parsed was a substitution, don't re-insert into the
        // substitution table.
        return Result;
      }
      break;
    }
    DEMANGLE_FALLTHROUGH;
  }
  //        ::= <class-enum-type>
  default: {
    Result = getDerived().parseClassEnumType();
    break;
  }
  }

  // If we parsed a type, insert it into the substitution table. Note that all
  // <builtin-type>s and <substitution>s have already bailed out, because they
  // don't get substitutions.
  if (Result != nullptr)
    Subs.push_back(Result);
  return Result;
}

/// Parse a prefix unary expression.
/// \param Kind The kind.
/// \param Prec The prec.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parsePrefixExpr(std::string_view Kind,
                                                        Node::Prec Prec) {
  Node *E = getDerived().parseExpr();
  if (E == nullptr)
    return nullptr;
  return make<PrefixExpr>(Kind, E, Prec);
}

/// Parse a binary operator expression.
/// \param Kind The kind.
/// \param Prec The prec.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseBinaryExpr(std::string_view Kind,
                                                        Node::Prec Prec) {
  Node *LHS = getDerived().parseExpr();
  if (LHS == nullptr)
    return nullptr;
  Node *RHS = getDerived().parseExpr();
  if (RHS == nullptr)
    return nullptr;
  return make<BinaryExpr>(LHS, Kind, RHS, Prec);
}

/// Parse an integer literal of the given type.
/// \param Lit The lit.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseIntegerLiteral(
    std::string_view Lit) {
  std::string_view Tmp = parseNumber(true);
  if (!Tmp.empty() && consumeIf('E'))
    return make<IntegerLiteral>(Lit, Tmp);
  return nullptr;
}

// <CV-Qualifiers> ::= [r] [V] [K]
/// Parse CV and restrict qualifiers.
/// \return The parsed CV and restrict qualifiers.
template <typename Alloc, typename Derived>
Qualifiers AbstractManglingParser<Alloc, Derived>::parseCVQualifiers() {
  Qualifiers CVR = QualNone;
  if (consumeIf('r'))
    CVR |= QualRestrict;
  if (consumeIf('V'))
    CVR |= QualVolatile;
  if (consumeIf('K'))
    CVR |= QualConst;
  return CVR;
}

// <function-param> ::= fp <top-level CV-Qualifiers> _                                     # L == 0, first parameter
//                  ::= fp <top-level CV-Qualifiers> <parameter-2 non-negative number> _   # L == 0, second and later parameters
//                  ::= fL <L-1 non-negative number> p <top-level CV-Qualifiers> _         # L > 0, first parameter
//                  ::= fL <L-1 non-negative number> p <top-level CV-Qualifiers> <parameter-2 non-negative number> _   # L > 0, second and later parameters
//                  ::= fpT      # 'this' expression (not part of standard?)
/// Parse a function parameter expression.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseFunctionParam() {
  if (consumeIf("fpT"))
    return make<NameType>("this");
  if (consumeIf("fp")) {
    parseCVQualifiers();
    std::string_view Num = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<FunctionParam>(Num);
  }
  if (consumeIf("fL")) {
    if (parseNumber().empty())
      return nullptr;
    if (!consumeIf('p'))
      return nullptr;
    parseCVQualifiers();
    std::string_view Num = parseNumber();
    if (!consumeIf('_'))
      return nullptr;
    return make<FunctionParam>(Num);
  }
  return nullptr;
}

// cv <type> <expression>                               # conversion with one argument
// cv <type> _ <expression>* E                          # conversion with a different number of arguments
/// Parse a conversion expression.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseConversionExpr() {
  if (!consumeIf("cv"))
    return nullptr;
  Node *Ty;
  {
    ScopedOverride<bool> SaveTemp(TryToParseTemplateArgs, false);
    Ty = getDerived().parseType();
  }

  if (Ty == nullptr)
    return nullptr;

  if (consumeIf('_')) {
    size_t ExprsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *E = getDerived().parseExpr();
      if (E == nullptr)
        return E;
      Names.push_back(E);
    }
    NodeArray Exprs = popTrailingNodeArray(ExprsBegin);
    return make<ConversionExpr>(Ty, Exprs);
  }

  Node *E[1] = {getDerived().parseExpr()};
  if (E[0] == nullptr)
    return nullptr;
  return make<ConversionExpr>(Ty, makeNodeArray(E, E + 1));
}

// <expr-primary> ::= L <type> <value number> E                          # integer literal
//                ::= L <type> <value float> E                           # floating literal
//                ::= L <string type> E                                  # string literal
//                ::= L <nullptr type> E                                 # nullptr literal (i.e., "LDnE")
//                ::= L <lambda type> E                                  # lambda expression
// FIXME:         ::= L <type> <real-part float> _ <imag-part float> E   # complex floating point literal (C 2000)
//                ::= L <mangled-name> E                                 # external name
/// Parse an <expr-primary> production.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseExprPrimary() {
  if (!consumeIf('L'))
    return nullptr;
  switch (look()) {
  case 'w':
    ++First;
    return getDerived().parseIntegerLiteral("wchar_t");
  case 'b':
    if (consumeIf("b0E"))
      return make<BoolExpr>(0);
    if (consumeIf("b1E"))
      return make<BoolExpr>(1);
    return nullptr;
  case 'c':
    ++First;
    return getDerived().parseIntegerLiteral("char");
  case 'a':
    ++First;
    return getDerived().parseIntegerLiteral("signed char");
  case 'h':
    ++First;
    return getDerived().parseIntegerLiteral("unsigned char");
  case 's':
    ++First;
    return getDerived().parseIntegerLiteral("short");
  case 't':
    ++First;
    return getDerived().parseIntegerLiteral("unsigned short");
  case 'i':
    ++First;
    return getDerived().parseIntegerLiteral("");
  case 'j':
    ++First;
    return getDerived().parseIntegerLiteral("u");
  case 'l':
    ++First;
    return getDerived().parseIntegerLiteral("l");
  case 'm':
    ++First;
    return getDerived().parseIntegerLiteral("ul");
  case 'x':
    ++First;
    return getDerived().parseIntegerLiteral("ll");
  case 'y':
    ++First;
    return getDerived().parseIntegerLiteral("ull");
  case 'n':
    ++First;
    return getDerived().parseIntegerLiteral("__int128");
  case 'o':
    ++First;
    return getDerived().parseIntegerLiteral("unsigned __int128");
  case 'f':
    ++First;
    return getDerived().template parseFloatingLiteral<float>();
  case 'd':
    ++First;
    return getDerived().template parseFloatingLiteral<double>();
  case 'e':
    ++First;
#if defined(__powerpc__) || defined(__s390__)
    // Handle cases where long doubles encoded with e have the same size
    // and representation as doubles.
    return getDerived().template parseFloatingLiteral<double>();
#else
    return getDerived().template parseFloatingLiteral<long double>();
#endif
  case '_':
    if (consumeIf("_Z")) {
      Node *R = getDerived().parseEncoding();
      if (R != nullptr && consumeIf('E'))
        return R;
    }
    return nullptr;
  case 'A': {
    Node *T = getDerived().parseType();
    if (T == nullptr)
      return nullptr;
    // FIXME: We need to include the string contents in the mangling.
    if (consumeIf('E'))
      return make<StringLiteral>(T);
    return nullptr;
  }
  case 'D':
    if (consumeIf("Dn") && (consumeIf('0'), consumeIf('E')))
      return make<NameType>("nullptr");
    return nullptr;
  case 'T':
    // Invalid mangled name per
    //   http://sourcerytools.com/pipermail/cxx-abi-dev/2011-August/002422.html
    return nullptr;
  case 'U': {
    // FIXME: Should we support LUb... for block literals?
    if (look(1) != 'l')
      return nullptr;
    Node *T = parseUnnamedTypeName(nullptr);
    if (!T || !consumeIf('E'))
      return nullptr;
    return make<LambdaExpr>(T);
  }
  default: {
    // might be named type
    Node *T = getDerived().parseType();
    if (T == nullptr)
      return nullptr;
    std::string_view N = parseNumber(/*AllowNegative=*/true);
    if (N.empty())
      return nullptr;
    if (!consumeIf('E'))
      return nullptr;
    return make<EnumLiteral>(T, N);
  }
  }
}

// <braced-expression> ::= <expression>
//                     ::= di <field source-name> <braced-expression>    # .name = expr
//                     ::= dx <index expression> <braced-expression>     # [expr] = expr
//                     ::= dX <range begin expression> <range end expression> <braced-expression>
/// Parse a braced initializer expression.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseBracedExpr() {
  if (look() == 'd') {
    switch (look(1)) {
    case 'i': {
      First += 2;
      Node *Field = getDerived().parseSourceName(/*NameState=*/nullptr);
      if (Field == nullptr)
        return nullptr;
      Node *Init = getDerived().parseBracedExpr();
      if (Init == nullptr)
        return nullptr;
      return make<BracedExpr>(Field, Init, /*isArray=*/false);
    }
    case 'x': {
      First += 2;
      Node *Index = getDerived().parseExpr();
      if (Index == nullptr)
        return nullptr;
      Node *Init = getDerived().parseBracedExpr();
      if (Init == nullptr)
        return nullptr;
      return make<BracedExpr>(Index, Init, /*isArray=*/true);
    }
    case 'X': {
      First += 2;
      Node *RangeBegin = getDerived().parseExpr();
      if (RangeBegin == nullptr)
        return nullptr;
      Node *RangeEnd = getDerived().parseExpr();
      if (RangeEnd == nullptr)
        return nullptr;
      Node *Init = getDerived().parseBracedExpr();
      if (Init == nullptr)
        return nullptr;
      return make<BracedRangeExpr>(RangeBegin, RangeEnd, Init);
    }
    }
  }
  return getDerived().parseExpr();
}

// (not yet in the spec)
// <fold-expr> ::= fL <binary-operator-name> <expression> <expression>
//             ::= fR <binary-operator-name> <expression> <expression>
//             ::= fl <binary-operator-name> <expression>
//             ::= fr <binary-operator-name> <expression>
/// Parse a fold expression.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseFoldExpr() {
  if (!consumeIf('f'))
    return nullptr;

  bool IsLeftFold = false, HasInitializer = false;
  switch (look()) {
  default:
    return nullptr;
  case 'L':
    IsLeftFold = true;
    HasInitializer = true;
    break;
  case 'R':
    HasInitializer = true;
    break;
  case 'l':
    IsLeftFold = true;
    break;
  case 'r':
    break;
  }
  ++First;

  const auto *Op = parseOperatorEncoding();
  if (!Op)
    return nullptr;
  if (!(Op->getKind() == OperatorInfo::Binary
        || (Op->getKind() == OperatorInfo::Member
            && Op->getName().back() == '*')))
    return nullptr;

  Node *Pack = getDerived().parseExpr();
  if (Pack == nullptr)
    return nullptr;

  Node *Init = nullptr;
  if (HasInitializer) {
    Init = getDerived().parseExpr();
    if (Init == nullptr)
      return nullptr;
  }

  if (IsLeftFold && Init)
    std::swap(Pack, Init);

  return make<FoldExpr>(IsLeftFold, Op->getSymbol(), Pack, Init);
}

// <expression> ::= mc <parameter type> <expr> [<offset number>] E
//
// Not yet in the spec: https://github.com/itanium-cxx-abi/cxx-abi/issues/47
/// Parse a pointer-to-member conversion expression.
/// \param Prec The prec.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parsePointerToMemberConversionExpr(
    Node::Prec Prec) {
  Node *Ty = getDerived().parseType();
  if (!Ty)
    return nullptr;
  Node *Expr = getDerived().parseExpr();
  if (!Expr)
    return nullptr;
  std::string_view Offset = getDerived().parseNumber(true);
  if (!consumeIf('E'))
    return nullptr;
  return make<PointerToMemberConversionExpr>(Ty, Expr, Offset, Prec);
}

// <expression> ::= so <referent type> <expr> [<offset number>] <union-selector>* [p] E
// <union-selector> ::= _ [<number>]
//
// Not yet in the spec: https://github.com/itanium-cxx-abi/cxx-abi/issues/47
/// Parse a subobject expression.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSubobjectExpr() {
  Node *Ty = getDerived().parseType();
  if (!Ty)
    return nullptr;
  Node *Expr = getDerived().parseExpr();
  if (!Expr)
    return nullptr;
  std::string_view Offset = getDerived().parseNumber(true);
  size_t SelectorsBegin = Names.size();
  while (consumeIf('_')) {
    Node *Selector = make<NameType>(parseNumber());
    if (!Selector)
      return nullptr;
    Names.push_back(Selector);
  }
  bool OnePastTheEnd = consumeIf('p');
  if (!consumeIf('E'))
    return nullptr;
  return make<SubobjectExpr>(
      Ty, Expr, Offset, popTrailingNodeArray(SelectorsBegin), OnePastTheEnd);
}

/// Parse a constraint expression.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseConstraintExpr() {
  // Within this expression, all enclosing template parameter lists are in
  // scope.
  ScopedOverride<bool> SaveIncompleteTemplateParameterTracking(
      HasIncompleteTemplateParameterTracking, true);
  return getDerived().parseExpr();
}

/// Parse a requires-expression.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseRequiresExpr() {
  NodeArray Params;
  if (consumeIf("rQ")) {
    // <expression> ::= rQ <bare-function-type> _ <requirement>+ E
    size_t ParamsBegin = Names.size();
    while (!consumeIf('_')) {
      Node *Type = getDerived().parseType();
      if (Type == nullptr)
        return nullptr;
      Names.push_back(Type);
    }
    Params = popTrailingNodeArray(ParamsBegin);
  } else if (!consumeIf("rq")) {
    // <expression> ::= rq <requirement>+ E
    return nullptr;
  }

  size_t ReqsBegin = Names.size();
  do {
    Node *Constraint = nullptr;
    if (consumeIf('X')) {
      // <requirement> ::= X <expression> [N] [R <type-constraint>]
      Node *Expr = getDerived().parseExpr();
      if (Expr == nullptr)
        return nullptr;
      bool Noexcept = consumeIf('N');
      Node *TypeReq = nullptr;
      if (consumeIf('R')) {
        TypeReq = getDerived().parseName();
        if (TypeReq == nullptr)
          return nullptr;
      }
      Constraint = make<ExprRequirement>(Expr, Noexcept, TypeReq);
    } else if (consumeIf('T')) {
      // <requirement> ::= T <type>
      Node *Type = getDerived().parseType();
      if (Type == nullptr)
        return nullptr;
      Constraint = make<TypeRequirement>(Type);
    } else if (consumeIf('Q')) {
      // <requirement> ::= Q <constraint-expression>
      //
      // FIXME: We use <expression> instead of <constraint-expression>. Either
      // the requires expression is already inside a constraint expression, in
      // which case it makes no difference, or we're in a requires-expression
      // that might be partially-substituted, where the language behavior is
      // not yet settled and clang mangles after substitution.
      Node *NestedReq = getDerived().parseExpr();
      if (NestedReq == nullptr)
        return nullptr;
      Constraint = make<NestedRequirement>(NestedReq);
    }
    if (Constraint == nullptr)
      return nullptr;
    Names.push_back(Constraint);
  } while (!consumeIf('E'));

  return make<RequiresExpr>(Params, popTrailingNodeArray(ReqsBegin));
}

// <expression> ::= <unary operator-name> <expression>
//              ::= <binary operator-name> <expression> <expression>
//              ::= <ternary operator-name> <expression> <expression> <expression>
//              ::= cl <expression>+ E                                   # call
//              ::= cp <base-unresolved-name> <expression>* E            # (name) (expr-list), call that would use argument-dependent lookup but for the parentheses
//              ::= cv <type> <expression>                               # conversion with one argument
//              ::= cv <type> _ <expression>* E                          # conversion with a different number of arguments
//              ::= [gs] nw <expression>* _ <type> E                     # new (expr-list) type
//              ::= [gs] nw <expression>* _ <type> <initializer>         # new (expr-list) type (init)
//              ::= [gs] na <expression>* _ <type> E                     # new[] (expr-list) type
//              ::= [gs] na <expression>* _ <type> <initializer>         # new[] (expr-list) type (init)
//              ::= [gs] dl <expression>                                 # delete expression
//              ::= [gs] da <expression>                                 # delete[] expression
//              ::= pp_ <expression>                                     # prefix ++
//              ::= mm_ <expression>                                     # prefix --
//              ::= ti <type>                                            # typeid (type)
//              ::= te <expression>                                      # typeid (expression)
//              ::= dc <type> <expression>                               # dynamic_cast<type> (expression)
//              ::= sc <type> <expression>                               # static_cast<type> (expression)
//              ::= cc <type> <expression>                               # const_cast<type> (expression)
//              ::= rc <type> <expression>                               # reinterpret_cast<type> (expression)
//              ::= st <type>                                            # sizeof (a type)
//              ::= sz <expression>                                      # sizeof (an expression)
//              ::= at <type>                                            # alignof (a type)
//              ::= az <expression>                                      # alignof (an expression)
//              ::= nx <expression>                                      # noexcept (expression)
//              ::= <template-param>
//              ::= <function-param>
//              ::= dt <expression> <unresolved-name>                    # expr.name
//              ::= pt <expression> <unresolved-name>                    # expr->name
//              ::= ds <expression> <expression>                         # expr.*expr
//              ::= sZ <template-param>                                  # size of a parameter pack
//              ::= sZ <function-param>                                  # size of a function parameter pack
//              ::= sP <template-arg>* E                                 # sizeof...(T), size of a captured template parameter pack from an alias template
//              ::= sp <expression>                                      # pack expansion
//              ::= tw <expression>                                      # throw expression
//              ::= tr                                                   # throw with no operand (rethrow)
//              ::= <unresolved-name>                                    # f(p), N::f(p), ::f(p),
//                                                                       # freestanding dependent name (e.g., T::x),
//                                                                       # objectless nonstatic member reference
//              ::= fL <binary-operator-name> <expression> <expression>
//              ::= fR <binary-operator-name> <expression> <expression>
//              ::= fl <binary-operator-name> <expression>
//              ::= fr <binary-operator-name> <expression>
//              ::= <expr-primary>
/// Parse an <expression> production.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseExpr() {
  bool Global = consumeIf("gs");

  const auto *Op = parseOperatorEncoding();
  if (Op) {
    auto Sym = Op->getSymbol();
    switch (Op->getKind()) {
    case OperatorInfo::Binary:
      // Binary operator: lhs @ rhs
      return getDerived().parseBinaryExpr(Sym, Op->getPrecedence());
    case OperatorInfo::Prefix:
      // Prefix unary operator: @ expr
      return getDerived().parsePrefixExpr(Sym, Op->getPrecedence());
    case OperatorInfo::Postfix: {
      // Postfix unary operator: expr @
      if (consumeIf('_'))
        return getDerived().parsePrefixExpr(Sym, Op->getPrecedence());
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return nullptr;
      return make<PostfixExpr>(Ex, Sym, Op->getPrecedence());
    }
    case OperatorInfo::Array: {
      // Array Index:  lhs [ rhs ]
      Node *Base = getDerived().parseExpr();
      if (Base == nullptr)
        return nullptr;
      Node *Index = getDerived().parseExpr();
      if (Index == nullptr)
        return nullptr;
      return make<ArraySubscriptExpr>(Base, Index, Op->getPrecedence());
    }
    case OperatorInfo::Member: {
      // Member access lhs @ rhs
      Node *LHS = getDerived().parseExpr();
      if (LHS == nullptr)
        return nullptr;
      Node *RHS = getDerived().parseExpr();
      if (RHS == nullptr)
        return nullptr;
      return make<MemberExpr>(LHS, Sym, RHS, Op->getPrecedence());
    }
    case OperatorInfo::New: {
      // New
      // # new (expr-list) type [(init)]
      // [gs] nw <expression>* _ <type> [pi <expression>*] E
      // # new[] (expr-list) type [(init)]
      // [gs] na <expression>* _ <type> [pi <expression>*] E
      size_t Exprs = Names.size();
      while (!consumeIf('_')) {
        Node *Ex = getDerived().parseExpr();
        if (Ex == nullptr)
          return nullptr;
        Names.push_back(Ex);
      }
      NodeArray ExprList = popTrailingNodeArray(Exprs);
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      bool HaveInits = consumeIf("pi");
      size_t InitsBegin = Names.size();
      while (!consumeIf('E')) {
        if (!HaveInits)
          return nullptr;
        Node *Init = getDerived().parseExpr();
        if (Init == nullptr)
          return Init;
        Names.push_back(Init);
      }
      NodeArray Inits = popTrailingNodeArray(InitsBegin);
      return make<NewExpr>(ExprList, Ty, Inits, Global,
                           /*IsArray=*/Op->getFlag(), Op->getPrecedence());
    }
    case OperatorInfo::Del: {
      // Delete
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return nullptr;
      return make<DeleteExpr>(Ex, Global, /*IsArray=*/Op->getFlag(),
                              Op->getPrecedence());
    }
    case OperatorInfo::Call: {
      // Function Call
      Node *Callee = getDerived().parseExpr();
      if (Callee == nullptr)
        return nullptr;
      size_t ExprsBegin = Names.size();
      while (!consumeIf('E')) {
        Node *E = getDerived().parseExpr();
        if (E == nullptr)
          return nullptr;
        Names.push_back(E);
      }
      return make<CallExpr>(Callee, popTrailingNodeArray(ExprsBegin),
                            /*IsParen=*/Op->getFlag(), Op->getPrecedence());
    }
    case OperatorInfo::CCast: {
      // C Cast: (type)expr
      Node *Ty;
      {
        ScopedOverride<bool> SaveTemp(TryToParseTemplateArgs, false);
        Ty = getDerived().parseType();
      }
      if (Ty == nullptr)
        return nullptr;

      size_t ExprsBegin = Names.size();
      bool IsMany = consumeIf('_');
      while (!consumeIf('E')) {
        Node *E = getDerived().parseExpr();
        if (E == nullptr)
          return E;
        Names.push_back(E);
        if (!IsMany)
          break;
      }
      NodeArray Exprs = popTrailingNodeArray(ExprsBegin);
      if (!IsMany && Exprs.size() != 1)
        return nullptr;
      return make<ConversionExpr>(Ty, Exprs, Op->getPrecedence());
    }
    case OperatorInfo::Conditional: {
      // Conditional operator: expr ? expr : expr
      Node *Cond = getDerived().parseExpr();
      if (Cond == nullptr)
        return nullptr;
      Node *LHS = getDerived().parseExpr();
      if (LHS == nullptr)
        return nullptr;
      Node *RHS = getDerived().parseExpr();
      if (RHS == nullptr)
        return nullptr;
      return make<ConditionalExpr>(Cond, LHS, RHS, Op->getPrecedence());
    }
    case OperatorInfo::NamedCast: {
      // Named cast operation, @<type>(expr)
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      Node *Ex = getDerived().parseExpr();
      if (Ex == nullptr)
        return nullptr;
      return make<CastExpr>(Sym, Ty, Ex, Op->getPrecedence());
    }
    case OperatorInfo::OfIdOp: {
      // [sizeof/alignof/typeid] ( <type>|<expr> )
      Node *Arg =
          Op->getFlag() ? getDerived().parseType() : getDerived().parseExpr();
      if (!Arg)
        return nullptr;
      return make<EnclosingExpr>(Sym, Arg, Op->getPrecedence());
    }
    case OperatorInfo::NameOnly: {
      // Not valid as an expression operand.
      return nullptr;
    }
    }
    DEMANGLE_UNREACHABLE;
  }

  if (numLeft() < 2)
    return nullptr;

  if (look() == 'L')
    return getDerived().parseExprPrimary();
  if (look() == 'T')
    return getDerived().parseTemplateParam();
  if (look() == 'f') {
    // Disambiguate a fold expression from a <function-param>.
    if (look(1) == 'p' || (look(1) == 'L' && std::isdigit(look(2))))
      return getDerived().parseFunctionParam();
    return getDerived().parseFoldExpr();
  }
  if (consumeIf("il")) {
    size_t InitsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *E = getDerived().parseBracedExpr();
      if (E == nullptr)
        return nullptr;
      Names.push_back(E);
    }
    return make<InitListExpr>(nullptr, popTrailingNodeArray(InitsBegin));
  }
  if (consumeIf("mc"))
    return parsePointerToMemberConversionExpr(Node::Prec::Unary);
  if (consumeIf("nx")) {
    Node *Ex = getDerived().parseExpr();
    if (Ex == nullptr)
      return Ex;
    return make<EnclosingExpr>("noexcept ", Ex, Node::Prec::Unary);
  }
  if (look() == 'r' && (look(1) == 'q' || look(1) == 'Q'))
    return parseRequiresExpr();
  if (consumeIf("so"))
    return parseSubobjectExpr();
  if (consumeIf("sp")) {
    Node *Child = getDerived().parseExpr();
    if (Child == nullptr)
      return nullptr;
    return make<ParameterPackExpansion>(Child);
  }
  if (consumeIf("sy")) {
    Node *Pattern = look() == 'T' ? getDerived().parseTemplateParam()
                                  : getDerived().parseFunctionParam();
    if (Pattern == nullptr)
      return nullptr;
    Node *Index = getDerived().parseExpr();
    if (Index == nullptr)
      return nullptr;
    return make<PackIndexing>(Pattern, Index);
  }
  if (consumeIf("sZ")) {
    if (look() == 'T') {
      Node *R = getDerived().parseTemplateParam();
      if (R == nullptr)
        return nullptr;
      return make<SizeofParamPackExpr>(R);
    }
    Node *FP = getDerived().parseFunctionParam();
    if (FP == nullptr)
      return nullptr;
    return make<EnclosingExpr>("sizeof... ", FP);
  }
  if (consumeIf("sP")) {
    size_t ArgsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
    }
    auto *Pack = make<NodeArrayNode>(popTrailingNodeArray(ArgsBegin));
    if (!Pack)
      return nullptr;
    return make<EnclosingExpr>("sizeof... ", Pack);
  }
  if (consumeIf("tl")) {
    Node *Ty = getDerived().parseType();
    if (Ty == nullptr)
      return nullptr;
    size_t InitsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *E = getDerived().parseBracedExpr();
      if (E == nullptr)
        return nullptr;
      Names.push_back(E);
    }
    return make<InitListExpr>(Ty, popTrailingNodeArray(InitsBegin));
  }
  if (consumeIf("tr"))
    return make<NameType>("throw");
  if (consumeIf("tw")) {
    Node *Ex = getDerived().parseExpr();
    if (Ex == nullptr)
      return nullptr;
    return make<ThrowExpr>(Ex);
  }
  if (consumeIf('u')) {
    Node *Name = getDerived().parseSourceName(/*NameState=*/nullptr);
    if (!Name)
      return nullptr;
    // Special case legacy __uuidof mangling. The 't' and 'z' appear where the
    // standard encoding expects a <template-arg>, and would be otherwise be
    // interpreted as <type> node 'short' or 'ellipsis'. However, neither
    // __uuidof(short) nor __uuidof(...) can actually appear, so there is no
    // actual conflict here.
    bool IsUUID = false;
    Node *UUID = nullptr;
    if (Name->getBaseName() == "__uuidof") {
      if (consumeIf('t')) {
        UUID = getDerived().parseType();
        IsUUID = true;
      } else if (consumeIf('z')) {
        UUID = getDerived().parseExpr();
        IsUUID = true;
      }
    }
    size_t ExprsBegin = Names.size();
    if (IsUUID) {
      if (UUID == nullptr)
        return nullptr;
      Names.push_back(UUID);
    } else {
      while (!consumeIf('E')) {
        Node *E = getDerived().parseTemplateArg();
        if (E == nullptr)
          return E;
        Names.push_back(E);
      }
    }
    return make<CallExpr>(Name, popTrailingNodeArray(ExprsBegin),
                          /*IsParen=*/false, Node::Prec::Postfix);
  }

  // Only unresolved names remain.
  return getDerived().parseUnresolvedName(Global);
}

// <call-offset> ::= h <nv-offset> _
//               ::= v <v-offset> _
//
// <nv-offset> ::= <offset number>
//               # non-virtual base override
//
// <v-offset>  ::= <offset number> _ <virtual offset number>
//               # virtual base override, with vcall offset
/// Parse a <call-offset> production.
/// \return True if a <call-offset> was successfully parsed.
template <typename Alloc, typename Derived>
bool AbstractManglingParser<Alloc, Derived>::parseCallOffset() {
  // Just scan through the call offset, we never add this information into the
  // output.
  if (consumeIf('h'))
    return parseNumber(true).empty() || !consumeIf('_');
  if (consumeIf('v'))
    return parseNumber(true).empty() || !consumeIf('_') ||
           parseNumber(true).empty() || !consumeIf('_');
  return true;
}

// <special-name> ::= TV <type>    # virtual table
//                ::= TT <type>    # VTT structure (construction vtable index)
//                ::= TI <type>    # typeinfo structure
//                ::= TS <type>    # typeinfo name (null-terminated byte string)
//                ::= Tc <call-offset> <call-offset> <base encoding>
//                    # base is the nominal target function of thunk
//                    # first call-offset is 'this' adjustment
//                    # second call-offset is result adjustment
//                ::= T <call-offset> <base encoding>
//                    # base is the nominal target function of thunk
//                # Guard variable for one-time initialization
//                ::= GV <object name>
//                                     # No <type>
//                ::= TW <object name> # Thread-local wrapper
//                ::= TH <object name> # Thread-local initialization
//                ::= GR <object name> _             # First temporary
//                ::= GR <object name> <seq-id> _    # Subsequent temporaries
//                # construction vtable for second-in-first
//      extension ::= TC <first type> <number> _ <second type>
//      extension ::= GR <object name> # reference temporary for object
//      extension ::= GI <module name> # module global initializer
/// Parse a <special-name> production.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSpecialName() {
  switch (look()) {
  case 'T':
    switch (look(1)) {
    // TA <template-arg>    # template parameter object
    //
    // Not yet in the spec: https://github.com/itanium-cxx-abi/cxx-abi/issues/63
    case 'A': {
      First += 2;
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      return make<SpecialName>("template parameter object for ", Arg);
    }
    // TV <type>    # virtual table
    case 'V': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<SpecialName>("vtable for ", Ty);
    }
    // TT <type>    # VTT structure (construction vtable index)
    case 'T': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<SpecialName>("VTT for ", Ty);
    }
    // TI <type>    # typeinfo structure
    case 'I': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<SpecialName>("typeinfo for ", Ty);
    }
    // TS <type>    # typeinfo name (null-terminated byte string)
    case 'S': {
      First += 2;
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;
      return make<SpecialName>("typeinfo name for ", Ty);
    }
    // Tc <call-offset> <call-offset> <base encoding>
    case 'c': {
      First += 2;
      if (parseCallOffset() || parseCallOffset())
        return nullptr;
      Node *Encoding = getDerived().parseEncoding();
      if (Encoding == nullptr)
        return nullptr;
      return make<SpecialName>("covariant return thunk to ", Encoding);
    }
    // extension ::= TC <first type> <number> _ <second type>
    //               # construction vtable for second-in-first
    case 'C': {
      First += 2;
      Node *FirstType = getDerived().parseType();
      if (FirstType == nullptr)
        return nullptr;
      if (parseNumber(true).empty() || !consumeIf('_'))
        return nullptr;
      Node *SecondType = getDerived().parseType();
      if (SecondType == nullptr)
        return nullptr;
      return make<CtorVtableSpecialName>(SecondType, FirstType);
    }
    // TW <object name> # Thread-local wrapper
    case 'W': {
      First += 2;
      Node *Name = getDerived().parseName();
      if (Name == nullptr)
        return nullptr;
      return make<SpecialName>("thread-local wrapper routine for ", Name);
    }
    // TH <object name> # Thread-local initialization
    case 'H': {
      First += 2;
      Node *Name = getDerived().parseName();
      if (Name == nullptr)
        return nullptr;
      return make<SpecialName>("thread-local initialization routine for ", Name);
    }
    // T <call-offset> <base encoding>
    default: {
      ++First;
      bool IsVirt = look() == 'v';
      if (parseCallOffset())
        return nullptr;
      Node *BaseEncoding = getDerived().parseEncoding();
      if (BaseEncoding == nullptr)
        return nullptr;
      if (IsVirt)
        return make<SpecialName>("virtual thunk to ", BaseEncoding);
      else
        return make<SpecialName>("non-virtual thunk to ", BaseEncoding);
    }
    }
  case 'G':
    switch (look(1)) {
    // GV <object name> # Guard variable for one-time initialization
    case 'V': {
      First += 2;
      Node *Name = getDerived().parseName();
      if (Name == nullptr)
        return nullptr;
      return make<SpecialName>("guard variable for ", Name);
    }
    // GR <object name> # reference temporary for object
    // GR <object name> _             # First temporary
    // GR <object name> <seq-id> _    # Subsequent temporaries
    case 'R': {
      First += 2;
      Node *Name = getDerived().parseName();
      if (Name == nullptr)
        return nullptr;
      size_t Count;
      bool ParsedSeqId = !parseSeqId(&Count);
      if (!consumeIf('_') && ParsedSeqId)
        return nullptr;
      return make<SpecialName>("reference temporary for ", Name);
    }
    // GI <module-name> v
    case 'I': {
      First += 2;
      ModuleName *Module = nullptr;
      if (getDerived().parseModuleNameOpt(Module))
        return nullptr;
      if (Module == nullptr)
        return nullptr;
      return make<SpecialName>("initializer for module ", Module);
    }
    }
  }
  return nullptr;
}

// <encoding> ::= <function name> <bare-function-type>
//                    [`Q` <requires-clause expr>]
//            ::= <data name>
//            ::= <special-name>
/// Parse an <encoding> production.
/// \param ParseParams When false, skip function parameter types.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseEncoding(bool ParseParams) {
  // The template parameters of an encoding are unrelated to those of the
  // enclosing context.
  SaveTemplateParams SaveTemplateParamsScope(this);

  if (look() == 'G' || look() == 'T')
    return getDerived().parseSpecialName();

  auto IsEndOfEncoding = [&] {
    // The set of chars that can potentially follow an <encoding> (none of which
    // can start a <type>). Enumerating these allows us to avoid speculative
    // parsing.
    return numLeft() == 0 || look() == 'E' || look() == '.' || look() == '_';
  };

  NameState NameInfo(this);
  Node *Name = getDerived().parseName(&NameInfo);
  if (Name == nullptr)
    return nullptr;

  if (resolveForwardTemplateRefs(NameInfo))
    return nullptr;

  if (IsEndOfEncoding())
    return Name;

  // ParseParams may be false at the top level only, when called from parse().
  // For example in the mangled name _Z3fooILZ3BarEET_f, ParseParams may be
  // false when demangling 3fooILZ3BarEET_f but is always true when demangling
  // 3Bar.
  if (!ParseParams) {
    while (consume())
      ;
    return Name;
  }

  Node *Attrs = nullptr;
  if (consumeIf("Ua9enable_ifI")) {
    size_t BeforeArgs = Names.size();
    while (!consumeIf('E')) {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
    }
    Attrs = make<EnableIfAttr>(popTrailingNodeArray(BeforeArgs));
    if (!Attrs)
      return nullptr;
  }

  Node *ReturnType = nullptr;
  if (!NameInfo.CtorDtorConversion && NameInfo.EndsWithTemplateArgs) {
    ReturnType = getDerived().parseType();
    if (ReturnType == nullptr)
      return nullptr;
  }

  NodeArray Params;
  if (!consumeIf('v')) {
    size_t ParamsBegin = Names.size();
    do {
      Node *Ty = getDerived().parseType();
      if (Ty == nullptr)
        return nullptr;

      const bool IsFirstParam = ParamsBegin == Names.size();
      if (NameInfo.HasExplicitObjectParameter && IsFirstParam)
        Ty = make<ExplicitObjectParameter>(Ty);

      if (Ty == nullptr)
        return nullptr;

      Names.push_back(Ty);
    } while (!IsEndOfEncoding() && look() != 'Q');
    Params = popTrailingNodeArray(ParamsBegin);
  }

  Node *Requires = nullptr;
  if (consumeIf('Q')) {
    Requires = getDerived().parseConstraintExpr();
    if (!Requires)
      return nullptr;
  }

  return make<FunctionEncoding>(ReturnType, Name, Params, Attrs, Requires,
                                NameInfo.CVQualifiers,
                                NameInfo.ReferenceQualifier);
}

template <class Float>
struct FloatData;

/// FloatData traits for `float`.
template <> struct FloatData<float> {
  /// Number of mangled hex characters in the literal encoding.
  static const size_t mangled_size = 8;
  /// Maximum characters needed for the demangled spelling.
  static const size_t max_demangled_size = 24;
  /// printf-style conversion specifier for this type.
  static constexpr const char *spec = "%af";
};

/// FloatData traits for `double`.
template <> struct FloatData<double> {
  /// Number of mangled hex characters in the literal encoding.
  static const size_t mangled_size = 16;
  /// Maximum characters needed for the demangled spelling.
  static const size_t max_demangled_size = 32;
  /// printf-style conversion specifier for this type.
  static constexpr const char *spec = "%a";
};

/// FloatData traits for `long double`.
template <> struct FloatData<long double> {
  /// Number of mangled hex characters in the literal encoding.
  static const size_t mangled_size = 32;
  /// Maximum characters needed for the demangled spelling.
  static const size_t max_demangled_size = 42;
  /// printf-style conversion specifier for this type.
  static constexpr const char *spec = "%LaL";
};

/// Parse a floating-point literal of type Float.
/// \return The parsed AST node, or nullptr on failure.
template <typename Alloc, typename Derived>
template <class Float>
Node *AbstractManglingParser<Alloc, Derived>::parseFloatingLiteral() {
  const size_t N = FloatData<Float>::mangled_size;
  if (numLeft() <= N)
    return nullptr;
  std::string_view Data(First, N);
  for (char C : Data)
    if (!(C >= '0' && C <= '9') && !(C >= 'a' && C <= 'f'))
      return nullptr;
  First += N;
  if (!consumeIf('E'))
    return nullptr;
  return make<FloatLiteralImpl<Float>>(Data);
}

// <seq-id> ::= <0-9A-Z>+
/// Parse a <seq-id> production into Out.
/// \param Out Destination for the parsed integer or sequence id.
/// \return True on success.
template <typename Alloc, typename Derived>
bool AbstractManglingParser<Alloc, Derived>::parseSeqId(size_t *Out) {
  if (!(look() >= '0' && look() <= '9') &&
      !(look() >= 'A' && look() <= 'Z'))
    return true;

  size_t Id = 0;
  while (true) {
    if (look() >= '0' && look() <= '9') {
      Id *= 36;
      Id += static_cast<size_t>(look() - '0');
    } else if (look() >= 'A' && look() <= 'Z') {
      Id *= 36;
      Id += static_cast<size_t>(look() - 'A') + 10;
    } else {
      *Out = Id;
      return false;
    }
    ++First;
  }
}

// <substitution> ::= S <seq-id> _
//                ::= S_
// <substitution> ::= Sa # ::std::allocator
// <substitution> ::= Sb # ::std::basic_string
// <substitution> ::= Ss # ::std::basic_string < char,
//                                               ::std::char_traits<char>,
//                                               ::std::allocator<char> >
// <substitution> ::= Si # ::std::basic_istream<char,  std::char_traits<char> >
// <substitution> ::= So # ::std::basic_ostream<char,  std::char_traits<char> >
// <substitution> ::= Sd # ::std::basic_iostream<char, std::char_traits<char> >
// The St case is handled specially in parseNestedName.
/// Parse a <substitution> production.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseSubstitution() {
  if (!consumeIf('S'))
    return nullptr;

  if (look() >= 'a' && look() <= 'z') {
    SpecialSubKind Kind;
    switch (look()) {
    case 'a':
      Kind = SpecialSubKind::allocator;
      break;
    case 'b':
      Kind = SpecialSubKind::basic_string;
      break;
    case 'd':
      Kind = SpecialSubKind::iostream;
      break;
    case 'i':
      Kind = SpecialSubKind::istream;
      break;
    case 'o':
      Kind = SpecialSubKind::ostream;
      break;
    case 's':
      Kind = SpecialSubKind::string;
      break;
    default:
      return nullptr;
    }
    ++First;
    auto *SpecialSub = make<SpecialSubstitution>(Kind);
    if (!SpecialSub)
      return nullptr;

    // Itanium C++ ABI 5.1.2: If a name that would use a built-in <substitution>
    // has ABI tags, the tags are appended to the substitution; the result is a
    // substitutable component.
    Node *WithTags = getDerived().parseAbiTags(SpecialSub);
    if (WithTags != SpecialSub) {
      Subs.push_back(WithTags);
      SpecialSub = WithTags;
    }
    return SpecialSub;
  }

  //                ::= S_
  if (consumeIf('_')) {
    if (Subs.empty())
      return nullptr;
    return Subs[0];
  }

  //                ::= S <seq-id> _
  size_t Index = 0;
  if (parseSeqId(&Index))
    return nullptr;
  ++Index;
  if (!consumeIf('_') || Index >= Subs.size())
    return nullptr;
  return Subs[Index];
}

// <template-param> ::= T_    # first template parameter
//                  ::= T <parameter-2 non-negative number> _
//                  ::= TL <level-1> __
//                  ::= TL <level-1> _ <parameter-2 non-negative number> _
/// Parse a <template-param> production.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseTemplateParam() {
  const char *Begin = First;
  if (!consumeIf('T'))
    return nullptr;

  size_t Level = 0;
  if (consumeIf('L')) {
    if (parsePositiveInteger(&Level))
      return nullptr;
    ++Level;
    if (!consumeIf('_'))
      return nullptr;
  }

  size_t Index = 0;
  if (!consumeIf('_')) {
    if (parsePositiveInteger(&Index))
      return nullptr;
    ++Index;
    if (!consumeIf('_'))
      return nullptr;
  }

  // We don't track enclosing template parameter levels well enough to reliably
  // substitute them all within a <constraint-expression>, so print the
  // parameter numbering instead for now.
  // TODO: Track all enclosing template parameters and substitute them here.
  if (HasIncompleteTemplateParameterTracking) {
    return make<NameType>(std::string_view(Begin, First - 1 - Begin));
  }

  // If we're in a context where this <template-param> refers to a
  // <template-arg> further ahead in the mangled name (currently just conversion
  // operator types), then we should only look it up in the right context.
  // This can only happen at the outermost level.
  if (PermitForwardTemplateReferences && Level == 0) {
    Node *ForwardRef = make<ForwardTemplateReference>(Index);
    if (!ForwardRef)
      return nullptr;
    DEMANGLE_ASSERT(ForwardRef->getKind() == Node::KForwardTemplateReference,
                    "");
    ForwardTemplateRefs.push_back(
        static_cast<ForwardTemplateReference *>(ForwardRef));
    return ForwardRef;
  }

  if (Level >= TemplateParams.size() || !TemplateParams[Level] ||
      Index >= TemplateParams[Level]->size()) {
    // Itanium ABI 5.1.8: In a generic lambda, uses of auto in the parameter
    // list are mangled as the corresponding artificial template type parameter.
    if (ParsingLambdaParamsAtLevel == Level && Level <= TemplateParams.size()) {
      // This will be popped by the ScopedTemplateParamList in
      // parseUnnamedTypeName.
      if (Level == TemplateParams.size())
        TemplateParams.push_back(nullptr);
      return make<NameType>("auto");
    }

    return nullptr;
  }

  return (*TemplateParams[Level])[Index];
}

// <template-param-decl> ::= Ty                          # type parameter
//                       ::= Tk <concept name> [<template-args>] # constrained type parameter
//                       ::= Tn <type>                   # non-type parameter
//                       ::= Tt <template-param-decl>* E # template parameter
//                       ::= Tp <template-param-decl>    # parameter pack
/// Parse a <template-param-decl> production.
/// \param Params The params.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseTemplateParamDecl(
    TemplateParamList *Params) {
  auto InventTemplateParamName = [&](TemplateParamKind Kind) {
    unsigned Index = NumSyntheticTemplateParameters[(int)Kind]++;
    Node *N = make<SyntheticTemplateParamName>(Kind, Index);
    if (N && Params)
      Params->push_back(N);
    return N;
  };

  if (consumeIf("Ty")) {
    Node *Name = InventTemplateParamName(TemplateParamKind::Type);
    if (!Name)
      return nullptr;
    return make<TypeTemplateParamDecl>(Name);
  }

  if (consumeIf("Tk")) {
    // We don't track enclosing template parameter levels well enough to
    // reliably demangle template parameter substitutions, so print an arbitrary
    // string in place of a parameter for now.
    // TODO: Track all enclosing template parameters and demangle substitutions.
    ScopedOverride<bool> SaveIncompleteTemplateParameterTrackingExpr(
        HasIncompleteTemplateParameterTracking, true);
    Node *Constraint = getDerived().parseName();
    if (!Constraint)
      return nullptr;
    Node *Name = InventTemplateParamName(TemplateParamKind::Type);
    if (!Name)
      return nullptr;
    return make<ConstrainedTypeTemplateParamDecl>(Constraint, Name);
  }

  if (consumeIf("Tn")) {
    Node *Name = InventTemplateParamName(TemplateParamKind::NonType);
    if (!Name)
      return nullptr;
    Node *Type = parseType();
    if (!Type)
      return nullptr;
    return make<NonTypeTemplateParamDecl>(Name, Type);
  }

  if (consumeIf("Tt")) {
    Node *Name = InventTemplateParamName(TemplateParamKind::Template);
    if (!Name)
      return nullptr;
    size_t ParamsBegin = Names.size();
    ScopedTemplateParamList TemplateTemplateParamParams(this);
    Node *Requires = nullptr;
    while (!consumeIf('E')) {
      Node *P = parseTemplateParamDecl(TemplateTemplateParamParams.params());
      if (!P)
        return nullptr;
      Names.push_back(P);
      if (consumeIf('Q')) {
        Requires = getDerived().parseConstraintExpr();
        if (Requires == nullptr || !consumeIf('E'))
          return nullptr;
        break;
      }
    }
    NodeArray InnerParams = popTrailingNodeArray(ParamsBegin);
    return make<TemplateTemplateParamDecl>(Name, InnerParams, Requires);
  }

  if (consumeIf("Tp")) {
    Node *P = parseTemplateParamDecl(Params);
    if (!P)
      return nullptr;
    return make<TemplateParamPackDecl>(P);
  }

  return nullptr;
}

// <template-arg> ::= <type>                    # type or template
//                ::= X <expression> E          # expression
//                ::= <expr-primary>            # simple expressions
//                ::= J <template-arg>* E       # argument pack
//                ::= LZ <encoding> E           # extension
//                ::= <template-param-decl> <template-arg>
/// Parse a <template-arg> production.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parseTemplateArg() {
  switch (look()) {
  case 'X': {
    ++First;
    Node *Arg = getDerived().parseExpr();
    if (Arg == nullptr || !consumeIf('E'))
      return nullptr;
    return Arg;
  }
  case 'J': {
    ++First;
    size_t ArgsBegin = Names.size();
    while (!consumeIf('E')) {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
    }
    NodeArray Args = popTrailingNodeArray(ArgsBegin);
    return make<TemplateArgumentPack>(Args);
  }
  case 'L': {
    //                ::= LZ <encoding> E           # extension
    if (look(1) == 'Z') {
      First += 2;
      Node *Arg = getDerived().parseEncoding();
      if (Arg == nullptr || !consumeIf('E'))
        return nullptr;
      return Arg;
    }
    //                ::= <expr-primary>            # simple expressions
    return getDerived().parseExprPrimary();
  }
  case 'T': {
    // Either <template-param> or a <template-param-decl> <template-arg>.
    if (!getDerived().isTemplateParamDecl())
      return getDerived().parseType();
    Node *Param = getDerived().parseTemplateParamDecl(nullptr);
    if (!Param)
      return nullptr;
    Node *Arg = getDerived().parseTemplateArg();
    if (!Arg)
      return nullptr;
    return make<TemplateParamQualifiedArg>(Param, Arg);
  }
  default:
    return getDerived().parseType();
  }
}

// <template-args> ::= I <template-arg>* [Q <requires-clause expr>] E
//     extension, the abi says <template-arg>+
/// Parse a <template-args> production.
/// \param TagTemplates When true, register template args for substitution.
/// \return The parsed AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *
AbstractManglingParser<Derived, Alloc>::parseTemplateArgs(bool TagTemplates) {
  if (!consumeIf('I'))
    return nullptr;

  // <template-params> refer to the innermost <template-args>. Clear out any
  // outer args that we may have inserted into TemplateParams.
  if (TagTemplates) {
    TemplateParams.clear();
    TemplateParams.push_back(&OuterTemplateParams);
    OuterTemplateParams.clear();
  }

  size_t ArgsBegin = Names.size();
  Node *Requires = nullptr;
  while (!consumeIf('E')) {
    if (TagTemplates) {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
      Node *TableEntry = Arg;
      if (Arg->getKind() == Node::KTemplateParamQualifiedArg) {
        TableEntry =
            static_cast<TemplateParamQualifiedArg *>(TableEntry)->getArg();
      }
      if (Arg->getKind() == Node::KTemplateArgumentPack) {
        TableEntry = make<ParameterPack>(
            static_cast<TemplateArgumentPack*>(TableEntry)->getElements());
        if (!TableEntry)
          return nullptr;
      }
      OuterTemplateParams.push_back(TableEntry);
    } else {
      Node *Arg = getDerived().parseTemplateArg();
      if (Arg == nullptr)
        return nullptr;
      Names.push_back(Arg);
    }
    if (consumeIf('Q')) {
      Requires = getDerived().parseConstraintExpr();
      if (!Requires || !consumeIf('E'))
        return nullptr;
      break;
    }
  }
  return make<TemplateArgs>(popTrailingNodeArray(ArgsBegin), Requires);
}

// <mangled-name> ::= _Z <encoding>
//                ::= <type>
// extension      ::= ___Z <encoding> _block_invoke
// extension      ::= ___Z <encoding> _block_invoke<decimal-digit>+
// extension      ::= ___Z <encoding> _block_invoke_<decimal-digit>+
// extension      ::= __alloc_token__Z <encoding>
// extension      ::= __alloc_token_<decimal-digit>+__Z <encoding>
/// Top-level entry point into the parser.
/// \param ParseParams When false, stop before function parameter types.
/// \return The root AST node, or nullptr on failure.
template <typename Derived, typename Alloc>
Node *AbstractManglingParser<Derived, Alloc>::parse(bool ParseParams) {
  bool AllocToken = consumeIf("__alloc_token_");
  if (AllocToken) {
    const char *Saved = First;
    if (parseNumber().empty() || !consumeIf('_'))
      First = Saved;
  }

  if (consumeIf("_Z") || consumeIf("__Z")) {
    Node *Encoding = getDerived().parseEncoding(ParseParams);
    if (Encoding == nullptr)
      return nullptr;
    if (look() == '.') {
      Encoding =
          make<DotSuffix>(Encoding, std::string_view(First, Last - First));
      First = Last;
    }
    if (AllocToken)
      Encoding = make<DotSuffix>(Encoding, ".alloc_token");
    if (numLeft() != 0)
      return nullptr;
    return Encoding;
  }

  if (consumeIf("___Z") || consumeIf("____Z")) {
    Node *Encoding = getDerived().parseEncoding(ParseParams);
    if (Encoding == nullptr || !consumeIf("_block_invoke"))
      return nullptr;
    bool RequireNumber = consumeIf('_');
    if (parseNumber().empty() && RequireNumber)
      return nullptr;
    if (look() == '.')
      First = Last;
    if (numLeft() != 0)
      return nullptr;
    return make<SpecialName>("invocation function for block in ", Encoding);
  }

  Node *Ty = getDerived().parseType();
  if (numLeft() != 0)
    return nullptr;
  return Ty;
}

/// Concrete Itanium mangling parser using allocator \p Alloc.
/// Concrete Itanium mangling parser using allocator Alloc.
template <typename Alloc>
struct ManglingParser : AbstractManglingParser<ManglingParser<Alloc>, Alloc> {
  /// Inherit the AbstractManglingParser constructors.
  using AbstractManglingParser<ManglingParser<Alloc>,
                               Alloc>::AbstractManglingParser;
};

/// Print the left-hand portion of \p N into this buffer.
/// \param N AST node to print.
inline void OutputBuffer::printLeft(const Node &N) { N.printLeft(*this); }

/// Print the right-hand portion of \p N into this buffer.
/// \param N AST node to print.
inline void OutputBuffer::printRight(const Node &N) { N.printRight(*this); }

} // namespace itanium_demangle
} // namespace llvm

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // DEMANGLE_ITANIUMDEMANGLE_H
