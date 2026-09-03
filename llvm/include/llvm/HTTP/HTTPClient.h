//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations of the HTTPClient library for issuing
/// HTTP requests and handling the responses.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_HTTP_HTTPCLIENT_H
#define LLVM_HTTP_HTTPCLIENT_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

#include <chrono>
#include <optional>

namespace llvm {

/// HTTP methods supported by HTTPRequest.
enum class HTTPMethod {
  /// HTTP GET method.
  GET
};

/// A stateless description of an outbound HTTP request.
struct HTTPRequest {
  /// Target URL of the request.
  SmallString<128> Url;
  /// Additional HTTP headers to include with the request.
  SmallVector<std::string, 0> Headers;
  /// HTTP method used for the request.
  HTTPMethod Method = HTTPMethod::GET;
  /// Follow redirects without security downgrades.
  bool FollowRedirects = true;
  /// Allow self-signed TLS certificates with this SHA-256 (WinHTTP only).
  std::optional<std::string> PinnedCertFingerprint;
  /// Construct a request for the given URL with default options.
  /// \param Url Target URL for the request.
  HTTPRequest(StringRef Url);
};

/// Return true if two HTTP requests describe the same outbound request.
/// \param A First request to compare.
/// \param B Second request to compare.
/// \return True if the requests describe the same outbound request.
bool operator==(const HTTPRequest &A, const HTTPRequest &B);

/// Handler for state updates while an HTTPRequest is performed.
///
/// Can trigger the client to abort the request by returning an Error from any
/// of its methods.
class HTTPResponseHandler {
public:
  /// Processes an additional chunk of bytes of the HTTP response body.
  /// \param BodyChunk Next contiguous slice of the response body.
  /// \return Success, or an error that aborts the request.
  virtual Error handleBodyChunk(StringRef BodyChunk) = 0;

protected:
  /// Destroy the response handler.
  ~HTTPResponseHandler();
};

/// A reusable client that can perform HTTPRequests through a network socket.
class HTTPClient {
#if defined(LLVM_ENABLE_CURL) || defined(_WIN32)
  void *Handle = nullptr;
#endif

public:
  /// Construct an HTTP client. Requires prior \c initialize().
  HTTPClient();
  /// Destroy the HTTP client and release its resources.
  ~HTTPClient();

  /// True after \c initialize() until \c cleanup().
  static bool IsInitialized;

  /// Returns true only if LLVM has been compiled with a working HTTPClient.
  /// \return True if a working HTTPClient is available in this build.
  static bool isAvailable();

  /// Must be called at the beginning of a program, while it is a single thread.
  static void initialize();

  /// Must be called at the end of a program, while it is a single thread.
  static void cleanup();

  /// Sets the timeout for the entire request, in milliseconds. A zero or
  /// negative value means the request never times out.
  /// \param Timeout Maximum duration for the request; zero or negative disables.
  void setTimeout(std::chrono::milliseconds Timeout);

  /// Perform an HTTP request, passing response data to a handler.
  ///
  /// Aborts if an error is returned by a Handler method.
  /// \param Request The HTTP request to perform.
  /// \param Handler Receives response body chunks during the transfer.
  /// \return Success, or any error that occurred during the request.
  Error perform(const HTTPRequest &Request, HTTPResponseHandler &Handler);

  /// Returns the last received response code or zero if none.
  /// \return The last HTTP response code, or zero if none was received.
  unsigned responseCode();
};

} // end namespace llvm

#endif // LLVM_HTTP_HTTPCLIENT_H
