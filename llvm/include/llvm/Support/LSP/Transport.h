//===--- Transport.h - Sending and Receiving LSP messages -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The language server protocol is usually implemented by writing messages as
// JSON-RPC over the stdin/stdout of a subprocess. This file contains a JSON
// transport interface that handles this communication.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_LSP_TRANSPORT_H
#define LLVM_SUPPORT_LSP_TRANSPORT_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/LSP/Logging.h"
#include "llvm/Support/LSP/Protocol.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace llvm {
/// Return \p Op rendered to a string via \c operator<<.
/// \param Op Value to print into the returned string.
/// \return A string containing the printed representation of \p Op.
template <typename T> static std::string debugString(T &&Op) {
  std::string InstrStr;
  llvm::raw_string_ostream Os(InstrStr);
  Os << Op;
  return Os.str();
}
namespace lsp {
class MessageHandler;

//===----------------------------------------------------------------------===//
// JSONTransport
//===----------------------------------------------------------------------===//

/// The encoding style of the JSON-RPC messages (both input and output).
enum JSONStreamStyle {
  /// Encoding per the LSP specification, with mandatory Content-Length header.
  Standard,
  /// Messages are delimited by a '// -----' line. Comment lines start with //.
  Delimited
};

/// An abstract class used by the JSONTransport to read JSON message.
class JSONTransportInput {
public:
  /// Construct a JSON transport input with the given stream style.
  /// \param Style Encoding style used when reading messages.
  explicit JSONTransportInput(JSONStreamStyle Style = JSONStreamStyle::Standard)
      : Style(Style) {}
  /// Destroy the JSON transport input.
  virtual ~JSONTransportInput() = default;

  /// Return true if the input stream is in an error state.
  /// \return True if the input stream is in an error state.
  virtual bool hasError() const = 0;
  /// Return true if the input stream has reached end-of-file.
  /// \return True if the input stream has reached end-of-file.
  virtual bool isEndOfInput() const = 0;

  /// Read in a message from the input stream.
  /// \param Json Buffer filled with the raw JSON message on success.
  /// \return Success if a message was read, or failure otherwise.
  LogicalResult readMessage(std::string &Json) {
    return Style == JSONStreamStyle::Delimited ? readDelimitedMessage(Json)
                                               : readStandardMessage(Json);
  }
  /// Read a delimited-style JSON message from the input stream.
  /// \param Json Buffer filled with the raw JSON message on success.
  /// \return Success if a message was read, or failure otherwise.
  virtual LogicalResult readDelimitedMessage(std::string &Json) = 0;
  /// Read a standard Content-Length JSON message from the input stream.
  /// \param Json Buffer filled with the raw JSON message on success.
  /// \return Success if a message was read, or failure otherwise.
  virtual LogicalResult readStandardMessage(std::string &Json) = 0;

private:
  /// The JSON stream style to use.
  JSONStreamStyle Style;
};

/// Concrete implementation of the JSONTransportInput that reads from a file.
class LLVM_ABI JSONTransportInputOverFile : public JSONTransportInput {
public:
  /// Construct a file-backed JSON transport input.
  /// \param In File handle to read messages from.
  /// \param Style Encoding style used when reading messages.
  explicit JSONTransportInputOverFile(
      std::FILE *In, JSONStreamStyle Style = JSONStreamStyle::Standard)
      : JSONTransportInput(Style), In(In) {}

  /// Return true if the input file is in an error state.
  /// \return True if the input file is in an error state.
  bool hasError() const final { return ferror(In); }
  /// Return true if the input file has reached end-of-file.
  /// \return True if the input file has reached end-of-file.
  bool isEndOfInput() const final { return feof(In); }

  /// Read a delimited-style JSON message from the input file.
  /// \param Json Buffer filled with the raw JSON message on success.
  /// \return Success if a message was read, or failure otherwise.
  LogicalResult readDelimitedMessage(std::string &Json) final;
  /// Read a standard Content-Length JSON message from the input file.
  /// \param Json Buffer filled with the raw JSON message on success.
  /// \return Success if a message was read, or failure otherwise.
  LogicalResult readStandardMessage(std::string &Json) final;

private:
  std::FILE *In;
};

/// A transport class that performs the JSON-RPC communication with the LSP
/// client.
class JSONTransport {
public:
  /// Construct a transport from an abstract JSON input and an output stream.
  /// \param In Input used to read incoming JSON-RPC messages.
  /// \param Out Stream used to write outgoing JSON-RPC messages.
  /// \param PrettyOutput Whether to pretty-print outgoing JSON.
  JSONTransport(std::unique_ptr<JSONTransportInput> In, raw_ostream &Out,
                bool PrettyOutput = false)
      : In(std::move(In)), Out(Out), PrettyOutput(PrettyOutput) {}

  /// Construct a transport that reads from a file handle.
  /// \param In File handle used to read incoming JSON-RPC messages.
  /// \param Out Stream used to write outgoing JSON-RPC messages.
  /// \param Style Encoding style used for the file-backed input.
  /// \param PrettyOutput Whether to pretty-print outgoing JSON.
  JSONTransport(std::FILE *In, raw_ostream &Out,
                JSONStreamStyle Style = JSONStreamStyle::Standard,
                bool PrettyOutput = false)
      : In(std::make_unique<JSONTransportInputOverFile>(In, Style)), Out(Out),
        PrettyOutput(PrettyOutput) {}

  /// Send a notification to the LSP client.
  /// \param Method LSP method name for the notification.
  /// \param Params JSON parameters for the notification.
  LLVM_ABI void notify(StringRef Method, llvm::json::Value Params);
  /// Send a request to the LSP client.
  /// \param Method LSP method name for the request.
  /// \param Params JSON parameters for the request.
  /// \param Id JSON-RPC request identifier.
  LLVM_ABI void call(StringRef Method, llvm::json::Value Params,
                     llvm::json::Value Id);
  /// Send a reply to the LSP client for a prior request.
  /// \param Id JSON-RPC identifier of the request being answered.
  /// \param Result Successful result value, or an error.
  LLVM_ABI void reply(llvm::json::Value Id,
                      llvm::Expected<llvm::json::Value> Result);

  /// Start executing the JSON-RPC transport.
  /// \param Handler Handler that dispatches incoming notifications, calls, and
  /// replies.
  /// \return Error::success() on clean exit, or an Error on I/O failure.
  LLVM_ABI llvm::Error run(MessageHandler &Handler);

private:
  /// Dispatches the given incoming json message to the message handler.
  bool handleMessage(llvm::json::Value Msg, MessageHandler &Handler);
  /// Writes the given message to the output stream.
  void sendMessage(llvm::json::Value Msg);

private:
  /// The input to read a message from.
  std::unique_ptr<JSONTransportInput> In;
  SmallVector<char, 0> OutputBuffer;
  /// The output file stream.
  raw_ostream &Out;
  /// If the output JSON should be formatted for easier readability.
  bool PrettyOutput;
};

//===----------------------------------------------------------------------===//
// MessageHandler
//===----------------------------------------------------------------------===//

/// A Callback<T> is a void function that accepts Expected<T>. This is
/// accepted by functions that logically return T.
template <typename T>
using Callback = llvm::unique_function<void(llvm::Expected<T>)>;

/// An OutgoingNotification<T> is a function used for outgoing notifications
/// send to the client.
template <typename T>
using OutgoingNotification = llvm::unique_function<void(const T &)>;

/// An OutgoingRequest<T> is a function used for outgoing requests to send to
/// the client.
template <typename T>
using OutgoingRequest =
    llvm::unique_function<void(const T &, llvm::json::Value Id)>;

/// Callback invoked when an outgoing request receives a response.
///
/// It is passed the original request's ID, as well as the response result.
template <typename T>
using OutgoingRequestCallback =
    std::function<void(llvm::json::Value, llvm::Expected<T>)>;

/// A handler used to process the incoming transport messages.
class MessageHandler {
public:
  /// Construct a message handler bound to the given transport.
  /// \param Transport Transport used to send outgoing messages.
  MessageHandler(JSONTransport &Transport) : Transport(Transport) {}

  /// Handle an incoming notification from the client.
  /// \param Method LSP method name of the notification.
  /// \param Value JSON parameters of the notification.
  /// \return False for an exit notification; true to continue.
  LLVM_ABI bool onNotify(StringRef Method, llvm::json::Value Value);
  /// Handle an incoming request from the client.
  /// \param Method LSP method name of the request.
  /// \param Params JSON parameters of the request.
  /// \param Id JSON-RPC identifier for the request.
  /// \return True to continue the transport loop.
  LLVM_ABI bool onCall(StringRef Method, llvm::json::Value Params,
                       llvm::json::Value Id);
  /// Handle an incoming reply to a prior outgoing request.
  /// \param Id JSON-RPC identifier of the request being answered.
  /// \param Result Successful result value, or an error.
  /// \return True to continue the transport loop.
  LLVM_ABI bool onReply(llvm::json::Value Id,
                        llvm::Expected<llvm::json::Value> Result);

  /// Parse \p Raw as JSON into type \c T.
  /// \param Raw JSON value to parse.
  /// \param PayloadName Name used in parse-error diagnostics.
  /// \param PayloadKind Kind of payload (for example, "request") in diagnostics.
  /// \return The parsed value of type \c T, or an error on failure.
  template <typename T>
  static llvm::Expected<T> parse(const llvm::json::Value &Raw,
                                 StringRef PayloadName, StringRef PayloadKind) {
    T Result;
    llvm::json::Path::Root Root;
    if (!fromJSON(Raw, Result, Root))
      return handleParseError(Raw, PayloadName, PayloadKind, Root);
    return std::move(Result);
  }

  /// Register a handler for an incoming request method.
  /// \param Method LSP method name to handle.
  /// \param ThisPtr Object whose member handler is invoked.
  /// \param Handler Member function invoked with parsed params and a reply
  /// callback.
  template <typename Param, typename Result, typename ThisT>
  void method(llvm::StringLiteral Method, ThisT *ThisPtr,
              void (ThisT::*Handler)(const Param &, Callback<Result>)) {
    MethodHandlers[Method] = [Method, Handler,
                              ThisPtr](llvm::json::Value RawParams,
                                       Callback<llvm::json::Value> Reply) {
      llvm::Expected<Param> Parameter =
          parse<Param>(RawParams, Method, "request");
      if (!Parameter)
        return Reply(Parameter.takeError());
      (ThisPtr->*Handler)(*Parameter, std::move(Reply));
    };
  }

  /// Register a handler for an incoming notification method.
  /// \param Method LSP method name to handle.
  /// \param ThisPtr Object whose member handler is invoked.
  /// \param Handler Member function invoked with the parsed parameters.
  template <typename Param, typename ThisT>
  void notification(llvm::StringLiteral Method, ThisT *ThisPtr,
                    void (ThisT::*Handler)(const Param &)) {
    NotificationHandlers[Method] = [Method, Handler,
                                    ThisPtr](llvm::json::Value RawParams) {
      llvm::Expected<Param> Parameter =
          parse<Param>(RawParams, Method, "notification");
      if (!Parameter) {
        return llvm::consumeError(llvm::handleErrors(
            Parameter.takeError(), [](const LSPError &LspError) {
              Logger::error("JSON parsing error: {0}",
                            LspError.message.c_str());
            }));
      }
      (ThisPtr->*Handler)(*Parameter);
    };
  }

  /// Create an OutgoingNotification object used for the given method.
  /// \param Method LSP method name for the outgoing notification.
  /// \return A callable that sends a notification with the given parameters.
  template <typename T>
  OutgoingNotification<T> outgoingNotification(llvm::StringLiteral Method) {
    return [&, Method](const T &Params) {
      std::lock_guard<std::mutex> TransportLock(TransportOutputMutex);
      Logger::info("--> {0}", Method);
      Transport.notify(Method, llvm::json::Value(Params));
    };
  }

  /// Create an OutgoingRequest that sends requests for the given method.
  ///
  /// When called, sends a request with the given method via the transport.
  /// Should the outgoing request be met with a response, the result JSON is
  /// parsed and the response callback is invoked.
  /// \param Method LSP method name for the outgoing request.
  /// \param Callback Invoked with the request ID and parsed response result.
  /// \return A callable that sends a request and registers \p Callback.
  template <typename Param, typename Result>
  OutgoingRequest<Param>
  outgoingRequest(llvm::StringLiteral Method,
                  OutgoingRequestCallback<Result> Callback) {
    return [&, Method, Callback](const Param &Parameter, llvm::json::Value Id) {
      auto CallbackWrapper = [Method, Callback = std::move(Callback)](
                                 llvm::json::Value Id,
                                 llvm::Expected<llvm::json::Value> Value) {
        if (!Value)
          return Callback(std::move(Id), Value.takeError());

        std::string ResponseName = llvm::formatv("reply:{0}({1})", Method, Id);
        llvm::Expected<Result> ParseResult =
            parse<Result>(*Value, ResponseName, "response");
        if (!ParseResult)
          return Callback(std::move(Id), ParseResult.takeError());

        return Callback(std::move(Id), *ParseResult);
      };

      {
        std::lock_guard<std::mutex> Lock(ResponseHandlersMutex);
        ResponseHandlers.insert(
            {debugString(Id), std::make_pair(Method.str(), CallbackWrapper)});
      }

      std::lock_guard<std::mutex> TransportLock(TransportOutputMutex);
      Logger::info("--> {0}({1})", Method, Id);
      Transport.call(Method, llvm::json::Value(Parameter), Id);
    };
  }

private:
  LLVM_ABI static llvm::Error
  handleParseError(const llvm::json::Value &Raw, StringRef PayloadName,
                   StringRef PayloadKind, const llvm::json::Path::Root &Root);

  template <typename HandlerT>
  using HandlerMap = llvm::StringMap<llvm::unique_function<HandlerT>>;

  HandlerMap<void(llvm::json::Value)> NotificationHandlers;
  HandlerMap<void(llvm::json::Value, Callback<llvm::json::Value>)>
      MethodHandlers;

  /// A pair of (1) the original request's method name, and (2) the callback
  /// function to be invoked for responses.
  using ResponseHandlerTy =
      std::pair<std::string, OutgoingRequestCallback<llvm::json::Value>>;
  /// A mapping from request/response ID to response handler.
  llvm::StringMap<ResponseHandlerTy> ResponseHandlers;
  /// Mutex to guard insertion into the response handler map.
  std::mutex ResponseHandlersMutex;

  JSONTransport &Transport;

  /// Mutex to guard sending output messages to the transport.
  std::mutex TransportOutputMutex;
};

} // namespace lsp
} // namespace llvm

#endif
