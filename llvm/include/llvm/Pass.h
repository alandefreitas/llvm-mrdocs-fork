//===- llvm/Pass.h - Base class for Passes ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a base class that indicates that a specified class is a
// transformation pass implementation.
//
// Passes are designed this way so that it is possible to run passes in a cache
// and organizationally optimal order without having to specify it at the front
// end.  This allows arbitrary passes to be strung together and have them
// executed as efficiently as possible.
//
// Passes should extend one of the classes below, depending on the guarantees
// that it can make about what will be modified as it is run.  For example, most
// global optimizations should derive from FunctionPass, because they do not add
// or delete functions, they operate on the internals of the function.
//
// Note that this file #includes PassSupport.h and PassAnalysisSupport.h (at the
// bottom), so the APIs exposed by these files are also automatically available
// to all users of this file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASS_H
#define LLVM_PASS_H

#ifdef EXPENSIVE_CHECKS
#include <cstdint>
#endif
#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {

/// Interface used by Pass objects to pull analysis info from the pass manager.
class AnalysisResolver;
/// Describes which analyses a pass requires and preserves.
class AnalysisUsage;
class Function;
class ImmutablePass;
class Module;
class PassInfo;
class PMDataManager;
class PMStack;
class raw_ostream;
class StringRef;

/// Opaque identity key for a pass, typically the address of its static ID.
using AnalysisID = const void *;

/// Identifies which internal pass-manager stack level owns a pass.
///
/// External pass managers (PassManager and FunctionPassManager) are not
/// represented here. Ordering of pass manager types is important here.
enum PassManagerType {
  /// Unspecified or unrecognized pass manager type.
  PMT_Unknown = 0,
  PMT_ModulePassManager = 1, ///< MPPassManager
  PMT_CallGraphPassManager,  ///< CGPassManager
  PMT_FunctionPassManager,   ///< FPPassManager
  PMT_LoopPassManager,       ///< LPPassManager
  PMT_RegionPassManager,     ///< RGPassManager
  /// Sentinel one past the last valid pass manager type.
  PMT_Last
};

/// Classifies a pass by the IR unit or manager it operates on.
enum PassKind {
  PT_Region,       ///< Region-scoped pass.
  PT_Loop,         ///< Loop-scoped pass.
  PT_Function,     ///< Function-scoped pass.
  PT_CallGraphSCC, ///< Call-graph SCC pass.
  PT_Module,       ///< Module-scoped pass.
  PT_PassManager   ///< Nested pass manager.
};

/// This enumerates the LLVM full LTO or ThinLTO optimization phases.
enum class ThinOrFullLTOPhase {
  /// No LTO/ThinLTO behavior needed.
  None,
  /// ThinLTO prelink (summary) phase.
  ThinLTOPreLink,
  /// ThinLTO postlink (backend compile) phase.
  ThinLTOPostLink,
  /// Full LTO prelink phase.
  FullLTOPreLink,
  /// Full LTO postlink (backend compile) phase.
  FullLTOPostLink
};

#ifndef NDEBUG
/// Convert an LTO phase enum value to a printable name.
/// @param Phase The ThinOrFullLTOPhase value to stringify.
/// @return A C string naming \p Phase.
const char *to_string(ThinOrFullLTOPhase Phase);
#endif

//===----------------------------------------------------------------------===//
/// Base interface implemented by every LLVM pass.
///
/// Subclass this if you are an interprocedural optimization or you do not fit
/// into any of the more constrained passes described below.
///
class LLVM_ABI Pass {
  AnalysisResolver *Resolver = nullptr;  // Used to resolve analysis
  const void *PassID;
  PassKind Kind;

public:
  /// Construct a pass of kind \p K whose identity is \p pid.
  /// @param K The PassKind classifying this pass.
  /// @param pid Static registration token that uniquely identifies the pass.
  explicit Pass(PassKind K, char &pid) : PassID(&pid), Kind(K) {}
  /// Passes are not copyable.
  /// @param Other Unused; copy construction is deleted.
  Pass(const Pass &Other) = delete;
  /// Passes are not assignable.
  /// @param Other Unused; copy assignment is deleted.
  Pass &operator=(const Pass &Other) = delete;
  /// Destroy the pass and release any associated resources.
  virtual ~Pass();

  /// Return the kind of pass (module, function, loop, etc.).
  /// @return The PassKind classifying this pass.
  PassKind getPassKind() const { return Kind; }

  /// Return a human-readable name for this pass.
  ///
  /// This is usually implemented in terms of the name that is registered by
  /// one of the Registration templates, but can be overloaded directly.
  /// @return A human-readable name for this pass.
  virtual StringRef getPassName() const;

  /// Return a nice clean name for a pass
  /// corresponding to that used to enable the pass in opt.
  /// @return The command-line argument string used to enable this pass.
  StringRef getPassArgument() const;

  /// getPassID - Return the PassID number that corresponds to this pass.
  /// @return The AnalysisID that uniquely identifies this pass.
  AnalysisID getPassID() const {
    return PassID;
  }

  /// Perform any initialization needed before any pass is run.
  /// @param M The module being processed.
  /// @return True if the module was modified; false otherwise.
  virtual bool doInitialization(Module &M)  { return false; }

  /// Perform any cleanup needed after all passes have run.
  /// @param M The module being processed.
  /// @return True if the module was modified; false otherwise.
  virtual bool doFinalization(Module &M) { return false; }

  /// Print the internal state of this pass for analysis dumping.
  ///
  /// This is called by Analyze to print out the contents of an analysis.
  /// Otherwise it is not necessary to implement this method. Beware that the
  /// module pointer MAY be null. This automatically forwards to a virtual
  /// function that does not provide the Module* in case the analysis doesn't
  /// need it it can just be ignored.
  /// @param OS Stream to write the pass state to.
  /// @param M Module being analyzed, which may be null.
  virtual void print(raw_ostream &OS, const Module *M) const;

  /// Print this pass's state to stderr for debugging.
  void dump() const;

  /// Get a Pass appropriate to print the IR this pass operates on.
  /// @param OS Stream the printer pass should write to.
  /// @param Banner Banner text prefixed to printed IR.
  /// @return A new printer pass for the IR unit this pass operates on.
  virtual Pass *createPrinterPass(raw_ostream &OS,
                                  const std::string &Banner) const = 0;

  /// Assign a suitable pass manager from \p PMS to this pass.
  ///
  /// Each pass is responsible for assigning a pass manager to itself. PMS is
  /// the stack of available pass managers.
  /// @param PMS Stack of available pass managers.
  /// @param PreferredType Preferred PassManagerType for this pass.
  virtual void assignPassManager(PMStack &PMS,
                                 PassManagerType PreferredType) {}

  /// Check whether available pass managers are suitable for this pass.
  /// @param PMS Stack of available pass managers to inspect.
  virtual void preparePassManager(PMStack &PMS);

  ///  Return what kind of Pass Manager can manage this pass.
  /// @return The PassManagerType suitable for managing this pass.
  virtual PassManagerType getPotentialPassManagerType() const;

  /// Install the analysis resolver used to satisfy this pass's dependencies.
  /// @param AR Resolver that looks up available analysis results.
  void setResolver(AnalysisResolver *AR);
  /// Return the analysis resolver installed for this pass.
  /// @return The AnalysisResolver for this pass, or null if none is set.
  AnalysisResolver *getResolver() const { return Resolver; }

  /// Declare which analyses this pass requires and preserves.
  ///
  /// Override this if the pass needs analysis information to do its job. If a
  /// pass specifies that it uses a particular analysis result here, it can then
  /// use getAnalysis<AnalysisType>() below.
  /// @param AU Usage object filled with required and preserved analyses.
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

  /// Release memory held by this pass when it is no longer needed.
  ///
  /// The default behavior of passes is to hold onto memory for the entire
  /// duration of their lifetime (which is the entire compile time). For
  /// pipelined passes, this is not a big deal because that memory gets recycled
  /// every time the pass is invoked on another program unit. For IP passes, it
  /// is more important to free memory when it is unused.
  ///
  /// Optionally implement this function to release pass memory when it is no
  /// longer used.
  virtual void releaseMemory();

  /// Return this pass as an ImmutablePass, or null if it is not one.
  /// @return This pass cast to ImmutablePass, or null if it is not one.
  virtual ImmutablePass *getAsImmutablePass();
  /// Return this pass as a PMDataManager when it is a nested pass manager.
  /// @return This pass cast to PMDataManager, or null if it is not one.
  virtual PMDataManager *getAsPMDataManager();

  /// verifyAnalysis() - This member can be implemented by a analysis pass to
  /// check state of analysis information.
  virtual void verifyAnalysis() const;

  /// Print the pass nesting structure for -debug-passes=PassStructure.
  /// @param Offset Indentation depth used when printing nested managers.
  virtual void dumpPassStructure(unsigned Offset = 0);

  /// Look up registered pass metadata by type identity.
  /// @param TI Type-info pointer identifying the pass class.
  /// @return The PassInfo for that class, or null if it is not known.
  static const PassInfo *lookupPassInfo(const void *TI);

  /// Look up registered pass metadata by command-line argument.
  /// @param Arg Command-line argument that selects the pass.
  /// @return The PassInfo for that argument, or null if it is not known.
  static const PassInfo *lookupPassInfo(StringRef Arg);

  /// Construct a new instance of the pass identified by \p ID.
  /// @param ID Analysis identity of the pass to create.
  /// @return A new Pass instance, or null if \p ID is not known.
  static Pass *createPass(AnalysisID ID);

  /// Get an optional analysis result that may already be available.
  ///
  /// Subclasses use this to get analysis information that might be around, for
  /// example to update it. This is different than getAnalysis in that it can
  /// fail (if the analysis results haven't been computed), so should only be
  /// used if you can handle the case when the analysis is not available. This
  /// method is often used by transformation APIs to update analysis results for
  /// a pass automatically as the transform is performed.
  /// @return A pointer to the analysis result, or null if unavailable.
  template<typename AnalysisType> AnalysisType *
    getAnalysisIfAvailable() const; // Defined in PassAnalysisSupport.h

  /// Report whether the analysis identified by \p AID must be preserved.
  ///
  /// This serves the same function as getAnalysisIfAvailable, but works if you
  /// just have an AnalysisID. This obviously cannot give you a properly typed
  /// instance of the class if you don't have the class name available (use
  /// getAnalysisIfAvailable if you do), but it can tell you if you need to
  /// preserve the pass at least.
  /// @param AID Registration token identifying the analysis pass.
  /// @return True if the analysis identified by \p AID must be preserved.
  bool mustPreserveAnalysisID(char &AID) const;

  /// Get a required analysis result claimed via getAnalysisUsage.
  ///
  /// Subclasses use this to access analysis information that they claim to use
  /// by overriding getAnalysisUsage.
  /// @return A reference to the required analysis result.
  template<typename AnalysisType>
  AnalysisType &getAnalysis() const; // Defined in PassAnalysisSupport.h

  /// Like getAnalysis(), but runs the analysis on function \p F.
  /// If \p Changed is non-null, it is set when the analysis modified \p F.
  /// @param F Function to obtain the analysis result for.
  /// @param Changed Optional out-parameter set when the analysis modified \p F.
  /// @return A reference to the analysis result for \p F.
  template <typename AnalysisType>
  AnalysisType &
  getAnalysis(Function &F,
              bool *Changed = nullptr); // Defined in PassAnalysisSupport.h

  /// Get a required analysis result using an explicit analysis identity.
  /// @param PI Analysis identity of the required analysis.
  /// @return A reference to the analysis result identified by \p PI.
  template<typename AnalysisType>
  AnalysisType &getAnalysisID(AnalysisID PI) const;

  /// Get a required analysis result for \p F using an explicit analysis identity.
  /// @param PI Analysis identity of the required analysis.
  /// @param F Function to obtain the analysis result for.
  /// @param Changed Optional out-parameter set when the analysis modified \p F.
  /// @return A reference to the analysis result for \p F identified by \p PI.
  template <typename AnalysisType>
  AnalysisType &getAnalysisID(AnalysisID PI, Function &F,
                              bool *Changed = nullptr);

#ifdef EXPENSIVE_CHECKS
  /// Hash a module in order to detect when a module (or more specific) pass has
  /// modified it.
  uint64_t structuralHash(Module &M) const;

  /// Hash a function in order to detect when a function (or more specific) pass
  /// has modified it.
  virtual uint64_t structuralHash(Function &F) const;
#endif
};

//===----------------------------------------------------------------------===//
/// Pass that may inspect or transform an entire module freely.
///
/// ModulePasses may do anything they want to the program and are used to
/// implement unstructured interprocedural optimizations and analyses.
///
class LLVM_ABI ModulePass : public Pass {
public:
  /// Construct a module pass whose identity is \p pid.
  /// @param pid Static registration token that uniquely identifies the pass.
  explicit ModulePass(char &pid) : Pass(PT_Module, pid) {}

  /// Destroy the module pass.
  ~ModulePass() override;

  /// createPrinterPass - Get a module printer pass.
  /// @param OS Stream the printer pass should write to.
  /// @param Banner Banner text prefixed to printed IR.
  /// @return A new module printer pass.
  Pass *createPrinterPass(raw_ostream &OS,
                          const std::string &Banner) const override;

  /// runOnModule - Virtual method overriden by subclasses to process the module
  /// being operated on.
  /// @param M The module being processed.
  /// @return True if the module was modified; false otherwise.
  virtual bool runOnModule(Module &M) = 0;

  /// Find or create a module pass manager on \p PMS and register this pass.
  /// @param PMS Stack of available pass managers.
  /// @param T Preferred PassManagerType for this pass.
  void assignPassManager(PMStack &PMS, PassManagerType T) override;

  ///  Return what kind of Pass Manager can manage this pass.
  /// @return The PassManagerType suitable for managing this pass.
  PassManagerType getPotentialPassManagerType() const override;

protected:
  /// Optional passes call this function to check whether the pass should be
  /// skipped. This is the case when optimization bisect is over the limit.
  /// @param M Module used to decide whether this pass should be skipped.
  /// @return True if this pass should be skipped for \p M.
  bool skipModule(const Module &M) const;
};

//===----------------------------------------------------------------------===//
/// ImmutablePass class - This class is used to provide information that does
/// not need to be run.  This is useful for things like target information.
///
class LLVM_ABI ImmutablePass : public ModulePass {
public:
  /// Construct an immutable pass whose identity is \p pid, the static
  /// registration token produced by the pass registration macros.
  /// @param pid Static registration token that uniquely identifies the pass.
  explicit ImmutablePass(char &pid) : ModulePass(pid) {}

  /// Destroy an immutable pass.
  ~ImmutablePass() override;

  /// Perform initialization for this immutable pass.
  ///
  /// This method may be overriden by immutable passes to allow them to perform
  /// various initialization actions they require. This is primarily because an
  /// ImmutablePass can "require" another ImmutablePass, and if it does, the
  /// overloaded version of initializePass may get access to these passes with
  /// getAnalysis<>.
  virtual void initializePass();

  /// Return this pass as an ImmutablePass.
  /// @return This immutable pass.
  ImmutablePass *getAsImmutablePass() override { return this; }

  /// ImmutablePasses are never run.
  /// @param M Unused; immutable passes do not process the module.
  /// @return Always false; immutable passes do not modify the module.
  bool runOnModule(Module &M) override { return false; }
};

//===----------------------------------------------------------------------===//
/// FunctionPass class - This class is used to implement most global
/// optimizations.  Optimizations should subclass this class if they meet the
/// following constraints:
///
///  1. Optimizations are organized globally, i.e., a function at a time
///  2. Optimizing a function does not cause the addition or removal of any
///     functions in the module
///
class LLVM_ABI FunctionPass : public Pass {
public:
  /// Construct a function pass with the given pass ID.
  /// @param pid Static registration token that uniquely identifies the pass.
  explicit FunctionPass(char &pid) : Pass(PT_Function, pid) {}

  /// createPrinterPass - Get a function printer pass.
  /// @param OS Stream the printer pass should write to.
  /// @param Banner Banner text prefixed to printed IR.
  /// @return A new function printer pass.
  Pass *createPrinterPass(raw_ostream &OS,
                          const std::string &Banner) const override;

  /// runOnFunction - Virtual method overriden by subclasses to do the
  /// per-function processing of the pass.
  /// @param F The function being processed.
  /// @return True if the function was modified; false otherwise.
  virtual bool runOnFunction(Function &F) = 0;

  /// Serialize the IR unit this pass operates on for --print-changed.
  ///
  /// The default prints \p F; MachineFunctionPass prints its MachineFunction.
  /// Returns false if there is nothing to report.
  /// @param OS Stream to write the serialized IR to.
  /// @param F Function whose IR should be printed by default.
  /// @return True if IR was printed; false if there is nothing to report.
  virtual bool printIRUnit(raw_ostream &OS, Function &F);

  /// Find or create a \c FunctionPassManager on \p PMS and register this pass.
  /// @param PMS Stack of available pass managers.
  /// @param T Preferred PassManagerType for this pass.
  void assignPassManager(PMStack &PMS, PassManagerType T) override;

  ///  Return what kind of Pass Manager can manage this pass.
  /// @return The PassManagerType suitable for managing this pass.
  PassManagerType getPotentialPassManagerType() const override;

protected:
  /// Check whether this optional pass should be skipped for \p F.
  ///
  /// This is the case when Attribute::OptimizeNone is set or when optimization
  /// bisect is over the limit.
  /// @param F Function used to decide whether this pass should be skipped.
  /// @return True if this pass should be skipped for \p F.
  bool skipFunction(const Function &F) const;
};

} // end namespace llvm

// Include support files that contain important APIs commonly used by Passes,
// but that we want to separate out to make it easier to read the header files.
#include "llvm/PassAnalysisSupport.h"
#include "llvm/PassSupport.h"

#endif // LLVM_PASS_H
