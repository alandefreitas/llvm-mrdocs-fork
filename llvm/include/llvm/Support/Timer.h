//===-- llvm/Support/Timer.h - Interval Timing Support ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TIMER_H
#define LLVM_SUPPORT_TIMER_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Mutex.h"
#include <cassert>
#include <string>
#include <vector>

namespace llvm {

/// Owns process-wide timer state, options, and the default timer group.
class TimerGlobals;
class TimerGroup;
class raw_ostream;

/// A snapshot of wall, user, and system time plus memory and instruction counts.
class TimeRecord {
  double WallTime = 0.0;             ///< Wall clock time elapsed in seconds.
  double UserTime = 0.0;             ///< User time elapsed.
  double SystemTime = 0.0;           ///< System time elapsed.
  ssize_t MemUsed = 0;               ///< Memory allocated (in bytes).
  uint64_t InstructionsExecuted = 0; ///< Number of instructions executed
public:
  /// Construct a zero-initialized time record.
  TimeRecord() = default;

  /// Get the current time and memory usage.
  ///
  /// If Start is true we get the memory usage before the time, otherwise we get
  /// time before memory usage. This matters if the time to get the memory usage
  /// is significant and shouldn't be counted as part of a duration.
  ///
  /// \param Start Whether this sample marks the start of an interval.
  /// \return A time record with the current wall, user, and system times.
  LLVM_ABI static TimeRecord getCurrentTime(bool Start = true);

  /// Return user plus system time in seconds.
  ///
  /// \return User plus system time in seconds.
  double getProcessTime() const { return UserTime + SystemTime; }
  /// Return user-mode CPU time in seconds.
  ///
  /// \return User-mode CPU time in seconds.
  double getUserTime() const { return UserTime; }
  /// Return system-mode CPU time in seconds.
  ///
  /// \return System-mode CPU time in seconds.
  double getSystemTime() const { return SystemTime; }
  /// Return wall-clock time in seconds.
  ///
  /// \return Wall-clock time in seconds.
  double getWallTime() const { return WallTime; }
  /// Return memory allocated in bytes.
  ///
  /// \return Memory allocated in bytes.
  ssize_t getMemUsed() const { return MemUsed; }
  /// Return the number of instructions executed.
  ///
  /// \return The number of instructions executed.
  uint64_t getInstructionsExecuted() const { return InstructionsExecuted; }

  /// Return true if this record's wall time is less than \p T's.
  ///
  /// \param T The time record to compare against.
  /// \return True if this record's wall time is less than \p T's.
  bool operator<(const TimeRecord &T) const {
    // Sort by Wall Time elapsed, as it is the only thing really accurate
    return WallTime < T.WallTime;
  }

  /// Add the times and resource counts from \p RHS into this record.
  ///
  /// \param RHS The time record to add.
  void operator+=(const TimeRecord &RHS) {
    WallTime += RHS.WallTime;
    UserTime += RHS.UserTime;
    SystemTime += RHS.SystemTime;
    MemUsed += RHS.MemUsed;
    InstructionsExecuted += RHS.InstructionsExecuted;
  }
  /// Subtract the times and resource counts in \p RHS from this record.
  ///
  /// \param RHS The time record to subtract.
  void operator-=(const TimeRecord &RHS) {
    WallTime -= RHS.WallTime;
    UserTime -= RHS.UserTime;
    SystemTime -= RHS.SystemTime;
    MemUsed -= RHS.MemUsed;
    InstructionsExecuted -= RHS.InstructionsExecuted;
  }
  /// Return a time record equal to this record minus \p RHS.
  ///
  /// \param RHS The time record to subtract.
  /// \return A time record equal to this record minus \p RHS.
  TimeRecord operator-(const TimeRecord &RHS) const {
    TimeRecord R = *this;
    R -= RHS;
    return R;
  }
  // Feel free to add operator+ if you need it

  /// Print the current time record to \p OS, with a breakdown showing
  /// contributions to the \p Total time record.
  ///
  /// \param Total The aggregate time record used for percentage breakdowns.
  /// \param OS The stream to print to.
  LLVM_ABI void print(const TimeRecord &Total, raw_ostream &OS) const;
};

/// Tracks elapsed time between paired startTimer() and stopTimer() calls.
///
/// Given appropriate OS support it can also keep track of the RSS of the
/// program at various points. By default, the Timer will print the amount of
/// time it has captured to standard error when the last timer is destroyed,
/// otherwise it is printed when its TimerGroup is destroyed. Timers do not
/// print their information if they are never started.
class Timer {
  TimeRecord Time;          ///< The total time captured.
  TimeRecord StartTime;     ///< The time startTimer() was last called.
  std::string Name;         ///< The name of this time variable.
  std::string Description;  ///< Description of this time variable.
  bool Running = false;     ///< Is the timer currently running?
  bool Triggered = false;   ///< Has the timer ever been triggered?
  TimerGroup *TG = nullptr; ///< The TimerGroup this Timer is in.

  Timer **Prev = nullptr;   ///< Pointer to \p Next of previous timer in group.
  Timer *Next = nullptr;    ///< Next timer in the group.
public:
  /// Create and initialize a timer with the given name and description.
  ///
  /// \param TimerName The name of this timer.
  /// \param TimerDescription A human-readable description of this timer.
  explicit Timer(StringRef TimerName, StringRef TimerDescription) {
    init(TimerName, TimerDescription);
  }
  /// Create and initialize a timer in the given timer group.
  ///
  /// \param TimerName The name of this timer.
  /// \param TimerDescription A human-readable description of this timer.
  /// \param tg The timer group that owns this timer.
  Timer(StringRef TimerName, StringRef TimerDescription, TimerGroup &tg) {
    init(TimerName, TimerDescription, tg);
  }
  /// Copy an uninitialized timer.
  ///
  /// \param RHS The uninitialized timer to copy.
  Timer(const Timer &RHS) {
    assert(!RHS.TG && "Can only copy uninitialized timers");
  }
  /// Assign from an uninitialized timer.
  ///
  /// \param T The uninitialized timer to assign from.
  /// \return A reference to this timer.
  const Timer &operator=(const Timer &T) {
    assert(!TG && !T.TG && "Can only assign uninit timers");
    return *this;
  }
  /// Destroy the timer and remove it from its group.
  LLVM_ABI ~Timer();

  /// Create an uninitialized timer, client must use 'init'.
  explicit Timer() = default;
  /// Initialize this timer with a name and description.
  ///
  /// \param TimerName The name of this timer.
  /// \param TimerDescription A human-readable description of this timer.
  LLVM_ABI void init(StringRef TimerName, StringRef TimerDescription);
  /// Initialize this timer and add it to the given group.
  ///
  /// \param TimerName The name of this timer.
  /// \param TimerDescription A human-readable description of this timer.
  /// \param tg The timer group that owns this timer.
  LLVM_ABI void init(StringRef TimerName, StringRef TimerDescription,
                     TimerGroup &tg);

  /// Return the name of this timer.
  ///
  /// \return The name of this timer.
  const std::string &getName() const { return Name; }
  /// Return the description of this timer.
  ///
  /// \return The description of this timer.
  const std::string &getDescription() const { return Description; }
  /// Return true if this timer has been initialized into a group.
  ///
  /// \return True if this timer has been initialized into a group.
  bool isInitialized() const { return TG != nullptr; }

  /// Check if the timer is currently running.
  ///
  /// \return True if the timer is currently running.
  bool isRunning() const { return Running; }

  /// Check if startTimer() has ever been called on this timer.
  ///
  /// \return True if startTimer() has ever been called on this timer.
  bool hasTriggered() const { return Triggered; }

  /// Start the timer running.  Time between calls to startTimer/stopTimer is
  /// counted by the Timer class.  Note that these calls must be correctly
  /// paired.
  LLVM_ABI void startTimer();

  /// Stop the timer.
  LLVM_ABI void stopTimer();

  /// Clear the timer state.
  LLVM_ABI void clear();

  /// Stop the timer and start another timer.
  ///
  /// \param Other The timer to start after stopping this one.
  LLVM_ABI void yieldTo(Timer &Other);

  /// Return the duration for which this timer has been running.
  ///
  /// \return The accumulated time record for this timer.
  TimeRecord getTotalTime() const { return Time; }

private:
  friend class TimerGroup;
};

/// RAII helper that starts a timer on construction and stops it on destruction.
///
/// The TimeRegion class is used as a helper class to call the startTimer() and
/// stopTimer() methods of the Timer class. When the object is constructed, it
/// starts the timer specified as its argument. When it is destroyed, it stops
/// the relevant timer. This makes it easy to time a region of code.
class TimeRegion {
  Timer *T;
  TimeRegion(const TimeRegion &) = delete;

public:
  /// Construct a region that starts \p t immediately.
  ///
  /// \param t The timer to start for the lifetime of this object.
  explicit TimeRegion(Timer &t) : T(&t) {
    T->startTimer();
  }
  /// Construct a region that starts \p t if non-null.
  ///
  /// \param t The timer to start, or null to leave timing inactive.
  explicit TimeRegion(Timer *t) : T(t) {
    if (T) T->startTimer();
  }
  /// Stop the associated timer if one was started.
  ~TimeRegion() {
    if (T) T->stopTimer();
  }
};

/// Combines Timer and TimeRegion to time a named region in one statement.
///
/// This class is basically a combination of TimeRegion and Timer. It allows you
/// to declare a new timer, AND specify the region to time, all in one statement.
/// All timers with the same name are merged. This is primarily used for
/// debugging and for hunting performance problems.
struct NamedRegionTimer : TimeRegion {
  /// Create a named region timer in the named group.
  ///
  /// \param Name The name of the timer.
  /// \param Description A human-readable description of the timer.
  /// \param GroupName The name of the timer group.
  /// \param GroupDescription A human-readable description of the group.
  /// \param Enabled If false, the timer is not started.
  LLVM_ABI explicit NamedRegionTimer(StringRef Name, StringRef Description,
                                     StringRef GroupName,
                                     StringRef GroupDescription,
                                     bool Enabled = true);

  /// Create or get a TimerGroup stored in the same global map owned by
  /// NamedRegionTimer.
  ///
  /// \param GroupName The name of the timer group.
  /// \param GroupDescription A human-readable description of the group.
  /// \return A reference to the named timer group.
  LLVM_ABI static TimerGroup &getNamedTimerGroup(StringRef GroupName,
                                                 StringRef GroupDescription);
};

/// Groups related timers into a single report printed on destruction.
///
/// The TimerGroup class is used to group together related timers into a single
/// report that is printed when the TimerGroup is destroyed. It is illegal to
/// destroy a TimerGroup object before all of the Timers in it are gone. A
/// TimerGroup can be specified for a newly created timer in its constructor.
class TimerGroup {
  struct PrintRecord {
    TimeRecord Time;
    std::string Name;
    std::string Description;

    PrintRecord(const PrintRecord &Other) = default;
    PrintRecord &operator=(const PrintRecord &Other) = default;
    PrintRecord(const TimeRecord &Time, const std::string &Name,
                const std::string &Description)
      : Time(Time), Name(Name), Description(Description) {}

    bool operator <(const PrintRecord &Other) const {
      return Time < Other.Time;
    }
  };
  std::string Name;
  std::string Description;
  Timer *FirstTimer = nullptr; ///< First timer in the group.
  std::vector<PrintRecord> TimersToPrint;
  bool PrintOnExit;

  TimerGroup **Prev; ///< Pointer to Next field of previous timergroup in list.
  TimerGroup *Next;  ///< Pointer to next timergroup in list.
  TimerGroup(const TimerGroup &TG) = delete;
  void operator=(const TimerGroup &TG) = delete;

  friend class TimerGlobals;
  explicit TimerGroup(StringRef Name, StringRef Description,
                      sys::SmartMutex<true> &lock, bool PrintOnExit);

public:
  /// Create a timer group with the given name and description.
  ///
  /// \param Name The name of this timer group.
  /// \param Description A human-readable description of this group.
  /// \param PrintOnExit Whether to print the group when it is destroyed.
  LLVM_ABI explicit TimerGroup(StringRef Name, StringRef Description,
                               bool PrintOnExit = true);

  /// Create a timer group seeded with existing timing records.
  ///
  /// \param Name The name of this timer group.
  /// \param Description A human-readable description of this group.
  /// \param Records Named timing records to include in the group.
  /// \param PrintOnExit Whether to print the group when it is destroyed.
  LLVM_ABI explicit TimerGroup(StringRef Name, StringRef Description,
                               const StringMap<TimeRecord> &Records,
                               bool PrintOnExit = true);

  /// Destroy the group and print its timers when PrintOnExit is set.
  LLVM_ABI ~TimerGroup();

  /// Set the name and description of this timer group.
  ///
  /// \param NewName The new name for this group.
  /// \param NewDescription The new description for this group.
  void setName(StringRef NewName, StringRef NewDescription) {
    Name.assign(NewName.begin(), NewName.end());
    Description.assign(NewDescription.begin(), NewDescription.end());
  }

  /// Print any started timers in this group, optionally resetting timers after
  /// printing them.
  ///
  /// \param OS The stream to print to.
  /// \param ResetAfterPrint If true, clear timers after printing.
  LLVM_ABI void print(raw_ostream &OS, bool ResetAfterPrint = false);

  /// Clear all timers in this group.
  LLVM_ABI void clear();

  /// This static method prints all timers.
  ///
  /// \param OS The stream to print to.
  LLVM_ABI static void printAll(raw_ostream &OS);

  /// Clear out all timers.
  ///
  /// This is mostly used to disable automatic printing on shutdown, when timers
  /// have already been printed explicitly using \c printAll or
  /// \c printJSONValues.
  LLVM_ABI static void clearAll();

  /// Print this group's timers as JSON key/value pairs.
  ///
  /// \param OS The stream to print to.
  /// \param delim The delimiter to emit before each value.
  /// \return The delimiter to use for the next JSON value.
  LLVM_ABI const char *printJSONValues(raw_ostream &OS, const char *delim);

  /// Prints all timers as JSON key/value pairs.
  ///
  /// \param OS The stream to print to.
  /// \param delim The delimiter to emit before each value.
  /// \return The delimiter to use for the next JSON value.
  LLVM_ABI static const char *printAllJSONValues(raw_ostream &OS,
                                                 const char *delim);

  /// Ensure global objects required for statistics printing are initialized.
  ///
  /// This function is used by the Statistic code to ensure correct order of
  /// global constructors and destructors.
  LLVM_ABI static void constructForStatistics();

  /// This makes the timer globals unmanaged, and lets the user manage the
  /// lifetime.
  ///
  /// \return An opaque pointer to the timer globals for the caller to own.
  LLVM_ABI static void *acquireTimerGlobals();

private:
  friend class Timer;
  LLVM_ABI friend void PrintStatisticsJSON(raw_ostream &OS);
  void addTimer(Timer &T);
  void removeTimer(Timer &T);
  void prepareToPrintList(bool reset_time = false);
  void PrintQueuedTimers(raw_ostream &OS);
  void printJSONValue(raw_ostream &OS, const PrintRecord &R,
                      const char *suffix, double Value);
};

} // end namespace llvm

#endif
