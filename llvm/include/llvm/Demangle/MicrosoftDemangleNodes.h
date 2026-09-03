//===- MicrosoftDemangleNodes.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the AST nodes used in the MSVC demangler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEMANGLE_MICROSOFTDEMANGLENODES_H
#define LLVM_DEMANGLE_MICROSOFTDEMANGLENODES_H

#include "DemangleConfig.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace llvm {
namespace itanium_demangle {
class OutputBuffer;
}
}

using llvm::itanium_demangle::OutputBuffer;

namespace llvm {
namespace ms_demangle {

/// Type and pointer CV / memory-model qualifiers encoded in Microsoft mangling.
enum Qualifiers : uint8_t {
  /// No qualifiers.
  Q_None = 0,
  /// `const` qualifier.
  Q_Const = 1 << 0,
  /// `volatile` qualifier.
  Q_Volatile = 1 << 1,
  /// Far pointer qualifier (legacy segmented memory).
  Q_Far = 1 << 2,
  /// Huge pointer qualifier (legacy segmented memory).
  Q_Huge = 1 << 3,
  /// `__unaligned` qualifier.
  Q_Unaligned = 1 << 4,
  /// `__restrict` qualifier.
  Q_Restrict = 1 << 5,
  /// `__ptr64` qualifier.
  Q_Pointer64 = 1 << 6
};

/// Storage class of a demangled variable symbol.
enum class StorageClass : uint8_t {
  /// No storage class.
  None,
  /// Private static data member.
  PrivateStatic,
  /// Protected static data member.
  ProtectedStatic,
  /// Public static data member.
  PublicStatic,
  /// Global (non-member) variable.
  Global,
  /// Function-local static variable.
  FunctionLocalStatic,
};

/// Whether a type is a pointer, lvalue reference, or rvalue reference.
enum class PointerAffinity {
  /// Not a pointer or reference.
  None,
  /// Ordinary pointer.
  Pointer,
  /// Lvalue reference.
  Reference,
  /// Rvalue reference.
  RValueReference
};

/// Ref-qualifier on a non-static member function (`&` or `&&`).
enum class FunctionRefQualifier {
  /// No ref-qualifier.
  None,
  /// Lvalue ref-qualifier (`&`).
  Reference,
  /// Rvalue ref-qualifier (`&&`).
  RValueReference
};

/// Calling convention encoded in a Microsoft function mangling.
enum class CallingConv : uint8_t {
  /// No calling convention specified.
  None,
  /// `__cdecl`.
  Cdecl,
  /// `__pascal`.
  Pascal,
  /// `__thiscall`.
  Thiscall,
  /// `__stdcall`.
  Stdcall,
  /// `__fastcall`.
  Fastcall,
  /// `__clrcall` (CLR).
  Clrcall,
  /// `__eabi`.
  Eabi,
  /// `__vectorcall`.
  Vectorcall,
  /// `__regcall`.
  Regcall,
  /// Swift calling convention (Clang-only).
  Swift,
  /// Swift async calling convention (Clang-only).
  SwiftAsync,
};

/// Reference kind used when demangling template parameter references.
enum class ReferenceKind : uint8_t {
  /// Not a reference.
  None,
  /// Lvalue reference.
  LValueRef,
  /// Rvalue reference.
  RValueRef
};

/// Flags that control how demangled names are printed.
enum OutputFlags {
  /// Default printing with all decorations enabled.
  OF_Default = 0,
  /// Omit the calling convention from output.
  OF_NoCallingConvention = 1 << 0,
  /// Omit `class`/`struct`/`union`/`enum` tag keywords.
  OF_NoTagSpecifier = 1 << 1,
  /// Omit access specifiers such as `public`/`protected`/`private`.
  OF_NoAccessSpecifier = 1 << 2,
  /// Omit member-type decorations such as `static`/`virtual`.
  OF_NoMemberType = 1 << 3,
  /// Omit the function return type.
  OF_NoReturnType = 1 << 4,
  /// Omit the variable type.
  OF_NoVariableType = 1 << 5,
  /// Omit an explicit `void` parameter list.
  OF_NoVoidParameter = 1 << 6,
  /// Suppress decorative names for RTTI type descriptors.
  OF_NoDecorativeRTTITypeDescriptor = 1 << 7,
};

/// Built-in / primitive type kind in a Microsoft mangling.
enum class PrimitiveKind {
  /// `void`.
  Void,
  /// `bool`.
  Bool,
  /// `char`.
  Char,
  /// `signed char`.
  Schar,
  /// `unsigned char`.
  Uchar,
  /// `char8_t`.
  Char8,
  /// `char16_t`.
  Char16,
  /// `char32_t`.
  Char32,
  /// `short`.
  Short,
  /// `unsigned short`.
  Ushort,
  /// `int`.
  Int,
  /// `unsigned int`.
  Uint,
  /// `long`.
  Long,
  /// `unsigned long`.
  Ulong,
  /// `__int64` / `long long`.
  Int64,
  /// `unsigned __int64` / `unsigned long long`.
  Uint64,
  /// `wchar_t`.
  Wchar,
  /// `float`.
  Float,
  /// `double`.
  Double,
  /// `long double`.
  Ldouble,
  /// `std::nullptr_t`.
  Nullptr,
  /// `auto`.
  Auto,
  /// `decltype(auto)`.
  DecltypeAuto,
};

/// Character type of an encoded string literal symbol.
enum class CharKind {
  /// Ordinary `char` string.
  Char,
  /// `char16_t` string.
  Char16,
  /// `char32_t` string.
  Char32,
  /// `wchar_t` string.
  Wchar,
};

/// Operator or special intrinsic function encoded in a Microsoft mangling.
enum class IntrinsicFunctionKind : uint8_t {
  /// No intrinsic.
  None,
  /// `operator new` (`?2`).
  New,                        // ?2 # operator new
  /// `operator delete` (`?3`).
  Delete,                     // ?3 # operator delete
  /// `operator=` (`?4`).
  Assign,                     // ?4 # operator=
  /// `operator>>` (`?5`).
  RightShift,                 // ?5 # operator>>
  /// `operator<<` (`?6`).
  LeftShift,                  // ?6 # operator<<
  /// `operator!` (`?7`).
  LogicalNot,                 // ?7 # operator!
  /// `operator==` (`?8`).
  Equals,                     // ?8 # operator==
  /// `operator!=` (`?9`).
  NotEquals,                  // ?9 # operator!=
  /// `operator[]` (`?A`).
  ArraySubscript,             // ?A # operator[]
  /// `operator->` (`?C`).
  Pointer,                    // ?C # operator->
  /// `operator*` (`?D`).
  Dereference,                // ?D # operator*
  /// `operator++` (`?E`).
  Increment,                  // ?E # operator++
  /// `operator--` (`?F`).
  Decrement,                  // ?F # operator--
  /// `operator-` (`?G`).
  Minus,                      // ?G # operator-
  /// `operator+` (`?H`).
  Plus,                       // ?H # operator+
  /// `operator&` (`?I`).
  BitwiseAnd,                 // ?I # operator&
  /// `operator->*` (`?J`).
  MemberPointer,              // ?J # operator->*
  /// `operator/` (`?K`).
  Divide,                     // ?K # operator/
  /// `operator%` (`?L`).
  Modulus,                    // ?L # operator%
  /// `operator<` (`?M`).
  LessThan,                   // ?M operator<
  /// `operator<=` (`?N`).
  LessThanEqual,              // ?N operator<=
  /// `operator>` (`?O`).
  GreaterThan,                // ?O operator>
  /// `operator>=` (`?P`).
  GreaterThanEqual,           // ?P operator>=
  /// `operator,` (`?Q`).
  Comma,                      // ?Q operator,
  /// `operator()` (`?R`).
  Parens,                     // ?R operator()
  /// `operator~` (`?S`).
  BitwiseNot,                 // ?S operator~
  /// `operator^` (`?T`).
  BitwiseXor,                 // ?T operator^
  /// `operator|` (`?U`).
  BitwiseOr,                  // ?U operator|
  /// `operator&&` (`?V`).
  LogicalAnd,                 // ?V operator&&
  /// `operator||` (`?W`).
  LogicalOr,                  // ?W operator||
  /// `operator*=` (`?X`).
  TimesEqual,                 // ?X operator*=
  /// `operator+=` (`?Y`).
  PlusEqual,                  // ?Y operator+=
  /// `operator-=` (`?Z`).
  MinusEqual,                 // ?Z operator-=
  /// `operator/=` (`?_0`).
  DivEqual,                   // ?_0 operator/=
  /// `operator%=` (`?_1`).
  ModEqual,                   // ?_1 operator%=
  /// `operator>>=` (`?_2`).
  RshEqual,                   // ?_2 operator>>=
  /// `operator<<=` (`?_3`).
  LshEqual,                   // ?_3 operator<<=
  /// `operator&=` (`?_4`).
  BitwiseAndEqual,            // ?_4 operator&=
  /// `operator|=` (`?_5`).
  BitwiseOrEqual,             // ?_5 operator|=
  /// `operator^=` (`?_6`).
  BitwiseXorEqual,            // ?_6 operator^=
  /// Virtual base destructor (`?_D`).
  VbaseDtor,                  // ?_D # vbase destructor
  /// Vector deleting destructor (`?_E`).
  VecDelDtor,                 // ?_E # vector deleting destructor
  /// Default constructor closure (`?_F`).
  DefaultCtorClosure,         // ?_F # default constructor closure
  /// Scalar deleting destructor (`?_G`).
  ScalarDelDtor,              // ?_G # scalar deleting destructor
  /// Vector constructor iterator (`?_H`).
  VecCtorIter,                // ?_H # vector constructor iterator
  /// Vector destructor iterator (`?_I`).
  VecDtorIter,                // ?_I # vector destructor iterator
  /// Vector virtual-base constructor iterator (`?_J`).
  VecVbaseCtorIter,           // ?_J # vector vbase constructor iterator
  /// Virtual displacement map (`?_K`).
  VdispMap,                   // ?_K # virtual displacement map
  /// EH vector constructor iterator (`?_L`).
  EHVecCtorIter,              // ?_L # eh vector constructor iterator
  /// EH vector destructor iterator (`?_M`).
  EHVecDtorIter,              // ?_M # eh vector destructor iterator
  /// EH vector virtual-base constructor iterator (`?_N`).
  EHVecVbaseCtorIter,         // ?_N # eh vector vbase constructor iterator
  /// Copy constructor closure (`?_O`).
  CopyCtorClosure,            // ?_O # copy constructor closure
  /// Local vftable constructor closure (`?_T`).
  LocalVftableCtorClosure,    // ?_T # local vftable constructor closure
  /// `operator new[]` (`?_U`).
  ArrayNew,                   // ?_U operator new[]
  /// `operator delete[]` (`?_V`).
  ArrayDelete,                // ?_V operator delete[]
  /// Managed vector constructor iterator (`?__A`).
  ManVectorCtorIter,          // ?__A managed vector ctor iterator
  /// Managed vector destructor iterator (`?__B`).
  ManVectorDtorIter,          // ?__B managed vector dtor iterator
  /// EH vector copy constructor iterator (`?__C`).
  EHVectorCopyCtorIter,       // ?__C EH vector copy ctor iterator
  /// EH vector virtual-base copy constructor iterator (`?__D`).
  EHVectorVbaseCopyCtorIter,  // ?__D EH vector vbase copy ctor iterator
  /// Vector copy constructor iterator (`?__G`).
  VectorCopyCtorIter,         // ?__G vector copy constructor iterator
  /// Vector virtual-base copy constructor iterator (`?__H`).
  VectorVbaseCopyCtorIter,    // ?__H vector vbase copy constructor iterator
  /// Managed vector virtual-base copy constructor iterator (`?__I`).
  ManVectorVbaseCopyCtorIter, // ?__I managed vector vbase copy constructor
  /// `operator co_await` (`?__L`).
  CoAwait,                    // ?__L operator co_await
  /// `operator<=>` (`?__M`).
  Spaceship,                  // ?__M operator<=>
  /// Sentinel one past the last intrinsic kind.
  MaxIntrinsic
};

/// Kind of special Microsoft intrinsic / compiler-generated symbol.
enum class SpecialIntrinsicKind {
  /// Not a special intrinsic.
  None,
  /// Virtual function table (`vftable`).
  Vftable,
  /// Virtual base table (`vbtable`).
  Vbtable,
  /// `typeof` intrinsic.
  Typeof,
  /// Virtual call thunk.
  VcallThunk,
  /// Local static guard variable.
  LocalStaticGuard,
  /// Encoded string literal symbol.
  StringLiteralSymbol,
  /// User-defined type returning function.
  UdtReturning,
  /// Unrecognized special intrinsic.
  Unknown,
  /// Dynamic initializer (`dynamic initializer`).
  DynamicInitializer,
  /// Dynamic atexit destructor.
  DynamicAtexitDestructor,
  /// RTTI type descriptor.
  RttiTypeDescriptor,
  /// RTTI base class descriptor.
  RttiBaseClassDescriptor,
  /// RTTI base class array.
  RttiBaseClassArray,
  /// RTTI class hierarchy descriptor.
  RttiClassHierarchyDescriptor,
  /// RTTI complete object locator.
  RttiCompleteObjLocator,
  /// Local virtual function table.
  LocalVftable,
  /// Local static thread guard.
  LocalStaticThreadGuard,
};

/// Access, linkage, and adjustment flags for a function signature.
enum FuncClass : uint16_t {
  /// No function-class flags.
  FC_None = 0,
  /// Public member function.
  FC_Public = 1 << 0,
  /// Protected member function.
  FC_Protected = 1 << 1,
  /// Private member function.
  FC_Private = 1 << 2,
  /// Global (non-member) function.
  FC_Global = 1 << 3,
  /// Static member function.
  FC_Static = 1 << 4,
  /// Virtual member function.
  FC_Virtual = 1 << 5,
  /// Far function (legacy segmented memory).
  FC_Far = 1 << 6,
  /// `extern "C"` linkage.
  FC_ExternC = 1 << 7,
  /// Mangling omits the parameter list.
  FC_NoParameterList = 1 << 8,
  /// Virtual this-adjustment thunk.
  FC_VirtualThisAdjust = 1 << 9,
  /// Extended virtual this-adjustment thunk.
  FC_VirtualThisAdjustEx = 1 << 10,
  /// Static this-adjustment thunk.
  FC_StaticThisAdjust = 1 << 11,
};

/// Tag keyword used when printing a tagged type.
enum class TagKind {
  /// `class`.
  Class,
  /// `struct`.
  Struct,
  /// `union`.
  Union,
  /// `enum`.
  Enum
};

/// Discriminator for concrete Microsoft demangler AST node types.
enum class NodeKind {
  /// Unknown or uninitialized node kind.
  Unknown,

  /// First kind in the symbol-node range.
  SymbolStart,
  /// MD5-hashed symbol name.
  Md5Symbol = SymbolStart,
  /// Encoded string literal symbol.
  EncodedStringLiteral,
  /// Function symbol.
  FunctionSymbol,
  /// Local static guard variable symbol.
  LocalStaticGuardVariable,
  /// Special table symbol (vftable, vbtable, and similar).
  SpecialTableSymbol,
  /// Variable symbol.
  VariableSymbol,
  /// Last kind in the symbol-node range.
  SymbolEnd = VariableSymbol,

  /// First kind in the identifier-node range.
  IdentifierStart,
  /// Conversion operator identifier.
  ConversionOperatorIdentifier = IdentifierStart,
  /// Dynamic initializer / atexit destructor identifier.
  DynamicStructorIdentifier,
  /// Intrinsic / operator function identifier.
  IntrinsicFunctionIdentifier,
  /// User-defined literal operator identifier.
  LiteralOperatorIdentifier,
  /// Local static guard identifier.
  LocalStaticGuardIdentifier,
  /// Ordinary named identifier.
  NamedIdentifier,
  /// RTTI base class descriptor identifier.
  RttiBaseClassDescriptor,
  /// Constructor or destructor identifier.
  StructorIdentifier,
  /// Virtual call thunk identifier.
  VcallThunkIdentifier,
  /// Last kind in the identifier-node range.
  IdentifierEnd = VcallThunkIdentifier,

  /// First kind in the type-node range.
  TypeStart,
  /// Array type.
  ArrayType = TypeStart,
  /// Vendor / custom type.
  Custom,

  /// Function signature type.
  FunctionSignature,
  /// Thunk function signature type.
  ThunkSignature,
  /// Last kind in the function-signature range.
  FunctionSignatureEnd = ThunkSignature,

  /// Intrinsic type placeholder.
  IntrinsicType,
  /// Pointer, reference, or member-pointer type.
  PointerType,
  /// Primitive type.
  PrimitiveType,
  /// Tagged (`class`/`struct`/`union`/`enum`) type.
  TagType,
  /// Last kind in the type-node range.
  TypeEnd = TagType,

  /// Pointer-authentication (`__ptrauth`) qualifier.
  PointerAuthQualifier,

  /// Integer literal used in a mangling (for example array bounds).
  IntegerLiteral,

  /// Ordered array of AST nodes.
  NodeArray,

  /// Nested / qualified name.
  QualifiedName,

  /// Template parameter that refers to a symbol or offset.
  TemplateParameterReference,
};

/// Base class of all Microsoft demangler AST nodes.
struct Node {
  /// Construct a node with the given concrete kind.
  /// \param K Discriminator for the derived node type.
  explicit Node(NodeKind K) : Kind(K) {}
  /// Destroy the node.
  virtual ~Node() = default;

  /// Return the concrete kind of this node.
  /// \returns The concrete NodeKind discriminator for this node.
  NodeKind kind() const { return Kind; }

  /// Write the demangled representation of this node into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  virtual void output(OutputBuffer &OB, OutputFlags Flags) const = 0;

  /// Return the demangled string for this node using \p Flags.
  /// \param Flags Printing options that suppress selected decorations.
  /// \returns The demangled string representation of this node.
  DEMANGLE_ABI std::string toString(OutputFlags Flags = OF_Default) const;

private:
  NodeKind Kind;
};

struct TypeNode;
struct PrimitiveTypeNode;
struct FunctionSignatureNode;
struct IdentifierNode;
struct NamedIdentifierNode;
struct VcallThunkIdentifierNode;
struct IntrinsicFunctionIdentifierNode;
struct LiteralOperatorIdentifierNode;
struct ConversionOperatorIdentifierNode;
struct StructorIdentifierNode;
struct ThunkSignatureNode;
struct PointerTypeNode;
struct ArrayTypeNode;
struct TagTypeNode;
struct NodeArrayNode;
struct QualifiedNameNode;
struct TemplateParameterReferenceNode;
struct EncodedStringLiteralNode;
struct IntegerLiteralNode;
struct RttiBaseClassDescriptorNode;
struct LocalStaticGuardVariableNode;
struct SymbolNode;
struct FunctionSymbolNode;
struct VariableSymbolNode;
struct SpecialTableSymbolNode;
struct PointerAuthQualifierNode;

/// Base AST node for demangled types.
struct TypeNode : public Node {
  /// Construct a type node with the given kind.
  /// \param K Concrete type-node kind.
  explicit TypeNode(NodeKind K) : Node(K) {}

  /// Write the left-hand (prefix) portion of this type into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  virtual void outputPre(OutputBuffer &OB, OutputFlags Flags) const = 0;
  /// Write the right-hand (postfix) portion of this type into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  virtual void outputPost(OutputBuffer &OB, OutputFlags Flags) const = 0;

  /// Write the full demangled type by emitting prefix then postfix.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override {
    outputPre(OB, Flags);
    outputPost(OB, Flags);
  }

  /// Return true if \p N is a type node.
  /// \param N Node to test.
  /// \returns True if \p N is a type node.
  static bool classof(const Node *N) {
    return N->kind() >= NodeKind::TypeStart && N->kind() <= NodeKind::TypeEnd;
  }

  /// CV and pointer qualifiers applied to this type.
  Qualifiers Quals = Q_None;
};

/// AST node for a primitive / built-in type.
struct DEMANGLE_ABI PrimitiveTypeNode : public TypeNode {
  /// Construct a primitive type node for kind \p K.
  /// \param K Primitive type kind to represent.
  explicit PrimitiveTypeNode(PrimitiveKind K)
      : TypeNode(NodeKind::PrimitiveType), PrimKind(K) {}

  /// Write the primitive type name into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  /// Write nothing; primitive types have no postfix.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override {}

  /// Return true if \p N is a PrimitiveTypeNode.
  /// \param N Node to test.
  /// \returns True if \p N is a PrimitiveTypeNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::PrimitiveType;
  }

  /// Kind of primitive type this node represents.
  PrimitiveKind PrimKind;
};

/// AST node for a function type / signature.
struct DEMANGLE_ABI FunctionSignatureNode : public TypeNode {
  /// Construct a function-signature node with an explicit kind.
  /// \param K Concrete signature kind (`FunctionSignature` or `ThunkSignature`).
  explicit FunctionSignatureNode(NodeKind K) : TypeNode(K) {}
  /// Construct a standard function-signature node.
  FunctionSignatureNode() : TypeNode(NodeKind::FunctionSignature) {}

  /// Write the left-hand portion of the function type into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  /// Write the parameter list and trailing qualifiers into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a function-signature or thunk-signature node.
  /// \param N Node to test.
  /// \returns True if \p N is a function-signature or thunk-signature node.
  static bool classof(const Node *N) {
    return N->kind() >= NodeKind::FunctionSignature &&
           N->kind() <= NodeKind::FunctionSignatureEnd;
  }

  /// Affinity when this signature is the pointee of a pointer or member pointer.
  ///
  /// Valid if this FunctionTypeNode is the Pointee of a PointerType or
  /// MemberPointerType.
  PointerAffinity Affinity = PointerAffinity::None;

  /// Calling convention of the function.
  CallingConv CallConvention = CallingConv::None;

  /// Access, linkage, and adjustment flags for the function.
  FuncClass FunctionClass = FC_Global;

  /// Ref-qualifier of a non-static member function.
  FunctionRefQualifier RefQualifier = FunctionRefQualifier::None;

  /// Return type of the function.
  TypeNode *ReturnType = nullptr;

  /// True if this is a C-style varargs function.
  bool IsVariadic = false;

  /// Function parameter types.
  NodeArrayNode *Params = nullptr;

  /// True if the function type is `noexcept`.
  bool IsNoexcept = false;
};

/// Base AST node for demangled identifiers.
struct IdentifierNode : public Node {
  /// Construct an identifier node with the given kind.
  /// \param K Concrete identifier-node kind.
  explicit IdentifierNode(NodeKind K) : Node(K) {}

  /// Return true if \p N is an identifier node.
  /// \param N Node to test.
  /// \returns True if \p N is an identifier node.
  static bool classof(const Node *N) {
    return N->kind() >= NodeKind::IdentifierStart &&
           N->kind() <= NodeKind::IdentifierEnd;
  }

  /// Template arguments attached to this identifier, if any.
  NodeArrayNode *TemplateParams = nullptr;

protected:
  /// Write template parameter lists for this identifier into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  DEMANGLE_ABI void outputTemplateParameters(OutputBuffer &OB,
                                             OutputFlags Flags) const;
};

/// Identifier for a virtual-call thunk.
struct DEMANGLE_ABI VcallThunkIdentifierNode : public IdentifierNode {
  /// Construct an empty virtual-call thunk identifier.
  VcallThunkIdentifierNode() : IdentifierNode(NodeKind::VcallThunkIdentifier) {}

  /// Write the demangled virtual-call thunk identifier into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a VcallThunkIdentifierNode.
  /// \param N Node to test.
  /// \returns True if \p N is a VcallThunkIdentifierNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::VcallThunkIdentifier;
  }

  /// Byte offset of the target entry within the virtual function table.
  uint64_t OffsetInVTable = 0;
};

/// Identifier for a dynamic initializer or atexit destructor.
struct DEMANGLE_ABI DynamicStructorIdentifierNode : public IdentifierNode {
  /// Construct an empty dynamic structor identifier.
  DynamicStructorIdentifierNode()
      : IdentifierNode(NodeKind::DynamicStructorIdentifier) {}

  /// Write the demangled dynamic structor identifier into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a DynamicStructorIdentifierNode.
  /// \param N Node to test.
  /// \returns True if \p N is a DynamicStructorIdentifierNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::DynamicStructorIdentifier;
  }

  /// Variable being initialized or destroyed, when applicable.
  VariableSymbolNode *Variable = nullptr;
  /// Qualified name used when the target is named rather than a variable.
  QualifiedNameNode *Name = nullptr;
  /// True if this identifier names a destructor rather than an initializer.
  bool IsDestructor = false;
};

/// Identifier consisting of an ordinary name string.
struct DEMANGLE_ABI NamedIdentifierNode : public IdentifierNode {
  /// Construct an empty named identifier.
  NamedIdentifierNode() : IdentifierNode(NodeKind::NamedIdentifier) {}

  /// Write the demangled name into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a NamedIdentifierNode.
  /// \param N Node to test.
  /// \returns True if \p N is a NamedIdentifierNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::NamedIdentifier;
  }

  /// Unqualified identifier text.
  std::string_view Name;
};

/// Identifier for an operator or other intrinsic function.
struct DEMANGLE_ABI IntrinsicFunctionIdentifierNode : public IdentifierNode {
  /// Construct an intrinsic-function identifier for \p Operator.
  /// \param Operator Intrinsic or operator kind to represent.
  explicit IntrinsicFunctionIdentifierNode(IntrinsicFunctionKind Operator)
      : IdentifierNode(NodeKind::IntrinsicFunctionIdentifier),
        Operator(Operator) {}

  /// Write the demangled operator / intrinsic name into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is an IntrinsicFunctionIdentifierNode.
  /// \param N Node to test.
  /// \returns True if \p N is an IntrinsicFunctionIdentifierNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::IntrinsicFunctionIdentifier;
  }

  /// Operator or intrinsic function kind.
  IntrinsicFunctionKind Operator;
};

/// Identifier for a user-defined literal operator.
struct DEMANGLE_ABI LiteralOperatorIdentifierNode : public IdentifierNode {
  /// Construct an empty literal-operator identifier.
  LiteralOperatorIdentifierNode()
      : IdentifierNode(NodeKind::LiteralOperatorIdentifier) {}

  /// Write the demangled literal operator into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a LiteralOperatorIdentifierNode.
  /// \param N Node to test.
  /// \returns True if \p N is a LiteralOperatorIdentifierNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::LiteralOperatorIdentifier;
  }

  /// Literal suffix / ud-suffix name.
  std::string_view Name;
};

/// Identifier for a local static guard.
struct DEMANGLE_ABI LocalStaticGuardIdentifierNode : public IdentifierNode {
  /// Construct an empty local-static-guard identifier.
  LocalStaticGuardIdentifierNode()
      : IdentifierNode(NodeKind::LocalStaticGuardIdentifier) {}

  /// Write the demangled local static guard identifier into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a LocalStaticGuardIdentifierNode.
  /// \param N Node to test.
  /// \returns True if \p N is a LocalStaticGuardIdentifierNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::LocalStaticGuardIdentifier;
  }

  /// True if this is a thread-local static guard.
  bool IsThread = false;
  /// Scope index distinguishing multiple guards in the same function.
  uint32_t ScopeIndex = 0;
};

/// Identifier for a conversion operator.
struct DEMANGLE_ABI ConversionOperatorIdentifierNode : public IdentifierNode {
  /// Construct an empty conversion-operator identifier.
  ConversionOperatorIdentifierNode()
      : IdentifierNode(NodeKind::ConversionOperatorIdentifier) {}

  /// Write the demangled conversion operator into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a ConversionOperatorIdentifierNode.
  /// \param N Node to test.
  /// \returns True if \p N is a ConversionOperatorIdentifierNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::ConversionOperatorIdentifier;
  }

  /// Target type of the conversion operator.
  ///
  /// The type that this operator converts to.
  TypeNode *TargetType = nullptr;
};

/// Identifier for a constructor or destructor.
struct DEMANGLE_ABI StructorIdentifierNode : public IdentifierNode {
  /// Construct an empty constructor identifier.
  StructorIdentifierNode() : IdentifierNode(NodeKind::StructorIdentifier) {}
  /// Construct a constructor or destructor identifier.
  /// \param IsDestructor True to name a destructor; false for a constructor.
  explicit StructorIdentifierNode(bool IsDestructor)
      : IdentifierNode(NodeKind::StructorIdentifier),
        IsDestructor(IsDestructor) {}

  /// Write the demangled constructor or destructor name into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a StructorIdentifierNode.
  /// \param N Node to test.
  /// \returns True if \p N is a StructorIdentifierNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::StructorIdentifier;
  }

  /// Class whose constructor or destructor this identifier names.
  IdentifierNode *Class = nullptr;
  /// True if this identifier names a destructor rather than a constructor.
  bool IsDestructor = false;
};

/// Function signature specialized for this-adjustment thunks.
struct DEMANGLE_ABI ThunkSignatureNode : public FunctionSignatureNode {
  /// Construct an empty thunk signature node.
  ThunkSignatureNode() : FunctionSignatureNode(NodeKind::ThunkSignature) {}

  /// Write the left-hand portion of the thunk signature into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  /// Write the right-hand portion of the thunk signature into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a ThunkSignatureNode.
  /// \param N Node to test.
  /// \returns True if \p N is a ThunkSignatureNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::ThunkSignature;
  }

  /// Offsets used to adjust `this` before calling the target function.
  struct ThisAdjustor {
    /// Static byte offset applied to `this`.
    uint32_t StaticOffset = 0;
    /// Offset of the virtual base pointer within the object.
    int32_t VBPtrOffset = 0;
    /// Offset within the vbtable used for virtual base adjustment.
    int32_t VBOffsetOffset = 0;
    /// Offset of the `vtordisp` field used during construction.
    int32_t VtordispOffset = 0;
  };

  /// This-pointer adjustment applied by the thunk.
  ThisAdjustor ThisAdjust;
};

/// AST node for a pointer, reference, or member-pointer type.
struct DEMANGLE_ABI PointerTypeNode : public TypeNode {
  /// Construct an empty pointer type node.
  PointerTypeNode() : TypeNode(NodeKind::PointerType) {}
  /// Write the left-hand portion of the pointer type into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  /// Write the right-hand portion of the pointer type into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a PointerTypeNode.
  /// \param N Node to test.
  /// \returns True if \p N is a PointerTypeNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::PointerType;
  }

  /// Whether this node is a pointer, reference, or rvalue-reference.
  PointerAffinity Affinity = PointerAffinity::None;

  /// Enclosing class when this is a pointer-to-member.
  QualifiedNameNode *ClassParent = nullptr;

  /// Optional pointer-authentication qualifier applied to this pointer.
  PointerAuthQualifierNode *PointerAuthQualifier = nullptr;

  /// Pointee type (pointer to X, reference to X, or rvalue-reference to X).
  TypeNode *Pointee = nullptr;
};

/// AST node for a tagged (`class`/`struct`/`union`/`enum`) type.
struct DEMANGLE_ABI TagTypeNode : public TypeNode {
  /// Construct a tagged type node with tag keyword \p Tag.
  /// \param Tag Tag keyword to print with the type name.
  explicit TagTypeNode(TagKind Tag) : TypeNode(NodeKind::TagType), Tag(Tag) {}

  /// Write the tag keyword and qualified name into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  /// Write nothing; tag types have no postfix.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a TagTypeNode.
  /// \param N Node to test.
  /// \returns True if \p N is a TagTypeNode.
  static bool classof(const Node *N) { return N->kind() == NodeKind::TagType; }

  /// Qualified name of the tagged type.
  QualifiedNameNode *QualifiedName = nullptr;
  /// Tag keyword associated with this type.
  TagKind Tag;
};

/// AST node for an array type.
struct DEMANGLE_ABI ArrayTypeNode : public TypeNode {
  /// Construct an empty array type node.
  ArrayTypeNode() : TypeNode(NodeKind::ArrayType) {}

  /// Write the left-hand portion of the array type into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  /// Write the array dimensions into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Write all array dimension brackets into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputDimensionsImpl(OutputBuffer &OB, OutputFlags Flags) const;
  /// Write a single dimension from node \p N into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  /// \param N Dimension expression or literal node to print.
  void outputOneDimension(OutputBuffer &OB, OutputFlags Flags, Node *N) const;

  /// Return true if \p N is an ArrayTypeNode.
  /// \param N Node to test.
  /// \returns True if \p N is an ArrayTypeNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::ArrayType;
  }

  /// Array dimensions, for example `[3][4][5]` in `int Foo[3][4][5]`.
  NodeArrayNode *Dimensions = nullptr;

  /// Element type of the array.
  TypeNode *ElementType = nullptr;
};

/// Placeholder AST node for an intrinsic type with empty output.
struct IntrinsicNode : public TypeNode {
  /// Construct an empty intrinsic type node.
  IntrinsicNode() : TypeNode(NodeKind::IntrinsicType) {}
  /// Write nothing; intrinsic types are not printed.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override {}

  /// Return true if \p N is an IntrinsicNode.
  /// \param N Node to test.
  /// \returns True if \p N is an IntrinsicNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::IntrinsicType;
  }
};

/// AST node for a vendor-defined / custom type.
struct DEMANGLE_ABI CustomTypeNode : public TypeNode {
  /// Construct an empty custom type node.
  CustomTypeNode() : TypeNode(NodeKind::Custom) {}

  /// Write the custom type identifier into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  /// Write nothing; custom types have no postfix.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a CustomTypeNode.
  /// \param N Node to test.
  /// \returns True if \p N is a CustomTypeNode.
  static bool classof(const Node *N) { return N->kind() == NodeKind::Custom; }

  /// Identifier naming the custom type.
  IdentifierNode *Identifier = nullptr;
};

/// Ordered array of sibling AST nodes.
struct DEMANGLE_ABI NodeArrayNode : public Node {
  /// Construct an empty node array.
  NodeArrayNode() : Node(NodeKind::NodeArray) {}

  /// Write the nodes as a comma-separated list into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Write the nodes separated by \p Separator into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  /// \param Separator Text inserted between consecutive nodes.
  void output(OutputBuffer &OB, OutputFlags Flags,
              std::string_view Separator) const;

  /// Return true if \p N is a NodeArrayNode.
  /// \param N Node to test.
  /// \returns True if \p N is a NodeArrayNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::NodeArray;
  }

  /// Pointer to the first element of the node array.
  Node **Nodes = nullptr;
  /// Number of nodes in the array.
  size_t Count = 0;
};

/// Nested name composed of identifier components.
struct DEMANGLE_ABI QualifiedNameNode : public Node {
  /// Construct an empty qualified name.
  QualifiedNameNode() : Node(NodeKind::QualifiedName) {}

  /// Write the demangled qualified name into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a QualifiedNameNode.
  /// \param N Node to test.
  /// \returns True if \p N is a QualifiedNameNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::QualifiedName;
  }

  /// Nested name components from outermost scope to the unqualified identifier.
  NodeArrayNode *Components = nullptr;

  /// Return the rightmost (unqualified) identifier component.
  /// \returns The rightmost identifier component of the qualified name.
  IdentifierNode *getUnqualifiedIdentifier() {
    Node *LastComponent = Components->Nodes[Components->Count - 1];
    return static_cast<IdentifierNode *>(LastComponent);
  }
};

/// Template argument that refers to a symbol, offset, or member pointer.
struct DEMANGLE_ABI TemplateParameterReferenceNode : public Node {
  /// Construct an empty template-parameter reference.
  TemplateParameterReferenceNode()
      : Node(NodeKind::TemplateParameterReference) {}

  /// Write the demangled template-parameter reference into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a TemplateParameterReferenceNode.
  /// \param N Node to test.
  /// \returns True if \p N is a TemplateParameterReferenceNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::TemplateParameterReference;
  }

  /// Referenced symbol, when the parameter names one.
  SymbolNode *Symbol = nullptr;

  /// Number of valid entries in ThunkOffsets.
  int ThunkOffsetCount = 0;
  /// Up to three thunk adjustment offsets associated with the reference.
  std::array<int64_t, 3> ThunkOffsets;
  /// Pointer / reference affinity of the referenced entity.
  PointerAffinity Affinity = PointerAffinity::None;
  /// True if the referenced entity is a pointer-to-member.
  bool IsMemberPointer = false;
};

/// Integer literal appearing in a Microsoft mangling.
struct DEMANGLE_ABI IntegerLiteralNode : public Node {
  /// Construct a zero integer literal.
  IntegerLiteralNode() : Node(NodeKind::IntegerLiteral) {}
  /// Construct an integer literal with magnitude \p Value and sign \p IsNegative.
  /// \param Value Absolute value of the literal.
  /// \param IsNegative True if the literal is negative.
  IntegerLiteralNode(uint64_t Value, bool IsNegative)
      : Node(NodeKind::IntegerLiteral), Value(Value), IsNegative(IsNegative) {}

  /// Write the demangled integer literal into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is an IntegerLiteralNode.
  /// \param N Node to test.
  /// \returns True if \p N is an IntegerLiteralNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::IntegerLiteral;
  }

  /// Absolute magnitude of the literal.
  uint64_t Value = 0;
  /// True if the literal is negative.
  bool IsNegative = false;
};

/// Identifier for an RTTI base class descriptor.
struct DEMANGLE_ABI RttiBaseClassDescriptorNode : public IdentifierNode {
  /// Construct an empty RTTI base class descriptor identifier.
  RttiBaseClassDescriptorNode()
      : IdentifierNode(NodeKind::RttiBaseClassDescriptor) {}

  /// Write the demangled RTTI base class descriptor name into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is an RttiBaseClassDescriptorNode.
  /// \param N Node to test.
  /// \returns True if \p N is an RttiBaseClassDescriptorNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::RttiBaseClassDescriptor;
  }

  /// Non-virtual offset of the base within the derived object.
  uint32_t NVOffset = 0;
  /// Offset of the virtual base pointer used to locate the base.
  int32_t VBPtrOffset = 0;
  /// Offset within the vbtable for a virtual base.
  uint32_t VBTableOffset = 0;
  /// Attributes / flags of the base class descriptor.
  uint32_t Flags = 0;
};

/// Base AST node for demangled symbols.
struct DEMANGLE_ABI SymbolNode : public Node {
  /// Construct a symbol node with the given kind.
  /// \param K Concrete symbol-node kind.
  explicit SymbolNode(NodeKind K) : Node(K) {}
  /// Write the demangled symbol name into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a symbol node.
  /// \param N Node to test.
  /// \returns True if \p N is a symbol node.
  static bool classof(const Node *N) {
    return N->kind() >= NodeKind::SymbolStart &&
           N->kind() <= NodeKind::SymbolEnd;
  }

  /// Qualified name of the symbol.
  QualifiedNameNode *Name = nullptr;
};

/// Symbol for a special table such as a vftable or vbtable.
struct DEMANGLE_ABI SpecialTableSymbolNode : public SymbolNode {
  /// Construct an empty special-table symbol.
  explicit SpecialTableSymbolNode()
      : SymbolNode(NodeKind::SpecialTableSymbol) {}

  /// Write the demangled special-table symbol into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a SpecialTableSymbolNode.
  /// \param N Node to test.
  /// \returns True if \p N is a SpecialTableSymbolNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::SpecialTableSymbol;
  }

  /// Target names associated with the special table.
  NodeArrayNode *TargetNames = nullptr;
  /// Qualifiers applied to the special table.
  Qualifiers Quals = Qualifiers::Q_None;
};

/// Symbol for a local static guard variable.
struct DEMANGLE_ABI LocalStaticGuardVariableNode : public SymbolNode {
  /// Construct an empty local-static-guard variable symbol.
  LocalStaticGuardVariableNode()
      : SymbolNode(NodeKind::LocalStaticGuardVariable) {}

  /// Write the demangled local static guard symbol into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a LocalStaticGuardVariableNode.
  /// \param N Node to test.
  /// \returns True if \p N is a LocalStaticGuardVariableNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::LocalStaticGuardVariable;
  }

  /// True if the guard is visible outside its translation unit.
  bool IsVisible = false;
};

/// Symbol for an encoded string literal.
struct DEMANGLE_ABI EncodedStringLiteralNode : public SymbolNode {
  /// Construct an empty encoded string-literal symbol.
  EncodedStringLiteralNode() : SymbolNode(NodeKind::EncodedStringLiteral) {}

  /// Write the demangled string literal into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is an EncodedStringLiteralNode.
  /// \param N Node to test.
  /// \returns True if \p N is an EncodedStringLiteralNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::EncodedStringLiteral;
  }

  /// Decoded contents of the string literal.
  std::string_view DecodedString;
  /// True if the mangling truncated the literal contents.
  bool IsTruncated = false;
  /// Character type of the string literal.
  CharKind Char = CharKind::Char;
};

/// Symbol for a variable.
struct DEMANGLE_ABI VariableSymbolNode : public SymbolNode {
  /// Construct an empty variable symbol.
  VariableSymbolNode() : SymbolNode(NodeKind::VariableSymbol) {}

  /// Write the demangled variable symbol into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a VariableSymbolNode.
  /// \param N Node to test.
  /// \returns True if \p N is a VariableSymbolNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::VariableSymbol;
  }

  /// Return true if the variable name should be printed under \p Flags.
  /// \param Flags Printing options that suppress selected decorations.
  /// \returns True if the variable name should be printed.
  virtual bool shouldOutputName(OutputFlags Flags) const { return true; }

  /// Storage class of the variable.
  StorageClass SC = StorageClass::None;
  /// Type of the variable.
  TypeNode *Type = nullptr;
};

/// Variable symbol specialized for RTTI type descriptors.
struct DEMANGLE_ABI TypeSymbolNode : public VariableSymbolNode {
  /// Inherit VariableSymbolNode constructors.
  using VariableSymbolNode::VariableSymbolNode;

  /// Return true unless decorative RTTI type-descriptor names are suppressed.
  /// \param Flags Printing options that suppress selected decorations.
  /// \returns True unless decorative RTTI type-descriptor names are suppressed.
  bool shouldOutputName(OutputFlags Flags) const override {
    return !(Flags & OF_NoDecorativeRTTITypeDescriptor);
  }
};

/// Symbol for a function.
struct DEMANGLE_ABI FunctionSymbolNode : public SymbolNode {
  /// Construct an empty function symbol.
  FunctionSymbolNode() : SymbolNode(NodeKind::FunctionSymbol) {}

  /// Write the demangled function symbol into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a FunctionSymbolNode.
  /// \param N Node to test.
  /// \returns True if \p N is a FunctionSymbolNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::FunctionSymbol;
  }

  /// Function signature associated with this symbol.
  FunctionSignatureNode *Signature = nullptr;
};

/// AST node for a `__ptrauth` pointer-authentication qualifier.
struct DEMANGLE_ABI PointerAuthQualifierNode : public Node {
  /// Construct an empty pointer-authentication qualifier.
  PointerAuthQualifierNode() : Node(NodeKind::PointerAuthQualifier) {}

  /// Number of `__ptrauth` arguments (key, address discrimination, discriminator).
  ///
  /// `__ptrauth` takes three arguments:
  ///  - key
  ///  - isAddressDiscriminated
  ///  - extra discriminator
  static constexpr unsigned NumArgs = 3;
  /// Array type holding the three `__ptrauth` numeric arguments.
  typedef std::array<uint64_t, NumArgs> ArgArray;

  /// Write the demangled `__ptrauth` qualifier into \p OB.
  /// \param OB Destination demangle output buffer.
  /// \param Flags Printing options that suppress selected decorations.
  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  /// Return true if \p N is a PointerAuthQualifierNode.
  /// \param N Node to test.
  /// \returns True if \p N is a PointerAuthQualifierNode.
  static bool classof(const Node *N) {
    return N->kind() == NodeKind::PointerAuthQualifier;
  }

  /// Argument nodes forming the `__ptrauth` qualifier.
  NodeArrayNode *Components = nullptr;
};

} // namespace ms_demangle
} // namespace llvm

#endif
