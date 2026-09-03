//===--------------- OrcError.h - Orc Error Types ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Define an error category, error codes, and helper utilities for Orc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_ORCERROR_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_ORCERROR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <system_error>

namespace llvm {
namespace orc {

/// Error codes used by ORC and its RPC layers.
enum class OrcErrorCode : int {
  /// Catch-all for unrecognized ORC failures.
  UnknownORCError = 1,
  /// A symbol was defined more than once.
  DuplicateDefinition,
  /// A requested JIT symbol could not be found.
  JITSymbolNotFound,
  /// The remote allocator handle does not exist.
  RemoteAllocatorDoesNotExist,
  /// The remote allocator id is already in use.
  RemoteAllocatorIdAlreadyInUse,
  /// Remote mprotect referenced an unrecognized address.
  RemoteMProtectAddrUnrecognized,
  /// The remote indirect-stubs owner does not exist.
  RemoteIndirectStubsOwnerDoesNotExist,
  /// The remote indirect-stubs owner id is already in use.
  RemoteIndirectStubsOwnerIdAlreadyInUse,
  /// The RPC connection was closed.
  RPCConnectionClosed,
  /// An RPC function could not be negotiated.
  RPCCouldNotNegotiateFunction,
  /// An RPC response was abandoned.
  RPCResponseAbandoned,
  /// An unexpected RPC call was received.
  UnexpectedRPCCall,
  /// An unexpected RPC response was received.
  UnexpectedRPCResponse,
  /// The remote side returned an unrecognized error code.
  UnknownErrorCodeFromRemote,
  /// The resource handle is unknown.
  UnknownResourceHandle,
  /// Expected symbol definitions were missing from a module.
  MissingSymbolDefinitions,
  /// A module defined symbols that were not expected.
  UnexpectedSymbolDefinitions,
};

/// Convert an ORC error code to a \c std::error_code.
/// @param ErrCode ORC-specific error code to convert.
/// @return A \c std::error_code in the ORC error category for \p ErrCode.
LLVM_ABI std::error_code orcError(OrcErrorCode ErrCode);

/// Error raised when a symbol is defined more than once.
class LLVM_ABI DuplicateDefinition : public ErrorInfo<DuplicateDefinition> {
public:
  /// RTTI identifier used by ErrorInfo::classID.
  static char ID;

  /// Construct a duplicate-definition error for \p SymbolName.
  /// @param SymbolName Name of the symbol that was defined twice.
  /// @param Context Optional context string describing where the duplicate
  ///        occurred.
  DuplicateDefinition(std::string SymbolName,
                      std::optional<std::string> Context = {});
  /// Convert this error to a \c std::error_code.
  /// @return A \c std::error_code for \c OrcErrorCode::DuplicateDefinition.
  std::error_code convertToErrorCode() const override;
  /// Write a description of this error to \p OS.
  /// @param OS Stream that receives the logged message.
  void log(raw_ostream &OS) const override;
  /// Return the name of the duplicated symbol.
  /// @return Name of the symbol that was defined more than once.
  const std::string &getSymbolName() const;
  /// Return the optional context string for this error.
  /// @return Optional context describing where the duplicate occurred.
  const std::optional<std::string> &getContext() const;

private:
  std::string SymbolName;
  std::optional<std::string> Context;
};

/// Error raised when a requested JIT symbol cannot be found.
class LLVM_ABI JITSymbolNotFound : public ErrorInfo<JITSymbolNotFound> {
public:
  /// RTTI identifier used by ErrorInfo::classID.
  static char ID;

  /// Construct a not-found error for \p SymbolName.
  /// @param SymbolName Name of the missing symbol.
  JITSymbolNotFound(std::string SymbolName);
  /// Convert this error to a \c std::error_code.
  /// @return A \c std::error_code for \c OrcErrorCode::JITSymbolNotFound.
  std::error_code convertToErrorCode() const override;
  /// Write a description of this error to \p OS.
  /// @param OS Stream that receives the logged message.
  void log(raw_ostream &OS) const override;
  /// Return the name of the missing symbol.
  /// @return Name of the symbol that could not be found.
  const std::string &getSymbolName() const;
private:
  std::string SymbolName;
};

} // End namespace orc.
} // End namespace llvm.

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_ORCERROR_H
