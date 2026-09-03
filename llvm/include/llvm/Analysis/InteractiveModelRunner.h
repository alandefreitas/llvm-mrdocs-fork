//===- InteractiveModelRunner.h ---- "gym" ML model runner  -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_ANALYSIS_INTERACTIVEMODELRUNNER_H
#define LLVM_ANALYSIS_INTERACTIVEMODELRUNNER_H

#include "llvm/Analysis/MLModelRunner.h"
#include "llvm/Analysis/TensorSpec.h"
#include "llvm/Analysis/Utils/TrainingLogger.h"
#include "llvm/Support/Compiler.h"
#include <system_error>

namespace llvm {

/// MLModelRunner that requests advice from an external agent over two files.
///
/// It uses 2 files - ideally named pipes - one to send data to that agent, and
/// one to receive advice.
/// The data exchange uses the training logger (Utils/TrainingLogger.h) format.
/// Specifically, the compiler will send the log header, set the context, and
/// send observations; the host is expected to reply with a tensor value after
/// each observation as a binary buffer that's conforming to the shape of the
/// advice. Interleaved, the data closely resembles the training log for a
/// log where we don't capture the reward signal.
///
/// Note that the correctness of the received data is the responsibility of the
/// host. In particular, if insufficient data were sent, the compiler will block
/// when waiting for an advice.
///
/// Note that the host can either open the pipes RW, or open first the pipe to
/// the compiler - i.e. the "Inbound" - and then the "Outbound", to avoid
/// deadlock. This is because the compiler first tries to open the inbound
/// (which will hang until there's a writer on the other end).
class LLVM_ABI InteractiveModelRunner : public MLModelRunner {
public:
  /// Construct an interactive runner that exchanges tensors with a host agent.
  /// @param Ctx LLVM context used for diagnostics.
  /// @param Inputs Tensor specifications describing the features to send.
  /// @param Advice Tensor specification describing the advice to receive.
  /// @param OutboundName Path of the file or pipe used to send data to the host.
  /// @param InboundName Path of the file or pipe used to receive advice.
  InteractiveModelRunner(LLVMContext &Ctx,
                         const std::vector<TensorSpec> &Inputs,
                         const TensorSpec &Advice, StringRef OutboundName,
                         StringRef InboundName);

  /// Return true if \p R is an InteractiveModelRunner.
  /// @param R Model runner to test.
  /// @return True if \p R is an InteractiveModelRunner.
  static bool classof(const MLModelRunner *R) {
    return R->getKind() == MLModelRunner::Kind::Interactive;
  }
  /// Switch the logging context to \p Name and flush pending output.
  /// @param Name New context name to switch to.
  void switchContext(StringRef Name) override {
    Log->switchContext(Name);
    Log->flush();
  }

  /// Destroy this InteractiveModelRunner.
  ~InteractiveModelRunner() override;

private:
  void *evaluateUntyped() override;
  // This must be declared before InEC if we want to initialize it in the
  // ctor initializer list.
  int Inbound = -1;
  const std::vector<TensorSpec> InputSpecs;
  const TensorSpec OutputSpec;
  std::error_code OutEC;
  std::error_code InEC;
  std::vector<char> OutputBuffer;
  std::unique_ptr<Logger> Log;
};
} // namespace llvm
#endif // LLVM_ANALYSIS_INTERACTIVEMODELRUNNER_H
