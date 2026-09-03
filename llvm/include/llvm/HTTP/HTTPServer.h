//===--- HTTPServer.h - HTTP server library ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations of the HTTPServer and HTTPServerRequest
/// classes, the HTTPResponse, and StreamingHTTPResponse structs, and the
/// streamFile function.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_HTTP_HTTPSERVER_H
#define LLVM_HTTP_HTTPSERVER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#ifdef LLVM_ENABLE_HTTPLIB
// forward declarations
namespace httplib {
class Request;
class Response;
class Server;
} // namespace httplib
#endif

namespace llvm {

struct HTTPResponse;
struct StreamingHTTPResponse;
class HTTPServer;

/// Error info for failures originating in the HTTP server.
class HTTPServerError : public ErrorInfo<HTTPServerError, ECError> {
public:
  /// RTTI identifier used by ErrorInfo::classID.
  static char ID;
  /// Construct an HTTPServerError with message \p Msg.
  ///
  /// \param Msg Human-readable description of the failure.
  HTTPServerError(const Twine &Msg);
  /// Print this error's message to stream \p OS.
  ///
  /// \param OS Stream to write the error message to.
  void log(raw_ostream &OS) const override;

private:
  std::string Msg;
};

/// An inbound HTTP request being handled by HTTPServer.
class HTTPServerRequest {
  friend HTTPServer;

#ifdef LLVM_ENABLE_HTTPLIB
private:
  HTTPServerRequest(const httplib::Request &HTTPLibRequest,
                    httplib::Response &HTTPLibResponse);
  httplib::Response &HTTPLibResponse;
#endif

public:
  /// The URL path of the request.
  std::string UrlPath;
  /// The elements correspond to match groups in the url path matching regex.
  SmallVector<std::string, 1> UrlPathMatches;

  // TODO bring in HTTP headers

  /// Set a streaming response for this request.
  ///
  /// \param Response Streaming response body and headers to send.
  void setResponse(StreamingHTTPResponse Response);
  /// Set a buffered response for this request.
  ///
  /// \param Response Complete response body and headers to send.
  void setResponse(HTTPResponse Response);
};

/// A complete buffered HTTP response with status, content type, and body.
struct HTTPResponse {
  /// HTTP status code to return.
  unsigned Code;
  /// MIME content type of the response body.
  const char *ContentType;
  /// Response body payload.
  StringRef Body;
};

/// Callable that handles an inbound HTTPServerRequest.
typedef std::function<void(HTTPServerRequest &)> HTTPRequestHandler;

/// Callback that supplies a chunk of a streaming HTTP response body.
///
/// An HTTPContentProvider is called by the HTTPServer to obtain chunks of the
/// streaming response body. The returned chunk should be located at Offset
/// bytes and have Length bytes.
typedef std::function<StringRef(size_t /*Offset*/, size_t /*Length*/)>
    HTTPContentProvider;

/// Wraps the content provider with HTTP Status code and headers.
struct StreamingHTTPResponse {
  /// HTTP status code to return.
  unsigned Code;
  /// MIME content type of the response body.
  const char *ContentType;
  /// Total length in bytes of the streamed body.
  size_t ContentLength;
  /// Provider invoked to obtain body chunks.
  HTTPContentProvider Provider;
  /// Called after the response transfer is complete with the success value of
  /// the transfer.
  std::function<void(bool)> CompletionHandler = [](bool Success) {};
};

/// Sets the response to stream the file at FilePath, if available, and
/// otherwise an HTTP 404 error response.
///
/// \param Request Request whose response will be set.
/// \param FilePath Path of the file to stream.
/// \return true if the file was opened and streamed; false if a 404 was set.
bool streamFile(HTTPServerRequest &Request, StringRef FilePath);

/// An HTTP server which can listen on a single TCP/IP port for HTTP
/// requests and delgate them to the appropriate registered handler.
class HTTPServer {
#ifdef LLVM_ENABLE_HTTPLIB
  std::unique_ptr<httplib::Server> Server;
  unsigned Port = 0;
#endif
public:
  /// Construct an HTTP server that is not yet bound or listening.
  HTTPServer();
  /// Destroy the server, stopping it if still listening.
  ~HTTPServer();

  /// Returns true only if LLVM has been compiled with a working HTTPServer.
  ///
  /// \return true if HTTP server support is available in this build.
  static bool isAvailable();

  /// Register a URL pattern routing rule with \p Handler.
  ///
  /// When the server is listening, each request is dispatched to the first
  /// registered handler whose UrlPathPattern matches the UrlPath.
  ///
  /// \param UrlPathPattern Regex pattern matched against request URL paths.
  /// \param Handler Handler invoked for requests matching \p UrlPathPattern.
  /// \return Success, or an error if \p UrlPathPattern is not a valid regex.
  Error get(StringRef UrlPathPattern, HTTPRequestHandler Handler);

  /// Attempts to assign the requested port and interface, returning an Error
  /// upon failure.
  ///
  /// \param Port TCP port to bind.
  /// \param HostInterface Network interface address to bind (default all).
  /// \return Success, or an error if the address could not be assigned.
  Error bind(unsigned Port, const char *HostInterface = "0.0.0.0");

  /// Attempts to assign any available port and interface, returning either the
  /// port number or an Error upon failure.
  ///
  /// \param HostInterface Network interface address to bind (default all).
  /// \return The assigned port number, or an error if binding fails.
  Expected<unsigned> bind(const char *HostInterface = "0.0.0.0");

  /// Attempts to listen for requests on the bound port. Returns an Error if
  /// called before binding a port.
  ///
  /// \return Success, or an error if unbound or listening fails.
  Error listen();

  /// If the server is listening, stop and unbind the socket.
  void stop();
};
} // end namespace llvm

#endif // LLVM_HTTP_HTTPSERVER_H
