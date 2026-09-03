//===--- Protocol.h - Language Server Protocol Implementation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains structs based on the LSP specification at
// https://microsoft.github.io/language-server-protocol/specification
//
// This is not meant to be a complete implementation, new interfaces are added
// when they're needed.
//
// Each struct has a toJSON and fromJSON function, that converts between
// the struct and a JSON representation. (See JSON.h)
//
// Some structs also have operator<< serialization. This is for debugging and
// tests, and is not generally machine-readable.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_LSP_PROTOCOL_H
#define LLVM_SUPPORT_LSP_PROTOCOL_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/LogicalResult.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <bitset>
#include <optional>
#include <string>
#include <utility>

// This file is using the LSP syntax for identifier names which is different
// from the LLVM coding standard. To avoid the clang-tidy warnings, we're
// disabling one check here.
// NOLINTBEGIN(readability-identifier-naming)

namespace llvm {
namespace lsp {

/// JSON-RPC and Language Server Protocol error codes.
enum class ErrorCode {
  // Defined by JSON RPC.
  /// Invalid JSON was received by the server.
  ParseError = -32700,
  /// The JSON sent is not a valid Request object.
  InvalidRequest = -32600,
  /// The method does not exist or is not available.
  MethodNotFound = -32601,
  /// Invalid method parameter(s).
  InvalidParams = -32602,
  /// Internal JSON-RPC error.
  InternalError = -32603,

  /// The server has not been initialized.
  ServerNotInitialized = -32002,
  /// An unknown or unspecified error code.
  UnknownErrorCode = -32001,

  // Defined by the protocol.
  /// The request was cancelled.
  RequestCancelled = -32800,
  /// Content was modified in a way that invalidates the request.
  ContentModified = -32801,
  /// The request failed for an application-specific reason.
  RequestFailed = -32803,
};

/// Defines how the host (editor) should sync document changes to the language
/// server.
enum class TextDocumentSyncKind {
  /// Documents should not be synced at all.
  None = 0,

  /// Documents are synced by always sending the full content of the document.
  Full = 1,

  /// Documents are synced by sending the full content on open. After that only
  /// incremental updates to the document are sent.
  Incremental = 2,
};

//===----------------------------------------------------------------------===//
// LSPError
//===----------------------------------------------------------------------===//

/// This class models an LSP error as an llvm::Error.
class LSPError : public llvm::ErrorInfo<LSPError> {
public:
  /// Human-readable error message.
  std::string message;
  /// LSP/JSON-RPC error code.
  ErrorCode code;
  /// RTTI identifier for LSPError.
  LLVM_ABI static char ID;

  /// Construct an LSP error with the given message and code.
  ///
  /// \param message Human-readable error description.
  /// \param code LSP/JSON-RPC error code.
  LSPError(std::string message, ErrorCode code)
      : message(std::move(message)), code(code) {}

  /// Write this error to the given stream.
  ///
  /// \param os Stream to write the error to.
  void log(raw_ostream &os) const override {
    os << int(code) << ": " << message;
  }
  /// Convert this LSP error to a std::error_code.
  /// \return An inconvertible error code.
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }
};

//===----------------------------------------------------------------------===//
// URIForFile
//===----------------------------------------------------------------------===//

/// URI in "file" scheme for a file.
class URIForFile {
public:
  /// Construct an empty URIForFile.
  URIForFile() = default;

  /// Try to build a URIForFile from the given URI string.
  ///
  /// \param uri URI string to parse.
  /// \return The parsed URIForFile, or an error on failure.
  LLVM_ABI static llvm::Expected<URIForFile> fromURI(StringRef uri);

  /// Try to build a URIForFile from the given absolute file path and optional
  /// scheme.
  ///
  /// \param absoluteFilepath Absolute filesystem path.
  /// \param scheme URI scheme to use (defaults to "file").
  /// \return The constructed URIForFile, or an error on failure.
  LLVM_ABI static llvm::Expected<URIForFile>
  fromFile(StringRef absoluteFilepath, StringRef scheme = "file");

  /// Returns the absolute path to the file.
  /// \return The absolute filesystem path.
  StringRef file() const { return filePath; }

  /// Returns the original uri of the file.
  /// \return The original URI string.
  StringRef uri() const { return uriStr; }

  /// Return the scheme of the uri.
  /// \return The URI scheme.
  LLVM_ABI StringRef scheme() const;

  /// Return true if this URI refers to a non-empty file path.
  /// \return True if this URI refers to a non-empty file path.
  explicit operator bool() const { return !filePath.empty(); }

  /// Return true if \p lhs and \p rhs refer to the same file path.
  ///
  /// \param lhs Left-hand URI.
  /// \param rhs Right-hand URI.
  /// \return True if \p lhs and \p rhs refer to the same file path.
  friend bool operator==(const URIForFile &lhs, const URIForFile &rhs) {
    return lhs.filePath == rhs.filePath;
  }
  /// Return true if \p lhs and \p rhs refer to different file paths.
  ///
  /// \param lhs Left-hand URI.
  /// \param rhs Right-hand URI.
  /// \return True if \p lhs and \p rhs refer to different file paths.
  friend bool operator!=(const URIForFile &lhs, const URIForFile &rhs) {
    return !(lhs == rhs);
  }
  /// Return true if \p lhs's file path sorts before \p rhs.
  ///
  /// \param lhs Left-hand URI.
  /// \param rhs Right-hand URI.
  /// \return True if \p lhs's file path sorts before \p rhs.
  friend bool operator<(const URIForFile &lhs, const URIForFile &rhs) {
    return lhs.filePath < rhs.filePath;
  }

  /// Register a supported URI scheme. The protocol supports `file` by default,
  /// so this is only necessary for any additional schemes that a server wants
  /// to support.
  ///
  /// \param scheme Additional URI scheme to accept.
  LLVM_ABI static void registerSupportedScheme(StringRef scheme);

private:
  explicit URIForFile(std::string &&filePath, std::string &&uriStr)
      : filePath(std::move(filePath)), uriStr(uriStr) {}

  std::string filePath;
  std::string uriStr;
};

/// Serialize a URIForFile to JSON.
///
/// \param value URI to serialize.
/// \return The JSON representation of the URI.
LLVM_ABI llvm::json::Value toJSON(const URIForFile &value);
/// Parse a URIForFile from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, URIForFile &result,
                       llvm::json::Path path);
/// Print a URIForFile for debugging.
///
/// \param os Stream to write to.
/// \param value URI to print.
/// \return The stream \p os.
LLVM_ABI raw_ostream &operator<<(raw_ostream &os, const URIForFile &value);

//===----------------------------------------------------------------------===//
// ClientCapabilities
//===----------------------------------------------------------------------===//

/// Capabilities advertised by the LSP client.
struct ClientCapabilities {
  /// Client supports hierarchical document symbols.
  /// textDocument.documentSymbol.hierarchicalDocumentSymbolSupport
  bool hierarchicalDocumentSymbol = false;

  /// Client supports CodeAction return value for textDocument/codeAction.
  /// textDocument.codeAction.codeActionLiteralSupport.
  bool codeActionStructure = false;

  /// Client supports server-initiated progress via the
  /// window/workDoneProgress/create method.
  ///
  /// window.workDoneProgress
  bool workDoneProgress = false;
};

/// Parse ClientCapabilities from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       ClientCapabilities &result, llvm::json::Path path);

//===----------------------------------------------------------------------===//
// ClientInfo
//===----------------------------------------------------------------------===//

/// Information about the LSP client.
struct ClientInfo {
  /// The name of the client as defined by the client.
  std::string name;

  /// The client's version as defined by the client.
  std::optional<std::string> version;
};

/// Parse ClientInfo from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, ClientInfo &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// InitializeParams
//===----------------------------------------------------------------------===//

/// Trace verbosity requested by the client during initialize.
enum class TraceLevel {
  /// Disable tracing.
  Off = 0,
  /// Trace only message traffic.
  Messages = 1,
  /// Trace messages with verbose detail.
  Verbose = 2,
};

/// Parse a TraceLevel from JSON.
///
/// \param value JSON value to parse.
/// \param result Enum filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, TraceLevel &result,
                       llvm::json::Path path);

/// Parameters for the LSP initialize request.
struct InitializeParams {
  /// The capabilities provided by the client (editor or tool).
  ClientCapabilities capabilities;

  /// Information about the client.
  std::optional<ClientInfo> clientInfo;

  /// The initial trace setting. If omitted trace is disabled ('off').
  std::optional<TraceLevel> trace;

  /// The root URI of the workspace. Is null if no folder is open.
  std::optional<std::string> rootUri;

  /// The root path of the workspace. Is null if no folder is open.
  /// This is deprecated, use rootUri instead, but kept for more compatibility.
  std::optional<std::string> rootPath;
};

/// Parse InitializeParams from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, InitializeParams &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// InitializedParams
//===----------------------------------------------------------------------===//

/// Empty parameter object used by notifications with no payload.
struct NoParams {};
/// Parse NoParams from JSON (always succeeds).
///
/// \param value JSON value to parse (ignored).
/// \param result Object filled on success (unchanged).
/// \param path JSON path used for error reporting (ignored).
/// \return True.
inline bool fromJSON(const llvm::json::Value &value, NoParams &result,
                     llvm::json::Path path) {
  return true;
}
/// Parameters for the initialized notification (empty).
using InitializedParams = NoParams;

//===----------------------------------------------------------------------===//
// TextDocumentItem
//===----------------------------------------------------------------------===//

/// An open text document, including language id, version, and content.
struct TextDocumentItem {
  /// The text document's URI.
  URIForFile uri;

  /// The text document's language identifier.
  std::string languageId;

  /// The content of the opened text document.
  std::string text;

  /// The version number of this document.
  int64_t version;
};

/// Parse a TextDocumentItem from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, TextDocumentItem &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// TextDocumentIdentifier
//===----------------------------------------------------------------------===//

/// Identifies a text document by URI.
struct TextDocumentIdentifier {
  /// The text document's URI.
  URIForFile uri;
};

/// Serialize a TextDocumentIdentifier to JSON.
///
/// \param value Identifier to serialize.
/// \return The JSON representation of the identifier.
LLVM_ABI llvm::json::Value toJSON(const TextDocumentIdentifier &value);
/// Parse a TextDocumentIdentifier from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       TextDocumentIdentifier &result, llvm::json::Path path);

//===----------------------------------------------------------------------===//
// VersionedTextDocumentIdentifier
//===----------------------------------------------------------------------===//

/// Identifies a text document by URI and version.
struct VersionedTextDocumentIdentifier {
  /// The text document's URI.
  URIForFile uri;
  /// The version number of this document.
  int64_t version;
};

/// Serialize a VersionedTextDocumentIdentifier to JSON.
///
/// \param value Identifier to serialize.
/// \return The JSON representation of the identifier.
LLVM_ABI llvm::json::Value toJSON(const VersionedTextDocumentIdentifier &value);
/// Parse a VersionedTextDocumentIdentifier from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       VersionedTextDocumentIdentifier &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// Position
//===----------------------------------------------------------------------===//

/// A zero-based line/character position in a text document.
struct Position {
  /// Construct a position from line and character offsets.
  ///
  /// \param line Zero-based line number.
  /// \param character Zero-based character offset on the line.
  Position(int line = 0, int character = 0)
      : line(line), character(character) {}

  /// Construct a position from the given source location.
  ///
  /// \param mgr Source manager used to resolve the location.
  /// \param loc Source location to convert.
  Position(llvm::SourceMgr &mgr, SMLoc loc) {
    std::pair<unsigned, unsigned> lineAndCol = mgr.getLineAndColumn(loc);
    line = lineAndCol.first - 1;
    character = lineAndCol.second - 1;
  }

  /// Line position in a document (zero-based).
  int line = 0;

  /// Character offset on a line in a document (zero-based).
  int character = 0;

  /// Return true if \p lhs and \p rhs have the same line and character.
  ///
  /// \param lhs Left-hand position.
  /// \param rhs Right-hand position.
  /// \return True if \p lhs and \p rhs have the same line and character.
  friend bool operator==(const Position &lhs, const Position &rhs) {
    return std::tie(lhs.line, lhs.character) ==
           std::tie(rhs.line, rhs.character);
  }
  /// Return true if \p lhs and \p rhs differ.
  ///
  /// \param lhs Left-hand position.
  /// \param rhs Right-hand position.
  /// \return True if \p lhs and \p rhs differ.
  friend bool operator!=(const Position &lhs, const Position &rhs) {
    return !(lhs == rhs);
  }
  /// Return true if \p lhs sorts before \p rhs.
  ///
  /// \param lhs Left-hand position.
  /// \param rhs Right-hand position.
  /// \return True if \p lhs sorts before \p rhs.
  friend bool operator<(const Position &lhs, const Position &rhs) {
    return std::tie(lhs.line, lhs.character) <
           std::tie(rhs.line, rhs.character);
  }
  /// Return true if \p lhs sorts before or equal to \p rhs.
  ///
  /// \param lhs Left-hand position.
  /// \param rhs Right-hand position.
  /// \return True if \p lhs sorts before or equal to \p rhs.
  friend bool operator<=(const Position &lhs, const Position &rhs) {
    return std::tie(lhs.line, lhs.character) <=
           std::tie(rhs.line, rhs.character);
  }

  /// Convert this position into a source location in the main file of the given
  /// source manager.
  ///
  /// \param mgr Source manager providing the main file.
  /// \return The corresponding source location in the main file.
  SMLoc getAsSMLoc(llvm::SourceMgr &mgr) const {
    return mgr.FindLocForLineAndColumn(mgr.getMainFileID(), line + 1,
                                       character + 1);
  }
};

/// Parse a Position from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, Position &result,
                       llvm::json::Path path);
/// Serialize a Position to JSON.
///
/// \param value Position to serialize.
/// \return The JSON representation of the position.
LLVM_ABI llvm::json::Value toJSON(const Position &value);
/// Print a Position for debugging.
///
/// \param os Stream to write to.
/// \param value Position to print.
/// \return The stream \p os.
LLVM_ABI raw_ostream &operator<<(raw_ostream &os, const Position &value);

//===----------------------------------------------------------------------===//
// Range
//===----------------------------------------------------------------------===//

/// A half-open start/end range in a text document.
struct Range {
  /// Construct an empty range at the origin.
  Range() = default;
  /// Construct a range from start and end positions.
  ///
  /// \param start Inclusive start position.
  /// \param end Exclusive end position.
  Range(Position start, Position end) : start(start), end(end) {}
  /// Construct a zero-width range at \p loc.
  ///
  /// \param loc Position used for both start and end.
  Range(Position loc) : Range(loc, loc) {}

  /// Construct a range from the given source range.
  ///
  /// \param mgr Source manager used to resolve the range.
  /// \param range Source range to convert.
  Range(llvm::SourceMgr &mgr, SMRange range)
      : Range(Position(mgr, range.Start), Position(mgr, range.End)) {}

  /// The range's start position.
  Position start;

  /// The range's end position.
  Position end;

  /// Return true if \p lhs and \p rhs have the same start and end.
  ///
  /// \param lhs Left-hand range.
  /// \param rhs Right-hand range.
  /// \return True if \p lhs and \p rhs have the same start and end.
  friend bool operator==(const Range &lhs, const Range &rhs) {
    return std::tie(lhs.start, lhs.end) == std::tie(rhs.start, rhs.end);
  }
  /// Return true if \p lhs and \p rhs differ.
  ///
  /// \param lhs Left-hand range.
  /// \param rhs Right-hand range.
  /// \return True if \p lhs and \p rhs differ.
  friend bool operator!=(const Range &lhs, const Range &rhs) {
    return !(lhs == rhs);
  }
  /// Return true if \p lhs sorts before \p rhs.
  ///
  /// \param lhs Left-hand range.
  /// \param rhs Right-hand range.
  /// \return True if \p lhs sorts before \p rhs.
  friend bool operator<(const Range &lhs, const Range &rhs) {
    return std::tie(lhs.start, lhs.end) < std::tie(rhs.start, rhs.end);
  }

  /// Return true if this range contains \p pos.
  ///
  /// \param pos Position to test.
  /// \return True if this range contains \p pos.
  bool contains(Position pos) const { return start <= pos && pos < end; }
  /// Return true if this range fully contains \p range.
  ///
  /// \param range Range to test.
  /// \return True if this range fully contains \p range.
  bool contains(Range range) const {
    return start <= range.start && range.end <= end;
  }

  /// Convert this range into a source range in the main file of the given
  /// source manager.
  ///
  /// \param mgr Source manager providing the main file.
  /// \return The corresponding source range, or an invalid range on failure.
  SMRange getAsSMRange(llvm::SourceMgr &mgr) const {
    SMLoc startLoc = start.getAsSMLoc(mgr);
    SMLoc endLoc = end.getAsSMLoc(mgr);
    // Check that the start and end locations are valid.
    if (!startLoc.isValid() || !endLoc.isValid() ||
        startLoc.getPointer() > endLoc.getPointer())
      return SMRange();
    return SMRange(startLoc, endLoc);
  }
};

/// Parse a Range from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, Range &result,
                       llvm::json::Path path);
/// Serialize a Range to JSON.
///
/// \param value Range to serialize.
/// \return The JSON representation of the range.
LLVM_ABI llvm::json::Value toJSON(const Range &value);
/// Print a Range for debugging.
///
/// \param os Stream to write to.
/// \param value Range to print.
/// \return The stream \p os.
LLVM_ABI raw_ostream &operator<<(raw_ostream &os, const Range &value);

//===----------------------------------------------------------------------===//
// Location
//===----------------------------------------------------------------------===//

/// A location inside a resource, expressed as a URI and a range.
struct Location {
  /// Construct an empty location.
  Location() = default;
  /// Construct a location from a URI and range.
  ///
  /// \param uri Document URI.
  /// \param range Range within the document.
  Location(const URIForFile &uri, Range range) : uri(uri), range(range) {}

  /// Construct a Location from the given source range.
  ///
  /// \param uri Document URI.
  /// \param mgr Source manager used to resolve the range.
  /// \param range Source range to convert.
  Location(const URIForFile &uri, llvm::SourceMgr &mgr, SMRange range)
      : Location(uri, Range(mgr, range)) {}

  /// The text document's URI.
  URIForFile uri;
  /// The range within the document.
  Range range;

  /// Return true if \p lhs and \p rhs have the same URI and range.
  ///
  /// \param lhs Left-hand location.
  /// \param rhs Right-hand location.
  /// \return True if \p lhs and \p rhs have the same URI and range.
  friend bool operator==(const Location &lhs, const Location &rhs) {
    return lhs.uri == rhs.uri && lhs.range == rhs.range;
  }

  /// Return true if \p lhs and \p rhs differ.
  ///
  /// \param lhs Left-hand location.
  /// \param rhs Right-hand location.
  /// \return True if \p lhs and \p rhs differ.
  friend bool operator!=(const Location &lhs, const Location &rhs) {
    return !(lhs == rhs);
  }

  /// Return true if \p lhs sorts before \p rhs.
  ///
  /// \param lhs Left-hand location.
  /// \param rhs Right-hand location.
  /// \return True if \p lhs sorts before \p rhs.
  friend bool operator<(const Location &lhs, const Location &rhs) {
    return std::tie(lhs.uri, lhs.range) < std::tie(rhs.uri, rhs.range);
  }
};

/// Parse a Location from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, Location &result,
                       llvm::json::Path path);
/// Serialize a Location to JSON.
///
/// \param value Location to serialize.
/// \return The JSON representation of the location.
LLVM_ABI llvm::json::Value toJSON(const Location &value);
/// Print a Location for debugging.
///
/// \param os Stream to write to.
/// \param value Location to print.
/// \return The stream \p os.
LLVM_ABI raw_ostream &operator<<(raw_ostream &os, const Location &value);

//===----------------------------------------------------------------------===//
// TextDocumentPositionParams
//===----------------------------------------------------------------------===//

/// Parameters identifying a position inside a text document.
struct TextDocumentPositionParams {
  /// The text document.
  TextDocumentIdentifier textDocument;

  /// The position inside the text document.
  Position position;
};

/// Parse TextDocumentPositionParams from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       TextDocumentPositionParams &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// ReferenceParams
//===----------------------------------------------------------------------===//

/// Options controlling a find-references request.
struct ReferenceContext {
  /// Include the declaration of the current symbol.
  bool includeDeclaration = false;
};

/// Parse a ReferenceContext from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, ReferenceContext &result,
                       llvm::json::Path path);

/// Parameters for the textDocument/references request.
struct ReferenceParams : TextDocumentPositionParams {
  /// Context options for the references request.
  ReferenceContext context;
};

/// Parse ReferenceParams from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, ReferenceParams &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// DidOpenTextDocumentParams
//===----------------------------------------------------------------------===//

/// Parameters for the textDocument/didOpen notification.
struct DidOpenTextDocumentParams {
  /// The document that was opened.
  TextDocumentItem textDocument;
};

/// Parse DidOpenTextDocumentParams from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       DidOpenTextDocumentParams &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// DidCloseTextDocumentParams
//===----------------------------------------------------------------------===//

/// Parameters for the textDocument/didClose notification.
struct DidCloseTextDocumentParams {
  /// The document that was closed.
  TextDocumentIdentifier textDocument;
};

/// Parse DidCloseTextDocumentParams from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       DidCloseTextDocumentParams &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// DidSaveTextDocumentParams
//===----------------------------------------------------------------------===//

/// Parameters for the textDocument/didSave notification.
struct DidSaveTextDocumentParams {
  /// The document that was saved.
  TextDocumentIdentifier textDocument;
};

/// Parse DidSaveTextDocumentParams from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       DidSaveTextDocumentParams &result, llvm::json::Path path);

//===----------------------------------------------------------------------===//
// DidChangeTextDocumentParams
//===----------------------------------------------------------------------===//

/// A content change to a text document.
struct TextDocumentContentChangeEvent {
  /// Try to apply this change to the given contents string.
  ///
  /// \param contents Document text to update in place.
  /// \return Success if the change was applied, or failure otherwise.
  LLVM_ABI LogicalResult applyTo(std::string &contents) const;
  /// Try to apply a set of changes to the given contents string.
  ///
  /// \param changes Content changes to apply in order.
  /// \param contents Document text to update in place.
  /// \return Success if all changes were applied, or failure otherwise.
  LLVM_ABI static LogicalResult
  applyTo(ArrayRef<TextDocumentContentChangeEvent> changes,
          std::string &contents);

  /// The range of the document that changed.
  std::optional<Range> range;

  /// The length of the range that got replaced.
  std::optional<int> rangeLength;

  /// The new text of the range/document.
  std::string text;
};

/// Parse a TextDocumentContentChangeEvent from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       TextDocumentContentChangeEvent &result,
                       llvm::json::Path path);

/// Parameters for the textDocument/didChange notification.
struct DidChangeTextDocumentParams {
  /// The document that changed.
  VersionedTextDocumentIdentifier textDocument;

  /// The actual content changes.
  std::vector<TextDocumentContentChangeEvent> contentChanges;
};

/// Parse DidChangeTextDocumentParams from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       DidChangeTextDocumentParams &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// MarkupContent
//===----------------------------------------------------------------------===//

/// Describes the content type that a client supports in various result literals
/// like `Hover`.
enum class MarkupKind {
  /// Plain text without markup.
  PlainText,
  /// Markdown markup.
  Markdown,
};
/// Print a MarkupKind for debugging.
///
/// \param os Stream to write to.
/// \param kind Markup kind to print.
/// \return The stream \p os.
LLVM_ABI raw_ostream &operator<<(raw_ostream &os, MarkupKind kind);

/// Human-readable content that may use a markup kind.
struct MarkupContent {
  /// The type of markup used in \c value.
  MarkupKind kind = MarkupKind::PlainText;
  /// The content itself.
  std::string value;
};

/// Serialize MarkupContent to JSON.
///
/// \param mc Markup content to serialize.
/// \return The JSON representation of the markup content.
LLVM_ABI llvm::json::Value toJSON(const MarkupContent &mc);

//===----------------------------------------------------------------------===//
// Hover
//===----------------------------------------------------------------------===//

/// Hover information shown when the user rests the cursor on a symbol.
struct Hover {
  /// Construct a default hover with the given range that uses Markdown content.
  ///
  /// \param range Optional highlight range for the hover.
  Hover(Range range) : contents{MarkupKind::Markdown, ""}, range(range) {}

  /// The hover's content.
  MarkupContent contents;

  /// An optional range is a range inside a text document that is used to
  /// visualize a hover, e.g. by changing the background color.
  std::optional<Range> range;
};

/// Serialize a Hover to JSON.
///
/// \param hover Hover information to serialize.
/// \return The JSON representation of the hover information.
LLVM_ABI llvm::json::Value toJSON(const Hover &hover);

//===----------------------------------------------------------------------===//
// SymbolKind
//===----------------------------------------------------------------------===//

/// Kind of a document or workspace symbol.
enum class SymbolKind {
  /// A file.
  File = 1,
  /// A module.
  Module = 2,
  /// A namespace.
  Namespace = 3,
  /// A package.
  Package = 4,
  /// A class.
  Class = 5,
  /// A method.
  Method = 6,
  /// A property.
  Property = 7,
  /// A field.
  Field = 8,
  /// A constructor.
  Constructor = 9,
  /// An enumeration.
  Enum = 10,
  /// An interface.
  Interface = 11,
  /// A function.
  Function = 12,
  /// A variable.
  Variable = 13,
  /// A constant.
  Constant = 14,
  /// A string.
  String = 15,
  /// A number.
  Number = 16,
  /// A boolean.
  Boolean = 17,
  /// An array.
  Array = 18,
  /// An object.
  Object = 19,
  /// A key.
  Key = 20,
  /// A null value.
  Null = 21,
  /// An enumeration member.
  EnumMember = 22,
  /// A struct.
  Struct = 23,
  /// An event.
  Event = 24,
  /// An operator.
  Operator = 25,
  /// A type parameter.
  TypeParameter = 26
};

//===----------------------------------------------------------------------===//
// DocumentSymbol
//===----------------------------------------------------------------------===//

/// Represents a hierarchical programming construct in a document.
///
/// Document symbols cover constructs like variables, classes, and interfaces.
/// They can be hierarchical and have two ranges: one that encloses its
/// definition and one that points to its most interesting range, e.g. the range
/// of an identifier.
struct DocumentSymbol {
  /// Construct an empty document symbol.
  DocumentSymbol() = default;
  /// Move-construct a document symbol.
  ///
  /// \param other Document symbol to move from.
  DocumentSymbol(DocumentSymbol &&other) = default;
  /// Construct a document symbol with the given name, kind, and ranges.
  ///
  /// \param name Display name of the symbol.
  /// \param kind Kind of the symbol.
  /// \param range Range enclosing the symbol.
  /// \param selectionRange Range to select when navigating to the symbol.
  DocumentSymbol(const Twine &name, SymbolKind kind, Range range,
                 Range selectionRange)
      : name(name.str()), kind(kind), range(range),
        selectionRange(selectionRange) {}

  /// The name of this symbol.
  std::string name;

  /// More detail for this symbol, e.g the signature of a function.
  std::string detail;

  /// The kind of this symbol.
  SymbolKind kind;

  /// The range enclosing this symbol, excluding leading/trailing whitespace.
  ///
  /// Includes comments and other trivia inside the symbol. Clients typically
  /// use this to decide whether the cursor is inside the symbol to reveal it in
  /// the UI.
  Range range;

  /// The range that should be selected and revealed when this symbol is being
  /// picked, e.g the name of a function. Must be contained by the `range`.
  Range selectionRange;

  /// Children of this symbol, e.g. properties of a class.
  std::vector<DocumentSymbol> children;
};

/// Serialize a DocumentSymbol to JSON.
///
/// \param symbol Document symbol to serialize.
/// \return The JSON representation of the document symbol.
LLVM_ABI llvm::json::Value toJSON(const DocumentSymbol &symbol);

//===----------------------------------------------------------------------===//
// DocumentSymbolParams
//===----------------------------------------------------------------------===//

/// Parameters for the textDocument/documentSymbol request.
struct DocumentSymbolParams {
  /// The text document to find symbols in.
  TextDocumentIdentifier textDocument;
};

/// Parse DocumentSymbolParams from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       DocumentSymbolParams &result, llvm::json::Path path);

//===----------------------------------------------------------------------===//
// DiagnosticRelatedInformation
//===----------------------------------------------------------------------===//

/// Related message and location associated with a diagnostic.
///
/// Use this to point to code locations that cause or relate to a diagnostic,
/// e.g. when duplicating a symbol in a scope.
struct DiagnosticRelatedInformation {
  /// Construct empty related diagnostic information.
  DiagnosticRelatedInformation() = default;
  /// Construct related information from a location and message.
  ///
  /// \param location Source location of the related information.
  /// \param message Related diagnostic message.
  DiagnosticRelatedInformation(Location location, std::string message)
      : location(std::move(location)), message(std::move(message)) {}

  /// The location of this related diagnostic information.
  Location location;
  /// The message of this related diagnostic information.
  std::string message;
};

/// Parse DiagnosticRelatedInformation from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       DiagnosticRelatedInformation &result,
                       llvm::json::Path path);
/// Serialize DiagnosticRelatedInformation to JSON.
///
/// \param info Related information to serialize.
/// \return The JSON representation of the related information.
LLVM_ABI llvm::json::Value toJSON(const DiagnosticRelatedInformation &info);

//===----------------------------------------------------------------------===//
// Diagnostic
//===----------------------------------------------------------------------===//

/// Severity level of a diagnostic.
enum class DiagnosticSeverity {
  /// Severity is unspecified; the client chooses how to present it.
  Undetermined = 0,
  /// Reports an error.
  Error = 1,
  /// Reports a warning.
  Warning = 2,
  /// Reports an informational message.
  Information = 3,
  /// Reports a hint.
  Hint = 4
};

/// Additional metadata tags for a diagnostic.
enum class DiagnosticTag {
  /// Unused or unnecessary code.
  Unnecessary = 1,
  /// Deprecated code.
  Deprecated = 2,
};

/// Serialize a DiagnosticTag to JSON.
///
/// \param tag Diagnostic tag to serialize.
/// \return The JSON representation of the diagnostic tag.
LLVM_ABI llvm::json::Value toJSON(DiagnosticTag tag);
/// Parse a DiagnosticTag from JSON.
///
/// \param value JSON value to parse.
/// \param result Enum filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, DiagnosticTag &result,
                       llvm::json::Path path);

/// A diagnostic message such as a compiler error or warning.
struct Diagnostic {
  /// The source range where the message applies.
  Range range;

  /// The diagnostic's severity. Can be omitted. If omitted it is up to the
  /// client to interpret diagnostics as error, warning, info or hint.
  DiagnosticSeverity severity = DiagnosticSeverity::Undetermined;

  /// A human-readable string describing the source of this diagnostic, e.g.
  /// 'typescript' or 'super lint'.
  std::string source;

  /// The diagnostic's message.
  std::string message;

  /// An array of related diagnostic information, e.g. when symbol-names within
  /// a scope collide all definitions can be marked via this property.
  std::optional<std::vector<DiagnosticRelatedInformation>> relatedInformation;

  /// Additional metadata about the diagnostic.
  std::vector<DiagnosticTag> tags;

  /// Optional category name for this diagnostic.
  ///
  /// An LSP extension used to send the category name to the client. The
  /// category typically describes the compilation stage during which the issue
  /// was produced, e.g. "Semantic Issue" or "Parse Issue".
  std::optional<std::string> category;
};

/// Serialize a Diagnostic to JSON.
///
/// \param diag Diagnostic to serialize.
/// \return The JSON representation of the diagnostic.
LLVM_ABI llvm::json::Value toJSON(const Diagnostic &diag);
/// Parse a Diagnostic from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, Diagnostic &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// PublishDiagnosticsParams
//===----------------------------------------------------------------------===//

/// Parameters for the textDocument/publishDiagnostics notification.
struct PublishDiagnosticsParams {
  /// Construct publish-diagnostics parameters for a document version.
  ///
  /// \param uri Document URI the diagnostics apply to.
  /// \param version Document version number.
  PublishDiagnosticsParams(URIForFile uri, int64_t version)
      : uri(std::move(uri)), version(version) {}

  /// The URI for which diagnostic information is reported.
  URIForFile uri;
  /// The list of reported diagnostics.
  std::vector<Diagnostic> diagnostics;
  /// The version number of the document the diagnostics are published for.
  int64_t version;
};

/// Serialize PublishDiagnosticsParams to JSON.
///
/// \param params Parameters to serialize.
/// \return The JSON representation of the parameters.
LLVM_ABI llvm::json::Value toJSON(const PublishDiagnosticsParams &params);

//===----------------------------------------------------------------------===//
// TextEdit
//===----------------------------------------------------------------------===//

/// A textual edit applicable to a text document.
struct TextEdit {
  /// The range of the text document to be manipulated. To insert
  /// text into a document create a range where start === end.
  Range range;

  /// The string to be inserted. For delete operations use an
  /// empty string.
  std::string newText;
};

/// Return true if \p lhs and \p rhs describe the same edit.
///
/// \param lhs Left-hand edit.
/// \param rhs Right-hand edit.
/// \return True if \p lhs and \p rhs describe the same edit.
inline bool operator==(const TextEdit &lhs, const TextEdit &rhs) {
  return std::tie(lhs.newText, lhs.range) == std::tie(rhs.newText, rhs.range);
}

/// Parse a TextEdit from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, TextEdit &result,
                       llvm::json::Path path);
/// Serialize a TextEdit to JSON.
///
/// \param value Edit to serialize.
/// \return The JSON representation of the edit.
LLVM_ABI llvm::json::Value toJSON(const TextEdit &value);
/// Print a TextEdit for debugging.
///
/// \param os Stream to write to.
/// \param value Edit to print.
/// \return The stream \p os.
LLVM_ABI raw_ostream &operator<<(raw_ostream &os, const TextEdit &value);

//===----------------------------------------------------------------------===//
// CompletionItemKind
//===----------------------------------------------------------------------===//

/// The kind of a completion entry.
enum class CompletionItemKind {
  /// No kind was provided.
  Missing = 0,
  /// Plain text.
  Text = 1,
  /// A method.
  Method = 2,
  /// A function.
  Function = 3,
  /// A constructor.
  Constructor = 4,
  /// A field.
  Field = 5,
  /// A variable.
  Variable = 6,
  /// A class.
  Class = 7,
  /// An interface.
  Interface = 8,
  /// A module.
  Module = 9,
  /// A property.
  Property = 10,
  /// A unit.
  Unit = 11,
  /// A value.
  Value = 12,
  /// An enumeration.
  Enum = 13,
  /// A keyword.
  Keyword = 14,
  /// A snippet.
  Snippet = 15,
  /// A color.
  Color = 16,
  /// A file.
  File = 17,
  /// A reference.
  Reference = 18,
  /// A folder.
  Folder = 19,
  /// An enumeration member.
  EnumMember = 20,
  /// A constant.
  Constant = 21,
  /// A struct.
  Struct = 22,
  /// An event.
  Event = 23,
  /// An operator.
  Operator = 24,
  /// A type parameter.
  TypeParameter = 25,
};
/// Parse a CompletionItemKind from JSON.
///
/// \param value JSON value to parse.
/// \param result Enum filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       CompletionItemKind &result, llvm::json::Path path);

/// Smallest valid CompletionItemKind value (excluding Missing).
constexpr auto kCompletionItemKindMin =
    static_cast<size_t>(CompletionItemKind::Text);
/// Largest valid CompletionItemKind value.
constexpr auto kCompletionItemKindMax =
    static_cast<size_t>(CompletionItemKind::TypeParameter);
/// Bitset of client-supported completion item kinds.
using CompletionItemKindBitset = std::bitset<kCompletionItemKindMax + 1>;
/// Parse a CompletionItemKindBitset from JSON.
///
/// \param value JSON value to parse.
/// \param result Bitset filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       CompletionItemKindBitset &result, llvm::json::Path path);

/// Map \p kind to a kind supported by the client capability bitset.
///
/// \param kind Requested completion item kind.
/// \param supportedCompletionItemKinds Client-supported kinds.
/// \return A completion item kind supported by the client.
LLVM_ABI CompletionItemKind
adjustKindToCapability(CompletionItemKind kind,
                       CompletionItemKindBitset &supportedCompletionItemKinds);

//===----------------------------------------------------------------------===//
// CompletionItem
//===----------------------------------------------------------------------===//

/// Defines whether the insert text in a completion item should be interpreted
/// as plain text or a snippet.
enum class InsertTextFormat {
  /// No insert-text format was provided.
  Missing = 0,
  /// The primary text to be inserted is treated as a plain string.
  PlainText = 1,
  /// The primary text to be inserted is treated as a snippet.
  ///
  /// A snippet can define tab stops and placeholders with `$1`, `$2`
  /// and `${3:foo}`. `$0` defines the final tab stop, it defaults to the end
  /// of the snippet. Placeholders with equal identifiers are linked, that is
  /// typing in one will update others too.
  ///
  /// See also:
  /// https//github.com/Microsoft/vscode/blob/master/src/vs/editor/contrib/snippet/common/snippet.md
  Snippet = 2,
};

/// A completion item suggested by the language server.
struct CompletionItem {
  /// Construct an empty completion item.
  CompletionItem() = default;
  /// Construct a completion item with label, kind, and optional sort text.
  ///
  /// \param label Display label for the item.
  /// \param kind Kind used to choose an icon in the editor.
  /// \param sortText Optional text used when sorting items.
  CompletionItem(const Twine &label, CompletionItemKind kind,
                 StringRef sortText = "")
      : label(label.str()), kind(kind), sortText(sortText.str()),
        insertTextFormat(InsertTextFormat::PlainText) {}

  /// The label of this completion item. By default also the text that is
  /// inserted when selecting this completion.
  std::string label;

  /// The kind of this completion item. Based of the kind an icon is chosen by
  /// the editor.
  CompletionItemKind kind = CompletionItemKind::Missing;

  /// A human-readable string with additional information about this item, like
  /// type or symbol information.
  std::string detail;

  /// A human-readable string that represents a doc-comment.
  std::optional<MarkupContent> documentation;

  /// A string that should be used when comparing this item with other items.
  /// When `falsy` the label is used.
  std::string sortText;

  /// A string that should be used when filtering a set of completion items.
  /// When `falsy` the label is used.
  std::string filterText;

  /// A string that should be inserted to a document when selecting this
  /// completion. When `falsy` the label is used.
  std::string insertText;

  /// The format of the insert text. The format applies to both the `insertText`
  /// property and the `newText` property of a provided `textEdit`.
  InsertTextFormat insertTextFormat = InsertTextFormat::Missing;

  /// An edit which is applied to a document when selecting this completion.
  /// When an edit is provided `insertText` is ignored.
  ///
  /// Note: The range of the edit must be a single line range and it must
  /// contain the position at which completion has been requested.
  std::optional<TextEdit> textEdit;

  /// An optional array of additional text edits that are applied when selecting
  /// this completion. Edits must not overlap with the main edit nor with
  /// themselves.
  std::vector<TextEdit> additionalTextEdits;

  /// Indicates if this item is deprecated.
  bool deprecated = false;
};

/// Serialize a CompletionItem to JSON.
///
/// \param value Completion item to serialize.
/// \return The JSON representation of the completion item.
LLVM_ABI llvm::json::Value toJSON(const CompletionItem &value);
/// Print a CompletionItem for debugging.
///
/// \param os Stream to write to.
/// \param value Completion item to print.
/// \return The stream \p os.
LLVM_ABI raw_ostream &operator<<(raw_ostream &os, const CompletionItem &value);
/// Return true if \p lhs sorts before \p rhs.
///
/// \param lhs Left-hand completion item.
/// \param rhs Right-hand completion item.
/// \return True if \p lhs sorts before \p rhs.
LLVM_ABI bool operator<(const CompletionItem &lhs, const CompletionItem &rhs);

//===----------------------------------------------------------------------===//
// CompletionList
//===----------------------------------------------------------------------===//

/// Represents a collection of completion items to be presented in the editor.
struct CompletionList {
  /// The list is not complete. Further typing should result in recomputing the
  /// list.
  bool isIncomplete = false;

  /// The completion items.
  std::vector<CompletionItem> items;
};

/// Serialize a CompletionList to JSON.
///
/// \param value Completion list to serialize.
/// \return The JSON representation of the completion list.
LLVM_ABI llvm::json::Value toJSON(const CompletionList &value);

//===----------------------------------------------------------------------===//
// CompletionContext
//===----------------------------------------------------------------------===//

/// How a completion was triggered.
enum class CompletionTriggerKind {
  /// Completion was triggered by typing an identifier (24x7 code
  /// complete), manual invocation (e.g Ctrl+Space) or via API.
  Invoked = 1,

  /// Completion was triggered by a trigger character specified by
  /// the `triggerCharacters` properties of the `CompletionRegistrationOptions`.
  TriggerCharacter = 2,

  /// Completion was re-triggered as the current completion list is incomplete.
  TriggerTriggerForIncompleteCompletions = 3
};

/// Additional context for a completion request.
struct CompletionContext {
  /// How the completion was triggered.
  CompletionTriggerKind triggerKind = CompletionTriggerKind::Invoked;

  /// The trigger character (a single character) that has trigger code complete.
  /// Is undefined if `triggerKind !== CompletionTriggerKind.TriggerCharacter`
  std::string triggerCharacter;
};

/// Parse a CompletionContext from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       CompletionContext &result, llvm::json::Path path);

//===----------------------------------------------------------------------===//
// CompletionParams
//===----------------------------------------------------------------------===//

/// Parameters for the textDocument/completion request.
struct CompletionParams : TextDocumentPositionParams {
  /// Additional completion context from the client.
  CompletionContext context;
};

/// Parse CompletionParams from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, CompletionParams &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// ParameterInformation
//===----------------------------------------------------------------------===//

/// A single parameter of a particular signature.
struct ParameterInformation {
  /// The label of this parameter. Ignored when labelOffsets is set.
  std::string labelString;

  /// Inclusive start and exclusive end offsets withing the containing signature
  /// label.
  std::optional<std::pair<unsigned, unsigned>> labelOffsets;

  /// The documentation of this parameter. Optional.
  std::string documentation;
};

/// Serialize ParameterInformation to JSON.
///
/// \param value Parameter information to serialize.
/// \return The JSON representation of the parameter information.
LLVM_ABI llvm::json::Value toJSON(const ParameterInformation &value);

//===----------------------------------------------------------------------===//
// SignatureInformation
//===----------------------------------------------------------------------===//

/// Represents the signature of something callable.
struct SignatureInformation {
  /// The label of this signature. Mandatory.
  std::string label;

  /// The documentation of this signature. Optional.
  std::string documentation;

  /// The parameters of this signature.
  std::vector<ParameterInformation> parameters;
};

/// Serialize SignatureInformation to JSON.
///
/// \param value Signature information to serialize.
/// \return The JSON representation of the signature information.
LLVM_ABI llvm::json::Value toJSON(const SignatureInformation &value);
/// Print SignatureInformation for debugging.
///
/// \param os Stream to write to.
/// \param value Signature information to print.
/// \return The stream \p os.
LLVM_ABI raw_ostream &operator<<(raw_ostream &os,
                                 const SignatureInformation &value);

//===----------------------------------------------------------------------===//
// SignatureHelp
//===----------------------------------------------------------------------===//

/// Represents the signature of a callable.
struct SignatureHelp {
  /// The resulting signatures.
  std::vector<SignatureInformation> signatures;

  /// The active signature.
  int activeSignature = 0;

  /// The active parameter of the active signature.
  int activeParameter = 0;
};

/// Serialize SignatureHelp to JSON.
///
/// \param value Signature help to serialize.
/// \return The JSON representation of the signature help.
LLVM_ABI llvm::json::Value toJSON(const SignatureHelp &value);

//===----------------------------------------------------------------------===//
// DocumentLinkParams
//===----------------------------------------------------------------------===//

/// Parameters for the document link request.
struct DocumentLinkParams {
  /// The document to provide document links for.
  TextDocumentIdentifier textDocument;
};

/// Parse DocumentLinkParams from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       DocumentLinkParams &result, llvm::json::Path path);

//===----------------------------------------------------------------------===//
// DocumentLink
//===----------------------------------------------------------------------===//

/// A range in a text document that links to an internal or external resource,
/// like another text document or a web site.
struct DocumentLink {
  /// Construct an empty document link.
  DocumentLink() = default;
  /// Construct a document link from a range and target URI.
  ///
  /// \param range Range this link applies to.
  /// \param target URI this link points to.
  DocumentLink(Range range, URIForFile target)
      : range(range), target(std::move(target)) {}

  /// The range this link applies to.
  Range range;

  /// The uri this link points to. If missing a resolve request is sent later.
  URIForFile target;

  // TODO: The following optional fields defined by the language server protocol
  // are unsupported:
  //
  // data?: any - A data entry field that is preserved on a document link
  //              between a DocumentLinkRequest and a
  //              DocumentLinkResolveRequest.

  /// Return true if \p lhs and \p rhs have the same range and target.
  ///
  /// \param lhs Left-hand link.
  /// \param rhs Right-hand link.
  /// \return True if \p lhs and \p rhs have the same range and target.
  friend bool operator==(const DocumentLink &lhs, const DocumentLink &rhs) {
    return lhs.range == rhs.range && lhs.target == rhs.target;
  }

  /// Return true if \p lhs and \p rhs differ.
  ///
  /// \param lhs Left-hand link.
  /// \param rhs Right-hand link.
  /// \return True if \p lhs and \p rhs differ.
  friend bool operator!=(const DocumentLink &lhs, const DocumentLink &rhs) {
    return !(lhs == rhs);
  }
};

/// Serialize a DocumentLink to JSON.
///
/// \param value Document link to serialize.
/// \return The JSON representation of the document link.
LLVM_ABI llvm::json::Value toJSON(const DocumentLink &value);

//===----------------------------------------------------------------------===//
// InlayHintsParams
//===----------------------------------------------------------------------===//

/// A parameter literal used in inlay hint requests.
struct InlayHintsParams {
  /// The text document.
  TextDocumentIdentifier textDocument;

  /// The visible document range for which inlay hints should be computed.
  Range range;
};

/// Parse InlayHintsParams from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, InlayHintsParams &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// InlayHintKind
//===----------------------------------------------------------------------===//

/// Inlay hint kinds.
enum class InlayHintKind {
  /// An inlay hint that for a type annotation.
  ///
  /// An example of a type hint is a hint in this position:
  ///    auto var ^ = expr;
  /// which shows the deduced type of the variable.
  Type = 1,

  /// An inlay hint that is for a parameter.
  ///
  /// An example of a parameter hint is a hint in this position:
  ///    func(^arg);
  /// which shows the name of the corresponding parameter.
  Parameter = 2,
};

//===----------------------------------------------------------------------===//
// InlayHint
//===----------------------------------------------------------------------===//

/// Inlay hint information.
struct InlayHint {
  /// Construct an inlay hint of the given kind at \p pos.
  ///
  /// \param kind Kind of inlay hint.
  /// \param pos Document position of the hint.
  InlayHint(InlayHintKind kind, Position pos) : position(pos), kind(kind) {}

  /// The position of this hint.
  Position position;

  /// The label of this hint. A human readable string or an array of
  /// InlayHintLabelPart label parts.
  ///
  /// *Note* that neither the string nor the label part can be empty.
  std::string label;

  /// The kind of this hint. Can be omitted in which case the client should fall
  /// back to a reasonable default.
  InlayHintKind kind;

  /// Render padding before the hint.
  ///
  /// Note: Padding should use the editor's background color, not the
  /// background color of the hint itself. That means padding can be used
  /// to visually align/separate an inlay hint.
  bool paddingLeft = false;

  /// Render padding after the hint.
  ///
  /// Note: Padding should use the editor's background color, not the
  /// background color of the hint itself. That means padding can be used
  /// to visually align/separate an inlay hint.
  bool paddingRight = false;
};

/// Serialize an InlayHint to JSON.
///
/// \param value Inlay hint to serialize.
/// \return The JSON representation of the inlay hint.
LLVM_ABI llvm::json::Value toJSON(const InlayHint &value);
/// Return true if \p lhs and \p rhs are equal.
///
/// \param lhs Left-hand inlay hint.
/// \param rhs Right-hand inlay hint.
/// \return True if \p lhs and \p rhs are equal.
LLVM_ABI bool operator==(const InlayHint &lhs, const InlayHint &rhs);
/// Return true if \p lhs sorts before \p rhs.
///
/// \param lhs Left-hand inlay hint.
/// \param rhs Right-hand inlay hint.
/// \return True if \p lhs sorts before \p rhs.
LLVM_ABI bool operator<(const InlayHint &lhs, const InlayHint &rhs);
/// Print an InlayHintKind for debugging.
///
/// \param os Stream to write to.
/// \param value Inlay hint kind to print.
/// \return The stream \p os.
LLVM_ABI llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                       InlayHintKind value);

//===----------------------------------------------------------------------===//
// CodeActionContext
//===----------------------------------------------------------------------===//

/// Additional context for a code-action request.
struct CodeActionContext {
  /// Client-side diagnostics overlapping the requested range.
  ///
  /// Provided so the server knows which errors are currently presented to the
  /// user for the given range. There is no guarantee that these accurately
  /// reflect the error state of the resource. The primary parameter to compute
  /// code actions is the provided range.
  std::vector<Diagnostic> diagnostics;

  /// Requested kind of actions to return.
  ///
  /// Actions not of this kind are filtered out by the client before being
  /// shown. So servers can omit computing them.
  std::vector<std::string> only;
};

/// Parse a CodeActionContext from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value,
                       CodeActionContext &result, llvm::json::Path path);

//===----------------------------------------------------------------------===//
// CodeActionParams
//===----------------------------------------------------------------------===//

/// Parameters for the textDocument/codeAction request.
struct CodeActionParams {
  /// The document in which the command was invoked.
  TextDocumentIdentifier textDocument;

  /// The range for which the command was invoked.
  Range range;

  /// Context carrying additional information.
  CodeActionContext context;
};

/// Parse CodeActionParams from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, CodeActionParams &result,
                       llvm::json::Path path);

//===----------------------------------------------------------------------===//
// WorkspaceEdit
//===----------------------------------------------------------------------===//

/// A set of changes to multiple resources described as document edits.
struct WorkspaceEdit {
  /// Holds changes to existing resources.
  std::map<std::string, std::vector<TextEdit>> changes;

  /// Note: "documentChanges" is not currently used because currently there is
  /// no support for versioned edits.
};

/// Parse a WorkspaceEdit from JSON.
///
/// \param value JSON value to parse.
/// \param result Object filled on success.
/// \param path JSON path used for error reporting.
/// \return True on success.
LLVM_ABI bool fromJSON(const llvm::json::Value &value, WorkspaceEdit &result,
                       llvm::json::Path path);
/// Serialize a WorkspaceEdit to JSON.
///
/// \param value Workspace edit to serialize.
/// \return The JSON representation of the workspace edit.
LLVM_ABI llvm::json::Value toJSON(const WorkspaceEdit &value);

//===----------------------------------------------------------------------===//
// CodeAction
//===----------------------------------------------------------------------===//

/// A code action represents a change that can be performed in code, e.g. to fix
/// a problem or to refactor code.
///
/// A CodeAction must set either `edit` and/or a `command`. If both are
/// supplied, the `edit` is applied first, then the `command` is executed.
struct CodeAction {
  /// A short, human-readable, title for this code action.
  std::string title;

  /// The kind of the code action.
  /// Used to filter code actions.
  std::optional<std::string> kind;
  /// Kind string for quick-fix code actions.
  LLVM_ABI const static llvm::StringLiteral kQuickFix;
  /// Kind string for refactor code actions.
  LLVM_ABI const static llvm::StringLiteral kRefactor;
  /// Kind string for informational code actions.
  LLVM_ABI const static llvm::StringLiteral kInfo;

  /// The diagnostics that this code action resolves.
  std::optional<std::vector<Diagnostic>> diagnostics;

  /// Whether this is a preferred action for auto-fix or keybindings.
  ///
  /// Preferred actions are used by the `auto fix` command and can be targeted
  /// by keybindings. A quick fix should be marked preferred if it properly
  /// addresses the underlying error. A refactoring should be marked preferred
  /// if it is the most reasonable choice of actions to take.
  bool isPreferred = false;

  /// The workspace edit this code action performs.
  std::optional<WorkspaceEdit> edit;
};

/// Serialize a CodeAction to JSON.
///
/// \param value Code action to serialize.
/// \return The JSON representation of the code action.
LLVM_ABI llvm::json::Value toJSON(const CodeAction &value);

//===----------------------------------------------------------------------===//
//  ShowMessageParams
//===----------------------------------------------------------------------===//

/// Type of a show-message notification.
enum class MessageType {
  /// An error message.
  Error = 1,
  /// A warning message.
  Warning = 2,
  /// An informational message.
  Info = 3,
  /// A log message.
  Log = 4,
  /// A debug message.
  Debug = 5
};

/// An action item presented with a show-message request.
struct MessageActionItem {
  /// A short title like 'Retry', 'Open Log' etc.
  std::string title;
};

/// Parameters for a window/showMessage notification.
struct ShowMessageParams {
  /// Construct show-message parameters from a type and message.
  ///
  /// \param Type Severity/type of the message.
  /// \param Message Human-readable message text.
  ShowMessageParams(MessageType Type, std::string Message)
      : type(Type), message(Message) {}
  /// The message type.
  MessageType type;
  /// The actual message.
  std::string message;
  /// The message action items to present.
  std::optional<std::vector<MessageActionItem>> actions;
};

/// Serialize a MessageActionItem to JSON.
///
/// \param Params Action item to serialize.
/// \return The JSON representation of the action item.
LLVM_ABI llvm::json::Value toJSON(const MessageActionItem &Params);

/// Serialize ShowMessageParams to JSON.
///
/// \param Params Parameters to serialize.
/// \return The JSON representation of the parameters.
LLVM_ABI llvm::json::Value toJSON(const ShowMessageParams &Params);

} // namespace lsp
} // namespace llvm

namespace llvm {
template <> struct format_provider<llvm::lsp::Position> {
  static void format(const llvm::lsp::Position &pos, raw_ostream &os,
                     StringRef style) {
    assert(style.empty() && "style modifiers for this type are not supported");
    os << pos;
  }
};
} // namespace llvm

#endif

// NOLINTEND(readability-identifier-naming)
