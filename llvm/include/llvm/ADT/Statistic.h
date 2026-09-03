//===-- llvm/ADT/Statistic.h - Easy way to expose stats ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the 'Statistic' class, which is designed to be an easy way
/// to expose various metrics from passes.  These statistics are printed at the
/// end of a run (from llvm_shutdown), when the -stats command line option is
/// passed on the command line.
///
/// This is useful for reporting information like the number of instructions
/// simplified, optimized or removed by various transformations, like this:
///
/// static Statistic NumInstsKilled("gcse", "Number of instructions killed");
///
/// Later, in the code: ++NumInstsKilled;
///
/// NOTE: Statistics *must* be declared as global variables.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STATISTIC_H
#define LLVM_ADT_STATISTIC_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"
#include <atomic>
#include <memory>
#include <vector>

// Determine whether statistics should be enabled. We must do it here rather
// than in CMake because multi-config generators cannot determine this at
// configure time.
#if !defined(NDEBUG) || LLVM_FORCE_ENABLE_STATS
#define LLVM_ENABLE_STATS 1
#else
#define LLVM_ENABLE_STATS 0
#endif

namespace llvm {

class raw_ostream;
class raw_fd_ostream;
class StringRef;

/// Atomic counter used when statistics collection is enabled.
class TrackingStatistic {
public:
  /// Debug type / pass name this statistic belongs to.
  const char *const DebugType;
  /// Short identifier for the statistic.
  const char *const Name;
  /// Human-readable description printed with -stats.
  const char *const Desc;

  /// Current accumulated value.
  std::atomic<uint64_t> Value;
  /// True once this statistic has been registered with the global list.
  std::atomic<bool> Initialized;

  /// Construct a statistic labeled by \p DebugType, \p Name, and \p Desc.
  ///
  /// \param DebugType Debug type / pass name this statistic belongs to.
  /// \param Name Short identifier for the statistic.
  /// \param Desc Human-readable description printed with -stats.
  constexpr TrackingStatistic(const char *DebugType, const char *Name,
                              const char *Desc)
      : DebugType(DebugType), Name(Name), Desc(Desc), Value(0),
        Initialized(false) {}

  /// Return the debug type / pass name string.
  ///
  /// \return The debug type / pass name string.
  const char *getDebugType() const { return DebugType; }
  /// Return the short statistic name.
  ///
  /// \return The short statistic name.
  const char *getName() const { return Name; }
  /// Return the human-readable description.
  ///
  /// \return The human-readable description.
  const char *getDesc() const { return Desc; }

  /// Load the current counter value.
  ///
  /// \return The current counter value.
  uint64_t getValue() const { return Value.load(std::memory_order_relaxed); }

  /// Convert to the current counter value.
  ///
  /// \return The current counter value.
  operator uint64_t() const { return getValue(); }

  /// Assign \p Val as the new counter value.
  ///
  /// \param Val New value to store in the counter.
  /// \return Reference to this statistic.
  const TrackingStatistic &operator=(uint64_t Val) {
    Value.store(Val, std::memory_order_relaxed);
    return init();
  }

  /// Pre-increment the counter by one.
  ///
  /// \return Reference to this statistic.
  const TrackingStatistic &operator++() {
    Value.fetch_add(1, std::memory_order_relaxed);
    return init();
  }

  /// Post-increment the counter by one; return the previous value.
  ///
  /// \param Unused Dummy parameter distinguishing postfix from prefix.
  /// \return The previous counter value.
  uint64_t operator++(int Unused) {
    init();
    return Value.fetch_add(1, std::memory_order_relaxed);
  }

  /// Pre-decrement the counter by one.
  ///
  /// \return Reference to this statistic.
  const TrackingStatistic &operator--() {
    Value.fetch_sub(1, std::memory_order_relaxed);
    return init();
  }

  /// Post-decrement the counter by one; return the previous value.
  ///
  /// \param Unused Dummy parameter distinguishing postfix from prefix.
  /// \return The previous counter value.
  uint64_t operator--(int Unused) {
    init();
    return Value.fetch_sub(1, std::memory_order_relaxed);
  }

  /// Add \p V to the counter.
  ///
  /// \param V Amount to add.
  /// \return Reference to this statistic.
  const TrackingStatistic &operator+=(uint64_t V) {
    if (V == 0)
      return *this;
    Value.fetch_add(V, std::memory_order_relaxed);
    return init();
  }

  /// Subtract \p V from the counter.
  ///
  /// \param V Amount to subtract.
  /// \return Reference to this statistic.
  const TrackingStatistic &operator-=(uint64_t V) {
    if (V == 0)
      return *this;
    Value.fetch_sub(V, std::memory_order_relaxed);
    return init();
  }

  /// Raise the counter to at least \p V (atomic max update).
  ///
  /// \param V Candidate maximum value.
  void updateMax(uint64_t V) {
    uint64_t PrevMax = Value.load(std::memory_order_relaxed);
    // Keep trying to update max until we succeed or another thread produces
    // a bigger max than us.
    while (V > PrevMax && !Value.compare_exchange_weak(
                              PrevMax, V, std::memory_order_relaxed)) {
    }
    init();
  }

protected:
  /// Ensure this statistic is registered before further updates.
  ///
  /// \return Reference to this statistic.
  TrackingStatistic &init() {
    if (!Initialized.load(std::memory_order_acquire))
      RegisterStatistic();
    return *this;
  }

  /// Register this statistic with the global statistics list.
  LLVM_ABI void RegisterStatistic();
};

/// No-op statistic used when LLVM_ENABLE_STATS is off.
class NoopStatistic {
public:
  /// Construct a discarded statistic; arguments are ignored.
  ///
  /// \param DebugType Unused debug type / pass name.
  /// \param Name Unused short identifier.
  /// \param Desc Unused human-readable description.
  constexpr NoopStatistic(const char *DebugType, const char *Name,
                          const char *Desc) {}

  /// Always returns zero.
  ///
  /// \return Always zero.
  uint64_t getValue() const { return 0; }

  /// Convert to zero.
  ///
  /// \return Always zero.
  operator uint64_t() const { return 0; }

  /// Assignment is a no-op.
  ///
  /// \param Val Unused new counter value.
  /// \return Reference to this statistic.
  const NoopStatistic &operator=(uint64_t Val) { return *this; }

  /// Pre-increment is a no-op.
  ///
  /// \return Reference to this statistic.
  const NoopStatistic &operator++() { return *this; }

  /// Post-increment returns zero.
  ///
  /// \param Unused Dummy parameter distinguishing postfix from prefix.
  /// \return Always zero.
  uint64_t operator++(int Unused) { return 0; }

  /// Pre-decrement is a no-op.
  ///
  /// \return Reference to this statistic.
  const NoopStatistic &operator--() { return *this; }

  /// Post-decrement returns zero.
  ///
  /// \param Unused Dummy parameter distinguishing postfix from prefix.
  /// \return Always zero.
  uint64_t operator--(int Unused) { return 0; }

  /// Addition is a no-op.
  ///
  /// \param V Unused amount to add.
  /// \return Reference to this statistic.
  const NoopStatistic &operator+=(const uint64_t &V) { return *this; }

  /// Subtraction is a no-op.
  ///
  /// \param V Unused amount to subtract.
  /// \return Reference to this statistic.
  const NoopStatistic &operator-=(const uint64_t &V) { return *this; }

  /// Max update is a no-op.
  ///
  /// \param V Unused candidate maximum value.
  void updateMax(uint64_t V) {}
};

#if LLVM_ENABLE_STATS
/// Statistic type that actually tracks values when stats are enabled.
using Statistic = TrackingStatistic;
#else
/// Statistic type that compiles to no-ops when stats are disabled.
using Statistic = NoopStatistic;
#endif

// STATISTIC - A macro to make definition of statistics really simple.  This
// automatically passes the DEBUG_TYPE of the file into the statistic.
#if LLVM_ENABLE_STATS
#define STATISTIC(VARNAME, DESC)                                               \
  static llvm::Statistic VARNAME = {DEBUG_TYPE, #VARNAME, DESC}
#else
#define STATISTIC(VARNAME, DESC)                                               \
  static llvm::Statistic VARNAME [[maybe_unused]] = {DEBUG_TYPE, #VARNAME, DESC}
#endif

// ALWAYS_ENABLED_STATISTIC - A macro to define a statistic like STATISTIC but
// it is enabled even if LLVM_ENABLE_STATS is off.
#define ALWAYS_ENABLED_STATISTIC(VARNAME, DESC)                                \
  static llvm::TrackingStatistic VARNAME = {DEBUG_TYPE, #VARNAME, DESC}

/// Enable the collection and printing of statistics.
///
/// \param DoPrintOnExit If true, print statistics when the process exits.
LLVM_ABI void EnableStatistics(bool DoPrintOnExit = true);

/// Check if statistics are enabled.
///
/// \return True if statistics collection is enabled.
LLVM_ABI bool AreStatisticsEnabled();

/// Return a stream to print our output on.
///
/// \return Unique pointer to the info output stream.
LLVM_ABI std::unique_ptr<raw_ostream> CreateInfoOutputFile();

/// Print statistics to the file returned by CreateInfoOutputFile().
LLVM_ABI void PrintStatistics();

/// Print statistics to the given output stream.
///
/// \param OS Stream that receives the human-readable statistics report.
LLVM_ABI void PrintStatistics(raw_ostream &OS);

/// Print statistics in JSON format. This does include all global timers (\see
/// Timer, TimerGroup). Note that the timers are cleared after printing and will
/// not be printed in human readable form or in a second call of
/// PrintStatisticsJSON().
///
/// \param OS Stream that receives the JSON statistics report.
LLVM_ABI void PrintStatisticsJSON(raw_ostream &OS);

/// Get the statistics. This can be used to look up the value of
/// statistics without needing to parse JSON.
///
/// This function does not prevent statistics being updated by other threads
/// during it's execution. It will return the value at the point that it is
/// read. However, it will prevent new statistics from registering until it
/// completes.
///
/// \return Vector of name/value pairs for all registered statistics.
LLVM_ABI std::vector<std::pair<StringRef, uint64_t>> GetStatistics();

/// Reset the statistics. This can be used to zero and de-register the
/// statistics in order to measure a compilation.
///
/// When this function begins to call destructors prior to returning, all
/// statistics will be zero and unregistered. However, that might not remain the
/// case by the time this function finishes returning. Whether update from other
/// threads are lost or merely deferred until during the function return is
/// timing sensitive.
///
/// Callers who intend to use this to measure statistics for a single
/// compilation should ensure that no compilations are in progress at the point
/// this function is called and that only one compilation executes until calling
/// GetStatistics().
LLVM_ABI void ResetStatistics();

} // end namespace llvm

#endif // LLVM_ADT_STATISTIC_H
