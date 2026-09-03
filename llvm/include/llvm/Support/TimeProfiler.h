//===- llvm/Support/TimeProfiler.h - Hierarchical Time Profiler -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides lightweight and dependency-free machinery to trace execution
// time around arbitrary code. Two API flavors are available.
//
// The primary API uses a RAII object to trigger tracing:
//
// \code
//   {
//     TimeTraceScope scope("my_event_name");
//     ...my code...
//   }
// \endcode
//
// If the code to be profiled does not have a natural lexical scope then
// it is also possible to start and end events with respect to an implicit
// per-thread stack of profiling entries:
//
// \code
//   timeTraceProfilerBegin("my_event_name");
//   ...my code...
//   timeTraceProfilerEnd();  // must be called on all control flow paths
// \endcode
//
// Time profiling entries can be given an arbitrary name and, optionally,
// an arbitrary 'detail' string. The resulting trace will include 'Total'
// entries summing the time spent for each name. Thus, it's best to choose
// names to be fairly generic, and rely on the detail field to capture
// everything else of interest.
//
// To avoid lifetime issues name and detail strings are copied into the event
// entries at their time of creation. Care should be taken to make string
// construction cheap to prevent 'Heisenperf' effects. In particular, the
// 'detail' argument may be a string-returning closure:
//
// \code
//   int n;
//   {
//     TimeTraceScope scope("my_event_name",
//                          [n]() { return (Twine("x=") + Twine(n)).str(); });
//     ...my code...
//   }
// \endcode
// The closure will not be called if tracing is disabled. Otherwise, the
// resulting string will be directly moved into the entry.
//
// The main process should begin with a timeTraceProfilerInitialize, and
// finish with timeTraceProfileWrite and timeTraceProfilerCleanup calls.
// Each new thread should begin with a timeTraceProfilerInitialize, and
// finish with a timeTraceProfilerFinishThread call.
//
// Timestamps come from std::chrono::stable_clock. Note that threads need
// not see the same time from that clock, and the resolution may not be
// the best available.
//
// Currently, there are a number of compatible viewers:
//  - chrome://tracing is the original chromium trace viewer.
//  - http://ui.perfetto.dev is the replacement for the above, under active
//    development by Google as part of the 'Perfetto' project.
//  - https://www.speedscope.app/ has also been reported as an option.
//
// Future work:
//  - Support akin to LLVM_DEBUG for runtime enable/disable of named tracing
//    families for non-debug builds which wish to support optional tracing.
//  - Evaluate the detail closures at profile write time to avoid
//    stringification costs interfering with tracing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TIMEPROFILER_H
#define LLVM_SUPPORT_TIMEPROFILER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {

class raw_pwrite_stream;

/// Chrome Trace Event phase kinds recorded by the time profiler.
enum class TimeTraceEventType {
  /// Complete event with a duration, marked by the "X" phase type.
  CompleteEvent,
  /// Instant event at a single time point, marked by the "i" phase type.
  ///
  /// Instant events have no duration; the End field is ignored for them.
  InstantEvent,
  /// Async event for asynchronous operations, marked by "b" and "e" phases.
  AsyncEvent
};

/// Optional detail, file, and line attached to a time-trace event.
struct TimeTraceMetadata {
  /// Extra human-readable description of the event.
  std::string Detail;
  /// Source file associated with the event, if any.
  std::string File;
  /// Source line associated with the event, or 0 if none.
  int Line = 0;

  /// Return true if this metadata has no detail and no file name.
  ///
  /// \return True if both Detail and File are empty.
  bool isEmpty() const { return Detail.empty() && File.empty(); }
};

/// Per-thread time-trace profiler that records events for later export.
struct TimeTraceProfiler;
/// Return the time-trace profiler for the current thread, or null if none.
///
/// \return The current thread's profiler, or nullptr if none is initialized.
LLVM_ABI TimeTraceProfiler *getTimeTraceProfilerInstance();

/// Return true if the current thread's profiler is capturing verbose details.
///
/// \return True when the current thread's profiler is in verbose mode.
LLVM_ABI bool isTimeTraceVerbose();

/// An open or completed time section recorded by the profiler.
struct TimeTraceProfilerEntry;

/// Initialize the time trace profiler.
///
/// This sets up the global \p TimeTraceProfilerInstance
/// variable to be the profiler instance.
///
/// \param TimeTraceGranularity Minimum event duration in microseconds to keep.
/// \param ProcName Process name stored in the trace metadata.
/// \param TimeTraceVerbose If true, capture extra details such as filenames.
LLVM_ABI void timeTraceProfilerInitialize(unsigned TimeTraceGranularity,
                                          StringRef ProcName,
                                          bool TimeTraceVerbose = false);

/// Cleanup the time trace profiler, if it was initialized.
LLVM_ABI void timeTraceProfilerCleanup();

/// Finish a time trace profiler running on a worker thread.
LLVM_ABI void timeTraceProfilerFinishThread();

/// Is the time trace profiler enabled, i.e. initialized?
///
/// \return True if a profiler instance is initialized for this thread.
inline bool timeTraceProfilerEnabled() {
  return getTimeTraceProfilerInstance() != nullptr;
}

/// Write profiling data to an output stream.
///
/// Data produced is JSON, in Chrome "Trace Event" format, see
/// https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview
///
/// \param OS Stream that receives the Chrome Trace Event JSON.
LLVM_ABI void timeTraceProfilerWrite(raw_pwrite_stream &OS);

/// Write profiling data to a file.
///
/// The function will write to \p PreferredFileName if provided, if not
/// then will write to \p FallbackFileName appending .time-trace.json.
/// Returns a StringError indicating a failure if the function is
/// unable to open the file for writing.
///
/// \param PreferredFileName Output path to use when non-empty.
/// \param FallbackFileName Path used when \p PreferredFileName is empty.
/// \return Success, or a StringError if the output file could not be opened.
LLVM_ABI Error timeTraceProfilerWrite(StringRef PreferredFileName,
                                      StringRef FallbackFileName);

/// Manually begin a time section with the given name and detail.
///
/// Profiler copies the string data, so the pointers can be given into
/// temporaries. Time sections can be hierarchical; every Begin must have a
/// matching End pair but they can nest.
///
/// \param Name Event name shown in the trace.
/// \param Detail Extra description copied into the event.
/// \return Profiler entry for the begun section, or nullptr if tracing is off.
LLVM_ABI TimeTraceProfilerEntry *timeTraceProfilerBegin(StringRef Name,
                                                        StringRef Detail);
/// Manually begin a time section with a lazily computed detail string.
///
/// \param Name Event name shown in the trace.
/// \param Detail Closure that produces the detail; not called if tracing is
///        disabled.
/// \return Profiler entry for the begun section, or nullptr if tracing is off.
LLVM_ABI TimeTraceProfilerEntry *
timeTraceProfilerBegin(StringRef Name,
                       llvm::function_ref<std::string()> Detail);

/// Manually begin a time section with lazily computed metadata.
///
/// \param Name Event name shown in the trace.
/// \param MetaData Closure that produces file, line, and detail metadata;
///        not called if tracing is disabled.
/// \return Profiler entry for the begun section, or nullptr if tracing is off.
LLVM_ABI TimeTraceProfilerEntry *
timeTraceProfilerBegin(StringRef Name,
                       llvm::function_ref<TimeTraceMetadata()> MetaData);

/// Manually begin an asynchronous time section with the given name and detail.
///
/// This starts Async Events having \p Name as a category which is shown
/// separately from other traces. See
/// https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview#heading=h.jh64i9l3vwa1
/// for more details.
///
/// \param Name Event name used as the async category.
/// \param Detail Extra description copied into the event.
/// \return Profiler entry for the begun async section, or nullptr if tracing
///         is off.
LLVM_ABI TimeTraceProfilerEntry *timeTraceAsyncProfilerBegin(StringRef Name,
                                                             StringRef Detail);

/// Record an instant event with the given name and lazily computed detail.
///
/// \param Name Event name shown in the trace.
/// \param Detail Closure that produces the detail; not called if tracing is
///        disabled.
LLVM_ABI void
timeTraceAddInstantEvent(StringRef Name,
                         llvm::function_ref<std::string()> Detail);

/// Manually end the last time section.
LLVM_ABI void timeTraceProfilerEnd();
/// Manually end the time section identified by \p E.
///
/// \param E Entry returned by a matching begin call.
LLVM_ABI void timeTraceProfilerEnd(TimeTraceProfilerEntry *E);

/// RAII helper that begins a time-trace section and ends it on destruction.
///
/// The TimeTraceScope is a helper class to call the begin and end functions
/// of the time trace profiler. When the object is constructed, it begins
/// the section; and when it is destroyed, it stops it. If the time profiler
/// is not initialized, the overhead is a single branch.
class TimeTraceScope {
public:
  /// TimeTraceScope cannot be default-constructed.
  TimeTraceScope() = delete;
  /// TimeTraceScope is not copy-constructible.
  ///
  /// \param Other Unused; copy construction is deleted.
  TimeTraceScope(const TimeTraceScope &Other) = delete;
  /// TimeTraceScope is not copy-assignable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  TimeTraceScope &operator=(const TimeTraceScope &Other) = delete;
  /// TimeTraceScope is not move-constructible.
  ///
  /// \param Other Unused; move construction is deleted.
  TimeTraceScope(TimeTraceScope &&Other) = delete;
  /// TimeTraceScope is not move-assignable.
  ///
  /// \param Other Unused; move assignment is deleted.
  TimeTraceScope &operator=(TimeTraceScope &&Other) = delete;

  /// Begin a time-trace section named \p Name with empty detail.
  ///
  /// \param Name Event name shown in the trace.
  TimeTraceScope(StringRef Name)
      : Entry(timeTraceProfilerBegin(Name, StringRef())) {}
  /// Begin a time-trace section named \p Name with the given detail.
  ///
  /// \param Name Event name shown in the trace.
  /// \param Detail Extra description copied into the event.
  TimeTraceScope(StringRef Name, StringRef Detail)
      : Entry(timeTraceProfilerBegin(Name, Detail)) {}
  /// Begin a time-trace section named \p Name with a lazily computed detail.
  ///
  /// \param Name Event name shown in the trace.
  /// \param Detail Closure that produces the detail; not called if tracing is
  ///        disabled.
  TimeTraceScope(StringRef Name, llvm::function_ref<std::string()> Detail)
      : Entry(timeTraceProfilerBegin(Name, Detail)) {}
  /// Begin a time-trace section named \p Name with lazily computed metadata.
  ///
  /// \param Name Event name shown in the trace.
  /// \param Metadata Closure that produces file, line, and detail metadata;
  ///        not called if tracing is disabled.
  TimeTraceScope(StringRef Name,
                 llvm::function_ref<TimeTraceMetadata()> Metadata)
      : Entry(timeTraceProfilerBegin(Name, Metadata)) {}
  /// End the time-trace section begun by this scope, if any.
  ~TimeTraceScope() {
    if (Entry)
      timeTraceProfilerEnd(Entry);
  }

private:
  TimeTraceProfilerEntry *Entry = nullptr;
};

} // end namespace llvm

#endif
