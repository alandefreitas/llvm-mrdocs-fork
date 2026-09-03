//===--- Mustache.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the Mustache templating language supports version 1.4.2
// currently relies on llvm::json::Value for data input.
// See the Mustache spec for more information
// (https://mustache.github.io/mustache.5.html).
//
// Current Features Supported:
// - Variables
// - Sections
// - Inverted Sections
// - Partials
// - Comments
// - Lambdas
// - Unescaped Variables
//
// Features Not Supported:
// - Set Delimiter
// - Blocks
// - Parents
// - Dynamic Names
//
// The Template class is a container class that outputs the Mustache template
// string and is the main class for users. It stores all the lambdas and the
// ASTNode Tree. When the Template is instantiated it tokenizes the Template
// String and creates a vector of Tokens. Then it calls a basic recursive
// descent parser to construct the ASTNode Tree. The ASTNodes are all stored
// in an arena allocator which is freed once the template class goes out of
// scope.
//
// Usage:
// \code
//   // Creating a simple template and rendering it
//   auto Template = Template("Hello, {{name}}!");
//   Value Data = {{"name", "World"}};
//   std::string Out;
//   raw_string_ostream OS(Out);
//   T.render(Data, OS);
//   // Out == "Hello, World!"
//
//   // Creating a template with a partial and rendering it
//   auto Template = Template("{{>partial}}");
//   Template.registerPartial("partial", "Hello, {{name}}!");
//   Value Data = {{"name", "World"}};
//   std::string Out;
//   raw_string_ostream OS(Out);
//   T.render(Data, OS);
//   // Out == "Hello, World!"
//
//   // Creating a template with a lambda and rendering it
//   Value D = Object{};
//   auto T = Template("Hello, {{lambda}}!");
//   Lambda L = []() -> llvm::json::Value { return "World"; };
//   T.registerLambda("lambda", L);
//   std::string Out;
//   raw_string_ostream OS(Out);
//   T.render(D, OS);
//   // Out == "Hello, World!"
// \endcode
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MUSTACHE
#define LLVM_SUPPORT_MUSTACHE

#include "Error.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/StringSaver.h"
#include <functional>

namespace llvm {
/// Mustache template rendering against \c llvm::json::Value data.
namespace mustache {

/// Callable that produces a JSON value for a Mustache lambda tag.
using Lambda = std::function<llvm::json::Value()>;
/// Callable that produces a JSON value for a Mustache section lambda.
using SectionLambda = std::function<llvm::json::Value(std::string)>;

/// Node in the Mustache template abstract syntax tree.
class ASTNode;
/// Owned or borrowed pointer to an \c ASTNode.
using AstPtr = ASTNode *;
/// Map from characters to the strings that replace them when escaping.
using EscapeMap = DenseMap<char, std::string>;
/// Intrusive linked list of \c ASTNode children.
using ASTNodeList = iplist<ASTNode>;

/// Shared rendering state for Mustache templates, partials, and lambdas.
struct MustacheContext {
  /// Construct a context that allocates AST nodes with \p Allocator and
  /// interns strings with \p Saver.
  ///
  /// \param Allocator Arena used to allocate AST nodes.
  /// \param Saver String saver used to intern template and partial text.
  MustacheContext(BumpPtrAllocator &Allocator, StringSaver &Saver)
      : Allocator(Allocator), Saver(Saver) {}
  /// Arena allocator that owns Mustache AST nodes for this context.
  BumpPtrAllocator &Allocator;
  /// String saver used to intern template and partial string data.
  StringSaver &Saver;
  /// Registered partial templates keyed by partial name.
  StringMap<AstPtr> Partials;
  /// Registered lambdas keyed by tag name.
  StringMap<Lambda> Lambdas;
  /// Registered section lambdas keyed by section name.
  StringMap<SectionLambda> SectionLambdas;
  /// Characters and replacement strings used when HTML-escaping output.
  EscapeMap Escapes;
};

/// Container for a compiled Mustache AST plus registered partials and lambdas.
class Template {
public:
  /// Parse \p TemplateStr into an AST using \p Ctx for allocation and state.
  ///
  /// \param TemplateStr Mustache source text to compile.
  /// \param Ctx Context that owns the allocator, saver, and registries.
  LLVM_ABI Template(StringRef TemplateStr, MustacheContext &Ctx);

  /// Deleted copy constructor; templates are move-only.
  ///
  /// \param Other Unused; copy construction is not supported.
  Template(const Template &Other) = delete;

  /// Deleted copy assignment; templates are move-only.
  ///
  /// \param Other Unused; copy assignment is not supported.
  Template &operator=(const Template &Other) = delete;

  /// Move-construct from \p Other, taking ownership of its AST.
  ///
  /// \param Other Template to move from.
  LLVM_ABI Template(Template &&Other) noexcept;

  /// Destroy the template and free its AST nodes.
  ///
  /// Defined out-of-line to work around \c ASTNode being an incomplete type.
  LLVM_ABI ~Template();

  /// Deleted move assignment; templates are not move-assignable.
  ///
  /// \param Other Unused; move assignment is not supported.
  Template &operator=(Template &&Other) = delete;

  /// Render this template against \p Data, writing the result to \p OS.
  ///
  /// \param Data JSON value used as the Mustache data context.
  /// \param OS Output stream that receives the rendered text.
  LLVM_ABI void render(const llvm::json::Value &Data, llvm::raw_ostream &OS);

  /// Register a named partial template string under \p Name.
  ///
  /// \param Name Name used to reference the partial via \c {{>name}}.
  /// \param Partial Mustache source text of the partial.
  LLVM_ABI void registerPartial(std::string Name, std::string Partial);

  /// Register a lambda that produces a value for tags named \p Name.
  ///
  /// \param Name Tag name that invokes the lambda.
  /// \param Lambda Callable that returns the JSON value to render.
  LLVM_ABI void registerLambda(std::string Name, Lambda Lambda);

  /// Register a section lambda that processes body text for sections named
  /// \p Name.
  ///
  /// \param Name Section name that invokes the lambda.
  /// \param Lambda Callable that receives the section body and returns a
  /// JSON value to render.
  LLVM_ABI void registerLambda(std::string Name, SectionLambda Lambda);

  /// Replace the set of characters that are HTML-escaped during rendering.
  ///
  /// By default the Mustache Spec Specifies that HTML special characters
  /// should be escaped. This function allows the user to specify which
  /// characters should be escaped.
  ///
  /// \param Escapes Map from characters to their escaped replacement strings.
  LLVM_ABI void overrideEscapeCharacters(DenseMap<char, std::string> Escapes);

private:
  MustacheContext &Ctx;
  AstPtr Tree;
};
} // namespace mustache
} // namespace llvm

#endif // LLVM_SUPPORT_MUSTACHE
