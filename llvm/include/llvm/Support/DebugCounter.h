//===- llvm/Support/DebugCounter.h - Debug counter support ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides an implementation of debug counters.  Debug
/// counters are a tool that let you narrow down a miscompilation to a specific
/// thing happening.
///
/// To give a use case: Imagine you have a file, very large, and you
/// are trying to understand the minimal transformation that breaks it.
/// llvm-reduce and bisection is often helpful here in narrowing it down to a
/// specific pass, but it's still a very large file, and a very complicated pass
/// to try to debug.  That is where debug counting steps in.  You can instrument
/// the pass with a debug counter before it does a certain thing, and depending
/// on the counts, it will either execute that thing or not.  The debug counter
/// itself consists of a list of chunks (inclusive numeric intervals).
/// `shouldExecute` returns true iff the list is empty or the current count is
/// in one of the chunks.
///
/// Note that a counter set to a negative number will always execute. For a
/// concrete example, during predicateinfo creation, the renaming pass replaces
/// each use with a renamed use.
////
/// If I use DEBUG_COUNTER to create a counter called "predicateinfo", and
/// variable name RenameCounter, and then instrument this renaming with a debug
/// counter, like so:
///
/// if (!DebugCounter::shouldExecute(RenameCounter)
/// <continue or return or whatever not executing looks like>
///
/// Now I can, from the command line, make it rename or not rename certain uses
/// by setting the chunk list.
/// So for example
/// bin/opt -debug-counter=predicateinfo=47
/// will skip renaming the first 47 uses, then rename one, then skip the rest.
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DEBUGCOUNTER_H
#define LLVM_SUPPORT_DEBUGCOUNTER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/IntegerInclusiveInterval.h"
#include <string>

namespace llvm {

class raw_ostream;

/// Manages named debug counters that enable or skip instrumented events.
///
/// Register a counter with \c DEBUG_COUNTER, then call \c shouldExecute
/// before the event. Chunk lists from \c -debug-counter select which
/// counts execute.
class DebugCounter {
public:
  /// Struct to store counter info.
  class CounterInfo {
    friend class DebugCounter;

    /// Whether counting should be enabled, either due to -debug-counter or
    /// -print-debug-counter.
    bool Active = false;
    /// Whether chunks for the counter are set (differs from Active in that
    /// -print-debug-counter uses Active=true, IsSet=false).
    bool IsSet = false;

    int64_t Count = 0;
    uint64_t CurrChunkIdx = 0;
    StringRef Name;
    StringRef Desc;
    IntegerInclusiveIntervalUtils::IntervalList Chunks;

  public:
    /// Construct a counter named \p Name with description \p Desc.
    ///
    /// Registers the counter with the singleton \c DebugCounter instance.
    ///
    /// \param Name Command-line name of the counter.
    /// \param Desc Human-readable description shown in help text.
    CounterInfo(StringRef Name, StringRef Desc) : Name(Name), Desc(Desc) {
      DebugCounter::registerCounter(this);
    }

    /// Reset this counter to its initial unset, inactive state.
    void reset() {
      Active = false;
      IsSet = false;
      Count = 0;
      CurrChunkIdx = 0;
      Chunks.clear();
    }
  };

  /// Print \p Intervals to \p OS, separated by ':' characters.
  ///
  /// \param OS Stream that receives the printed chunk list.
  /// \param Intervals Inclusive intervals to print.
  LLVM_ABI static void
  printChunks(raw_ostream &OS, ArrayRef<IntegerInclusiveInterval> Intervals);

  /// Return true on parsing error and print the error message on the
  /// llvm::errs()
  ///
  /// \param Str Chunk specification string to parse.
  /// \param Res Parsed interval list written on success.
  /// \return True on parsing error; false on success.
  LLVM_ABI static bool
  parseChunks(StringRef Str, IntegerInclusiveIntervalUtils::IntervalList &Res);

  /// Returns a reference to the singleton instance.
  ///
  /// \return Reference to the singleton DebugCounter instance.
  LLVM_ABI static DebugCounter &instance();

  /// Apply a parsed \c -debug-counter value to a registered counter.
  ///
  /// Used by the command-line option parser. \p Val has the form
  /// \c counter=chunk_list.
  ///
  /// \param Val Option value of the form \c name=chunks.
  LLVM_ABI void push_back(const std::string &Val);

  /// Register \p Info with the singleton debug-counter instance.
  ///
  /// Counter registration must currently happen before command-line option
  /// parsing so that \c -debug-counter can list available counters.
  ///
  /// \param Info Counter to register.
  static void registerCounter(CounterInfo *Info) {
    instance().addCounter(Info);
  }
  /// Increment \p Counter and return whether this count should execute.
  ///
  /// When query printing is enabled and the counter is set, writes the
  /// decision to the debug stream.
  ///
  /// \param Counter Counter to increment and test.
  /// \return True if this count should execute.
  LLVM_ABI static bool shouldExecuteImpl(CounterInfo &Counter);

  /// Return true if the event gated by \p Counter should run.
  ///
  /// Returns true immediately when the counter is not active. Otherwise
  /// increments the counter and returns whether the current count falls in
  /// an enabled chunk.
  ///
  /// \param Counter Counter that gates the event.
  /// \return True if the gated event should run.
  inline static bool shouldExecute(CounterInfo &Counter) {
    if (!Counter.Active)
      return true;
    return shouldExecuteImpl(Counter);
  }

  /// Return true if \p Info has chunks set from the command line or in code.
  ///
  /// Returns true even when the current state would always execute.
  ///
  /// \param Info Counter to query.
  /// \return True if \p Info has chunks set.
  static bool isCounterSet(CounterInfo &Info) { return Info.IsSet; }

  /// Snapshot of a counter's count and current chunk index.
  struct CounterState {
    /// Number of times the counter has been queried.
    int64_t Count;
    /// Index of the chunk currently being considered.
    uint64_t ChunkIdx;
  };

  /// Return the current count and chunk index of set counter \p Info.
  ///
  /// \param Info Counter whose state is returned; must already be set.
  /// \return The current count and chunk index of \p Info.
  static CounterState getCounterState(CounterInfo &Info) {
    return {Info.Count, Info.CurrChunkIdx};
  }

  /// Restore registered counter \p Info to previously captured \p State.
  ///
  /// \param Info Registered counter to update.
  /// \param State Count and chunk index to apply.
  static void setCounterState(CounterInfo &Info, CounterState State) {
    Info.Count = State.Count;
    Info.CurrChunkIdx = State.ChunkIdx;
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the current counter set to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif

  /// Print registered counters and their values to \p OS.
  ///
  /// \param OS Stream that receives the counter listing.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Return the counter named \p Name, or null if none is registered.
  ///
  /// \param Name Command-line name of the counter.
  /// \return The counter named \p Name, or null if none is registered.
  CounterInfo *getCounterInfo(StringRef Name) const {
    return Counters.lookup(Name);
  }

  /// Return the number of registered counters.
  ///
  /// \return The number of registered counters.
  unsigned int getNumCounters() const { return Counters.size(); }

  /// Return the name and description of counter \p Info.
  ///
  /// \param Info Counter whose name and description are returned.
  /// \return The name and description of \p Info.
  std::pair<StringRef, StringRef> getCounterDesc(CounterInfo *Info) const {
    return {Info->Name, Info->Desc};
  }

  /// Return an iterator to the first registered counter.
  ///
  /// \return An iterator to the first registered counter.
  MapVector<StringRef, CounterInfo *>::const_iterator begin() const {
    return Counters.begin();
  }
  /// Return an iterator past the last registered counter.
  ///
  /// \return An iterator past the last registered counter.
  MapVector<StringRef, CounterInfo *>::const_iterator end() const {
    return Counters.end();
  }

  /// Mark every registered counter as active.
  void activateAllCounters() {
    for (auto &[_, Counter] : Counters)
      Counter->Active = true;
  }

  /// Reset every registered counter to its initial state.
  void resetAllCounters() {
    for (auto &[_, Counter] : Counters)
      Counter->reset();
  }

protected:
  /// Insert \p Info into the name-to-counter map.
  ///
  /// \param Info Counter to register.
  void addCounter(CounterInfo *Info) { Counters[Info->Name] = Info; }
  /// Increment \p Info and return whether this count is in an enabled chunk.
  ///
  /// Always returns true when \p Info has no chunks. When past the last
  /// chunk, returns false. If \c BreakOnLast is set and this is the last
  /// count of the last chunk, triggers a debug trap.
  ///
  /// \param Info Counter to increment and test.
  /// \return True if this count is in an enabled chunk.
  LLVM_ABI bool handleCounterIncrement(CounterInfo &Info);

  /// Map from counter name to registered counter info.
  MapVector<StringRef, CounterInfo *> Counters;

  /// Whether \c -print-debug-counter requested a summary at shutdown.
  bool ShouldPrintCounter = false;

  /// Whether \c -print-debug-counter-queries logs each counter decision.
  bool ShouldPrintCounterQueries = false;

  /// Whether \c -debug-counter-break-on-last traps on the last enabled count.
  bool BreakOnLast = false;
};

#define DEBUG_COUNTER(VARNAME, COUNTERNAME, DESC)                              \
  static DebugCounter::CounterInfo VARNAME(COUNTERNAME, DESC)

} // namespace llvm
#endif
