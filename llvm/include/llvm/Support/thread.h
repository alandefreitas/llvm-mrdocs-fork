//===-- llvm/Support/thread.h - Wrapper for <thread> ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header is a wrapper for <thread> that works around problems with the
// MSVC headers when exceptions are disabled. It also provides llvm::thread,
// which is either a typedef of std::thread or a replacement that calls the
// function synchronously depending on the value of LLVM_ENABLE_THREADS.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_THREAD_H
#define LLVM_SUPPORT_THREAD_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"
#include <optional>
#include <tuple>
#include <utility>

#ifdef _WIN32
typedef unsigned long DWORD;
typedef void *PVOID;
typedef PVOID HANDLE;
#endif

#if LLVM_ENABLE_THREADS

#include <thread>

namespace llvm {

#if defined(LLVM_ON_UNIX) || defined(_WIN32)

/// LLVM thread following std::thread interface with added constructor to
/// specify stack size.
class thread {
  template <typename CalleeTuple> static void GenericThreadProxy(void *Ptr) {
    std::unique_ptr<CalleeTuple> Callee(static_cast<CalleeTuple *>(Ptr));
    std::apply(
        [](auto &&F, auto &&...Args) {
          std::forward<decltype(F)>(F)(std::forward<decltype(Args)>(Args)...);
        },
        *Callee);
  }

public:
#ifdef LLVM_ON_UNIX
  /// Native OS handle type for the underlying thread.
  using native_handle_type = pthread_t;
#if defined(__LLVM_LIBC__)
  /// Platform-specific thread identifier type.
  using id = pthread_id_np_t;
#elif defined(__MVS__)
  /// Platform-specific thread identifier type.
  using id = unsigned long long;
#else
  /// Platform-specific thread identifier type.
  using id = pthread_t;
#endif
  /// Function pointer type for the platform thread entry routine.
  using start_routine_type = void *(*)(void *);

  /// Platform thread entry that runs the callable in \p Ptr.
  ///
  /// \param Ptr Owning pointer to a CalleeTuple holding the callable and args.
  /// \return Always \c nullptr after the callable finishes.
  template <typename CalleeTuple> static void *ThreadProxy(void *Ptr) {
    GenericThreadProxy<CalleeTuple>(Ptr);
    return nullptr;
  }
#elif _WIN32
  /// Native OS handle type for the underlying thread.
  using native_handle_type = HANDLE;
  /// Platform-specific thread identifier type.
  using id = DWORD;
  /// Function pointer type for the platform thread entry routine.
  using start_routine_type = unsigned(__stdcall *)(void *);

  /// Platform thread entry that runs the callable in \p Ptr.
  ///
  /// \param Ptr Owning pointer to a CalleeTuple holding the callable and args.
  /// \return Always \c 0 after the callable finishes.
  template <typename CalleeTuple>
  static unsigned __stdcall ThreadProxy(void *Ptr) {
    GenericThreadProxy<CalleeTuple>(Ptr);
    return 0;
  }
#endif

  /// Default stack size in bytes for new threads; nullopt means platform default.
  LLVM_ABI static const std::optional<unsigned> DefaultStackSize;

  /// Constructs a thread object that does not represent a thread of execution.
  thread() : Thread(native_handle_type()) {}
  /// Move-constructs a thread, transferring ownership from \p Other.
  ///
  /// \param Other Thread to move from; left non-joinable.
  thread(thread &&Other) noexcept
      : Thread(std::exchange(Other.Thread, native_handle_type())) {}

  /// Starts a new thread executing \p f with \p args using DefaultStackSize.
  ///
  /// \param f Callable invoked on the new thread.
  /// \param args Arguments forwarded to \p f.
  template <class Function, class... Args>
  explicit thread(Function &&f, Args &&...args)
      : thread(DefaultStackSize, f, args...) {}

  /// Starts a new thread executing \p f with \p args and optional stack size.
  ///
  /// \param StackSizeInBytes Requested stack size in bytes, or nullopt for
  /// default.
  /// \param f Callable invoked on the new thread.
  /// \param args Arguments forwarded to \p f.
  template <class Function, class... Args>
  explicit thread(std::optional<unsigned> StackSizeInBytes, Function &&f,
                  Args &&...args);
  /// Copy construction is deleted; threads are move-only.
  ///
  /// \param Other Unused; copy construction is not allowed.
  thread(const thread &Other) = delete;

  /// Destroys the thread handle; calls std::terminate if still joinable.
  ~thread() {
    if (joinable())
      std::terminate();
  }

  /// Move-assigns from \p Other; terminates if this thread is still joinable.
  ///
  /// \param Other Thread to move from; left non-joinable.
  /// \return A reference to this thread after the move.
  thread &operator=(thread &&Other) noexcept {
    if (joinable())
      std::terminate();
    Thread = std::exchange(Other.Thread, native_handle_type());
    return *this;
  }

  /// Returns true if this object represents an active, joinable thread.
  ///
  /// \return \c true if this object represents a joinable thread.
  bool joinable() const noexcept { return get_id() != 0; }

  /// Returns the id of the thread represented by this object.
  ///
  /// \return The platform-specific id of this thread.
  inline id get_id() const noexcept;

  /// Returns the native handle for this thread.
  ///
  /// \return The native OS handle for this thread.
  native_handle_type native_handle() const noexcept { return Thread; }

  /// Returns an estimate of concurrent threads supported by the hardware.
  ///
  /// \return An estimate of hardware thread concurrency.
  static unsigned hardware_concurrency() {
    return std::thread::hardware_concurrency();
  };

  /// Blocks until the thread of execution finishes.
  inline void join();
  /// Separates the thread of execution from this object.
  inline void detach();

  /// Exchanges the underlying thread handles of this object and \p Other.
  ///
  /// \param Other Thread to swap with.
  void swap(llvm::thread &Other) noexcept { std::swap(Thread, Other.Thread); }

private:
  native_handle_type Thread;
};

/// Creates a native thread running \p ThreadFunc(\p Arg) with optional stack
/// size.
///
/// \param ThreadFunc Entry routine invoked on the new thread.
/// \param Arg Argument passed to \p ThreadFunc.
/// \param StackSizeInBytes Requested stack size in bytes, or nullopt for
/// default.
/// \return The native handle of the newly created thread.
LLVM_ABI thread::native_handle_type
llvm_execute_on_thread_impl(thread::start_routine_type ThreadFunc, void *Arg,
                            std::optional<unsigned> StackSizeInBytes);
/// Blocks until the native thread \p Thread completes.
///
/// \param Thread Native handle of the thread to join.
LLVM_ABI void llvm_thread_join_impl(thread::native_handle_type Thread);
/// Detaches the native thread \p Thread so it runs independently.
///
/// \param Thread Native handle of the thread to detach.
LLVM_ABI void llvm_thread_detach_impl(thread::native_handle_type Thread);
/// Returns the thread id corresponding to native handle \p Thread.
///
/// \param Thread Native handle whose id is requested.
/// \return The platform-specific id for \p Thread.
LLVM_ABI thread::id llvm_thread_get_id_impl(thread::native_handle_type Thread);
/// Returns the id of the calling thread.
///
/// \return The platform-specific id of the current thread.
LLVM_ABI thread::id llvm_thread_get_current_id_impl();

template <class Function, class... Args>
thread::thread(std::optional<unsigned> StackSizeInBytes, Function &&f,
               Args &&...args) {
  using CalleeTuple = std::tuple<std::decay_t<Function>, std::decay_t<Args>...>;
  std::unique_ptr<CalleeTuple> Callee(
      new CalleeTuple(std::forward<Function>(f), std::forward<Args>(args)...));

  Thread = llvm_execute_on_thread_impl(ThreadProxy<CalleeTuple>, Callee.get(),
                                       StackSizeInBytes);
  if (joinable())
    Callee.release();
}

thread::id thread::get_id() const noexcept {
  return llvm_thread_get_id_impl(Thread);
}

void thread::join() {
  llvm_thread_join_impl(Thread);
  Thread = native_handle_type();
}

void thread::detach() {
  llvm_thread_detach_impl(Thread);
  Thread = native_handle_type();
}

/// Utilities for querying the calling thread, analogous to std::this_thread.
namespace this_thread {
/// Returns the id of the calling thread.
///
/// \return The platform-specific id of the current thread.
inline thread::id get_id() { return llvm_thread_get_current_id_impl(); }
} // namespace this_thread

#else // !LLVM_ON_UNIX && !_WIN32

/// std::thread backed implementation of llvm::thread interface that ignores the
/// stack size request.
class thread {
public:
  using native_handle_type = std::thread::native_handle_type;
  using id = std::thread::id;

  thread() : Thread(std::thread()) {}
  thread(thread &&Other) noexcept
      : Thread(std::exchange(Other.Thread, std::thread())) {}

  template <class Function, class... Args>
  explicit thread(std::optional<unsigned> StackSizeInBytes, Function &&f,
                  Args &&...args)
      : Thread(std::forward<Function>(f), std::forward<Args>(args)...) {}

  template <class Function, class... Args>
  explicit thread(Function &&f, Args &&...args) : Thread(f, args...) {}

  thread(const thread &) = delete;

  ~thread() {}

  thread &operator=(thread &&Other) noexcept {
    Thread = std::exchange(Other.Thread, std::thread());
    return *this;
  }

  bool joinable() const noexcept { return Thread.joinable(); }

  id get_id() const noexcept { return Thread.get_id(); }

  native_handle_type native_handle() noexcept { return Thread.native_handle(); }

  static unsigned hardware_concurrency() {
    return std::thread::hardware_concurrency();
  };

  inline void join() { Thread.join(); }
  inline void detach() { Thread.detach(); }

  void swap(llvm::thread &Other) noexcept { std::swap(Thread, Other.Thread); }

private:
  std::thread Thread;
};

namespace this_thread {
inline thread::id get_id() { return std::this_thread::get_id(); }
} // namespace this_thread

#endif // LLVM_ON_UNIX || _WIN32

} // namespace llvm

#else // !LLVM_ENABLE_THREADS

#include "llvm/Support/ErrorHandling.h"
#include <utility>

namespace llvm {

struct thread {
  thread() {}
  thread(thread &&other) {}
  template <class Function, class... Args>
  explicit thread(std::optional<unsigned> StackSizeInBytes, Function &&f,
                  Args &&...args) {
    f(std::forward<Args>(args)...);
  }
  template <class Function, class... Args>
  explicit thread(Function &&f, Args &&...args) {
    f(std::forward<Args>(args)...);
  }
  thread(const thread &) = delete;

  void detach() {
    report_fatal_error("Detaching from a thread does not make sense with no "
                       "threading support");
  }
  void join() {}
  static unsigned hardware_concurrency() { return 1; };
};

} // namespace llvm

#endif // LLVM_ENABLE_THREADS

#endif // LLVM_SUPPORT_THREAD_H
