//===--- Demangle.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEMANGLE_DEMANGLE_H
#define LLVM_DEMANGLE_DEMANGLE_H

#include "DemangleConfig.h"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace llvm {
/// This is a llvm local version of __cxa_demangle. Other than the name and
/// being in the llvm namespace it is identical.
///
/// The mangled_name is demangled into buf and returned. If the buffer is not
/// large enough, realloc is used to expand it.
///
/// The *status will be set to a value from the following enumeration
enum : int {
  /// An unspecified demangling failure occurred.
  demangle_unknown_error = -4,
  /// One or more demangle arguments were invalid.
  demangle_invalid_args = -3,
  /// The input was not a valid mangled name.
  demangle_invalid_mangled_name = -2,
  /// Memory allocation failed during demangling.
  demangle_memory_alloc_failure = -1,
  /// Demangling completed successfully.
  demangle_success = 0,
};

/// Demangle an Itanium ABI mangled name.
///
/// \param mangled_name - the Itanium mangled name to demangle.
/// \param ParseParams - if false, skip demangling function parameters.
/// \returns a non-NULL pointer to a NUL-terminated C style string that should
/// be explicitly freed, if successful; otherwise nullptr if mangled_name is
/// not a valid mangling or is nullptr.
DEMANGLE_ABI char *itaniumDemangle(std::string_view mangled_name,
                                   bool ParseParams = true);

/// Flags that control Microsoft demangled name formatting.
enum MSDemangleFlags {
  /// No special formatting options.
  MSDF_None = 0,
  /// Dump back-reference information while demangling.
  MSDF_DumpBackrefs = 1 << 0,
  /// Omit access specifiers such as public and private.
  MSDF_NoAccessSpecifier = 1 << 1,
  /// Omit the calling convention from the demangled name.
  MSDF_NoCallingConvention = 1 << 2,
  /// Omit the function return type.
  MSDF_NoReturnType = 1 << 3,
  /// Omit member type information.
  MSDF_NoMemberType = 1 << 4,
  /// Omit variable type information.
  MSDF_NoVariableType = 1 << 5,
  /// Omit tag specifiers such as class, struct, or enum.
  MSDF_NoTagSpecifier = 1 << 6,
  /// Don't write "(void)" for functions that take no parameters.
  MSDF_NoVoidParameter = 1 << 7,
  /// Don't add decoration to RTTI type descriptors:
  ///   struct MyStruct `RTTI Type Descriptor Name'
  /// will instead output
  ///   struct MyStruct
  MSDF_NoDecorativeRTTITypeDescriptor = 1 << 8,
};

/// Demangle a Microsoft mangled symbol name.
///
/// If n_read is non-null and demangling was successful, it receives how many
/// bytes of the input string were consumed.
/// status receives one of the demangle_ enum entries above if it's not nullptr.
/// Flags controls various details of the demangled representation.
/// \param mangled_name - the Microsoft mangled name to demangle.
/// \param n_read - if non-null, set to the number of input bytes consumed.
/// \param status - if non-null, set to a demangle_ status code.
/// \param Flags - formatting options for the demangled representation.
/// \returns a pointer to a null-terminated demangled string on success, or
/// nullptr on error.
DEMANGLE_ABI char *microsoftDemangle(std::string_view mangled_name,
                                     size_t *n_read, int *status,
                                     MSDemangleFlags Flags = MSDF_None);

/// Find the Arm64EC name insertion point in a Microsoft mangled name.
///
/// \param MangledName - the Microsoft mangled name to inspect.
/// \returns the byte offset of the insertion point, or std::nullopt on error.
DEMANGLE_ABI std::optional<size_t>
getArm64ECInsertionPointInMangledName(std::string_view MangledName);

/// Demangle a Rust v0 mangled symbol.
///
/// \param MangledName - the Rust mangled name to demangle.
/// \returns a newly allocated demangled string, or nullptr on failure.
DEMANGLE_ABI char *rustDemangle(std::string_view MangledName);

/// Demangle a D mangled symbol.
///
/// \param MangledName - the D mangled name to demangle.
/// \returns a newly allocated demangled string, or nullptr on failure.
DEMANGLE_ABI char *dlangDemangle(std::string_view MangledName);

/// Attempt to demangle a string using different demangling schemes.
/// The function uses heuristics to determine which demangling scheme to use.
/// \param MangledName - reference to string to demangle.
/// \returns - the demangled string, or a copy of the input string if no
/// demangling occurred.
DEMANGLE_ABI std::string demangle(std::string_view MangledName);

/// Demangle a non-Microsoft mangled name into \p Result.
///
/// Tries Itanium, Rust, and D demangling schemes.
/// \param MangledName - the mangled name to demangle.
/// \param Result - receives the demangled name on success.
/// \param CanHaveLeadingDot - if true, allow and preserve a leading '.'.
/// \param ParseParams - if false, skip demangling function parameters.
/// \returns true if demangling succeeded, false otherwise.
DEMANGLE_ABI bool nonMicrosoftDemangle(std::string_view MangledName,
                                       std::string &Result,
                                       bool CanHaveLeadingDot = true,
                                       bool ParseParams = true);

/// Partial Itanium demangler for AST queries and selective printing.
///
/// "Partial" demangler. This supports demangling a string into an AST
/// (typically an intermediate stage in itaniumDemangle) and querying certain
/// properties or partially printing the demangled name.
struct ItaniumPartialDemangler {
  /// Construct an empty partial demangler.
  DEMANGLE_ABI ItaniumPartialDemangler();

  /// Move-construct from another partial demangler.
  /// \param Other - the partial demangler to move from.
  DEMANGLE_ABI ItaniumPartialDemangler(ItaniumPartialDemangler &&Other);
  /// Move-assign from another partial demangler.
  /// \param Other - the partial demangler to move from.
  /// \returns a reference to this partial demangler.
  DEMANGLE_ABI ItaniumPartialDemangler &
  operator=(ItaniumPartialDemangler &&Other);

  /// Demangle into an AST. Subsequent calls to the rest of the member functions
  /// implicitly operate on the AST this produces.
  /// \param MangledName - the Itanium mangled name to parse into an AST.
  /// \return true on error, false otherwise
  DEMANGLE_ABI bool partialDemangle(const char *MangledName);

  /// Just print the entire mangled name into Buf. Buf and N behave like the
  /// second and third parameters to __cxa_demangle.
  /// \param Buf - output buffer for the demangled name, or nullptr to allocate.
  /// \param N - pointer to the size of Buf; updated with the needed size.
  /// \returns a pointer to the demangled name in Buf, or nullptr on failure.
  DEMANGLE_ABI char *finishDemangle(char *Buf, size_t *N) const;

  /// See \ref finishDemangle
  ///
  /// \param[in] OB A llvm::itanium_demangle::OutputBuffer that the demangled
  /// name will be printed into.
  /// \returns a pointer to the demangled name in OB, or nullptr on failure.
  DEMANGLE_ABI char *finishDemangle(void *OB) const;

  /// Get the base name of a function. This doesn't include trailing template
  /// arguments, ie for "a::b<int>" this function returns "b".
  /// \param Buf - output buffer for the base name, or nullptr to allocate.
  /// \param N - pointer to the size of Buf; updated with the needed size.
  /// \returns a pointer to the demangled base name in Buf, or nullptr on
  /// failure.
  DEMANGLE_ABI char *getFunctionBaseName(char *Buf, size_t *N) const;

  /// Get the context name for a function. For "a::b::c", this function returns
  /// "a::b".
  /// \param Buf - output buffer for the context name, or nullptr to allocate.
  /// \param N - pointer to the size of Buf; updated with the needed size.
  /// \returns a pointer to the demangled context name in Buf, or nullptr on
  /// failure.
  DEMANGLE_ABI char *getFunctionDeclContextName(char *Buf, size_t *N) const;

  /// Get the entire name of this function.
  /// \param Buf - output buffer for the function name, or nullptr to allocate.
  /// \param N - pointer to the size of Buf; updated with the needed size.
  /// \returns a pointer to the demangled function name in Buf, or nullptr on
  /// failure.
  DEMANGLE_ABI char *getFunctionName(char *Buf, size_t *N) const;

  /// Get the parameters for this function.
  /// \param Buf - output buffer for the parameters, or nullptr to allocate.
  /// \param N - pointer to the size of Buf; updated with the needed size.
  /// \returns a pointer to the demangled parameters in Buf, or nullptr on
  /// failure.
  DEMANGLE_ABI char *getFunctionParameters(char *Buf, size_t *N) const;
  /// Get the return type for this function.
  /// \param Buf - output buffer for the return type, or nullptr to allocate.
  /// \param N - pointer to the size of Buf; updated with the needed size.
  /// \returns a pointer to the demangled return type in Buf, or nullptr on
  /// failure.
  DEMANGLE_ABI char *getFunctionReturnType(char *Buf, size_t *N) const;

  /// If this function has any cv or reference qualifiers. These imply that
  /// the function is a non-static member function.
  /// \returns true if the function has cv or reference qualifiers, false
  /// otherwise.
  DEMANGLE_ABI bool hasFunctionQualifiers() const;

  /// If this symbol describes a constructor or destructor.
  /// \returns true if this symbol is a constructor or destructor, false
  /// otherwise.
  DEMANGLE_ABI bool isCtorOrDtor() const;

  /// If this symbol describes a function.
  /// \returns true if this symbol describes a function, false otherwise.
  DEMANGLE_ABI bool isFunction() const;

  /// If this symbol describes a variable.
  /// \returns true if this symbol describes a variable, false otherwise.
  DEMANGLE_ABI bool isData() const;

  /// If this symbol is a <special-name>. These are generally implicitly
  /// generated by the implementation, such as vtables and typeinfo names.
  /// \returns true if this symbol is a special name, false otherwise.
  DEMANGLE_ABI bool isSpecialName() const;

  /// Destroy the partial demangler and free its AST.
  DEMANGLE_ABI ~ItaniumPartialDemangler();

private:
  void *RootNode;
  void *Context;
};
} // namespace llvm

#endif
