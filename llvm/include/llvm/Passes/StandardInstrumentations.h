//===- StandardInstrumentations.h ------------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header defines a class that provides bookkeeping for all standard
/// (i.e in-tree) pass instrumentations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASSES_STANDARDINSTRUMENTATIONS_H
#define LLVM_PASSES_STANDARDINSTRUMENTATIONS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DroppedVariableStatsIR.h"
#include "llvm/IR/IRUnitRef.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Transforms/IPO/SampleProfileProbe.h"

#include <string>
#include <utility>

namespace llvm {

class Module;
class Function;
class MachineFunction;
class PassInstrumentationCallbacks;

/// Instrumentation to print IR before/after passes.
///
/// Needs state to be able to print module after pass that invalidates IR unit
/// (typically Loop or SCC).
class PrintIRInstrumentation {
public:
  /// Destroy this instrumentation and flush any pending IR dumps.
  LLVM_ABI ~PrintIRInstrumentation();

  /// Register before/after/invalidate callbacks on \p PIC.
  /// \param PIC Pass instrumentation callbacks to register with.
  LLVM_ABI void registerCallbacks(PassInstrumentationCallbacks &PIC);

private:
  struct PassRunDescriptor {
    const Module *M;
    const unsigned PassNumber;
    const std::string IRFileDisplayName;
    const std::string IRName;
    const StringRef PassID;

    PassRunDescriptor(const Module *M, unsigned PassNumber,
                      std::string &&IRFileDisplayName, std::string &&IRName,
                      const StringRef PassID)
        : M{M}, PassNumber{PassNumber}, IRFileDisplayName(IRFileDisplayName),
          IRName{IRName}, PassID(PassID) {}
  };

  void printBeforePass(StringRef PassID, IRUnitRef IR);
  void printAfterPass(StringRef PassID, IRUnitRef IR);
  void printAfterPassInvalidated(StringRef PassID);

  bool shouldPrintBeforePass(StringRef PassID);
  bool shouldPrintAfterPass(StringRef PassID);
  bool shouldPrintBeforeCurrentPassNumber();
  bool shouldPrintAfterCurrentPassNumber();
  bool shouldPrintPassNumbers();
  bool shouldPrintBeforeSomePassNumber();
  bool shouldPrintAfterSomePassNumber();

  void pushPassRunDescriptor(StringRef PassID, IRUnitRef IR,
                             unsigned PassNumber);
  PassRunDescriptor popPassRunDescriptor(StringRef PassID);

  enum class IRDumpFileSuffixType {
    Before,
    After,
    Invalidated,
  };

  static StringRef
  getFileSuffix(PrintIRInstrumentation::IRDumpFileSuffixType Type);
  std::string fetchDumpFilename(StringRef PassId, StringRef IRFileDisplayName,
                                unsigned PassNumber,
                                IRDumpFileSuffixType SuffixType);

  PassInstrumentationCallbacks *PIC;
  /// Stack of Pass Run descriptions, enough to print the IR unit after a given
  /// pass.
  SmallVector<PassRunDescriptor, 2> PassRunDescriptorStack;

  /// Used for print-at-pass-number
  unsigned CurrentPassNumber = 0;
};

/// Instrumentation that skips passes on functions marked `optnone`.
class OptNoneInstrumentation {
public:
  /// Construct instrumentation that optionally logs skipped passes.
  /// \param DebugLogging When true, log when a pass is skipped for optnone.
  OptNoneInstrumentation(bool DebugLogging) : DebugLogging(DebugLogging) {}
  /// Register the should-run callback on \p PIC.
  /// \param PIC Pass instrumentation callbacks to register with.
  LLVM_ABI void registerCallbacks(PassInstrumentationCallbacks &PIC);

private:
  bool DebugLogging;
  bool shouldRun(StringRef PassID, IRUnitRef IR);
};

/// Instrumentation that respects `-opt-bisect-limit` and related pass gates.
class OptPassGateInstrumentation {
  LLVMContext &Context;
  bool HasWrittenIR = false;
public:
  /// Construct instrumentation bound to \p Context's opt-bisect gate.
  /// \param Context LLVM context providing the OptPassGate.
  OptPassGateInstrumentation(LLVMContext &Context) : Context(Context) {}
  /// Return whether \p PassName should run on \p IR under the current gate.
  /// \param PassName Name of the pass being considered.
  /// \param IR IR unit the pass would run on.
  /// \return True if the pass should run.
  LLVM_ABI bool shouldRun(StringRef PassName, IRUnitRef IR);
  /// Register the should-run callback on \p PIC.
  /// \param PIC Pass instrumentation callbacks to register with.
  LLVM_ABI void registerCallbacks(PassInstrumentationCallbacks &PIC);
};

/// Options controlling what PrintPassInstrumentation prints.
struct PrintPassOptions {
  /// Print adaptors and pass managers.
  bool Verbose = false;
  /// Don't print information for analyses.
  bool SkipAnalyses = false;
  /// Indent based on hierarchy.
  bool Indent = false;
};

/// Debug logging for transformation and analysis passes.
class PrintPassInstrumentation {
  raw_ostream &print();

public:
  /// Construct instrumentation that logs pass runs when \p Enabled.
  /// \param Enabled When false, no pass logging is performed.
  /// \param Opts Printing options (verbosity, skip analyses, indent).
  PrintPassInstrumentation(bool Enabled, PrintPassOptions Opts)
      : Enabled(Enabled), Opts(Opts) {}
  /// Register before/after pass logging callbacks on \p PIC.
  /// \param PIC Pass instrumentation callbacks to register with.
  LLVM_ABI void registerCallbacks(PassInstrumentationCallbacks &PIC);

private:
  bool Enabled;
  PrintPassOptions Opts;
  int Indent = 0;
};

/// Instrumentation that verifies CFG structure is preserved across passes.
class PreservedCFGCheckerInstrumentation {
public:
  /// Sticky poison flag for a basic block once it has been deleted or RAUWed.
  struct BBGuard final : public CallbackVH {
    /// Track \p BB so deletion or RAUW poisons this guard.
    /// \param BB Basic block to watch.
    BBGuard(const BasicBlock *BB) : CallbackVH(BB) {}
    /// Mark this guard poisoned when the tracked block is deleted.
    void deleted() override { CallbackVH::deleted(); }
    /// Mark this guard poisoned when all uses of the tracked block are replaced.
    /// \param New Replacement value (ignored; block is treated as deleted).
    void allUsesReplacedWith(Value *New) override {
      (void)New;
      CallbackVH::deleted();
    }
    /// Return true if the tracked basic block was deleted or RAUWed.
    /// \return True if the tracked block is gone.
    bool isPoisoned() const { return !getValPtr(); }
  };

  /// Snapshot of a function CFG as a successor multigraph, optionally guarded.
  ///
  /// CFG is a map BB -> {(Succ, Multiplicity)}, where BB is a non-leaf basic
  /// block, {(Succ, Multiplicity)} set of all pairs of the block's successors
  /// and the multiplicity of the edge (BB->Succ). As the mapped sets are
  /// unordered the order of successors is not tracked by the CFG. In other words
  /// this allows basic block successors to be swapped by a pass without
  /// reporting a CFG change. CFG can be guarded by basic block tracking pointers
  /// in the Graph (BBGuard). That is if any of the block is deleted or RAUWed
  /// then the CFG is treated poisoned and no block pointer of the Graph is used.
  struct CFG {
    /// Optional guards that poison this CFG when a tracked block is deleted.
    std::optional<DenseMap<intptr_t, BBGuard>> BBGuards;
    /// Successor multigraph: block -> (successor -> edge multiplicity).
    DenseMap<const BasicBlock *, DenseMap<const BasicBlock *, unsigned>> Graph;

    /// Build a CFG snapshot of \p F, optionally tracking block lifetime.
    /// \param F Function whose CFG is captured.
    /// \param TrackBBLifetime When true, install BBGuard entries for blocks.
    LLVM_ABI CFG(const Function *F, bool TrackBBLifetime);

    /// Return true if both CFGs are unpoisoned and have equal graphs.
    /// \param G Other CFG to compare against.
    /// \return True if both CFGs are equal and unpoisoned.
    bool operator==(const CFG &G) const {
      return !isPoisoned() && !G.isPoisoned() && Graph == G.Graph;
    }

    /// Return true if any guarded basic block was deleted or RAUWed.
    /// \return True if this CFG is poisoned.
    bool isPoisoned() const {
      return BBGuards && llvm::any_of(*BBGuards, [](const auto &BB) {
               return BB.second.isPoisoned();
             });
    }

    /// Print a textual diff of successor edges between \p Before and \p After.
    /// \param out Stream to write the diff to.
    /// \param Before CFG before the pass.
    /// \param After CFG after the pass.
    LLVM_ABI static void printDiff(raw_ostream &out, const CFG &Before,
                                   const CFG &After);
    /// Invalidate cached CFG analysis state for \p F when required by \p PA.
    /// \param F Function whose analysis may be invalidated.
    /// \param PA Set of analyses preserved by the pass.
    /// \param Inv Invalidator used to cascade invalidation.
    /// \return True if this CFG analysis result should be discarded.
    LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                             FunctionAnalysisManager::Invalidator &Inv);
  };

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  SmallVector<StringRef, 8> PassStack;
#endif

  /// Register CFG-checking callbacks on \p PIC using analyses from \p MAM.
  /// \param PIC Pass instrumentation callbacks to register with.
  /// \param MAM Module analysis manager used to access function analyses.
  LLVM_ABI void registerCallbacks(PassInstrumentationCallbacks &PIC,
                                  ModuleAnalysisManager &MAM);
};

/// Base class for reporting IR changes under the new pass manager.
///
/// It presents an interface for such classes and provides calls on various
/// events as the new pass manager transforms the IR. It also provides filtering
/// of information based on hidden options specifying which functions are
/// interesting. Calls are made for the following events/queries:
/// 1.  The initial IR processed.
/// 2.  To get the representation of the IR (of type \p T).
/// 3.  When a pass does not change the IR.
/// 4.  When a pass changes the IR (given both before and after representations
///         of type \p T).
/// 5.  When an IR is invalidated.
/// 6.  When a pass is run on an IR that is not interesting (based on options).
/// 7.  When a pass is ignored (pass manager or adapter pass).
/// 8.  To compare two IR representations (of type \p T).
template <typename IRUnitT> class LLVM_ABI ChangeReporter {
protected:
  /// Construct a change reporter with the given verbosity.
  /// \param RunInVerboseMode When true, report every interesting event.
  ChangeReporter(bool RunInVerboseMode) : VerboseMode(RunInVerboseMode) {}

public:
  /// Destroy this change reporter.
  virtual ~ChangeReporter();

  /// Save \p IR before \p PassID when the pass/IR is interesting.
  ///
  /// Otherwise the IR is left on the stack without data.
  /// \param IR IR unit about to be transformed.
  /// \param PassID Stable identifier of the pass.
  /// \param PassName Display name of the pass.
  void saveIRBeforePass(IRUnitRef IR, StringRef PassID, StringRef PassName);
  /// Compare saved before-IR with \p IR after \p PassID runs.
  /// \param IR IR unit after the pass.
  /// \param PassID Stable identifier of the pass.
  /// \param PassName Display name of the pass.
  void handleIRAfterPass(IRUnitRef IR, StringRef PassID, StringRef PassName);
  /// Handle invalidation of the IR for \p PassID.
  /// \param PassID Stable identifier of the invalidated pass.
  void handleInvalidatedPass(StringRef PassID);

protected:
  /// Register the before/after/invalidate callbacks required by this reporter.
  /// \param PIC Pass instrumentation callbacks to register with.
  void registerRequiredCallbacks(PassInstrumentationCallbacks &PIC);

  /// Handle the first IR unit processed by this reporter.
  /// \param IR First IR unit seen.
  virtual void handleInitialIR(IRUnitRef IR) = 0;
  /// Fill \p Output with the representation of \p IR for \p PassID.
  /// \param IR IR unit to capture.
  /// \param PassID Stable identifier of the pass.
  /// \param Output Destination representation.
  virtual void generateIRRepresentation(IRUnitRef IR, StringRef PassID,
                                        IRUnitT &Output) = 0;
  /// Report that interesting IR for \p PassID/\p Name did not change.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  virtual void omitAfter(StringRef PassID, std::string &Name) = 0;
  /// Report that interesting IR for \p PassID/\p Name changed.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  /// \param Before Representation before the pass.
  /// \param After Representation after the pass.
  /// \param IR IR unit after the pass (may be unused by subclasses).
  virtual void handleAfter(StringRef PassID, std::string &Name,
                           const IRUnitT &Before, const IRUnitT &After,
                           IRUnitRef IR) = 0;
  /// Report that an interesting pass \p PassID was invalidated.
  /// \param PassID Stable identifier of the pass.
  virtual void handleInvalidated(StringRef PassID) = 0;
  /// Report that \p PassID/\p Name was filtered out as uninteresting.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  virtual void handleFiltered(StringRef PassID, std::string &Name) = 0;
  /// Report that ignored pass \p PassID ran on \p Name.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  virtual void handleIgnored(StringRef PassID, std::string &Name) = 0;

  /// Stack of IR representations saved before passes.
  std::vector<IRUnitT> BeforeStack;
  /// Whether the next IR seen is the first one processed.
  bool InitialIR = true;

  /// When true, print every interesting event rather than only changes.
  const bool VerboseMode;
};

/// Textual change reporter that prints banners and no-change/filter messages.
template <typename IRUnitT>
class LLVM_ABI TextChangeReporter : public ChangeReporter<IRUnitT> {
protected:
  /// Construct a text reporter that writes to the standard change stream.
  /// \param Verbose When true, report every interesting event.
  TextChangeReporter(bool Verbose);

  /// Print a module dump of the first IR that is changed.
  /// \param IR First IR unit seen.
  void handleInitialIR(IRUnitRef IR) override;
  /// Report that the IR was omitted because it did not change.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  void omitAfter(StringRef PassID, std::string &Name) override;
  /// Report that the pass was invalidated.
  /// \param PassID Stable identifier of the pass.
  void handleInvalidated(StringRef PassID) override;
  /// Report that the IR was filtered out.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  void handleFiltered(StringRef PassID, std::string &Name) override;
  /// Report that the pass was ignored.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  void handleIgnored(StringRef PassID, std::string &Name) override;

  /// Output stream used for textual change banners and reports.
  raw_ostream &Out;
};

/// Change printer based on the string IR from unwrapAndPrint.
///
/// The string representation is stored in a std::string to preserve it as the
/// IR changes in each pass. Note that the banner is included in this
/// representation but it is massaged before reporting.
class LLVM_ABI IRChangedPrinter : public TextChangeReporter<std::string> {
public:
  /// Construct a string-based IR change printer.
  /// \param VerboseMode When true, report every interesting event.
  IRChangedPrinter(bool VerboseMode)
      : TextChangeReporter<std::string>(VerboseMode) {}
  /// Destroy this IR change printer.
  ~IRChangedPrinter() override;
  /// Register change-reporting callbacks on \p PIC.
  /// \param PIC Pass instrumentation callbacks to register with.
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

protected:
  /// Capture the string representation of \p IR for \p PassID into \p Output.
  /// \param IR IR unit to capture.
  /// \param PassID Stable identifier of the pass.
  /// \param Output Destination string representation.
  void generateIRRepresentation(IRUnitRef IR, StringRef PassID,
                                std::string &Output) override;
  /// Print the before/after string IR when an interesting unit changed.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  /// \param Before String IR before the pass.
  /// \param After String IR after the pass.
  /// \param IR IR unit after the pass (unused).
  void handleAfter(StringRef PassID, std::string &Name,
                   const std::string &Before, const std::string &After,
                   IRUnitRef IR) override;
};

/// Tester that asserts when interesting IR changes under print-changed.
class LLVM_ABI IRChangedTester : public IRChangedPrinter {
public:
  /// Construct a verbose IR change tester.
  IRChangedTester() : IRChangedPrinter(true) {}
  /// Destroy this IR change tester.
  ~IRChangedTester() override;
  /// Register testing callbacks on \p PIC.
  /// \param PIC Pass instrumentation callbacks to register with.
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

protected:
  /// Run the change test on string IR \p IR for \p PassID.
  /// \param IR Captured string IR.
  /// \param PassID Stable identifier of the pass.
  void handleIR(const std::string &IR, StringRef PassID);

  /// Check the initial IR by running the change test on it.
  /// \param IR First IR unit seen.
  void handleInitialIR(IRUnitRef IR) override;
  /// No-op when IR did not change.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  void omitAfter(StringRef PassID, std::string &Name) override;
  /// No-op when a pass is invalidated.
  /// \param PassID Stable identifier of the pass.
  void handleInvalidated(StringRef PassID) override;
  /// No-op when IR or pass is filtered out.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  void handleFiltered(StringRef PassID, std::string &Name) override;
  /// No-op when a pass is ignored.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  void handleIgnored(StringRef PassID, std::string &Name) override;

  /// Run the change test when interesting IR has changed.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  /// \param Before String IR before the pass.
  /// \param After String IR after the pass.
  /// \param IR IR unit after the pass (unused).
  void handleAfter(StringRef PassID, std::string &Name,
                   const std::string &Before, const std::string &After,
                   IRUnitRef IR) override;
};

/// Per-block data saved to compare IR before and after a pass.
template <typename T> class BlockDataT {
public:
  /// Capture label, printed body, and extra data from basic block \p B.
  /// \param B Basic block to snapshot.
  BlockDataT(const BasicBlock &B) : Label(B.getName().str()), Data(B) {
    raw_string_ostream SS(Body);
    B.print(SS, nullptr, true, true);
  }

  /// Capture label, printed body, and extra data from machine block \p B.
  /// \param B Machine basic block to snapshot.
  BlockDataT(const MachineBasicBlock &B) : Label(B.getName().str()), Data(B) {
    raw_string_ostream SS(Body);
    B.print(SS);
  }

  /// Return true if the printed bodies of both blocks are equal.
  /// \param That Other block data to compare.
  /// \return True if the printed bodies are equal.
  bool operator==(const BlockDataT &That) const { return Body == That.Body; }
  /// Return true if the printed bodies of both blocks differ.
  /// \param That Other block data to compare.
  /// \return True if the printed bodies differ.
  bool operator!=(const BlockDataT &That) const { return Body != That.Body; }

  /// Return the label of the represented basic block.
  /// \return Label of the represented basic block.
  StringRef getLabel() const { return Label; }
  /// Return the string representation of the basic block.
  /// \return Printed textual body of the block.
  StringRef getBody() const { return Body; }

  /// Return the associated per-block payload.
  /// \return Const reference to the per-block payload.
  const T &getData() const { return Data; }

protected:
  /// Name/label of the captured basic block.
  std::string Label;
  /// Printed textual body of the captured basic block.
  std::string Body;

  /// Extra data associated with a basic block.
  T Data;
};

/// Ordered map of named items used when comparing before/after IR.
template <typename T> class OrderedChangedData {
public:
  /// Return the mutable list of names in the order they were saved.
  /// \return Mutable reference to the ordered name list.
  std::vector<std::string> &getOrder() { return Order; }
  /// Return the names in the order they were saved.
  /// \return Const reference to the ordered name list.
  const std::vector<std::string> &getOrder() const { return Order; }

  /// Return the mutable map of names to saved representations.
  /// \return Mutable reference to the name-to-data map.
  StringMap<T> &getData() { return Data; }
  /// Return a map of names to saved representations.
  /// \return Const reference to the name-to-data map.
  const StringMap<T> &getData() const { return Data; }

  /// Return true if the saved data maps are equal.
  /// \param That Other ordered data to compare.
  /// \return True if both data maps are equal.
  bool operator==(const OrderedChangedData<T> &That) const {
    return Data == That.getData();
  }

  /// Invoke \p HandlePair on each corresponding before/after data pair.
  ///
  /// The order is based on the order in \p After with ones that are only in
  /// \p Before interspersed based on where they occur in \p Before. This is
  /// used to present the output in an order based on how the data is ordered
  /// in LLVM.
  /// \param Before Data captured before the pass.
  /// \param After Data captured after the pass.
  /// \param HandlePair Callback receiving before/after pointers (either may be
  ///        null).
  static void report(const OrderedChangedData &Before,
                     const OrderedChangedData &After,
                     function_ref<void(const T *, const T *)> HandlePair);

protected:
  /// Names of saved entries in encounter order.
  std::vector<std::string> Order;
  /// Map from entry name to saved representation.
  StringMap<T> Data;
};

/// Empty per-block payload for patch-style change reporters.
class EmptyData {
public:
  /// Construct empty payload for basic block (no extra data stored).
  /// \param B Basic block associated with this payload.
  EmptyData(const BasicBlock &B) { (void)B; }
  /// Construct empty payload for machine basic block (no extra data stored).
  /// \param B Machine basic block associated with this payload.
  EmptyData(const MachineBasicBlock &B) { (void)B; }
};

/// Per-function block map saved for comparing functions.
template <typename T>
class FuncDataT : public OrderedChangedData<BlockDataT<T>> {
public:
  /// Construct function data with entry block name \p S.
  /// \param S Name of the function's entry block.
  FuncDataT(std::string S) : EntryBlockName(S) {}

  /// Return the name of the entry block.
  /// \return Name of the function's entry block.
  std::string getEntryBlockName() const { return EntryBlockName; }

protected:
  /// Name of the function's entry basic block.
  std::string EntryBlockName;
};

/// Per-module (or IR-unit) function map saved for comparing IRs.
template <typename T>
class IRDataT : public OrderedChangedData<FuncDataT<T>> {};

/// Compares two IR snapshots and reports per-function differences.
///
/// The class is created with the 2 IRs to compare and then compare is called.
/// The static function analyzeIR is used to build up the IR representation.
template <typename T> class IRComparer {
public:
  /// Construct a comparer for \p Before and \p After IR snapshots.
  /// \param Before IR data captured before a pass.
  /// \param After IR data captured after a pass.
  IRComparer(const IRDataT<T> &Before, const IRDataT<T> &After)
      : Before(Before), After(After) {}

  /// Compare the two IRs, invoking \p CompareFunc for each function pair.
  ///
  /// When comparing a module, \p CompareFunc is told it is part of a module
  /// compare via its InModule argument.
  /// \param CompareModule True when comparing at module scope.
  /// \param CompareFunc Callback for each before/after function data pair.
  void compare(
      bool CompareModule,
      std::function<void(bool InModule, unsigned Minor,
                         const FuncDataT<T> &Before, const FuncDataT<T> &After)>
          CompareFunc);

  /// Analyze \p IR and build the IR representation in \p Data.
  /// \param IR IR unit to analyze.
  /// \param Data Destination IR representation.
  static void analyzeIR(IRUnitRef IR, IRDataT<T> &Data);

protected:
  /// Generate the data for \p F into \p Data.
  /// \param Data Destination IR representation.
  /// \param F Function or machine function to capture.
  /// \return False if \p F should be skipped; true otherwise.
  template <typename FunctionT>
  static bool generateFunctionData(IRDataT<T> &Data, const FunctionT &F);

  /// IR snapshot taken before the pass.
  const IRDataT<T> &Before;
  /// IR snapshot taken after the pass.
  const IRDataT<T> &After;
};

/// Change printer that shows in-line basic-block diffs with +/- markers.
///
/// It uses an InlineComparer to do the comparison so it shows the differences
/// prefixed with '-' and '+' for code that is removed and added, respectively.
/// Changes to the IR that do not affect basic blocks are not reported as having
/// changed the IR. The option -print-module-scope does not affect this change
/// reporter.
class LLVM_ABI InLineChangePrinter
    : public TextChangeReporter<IRDataT<EmptyData>> {
public:
  /// Construct an in-line diff printer with verbosity and colour settings.
  /// \param VerboseMode When true, report every interesting event.
  /// \param ColourMode When true, colourize added/removed lines.
  InLineChangePrinter(bool VerboseMode, bool ColourMode)
      : TextChangeReporter<IRDataT<EmptyData>>(VerboseMode),
        UseColour(ColourMode) {}
  /// Destroy this in-line change printer.
  ~InLineChangePrinter() override;
  /// Register in-line diff callbacks on \p PIC.
  /// \param PIC Pass instrumentation callbacks to register with.
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

protected:
  /// Create a block-oriented representation of \p IR in \p Output.
  /// \param IR IR unit to capture.
  /// \param PassID Stable identifier of the pass.
  /// \param Output Destination IR data.
  void generateIRRepresentation(IRUnitRef IR, StringRef PassID,
                                IRDataT<EmptyData> &Output) override;

  /// Print in-line diffs when interesting IR has changed.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  /// \param Before IR data before the pass.
  /// \param After IR data after the pass.
  /// \param IR IR unit after the pass (unused).
  void handleAfter(StringRef PassID, std::string &Name,
                   const IRDataT<EmptyData> &Before,
                   const IRDataT<EmptyData> &After, IRUnitRef IR) override;

  /// Print the in-line comparison for one function's before/after data.
  /// \param Name Function name.
  /// \param Prefix Banner prefix text.
  /// \param PassID Stable identifier of the pass.
  /// \param Divider Banner divider string.
  /// \param InModule True when this function is part of a module compare.
  /// \param Minor Minor section index for banners.
  /// \param Before Function data before the pass.
  /// \param After Function data after the pass.
  void handleFunctionCompare(StringRef Name, StringRef Prefix, StringRef PassID,
                             StringRef Divider, bool InModule, unsigned Minor,
                             const FuncDataT<EmptyData> &Before,
                             const FuncDataT<EmptyData> &After);

  /// Whether added/removed lines should be colourized.
  bool UseColour;
};

/// Instrumentation that runs the IR verifier after passes.
class VerifyInstrumentation {
  bool DebugLogging;

public:
  /// Construct verifier instrumentation with optional debug logging.
  /// \param DebugLogging When true, log verification activity.
  VerifyInstrumentation(bool DebugLogging) : DebugLogging(DebugLogging) {}
  /// Register after-pass verification callbacks on \p PIC.
  /// \param PIC Pass instrumentation callbacks to register with.
  /// \param MAM Optional module analysis manager for analysis-aware verify.
  LLVM_ABI void registerCallbacks(PassInstrumentationCallbacks &PIC,
                                  ModuleAnalysisManager *MAM);
};

/// Pass instrumentation for `--time-trace` under the new pass manager.
///
/// Provides the pass-instrumentation callbacks that measure the pass execution
/// time. They collect time tracing info by TimeProfiler.
class TimeProfilingPassesHandler {
public:
  /// Construct a time-profiling passes handler for this compilation.
  LLVM_ABI TimeProfilingPassesHandler();
  /// Deleted copy constructor; this handler is unique per compilation.
  /// \param Other Other handler (unused; copy is deleted).
  TimeProfilingPassesHandler(const TimeProfilingPassesHandler &Other) = delete;
  /// Deleted copy assignment; this handler is unique per compilation.
  /// \param Other Other handler (unused; assignment is deleted).
  void operator=(const TimeProfilingPassesHandler &Other) = delete;

  /// Register time-trace before/after pass callbacks on \p PIC.
  /// \param PIC Pass instrumentation callbacks to register with.
  LLVM_ABI void registerCallbacks(PassInstrumentationCallbacks &PIC);

private:
  // Implementation of pass instrumentation callbacks.
  void runBeforePass(StringRef PassID, IRUnitRef IR);
  void runAfterPass();
};

/// Holds transitions between basic blocks for dot-CFG change reporting.
///
/// The transitions are contained in a map of values to names of basic blocks.
class DCData {
public:
  /// Fill the map with the transitions from basic block \p B.
  /// \param B Basic block whose successors are recorded.
  LLVM_ABI DCData(const BasicBlock &B);
  /// Fill the map with the transitions from machine basic block \p B.
  /// \param B Machine basic block whose successors are recorded.
  LLVM_ABI DCData(const MachineBasicBlock &B);

  /// Return an iterator to the names of the successor blocks.
  /// \return Iterator to the first successor entry.
  StringMap<std::string>::const_iterator begin() const {
    return Successors.begin();
  }
  /// Return the end iterator for successor block names.
  /// \return Past-the-end iterator for the successors map.
  StringMap<std::string>::const_iterator end() const {
    return Successors.end();
  }

  /// Return the label of the basic block reached on a transition on \p S.
  /// \param S Successor key / transition value.
  /// \return Label of the successor basic block.
  StringRef getSuccessorLabel(StringRef S) const {
    assert(Successors.count(S) == 1 && "Expected to find successor.");
    return Successors.find(S)->getValue();
  }

protected:
  /// Add a transition to \p Succ on \p Label.
  /// \param Succ Successor block name.
  /// \param Label Transition label / value.
  void addSuccessorLabel(StringRef Succ, StringRef Label) {
    std::pair<std::string, std::string> SS{Succ.str(), Label.str()};
    Successors.insert(SS);
  }

  /// Map from transition value to successor basic-block name.
  StringMap<std::string> Successors;
};

/// Builds a website with PDF CFGs highlighting changed instructions.
///
/// Changed instructions are shown in colour in the generated dot control-flow
/// graphs.
class LLVM_ABI DotCfgChangeReporter : public ChangeReporter<IRDataT<DCData>> {
public:
  /// Construct a dot-CFG change reporter with the given verbosity.
  /// \param Verbose When true, report every interesting event.
  DotCfgChangeReporter(bool Verbose);
  /// Destroy this reporter and finalize any open HTML output.
  ~DotCfgChangeReporter() override;
  /// Register dot-CFG change-reporting callbacks on \p PIC.
  /// \param PIC Pass instrumentation callbacks to register with.
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

protected:
  /// Initialize the HTML file and output the header.
  /// \return True on success.
  bool initializeHTML();

  /// Handle the first IR unit by generating the initial CFG site content.
  /// \param IR First IR unit seen.
  void handleInitialIR(IRUnitRef IR) override;
  /// Capture IR representation of \p IR for \p PassID into \p Output.
  /// \param IR IR unit to capture.
  /// \param PassID Stable identifier of the pass.
  /// \param Output Destination IR data with DCData payloads.
  void generateIRRepresentation(IRUnitRef IR, StringRef PassID,
                                IRDataT<DCData> &Output) override;
  /// Report that the pass is not interesting / IR did not change.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  void omitAfter(StringRef PassID, std::string &Name) override;
  /// Emit HTML/PDF artifacts when interesting IR has changed.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  /// \param Before IR data before the pass.
  /// \param After IR data after the pass.
  /// \param IR IR unit after the pass (unused).
  void handleAfter(StringRef PassID, std::string &Name,
                   const IRDataT<DCData> &Before, const IRDataT<DCData> &After,
                   IRUnitRef IR) override;
  /// Record that an interesting pass was invalidated.
  /// \param PassID Stable identifier of the pass.
  void handleInvalidated(StringRef PassID) override;
  /// Record that the IR or pass is not interesting.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  void handleFiltered(StringRef PassID, std::string &Name) override;
  /// Record that an ignored pass was encountered.
  /// \param PassID Stable identifier of the pass.
  /// \param Name Display name of the IR unit.
  void handleIgnored(StringRef PassID, std::string &Name) override;

  /// Generate a PDF from \p DotFile and return an HTML link with \p Text.
  ///
  /// The PDF is written as \p PDFFileName under the configured output directory.
  /// \param Text Link text for the HTML anchor.
  /// \param DotFile Path to the input dot file.
  /// \param PDFFileName Output PDF file name.
  /// \return HTML anchor string linking to the generated PDF.
  static std::string genHTML(StringRef Text, StringRef DotFile,
                             StringRef PDFFileName);

  /// Compare one function's CFG data and emit corresponding HTML/PDF output.
  /// \param Name Function name.
  /// \param Prefix Banner prefix text.
  /// \param PassID Stable identifier of the pass.
  /// \param Divider Banner divider string.
  /// \param InModule True when this function is part of a module compare.
  /// \param Minor Minor section index for banners.
  /// \param Before Function data before the pass.
  /// \param After Function data after the pass.
  void handleFunctionCompare(StringRef Name, StringRef Prefix, StringRef PassID,
                             StringRef Divider, bool InModule, unsigned Minor,
                             const FuncDataT<DCData> &Before,
                             const FuncDataT<DCData> &After);

  /// Monotonic index used when naming generated HTML/PDF artifacts.
  unsigned N = 0;
  /// Output stream for the generated HTML change-report site.
  std::unique_ptr<raw_fd_ostream> HTML;
};

/// Instrumentation that dumps the last IR seen when the process crashes.
class PrintCrashIRInstrumentation {
public:
  /// Construct crash IR instrumentation with an unknown-IR placeholder.
  PrintCrashIRInstrumentation()
      : SavedIR("*** Dump of IR Before Last Pass Unknown ***") {}
  /// Unregister the crash handler and destroy this instrumentation.
  LLVM_ABI ~PrintCrashIRInstrumentation();
  /// Register callbacks that save IR before each pass for crash dumps.
  /// \param PIC Pass instrumentation callbacks to register with.
  LLVM_ABI void registerCallbacks(PassInstrumentationCallbacks &PIC);
  /// Print the saved IR to the configured crash-report destination.
  LLVM_ABI void reportCrashIR();

protected:
  /// Textual IR captured before the most recent pass.
  std::string SavedIR;

private:
  // The crash reporter that will report on a crash.
  static PrintCrashIRInstrumentation *CrashReporter;
  // Crash handler registered when print-on-crash is specified.
  static void SignalHandler(void *);
};

/// This class provides an interface to register all the standard pass
/// instrumentations and manages their state (if any).
class StandardInstrumentations {
  PrintIRInstrumentation PrintIR;
  PrintPassInstrumentation PrintPass;
  TimePassesHandler TimePasses;
  TimeProfilingPassesHandler TimeProfilingPasses;
  OptNoneInstrumentation OptNone;
  OptPassGateInstrumentation OptPassGate;
  PreservedCFGCheckerInstrumentation PreservedCFGChecker;
  IRChangedPrinter PrintChangedIR;
  PseudoProbeVerifier PseudoProbeVerification;
  InLineChangePrinter PrintChangedDiff;
  DotCfgChangeReporter WebsiteChangeReporter;
  PrintCrashIRInstrumentation PrintCrashIR;
  IRChangedTester ChangeTester;
  VerifyInstrumentation Verify;
  DroppedVariableStatsIR DroppedStatsIR;

  bool VerifyEach;

public:
  /// Construct the standard instrumentation suite for \p Context.
  /// \param Context LLVM context used by gated/optnone instrumentations.
  /// \param DebugLogging Enable debug logging in applicable instrumentations.
  /// \param VerifyEach When true, verify IR after each pass.
  /// \param PrintPassOpts Options for PrintPassInstrumentation.
  LLVM_ABI
  StandardInstrumentations(LLVMContext &Context, bool DebugLogging,
                           bool VerifyEach = false,
                           PrintPassOptions PrintPassOpts = PrintPassOptions());

  /// Register all standard instrumentation callbacks on \p PIC.
  ///
  /// If \p MAM is nullptr then PreservedCFGChecker is not enabled.
  /// \param PIC Pass instrumentation callbacks to register with.
  /// \param MAM Optional module analysis manager for CFG checking.
  LLVM_ABI void registerCallbacks(PassInstrumentationCallbacks &PIC,
                                  ModuleAnalysisManager *MAM = nullptr);

  /// Return the handler that records pass timing information.
  /// \return Reference to the TimePassesHandler.
  TimePassesHandler &getTimePasses() { return TimePasses; }
};

/// Per-block data saved to compare IR before and after a pass.
extern template class BlockDataT<EmptyData>;
/// Per-function block map saved for comparing functions.
extern template class FuncDataT<EmptyData>;
/// Per-module (or IR-unit) function map saved for comparing IRs.
extern template class IRDataT<EmptyData>;
/// Compares two IR snapshots and reports per-function differences.
extern template class IRComparer<EmptyData>;

} // namespace llvm

#endif
