//===------------------------- MicrosoftDemangle.h --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEMANGLE_MICROSOFTDEMANGLE_H
#define LLVM_DEMANGLE_MICROSOFTDEMANGLE_H

#include "llvm/Demangle/Demangle.h"
#include "llvm/Demangle/DemangleConfig.h"
#include "llvm/Demangle/MicrosoftDemangleNodes.h"

#include <cassert>
#include <string_view>
#include <utility>

namespace llvm {
/// Microsoft Visual C++ name-mangling demangler.
namespace ms_demangle {
/// Default bump-allocation block size used by ArenaAllocator.
constexpr size_t AllocUnit = 4096;

/// Fast bump-pointer arena used while demangling Microsoft symbols.
///
/// This memory allocator is extremely fast, but it doesn't call dtors
/// for allocated objects. That means you can't use STL containers
/// (such as std::vector) with this allocator. But it pays off --
/// the demangler is 3x faster with this allocator compared to one with
/// STL containers.
class ArenaAllocator {
  struct AllocatorNode {
    uint8_t *Buf = nullptr;
    size_t Used = 0;
    size_t Capacity = 0;
    AllocatorNode *Next = nullptr;
  };

  void addNode(size_t Capacity) {
    AllocatorNode *NewHead = new AllocatorNode;
    NewHead->Buf = new uint8_t[Capacity];
    NewHead->Next = Head;
    NewHead->Capacity = Capacity;
    Head = NewHead;
    NewHead->Used = 0;
  }

public:
  /// Construct an arena with an initial AllocUnit-sized block.
  ArenaAllocator() { addNode(AllocUnit); }

  /// Free every arena block owned by this allocator.
  ~ArenaAllocator() {
    while (Head) {
      assert(Head->Buf);
      delete[] Head->Buf;
      AllocatorNode *Next = Head->Next;
      delete Head;
      Head = Next;
    }
  }

  /// Copy construction is deleted.
  /// \param Other Unused; copy construction is deleted.
  ArenaAllocator(const ArenaAllocator &Other) = delete;
  /// Copy assignment is deleted.
  /// \param Other Unused; copy assignment is deleted.
  ArenaAllocator &operator=(const ArenaAllocator &Other) = delete;

  /// Allocate \p Size bytes without alignment guarantees.
  /// \param Size Number of bytes to reserve from the arena.
  /// \returns Pointer to the reserved buffer.
  char *allocUnalignedBuffer(size_t Size) {
    assert(Head && Head->Buf);

    uint8_t *P = Head->Buf + Head->Used;

    Head->Used += Size;
    if (Head->Used <= Head->Capacity)
      return reinterpret_cast<char *>(P);

    addNode(std::max(AllocUnit, Size));
    Head->Used = Size;
    return reinterpret_cast<char *>(Head->Buf);
  }

  /// Allocate and default-construct an array of \p Count objects of type \p T.
  /// \param Count Number of array elements to allocate.
  /// \returns Pointer to the first constructed element.
  template <typename T, typename... Args> T *allocArray(size_t Count) {
    size_t Size = Count * sizeof(T);
    assert(Head && Head->Buf);

    size_t P = (size_t)Head->Buf + Head->Used;
    uintptr_t AlignedP =
        (((size_t)P + alignof(T) - 1) & ~(size_t)(alignof(T) - 1));
    uint8_t *PP = (uint8_t *)AlignedP;
    size_t Adjustment = AlignedP - P;

    Head->Used += Size + Adjustment;
    if (Head->Used <= Head->Capacity)
      return new (PP) T[Count]();

    addNode(std::max(AllocUnit, Size));
    Head->Used = Size;
    return new (Head->Buf) T[Count]();
  }

  /// Allocate and construct a single object of type \p T in the arena.
  /// \param ConstructorArgs Arguments forwarded to \p T's constructor.
  /// \returns Pointer to the constructed object.
  template <typename T, typename... Args> T *alloc(Args &&... ConstructorArgs) {
    constexpr size_t Size = sizeof(T);
    assert(Head && Head->Buf);

    size_t P = (size_t)Head->Buf + Head->Used;
    uintptr_t AlignedP =
        (((size_t)P + alignof(T) - 1) & ~(size_t)(alignof(T) - 1));
    uint8_t *PP = (uint8_t *)AlignedP;
    size_t Adjustment = AlignedP - P;

    Head->Used += Size + Adjustment;
    if (Head->Used <= Head->Capacity)
      return new (PP) T(std::forward<Args>(ConstructorArgs)...);

    static_assert(Size < AllocUnit);
    addNode(AllocUnit);
    Head->Used = Size;
    return new (Head->Buf) T(std::forward<Args>(ConstructorArgs)...);
  }

private:
  AllocatorNode *Head = nullptr;
};

/// Storage for Microsoft mangling name and function-parameter back-references.
struct BackrefContext {
  /// Maximum number of back-references stored in each table.
  static constexpr size_t Max = 10;

  /// Function parameter types eligible for `@` digit back-references.
  TypeNode *FunctionParams[Max];
  /// Number of entries currently stored in FunctionParams.
  size_t FunctionParamCount = 0;

  /// Named identifiers recorded for `@` digit back-references.
  ///
  /// The first 10 BackReferences in a mangled name can be back-referenced by
  /// special name @[0-9]. This is a storage for the first 10 BackReferences.
  NamedIdentifierNode *Names[Max];
  /// Number of entries currently stored in Names.
  size_t NamesCount = 0;
};

/// How CV-qualifiers are handled when demangling a type.
enum class QualifierMangleMode {
  /// Discard qualifiers from the mangled encoding.
  Drop,
  /// Decode and attach qualifiers from the mangled encoding.
  Mangle,
  /// Treat the encoding as a function result type's qualifiers.
  Result
};

/// Which identifier names should be recorded for later back-references.
enum NameBackrefBehavior : uint8_t {
  /// Don't save any names as backrefs.
  NBB_None = 0,
  /// Save template instantiations.
  NBB_Template = 1 << 0,
  /// Save simple names.
  NBB_Simple = 1 << 1,
};

/// Prefix group for Microsoft special function-identifier codes.
enum class FunctionIdentifierCodeGroup {
  /// Codes introduced with a single letter (no underscore prefix).
  Basic,
  /// Codes introduced with a single underscore.
  Under,
  /// Codes introduced with a double underscore.
  DoubleUnder
};

/// Parser that demangles Microsoft mangled symbols into AST nodes.
///
/// It has a set of functions to parse mangled symbols into Type instances.
/// It also has a set of functions to convert Type instances to strings.
class Demangler {
  friend std::optional<size_t>
  llvm::getArm64ECInsertionPointInMangledName(std::string_view MangledName);

public:
  /// Construct an empty demangler ready to parse a mangled name.
  Demangler() = default;
  /// Destroy the demangler and release arena-allocated AST nodes.
  virtual ~Demangler() = default;

  /// Parse a Microsoft mangled symbol into an AST.
  ///
  /// Call this first, then check if Error is true. If it is false, call
  /// output() to write the formatted name to the given stream.
  /// \param MangledName Mangled input; advanced past the consumed prefix.
  /// \returns Root symbol node, or an incomplete/null result when Error is set.
  DEMANGLE_ABI SymbolNode *parse(std::string_view &MangledName);

  /// Parse a mangled unique name for a tag type.
  /// \param MangledName Mangled input; advanced past the consumed prefix.
  /// \returns Tag type node describing the unique name, or null on error.
  DEMANGLE_ABI TagTypeNode *parseTagUniqueName(std::string_view &MangledName);

  /// True if an error occurred during the last parse.
  bool Error = false;

  /// Print the current name and function-parameter back-reference tables.
  DEMANGLE_ABI void dumpBackReferences();

private:
  SymbolNode *demangleEncodedSymbol(std::string_view &MangledName,
                                    QualifiedNameNode *QN);
  SymbolNode *demangleDeclarator(std::string_view &MangledName);
  SymbolNode *demangleMD5Name(std::string_view &MangledName);
  SymbolNode *demangleTypeinfoName(std::string_view &MangledName);

  VariableSymbolNode *demangleVariableEncoding(std::string_view &MangledName,
                                               StorageClass SC);
  FunctionSymbolNode *demangleFunctionEncoding(std::string_view &MangledName);

  Qualifiers demanglePointerExtQualifiers(std::string_view &MangledName);

  bool isMemberPointer(std::string_view MangledName, bool &Error);

  std::optional<PointerAuthQualifierNode::ArgArray>
  demanglePointerAuthQualifier(std::string_view &MangledName);

  PointerAuthQualifierNode *
  createPointerAuthQualifier(std::string_view &MangledName);

  // Parser functions. This is a recursive-descent parser.
  TypeNode *demangleType(std::string_view &MangledName,
                         QualifierMangleMode QMM);
  PrimitiveTypeNode *demanglePrimitiveType(std::string_view &MangledName);
  CustomTypeNode *demangleCustomType(std::string_view &MangledName);
  TagTypeNode *demangleClassType(std::string_view &MangledName);
  PointerTypeNode *demanglePointerType(std::string_view &MangledName);
  PointerTypeNode *demangleMemberPointerType(std::string_view &MangledName);
  FunctionSignatureNode *demangleFunctionType(std::string_view &MangledName,
                                              bool HasThisQuals);

  ArrayTypeNode *demangleArrayType(std::string_view &MangledName);

  NodeArrayNode *demangleFunctionParameterList(std::string_view &MangledName,
                                               bool &IsVariadic);
  NodeArrayNode *demangleTemplateParameterList(std::string_view &MangledName);

  std::pair<uint64_t, bool> demangleNumber(std::string_view &MangledName);
  uint64_t demangleUnsigned(std::string_view &MangledName);
  int64_t demangleSigned(std::string_view &MangledName);

  void memorizeString(std::string_view s);
  void memorizeIdentifier(IdentifierNode *Identifier);

  /// Allocate a copy of \p Borrowed into memory that we own.
  std::string_view copyString(std::string_view Borrowed);

  QualifiedNameNode *
  demangleFullyQualifiedTypeName(std::string_view &MangledName);
  QualifiedNameNode *
  demangleFullyQualifiedSymbolName(std::string_view &MangledName);

  IdentifierNode *demangleUnqualifiedTypeName(std::string_view &MangledName,
                                              bool Memorize);
  IdentifierNode *demangleUnqualifiedSymbolName(std::string_view &MangledName,
                                                NameBackrefBehavior NBB);

  QualifiedNameNode *demangleNameScopeChain(std::string_view &MangledName,
                                            IdentifierNode *UnqualifiedName);
  IdentifierNode *demangleNameScopePiece(std::string_view &MangledName);

  NamedIdentifierNode *demangleBackRefName(std::string_view &MangledName);
  IdentifierNode *
  demangleTemplateInstantiationName(std::string_view &MangledName,
                                    NameBackrefBehavior NBB);
  IntrinsicFunctionKind
  translateIntrinsicFunctionCode(char CH, FunctionIdentifierCodeGroup Group);
  IdentifierNode *demangleFunctionIdentifierCode(std::string_view &MangledName);
  IdentifierNode *
  demangleFunctionIdentifierCode(std::string_view &MangledName,
                                 FunctionIdentifierCodeGroup Group);
  StructorIdentifierNode *
  demangleStructorIdentifier(std::string_view &MangledName, bool IsDestructor);
  ConversionOperatorIdentifierNode *
  demangleConversionOperatorIdentifier(std::string_view &MangledName);
  LiteralOperatorIdentifierNode *
  demangleLiteralOperatorIdentifier(std::string_view &MangledName);

  SymbolNode *demangleSpecialIntrinsic(std::string_view &MangledName);
  SpecialTableSymbolNode *
  demangleSpecialTableSymbolNode(std::string_view &MangledName,
                                 SpecialIntrinsicKind SIK);
  LocalStaticGuardVariableNode *
  demangleLocalStaticGuard(std::string_view &MangledName, bool IsThread);
  VariableSymbolNode *demangleUntypedVariable(ArenaAllocator &Arena,
                                              std::string_view &MangledName,
                                              std::string_view VariableName);
  VariableSymbolNode *
  demangleRttiBaseClassDescriptorNode(ArenaAllocator &Arena,
                                      std::string_view &MangledName);
  FunctionSymbolNode *demangleInitFiniStub(std::string_view &MangledName,
                                           bool IsDestructor);

  NamedIdentifierNode *demangleSimpleName(std::string_view &MangledName,
                                          bool Memorize);
  NamedIdentifierNode *
  demangleAnonymousNamespaceName(std::string_view &MangledName);
  NamedIdentifierNode *
  demangleLocallyScopedNamePiece(std::string_view &MangledName);
  EncodedStringLiteralNode *
  demangleStringLiteral(std::string_view &MangledName);
  FunctionSymbolNode *demangleVcallThunkNode(std::string_view &MangledName);

  std::string_view demangleSimpleString(std::string_view &MangledName,
                                        bool Memorize);

  FuncClass demangleFunctionClass(std::string_view &MangledName);
  CallingConv demangleCallingConvention(std::string_view &MangledName);
  StorageClass demangleVariableStorageClass(std::string_view &MangledName);
  bool demangleThrowSpecification(std::string_view &MangledName);
  wchar_t demangleWcharLiteral(std::string_view &MangledName);
  uint8_t demangleCharLiteral(std::string_view &MangledName);

  std::pair<Qualifiers, bool> demangleQualifiers(std::string_view &MangledName);

  // Memory allocator.
  ArenaAllocator Arena;

  // A single type uses one global back-ref table for all function params.
  // This means back-refs can even go "into" other types.  Examples:
  //
  //  // Second int* is a back-ref to first.
  //  void foo(int *, int*);
  //
  //  // Second int* is not a back-ref to first (first is not a function param).
  //  int* foo(int*);
  //
  //  // Second int* is a back-ref to first (ALL function types share the same
  //  // back-ref map.
  //  using F = void(*)(int*);
  //  F G(int *);
  BackrefContext Backrefs;
};

} // namespace ms_demangle
} // namespace llvm

#endif // LLVM_DEMANGLE_MICROSOFTDEMANGLE_H
