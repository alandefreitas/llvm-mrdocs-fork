//===--------- TaskDispatch.h - ORC task dispatch utils ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Task and TaskDispatch classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TASKDISPATCH_H
#define LLVM_EXECUTIONENGINE_ORC_TASKDISPATCH_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ExtensibleRTTI.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <string>

#if LLVM_ENABLE_THREADS
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#endif

namespace llvm {
namespace orc {

/// Represents an abstract task for ORC to run.
class LLVM_ABI Task : public RTTIExtends<Task, RTTIRoot> {
public:
  /// RTTI identifier for this Task type.
  static char ID;

  /// Destroy this Task.
  ~Task() override = default;

  /// Description of the task to be performed. Used for logging.
  /// \param OS Stream to write the description to.
  virtual void printDescription(raw_ostream &OS) = 0;

  /// Run the task.
  virtual void run() = 0;

private:
  void anchor() override;
};

/// Base class for generic tasks.
class GenericNamedTask : public RTTIExtends<GenericNamedTask, Task> {
public:
  /// RTTI identifier for this GenericNamedTask type.
  LLVM_ABI static char ID;
  /// Default description used when none is provided.
  LLVM_ABI static const char *DefaultDescription;
};

/// Generic task implementation.
template <typename FnT> class GenericNamedTaskImpl : public GenericNamedTask {
public:
  /// Construct a generic named task that owns its description string.
  /// \param Fn Callable to invoke when the task runs.
  /// \param InDescBuffer Description string buffer to own and print.
  GenericNamedTaskImpl(FnT &&Fn, std::string InDescBuffer)
      : Fn(std::forward<FnT>(Fn)), DescBuffer(std::move(InDescBuffer)),
        Desc(DescBuffer.c_str()) {}
  /// Construct a generic named task with a non-owning description.
  /// \param Fn Callable to invoke when the task runs.
  /// \param Desc Null-terminated description string; must not be null.
  GenericNamedTaskImpl(FnT &&Fn, const char *Desc)
      : Fn(std::forward<FnT>(Fn)), Desc(Desc) {
    assert(Desc && "Description cannot be null");
  }
  /// Print this task's description to \p OS.
  /// \param OS Stream to write the description to.
  void printDescription(raw_ostream &OS) override { OS << Desc; }
  /// Run the wrapped callable.
  void run() override { Fn(); }

private:
  FnT Fn;
  std::string DescBuffer;
  const char *Desc;
};

/// Create a generic named task from a std::string description.
/// \param Fn Callable to invoke when the task runs.
/// \param Desc Description string to own and associate with the task.
/// \return A unique_ptr owning the created GenericNamedTask.
template <typename FnT>
std::unique_ptr<GenericNamedTask> makeGenericNamedTask(FnT &&Fn,
                                                       std::string Desc) {
  return std::make_unique<GenericNamedTaskImpl<FnT>>(std::forward<FnT>(Fn),
                                                     std::move(Desc));
}

/// Create a generic named task from a const char * description.
/// \param Fn Callable to invoke when the task runs.
/// \param Desc Description string, or nullptr to use DefaultDescription.
/// \return A unique_ptr owning the created GenericNamedTask.
template <typename FnT>
std::unique_ptr<GenericNamedTask>
makeGenericNamedTask(FnT &&Fn, const char *Desc = nullptr) {
  if (!Desc)
    Desc = GenericNamedTask::DefaultDescription;
  return std::make_unique<GenericNamedTaskImpl<FnT>>(std::forward<FnT>(Fn),
                                                     Desc);
}

/// IdleTask can be used as the basis for low-priority tasks, e.g. speculative
/// lookup.
class LLVM_ABI IdleTask : public RTTIExtends<IdleTask, Task> {
public:
  /// RTTI identifier for this IdleTask type.
  static char ID;

private:
  void anchor() override;
};

/// Abstract base for classes that dispatch ORC Tasks.
class LLVM_ABI TaskDispatcher {
public:
  /// Destroy this TaskDispatcher.
  virtual ~TaskDispatcher();

  /// Run the given task.
  /// \param T Task to dispatch.
  virtual void dispatch(std::unique_ptr<Task> T) = 0;

  /// Called by ExecutionSession. Waits until all tasks have completed.
  virtual void shutdown() = 0;
};

/// Runs all tasks on the current thread.
class LLVM_ABI InPlaceTaskDispatcher : public TaskDispatcher {
public:
  /// Run the given task immediately on the calling thread.
  /// \param T Task to run.
  void dispatch(std::unique_ptr<Task> T) override;
  /// Shut down this dispatcher (no-op for in-place dispatch).
  void shutdown() override;
};

#if LLVM_ENABLE_THREADS

/// Dispatches tasks onto a dynamically sized pool of worker threads.
class LLVM_ABI DynamicThreadPoolTaskDispatcher : public TaskDispatcher {
public:
  /// Construct a dynamic thread-pool task dispatcher.
  /// \param MaxMaterializationThreads Optional cap on concurrent
  ///        materialization threads; std::nullopt means uncapped.
  DynamicThreadPoolTaskDispatcher(
      std::optional<size_t> MaxMaterializationThreads)
      : MaxMaterializationThreads(MaxMaterializationThreads) {}

  /// Dispatch the given task to a worker thread.
  /// \param T Task to dispatch.
  void dispatch(std::unique_ptr<Task> T) override;
  /// Wait until all outstanding tasks complete, then shut down the pool.
  void shutdown() override;
private:
  bool canRunMaterializationTaskNow();
  bool canRunIdleTaskNow();

  std::mutex DispatchMutex;
  bool Shutdown = false;
  size_t Outstanding = 0;
  std::condition_variable OutstandingCV;

  std::optional<size_t> MaxMaterializationThreads;
  size_t NumMaterializationThreads = 0;
  std::deque<std::unique_ptr<Task>> MaterializationTaskQueue;
  std::deque<std::unique_ptr<Task>> IdleTaskQueue;
};

#endif // LLVM_ENABLE_THREADS

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_TASKDISPATCH_H
