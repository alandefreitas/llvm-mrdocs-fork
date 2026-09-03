//===- llvm/LLVMContext.h - Class for managing "global" state ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares LLVMContext, a container of "global" state in LLVM, such
// as the global type and constant uniquing tables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_LLVMCONTEXT_H
#define LLVM_IR_LLVMCONTEXT_H

#include "llvm-c/Types.h"
#include "llvm/IR/DiagnosticHandler.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace llvm {

class DiagnosticInfo;
enum DiagnosticSeverity : char;
class Function;
class Instruction;
/// Private implementation details of \c LLVMContext.
class LLVMContextImpl;
class Module;
class OptPassGate;
template <typename T> class SmallVectorImpl;
template <typename T> class StringMapEntry;
class StringRef;
class Twine;
/// Streams LLVM remark diagnostics from IR and MIR passes.
class LLVMRemarkStreamer;

/// Interfaces for emitting and serializing optimization remarks.
namespace remarks {
class RemarkStreamer;
}

/// Synchronization scope names and IDs for atomic memory operations.
namespace SyncScope {

/// Opaque synchronization scope identifier type.
typedef uint8_t ID;

/// Known synchronization scope IDs with fixed numeric values.
///
/// All synchronization scope IDs that LLVM has special knowledge of are listed
/// here. Additionally, this scheme allows LLVM to efficiently check for
/// specific synchronization scope ID without comparing strings.
enum {
  /// Synchronized with respect to signal handlers executing in the same thread.
  SingleThread = 0,

  /// Synchronized with respect to all concurrently executing threads.
  System = 1
};

} // end namespace SyncScope

/// This is an important class for using LLVM in a threaded context.  It
/// (opaquely) owns and manages the core "global" data of LLVM's core
/// infrastructure, including the type and constant uniquing tables.
/// LLVMContext itself provides no locking guarantees, so you should be careful
/// to have one context per thread.
class LLVMContext {
public:
  /// Pointer to the private implementation of this context.
  LLVMContextImpl *const pImpl;
  /// Create a new LLVM context.
  LLVM_ABI LLVMContext();
  /// Copy construction is deleted; LLVMContext is non-copyable.
  /// \param Other Unused; copy construction is deleted.
  LLVMContext(const LLVMContext &Other) = delete;
  /// Copy assignment is deleted; LLVMContext is non-copyable.
  /// \param Other Unused; copy assignment is deleted.
  LLVMContext &operator=(const LLVMContext &Other) = delete;
  /// Destroy this context and release all associated global state.
  LLVM_ABI ~LLVMContext();

  // Pinned metadata names, which always have the same value.  This is a
  // compile-time performance optimization, not a correctness optimization.
  /// Fixed metadata kind IDs that are always available in every context.
  enum : unsigned {
#define LLVM_FIXED_MD_KIND(EnumID, Name, Value) EnumID = Value,
#include "llvm/IR/FixedMetadataKinds.def"
#undef LLVM_FIXED_MD_KIND
  };

  /// Known operand bundle tag IDs with fixed numeric values.
  ///
  /// All operand bundle tags that LLVM has special knowledge of are listed
  /// here. Additionally, this scheme allows LLVM to efficiently check for
  /// specific operand bundle tags without comparing strings. Keep this in sync
  /// with LLVMContext::LLVMContext().
  enum : unsigned {
#define ATTR(Name, Str) OB_##Name,
#include "llvm/IR/BundleAttributes.def"
    OB_deopt,                  ///< Operand bundle tag for \c "deopt".
    OB_funclet,                ///< Operand bundle tag for \c "funclet".
    OB_gc_transition,          ///< Operand bundle tag for \c "gc-transition".
    OB_cfguardtarget,          ///< Operand bundle tag for \c "cfguardtarget".
    OB_preallocated,           ///< Operand bundle tag for \c "preallocated".
    OB_gc_live,                ///< Operand bundle tag for \c "gc-live".
    OB_clang_arc_attachedcall, ///< Operand bundle tag for \c "clang.arc.attachedcall".
    OB_ptrauth,                ///< Operand bundle tag for \c "ptrauth".
    OB_kcfi,                   ///< Operand bundle tag for \c "kcfi".
    /// Operand bundle tag for \c "convergencectrl" tokens on convergent calls.
    OB_convergencectrl,        // "convergencectrl"
    OB_deactivation_symbol,    // "deactivation-symbol"
    OB_LastBundleID = OB_deactivation_symbol
  };

  /// Return a unique non-zero ID for the specified metadata kind.
  ///
  /// This ID is uniqued across modules in the current LLVMContext.
  /// \param Name The metadata kind name to look up or create.
  /// \return The unique metadata kind ID for \p Name.
  LLVM_ABI unsigned getMDKindID(StringRef Name) const;

  /// Populate \p Result with the names of custom metadata IDs in this context.
  /// \param Result Output vector filled with metadata kind names.
  LLVM_ABI void getMDKindNames(SmallVectorImpl<StringRef> &Result) const;

  /// Populate \p Result with the operand bundle tags registered in this
  /// context.
  ///
  /// The bundle tags are ordered by increasing bundle IDs.
  /// \param Result Output vector filled with bundle tag names.
  /// \see LLVMContext::getOperandBundleTagID
  LLVM_ABI void getOperandBundleTags(SmallVectorImpl<StringRef> &Result) const;

  /// Return the tag entry to use for an operand bundle named \p TagName.
  /// \param TagName The operand bundle tag name to look up or insert.
  /// \return The string-map entry for the bundle tag.
  LLVM_ABI StringMapEntry<uint32_t> *
  getOrInsertBundleTag(StringRef TagName) const;

  /// Map a bundle tag name to its integer ID.
  ///
  /// Every bundle tag registered with an LLVMContext has a unique ID.
  /// \param Tag The bundle tag name to map.
  /// \return The integer ID for \p Tag.
  LLVM_ABI uint32_t getOperandBundleTagID(StringRef Tag) const;

  /// Map a synchronization scope name to its synchronization scope ID.
  ///
  /// Every synchronization scope registered with LLVMContext has a unique ID
  /// except the pre-defined ones.
  /// \param SSN The synchronization scope name to look up or insert.
  /// \return The synchronization scope ID for \p SSN.
  LLVM_ABI SyncScope::ID getOrInsertSyncScopeID(StringRef SSN);

  /// Populate \p SSNs with synchronization scope names registered in this
  /// context.
  ///
  /// Synchronization scope names are ordered by increasing synchronization
  /// scope IDs.
  /// \param SSNs Output vector filled with synchronization scope names.
  LLVM_ABI void getSyncScopeNames(SmallVectorImpl<StringRef> &SSNs) const;

  /// Return the name of a SyncScope::ID registered with LLVMContext, if any.
  /// \param Id The synchronization scope ID to look up.
  /// \return The synchronization scope name, or \c std::nullopt if unknown.
  LLVM_ABI std::optional<StringRef> getSyncScopeName(SyncScope::ID Id) const;

  /// Define the GC for a function.
  /// \param Fn The function whose GC strategy to set.
  /// \param GCName The name of the garbage collector strategy.
  LLVM_ABI void setGC(const Function &Fn, std::string GCName);

  /// Return the GC for a function.
  /// \param Fn The function whose GC strategy to query.
  /// \return The garbage collector strategy name for \p Fn.
  LLVM_ABI const std::string &getGC(const Function &Fn);

  /// Remove the GC for a function.
  /// \param Fn The function whose GC strategy to clear.
  LLVM_ABI void deleteGC(const Function &Fn);

  /// Return true if the Context runtime configuration is set to discard all
  /// value names. When true, only GlobalValue names will be available in the
  /// IR.
  /// \return True if non-GlobalValue value names are discarded.
  LLVM_ABI bool shouldDiscardValueNames() const;

  /// Configure whether non-GlobalValue value names are discarded.
  ///
  /// Clients can use this flag to save memory and runtime, especially in
  /// release mode.
  /// \param Discard If true, discard value names except for GlobalValue.
  LLVM_ABI void setDiscardValueNames(bool Discard);

  /// Whether there is a string map for uniquing debug info
  /// identifiers across the context.  Off by default.
  /// \return True if ODR uniquing of debug types is enabled.
  LLVM_ABI bool isODRUniquingDebugTypes() const;
  /// Enable ODR-based uniquing of debug type identifiers in this context.
  LLVM_ABI void enableDebugTypeODRUniquing();
  /// Disable ODR-based uniquing of debug type identifiers in this context.
  LLVM_ABI void disableDebugTypeODRUniquing();

  /// Get a unique number for a MachineFunction associated with \p F.
  /// \param F The function that needs a MachineFunction number.
  /// \return A unique MachineFunction number for \p F.
  LLVM_ABI unsigned generateMachineFunctionNum(Function &F);

  /// Defines the type of a yield callback.
  /// \see LLVMContext::setYieldCallback.
  using YieldCallbackTy = void (*)(LLVMContext *Context, void *OpaqueHandle);

  /// Set a diagnostic handler callback invoked when the backend reports to the
  /// user.
  ///
  /// The first argument is a function pointer and the second is a context
  /// pointer that gets passed into the DiagHandler. The third argument should
  /// be set to true if the handler only expects enabled diagnostics.
  ///
  /// LLVMContext doesn't take ownership or interpret either of these
  /// pointers.
  /// \param DiagHandler Callback invoked for each diagnostic.
  /// \param DiagContext Opaque context pointer passed to \p DiagHandler.
  /// \param RespectFilters If true, only enabled diagnostics are delivered.
  LLVM_ABI void setDiagnosticHandlerCallBack(
      DiagnosticHandler::DiagnosticHandlerTy DiagHandler,
      void *DiagContext = nullptr, bool RespectFilters = false);

  /// Install a DiagnosticHandler object for custom diagnostic handling.
  ///
  /// The first argument is a unique_ptr of an object of type DiagnosticHandler
  /// or a derived class. The second argument should be set to true if the
  /// handler only expects enabled diagnostics.
  ///
  /// Ownership of this pointer is moved to LLVMContextImpl.
  /// \param DH The diagnostic handler to take ownership of.
  /// \param RespectFilters If true, only enabled diagnostics are delivered.
  LLVM_ABI void setDiagnosticHandler(std::unique_ptr<DiagnosticHandler> &&DH,
                                     bool RespectFilters = false);

  /// getDiagnosticHandlerCallBack - Return the diagnostic handler call back set by
  /// setDiagnosticHandlerCallBack.
  /// \return The diagnostic handler callback function pointer.
  LLVM_ABI DiagnosticHandler::DiagnosticHandlerTy
  getDiagnosticHandlerCallBack() const;

  /// getDiagnosticContext - Return the diagnostic context set by
  /// setDiagnosticContext.
  /// \return The opaque diagnostic context pointer.
  LLVM_ABI void *getDiagnosticContext() const;

  /// getDiagHandlerPtr - Returns const raw pointer of DiagnosticHandler set by
  /// setDiagnosticHandler.
  /// \return A const pointer to the installed diagnostic handler.
  LLVM_ABI const DiagnosticHandler *getDiagHandlerPtr() const;

  /// getDiagnosticHandler - transfers ownership of DiagnosticHandler unique_ptr
  /// to caller.
  /// \return The diagnostic handler, transferring ownership to the caller.
  LLVM_ABI std::unique_ptr<DiagnosticHandler> getDiagnosticHandler();

  /// Return if a code hotness metric should be included in optimization
  /// diagnostics.
  /// \return True if hotness should be included in optimization diagnostics.
  LLVM_ABI bool getDiagnosticsHotnessRequested() const;
  /// Set if a code hotness metric should be included in optimization
  /// diagnostics.
  /// \param Requested Whether hotness should be included in diagnostics.
  LLVM_ABI void setDiagnosticsHotnessRequested(bool Requested);

  /// Return whether misexpect (branch-weight mismatch) warnings are enabled.
  /// \return True if misexpect warnings are requested.
  LLVM_ABI bool getMisExpectWarningRequested() const;
  /// Enable or disable misexpect (branch-weight mismatch) warnings.
  /// \param Requested Whether misexpect warnings should be emitted.
  LLVM_ABI void setMisExpectWarningRequested(bool Requested);
  /// Set the tolerance used when diagnosing misexpect warnings.
  /// \param Tolerance Optional misexpect tolerance percentage; none to clear.
  LLVM_ABI void
  setDiagnosticsMisExpectTolerance(std::optional<uint32_t> Tolerance);
  /// Return the tolerance used when diagnosing misexpect warnings.
  /// \return The misexpect tolerance percentage.
  LLVM_ABI uint32_t getDiagnosticsMisExpectTolerance() const;

  /// Return the minimum hotness value a diagnostic would need in order
  /// to be included in optimization diagnostics.
  ///
  /// Three possible return values:
  /// 0            - threshold is disabled. Everything will be printed out.
  /// positive int - threshold is set.
  /// UINT64_MAX   - threshold is not yet set, and needs to be synced from
  ///                profile summary. Note that in case of missing profile
  ///                summary, threshold will be kept at "MAX", effectively
  ///                suppresses all remarks output.
  /// \return The hotness threshold, or UINT64_MAX when not yet set from PSI.
  LLVM_ABI uint64_t getDiagnosticsHotnessThreshold() const;

  /// Set the minimum hotness value a diagnostic needs in order to be
  /// included in optimization diagnostics.
  /// \param Threshold Optional hotness threshold; none to leave unset.
  LLVM_ABI void
  setDiagnosticsHotnessThreshold(std::optional<uint64_t> Threshold);

  /// Return if hotness threshold is requested from PSI.
  /// \return True if the hotness threshold should be synced from PSI.
  LLVM_ABI bool isDiagnosticsHotnessThresholdSetFromPSI() const;

  /// Return the main remark streamer used by specialized remark streamers.
  ///
  /// This streamer keeps generic remark metadata in memory throughout the life
  /// of the LLVMContext. This metadata may be emitted in a section in object
  /// files depending on the format requirements.
  ///
  /// All specialized remark streamers should convert remarks to
  /// llvm::remarks::Remark and emit them through this streamer.
  /// \return The main remark streamer for this context, or nullptr if unset.
  LLVM_ABI remarks::RemarkStreamer *getMainRemarkStreamer();
  /// Const overload of \c getMainRemarkStreamer().
  /// \return The main remark streamer for this context, or nullptr if unset.
  LLVM_ABI const remarks::RemarkStreamer *getMainRemarkStreamer() const;
  /// Set the main remark streamer used by specialized remark streamers.
  /// \param MainRemarkStreamer The remark streamer to take ownership of.
  LLVM_ABI void setMainRemarkStreamer(
      std::unique_ptr<remarks::RemarkStreamer> MainRemarkStreamer);

  /// The "LLVM remark streamer" used by LLVM to serialize remark diagnostics
  /// comming from IR and MIR passes.
  ///
  /// If it does not exist, diagnostics are not saved in a file but only emitted
  /// via the diagnostic handler.
  /// \return The LLVM remark streamer, or nullptr if none is installed.
  LLVM_ABI LLVMRemarkStreamer *getLLVMRemarkStreamer();
  /// Const overload of \c getLLVMRemarkStreamer().
  /// \return The LLVM remark streamer, or nullptr if none is installed.
  LLVM_ABI const LLVMRemarkStreamer *getLLVMRemarkStreamer() const;
  /// Set the LLVM remark streamer used to serialize IR/MIR remark diagnostics.
  /// \param RemarkStreamer The LLVM remark streamer to take ownership of.
  LLVM_ABI void
  setLLVMRemarkStreamer(std::unique_ptr<LLVMRemarkStreamer> RemarkStreamer);

  /// Get the prefix that should be printed in front of a diagnostic of
  ///        the given \p Severity
  /// \param Severity The diagnostic severity that selects the prefix.
  /// \return A C string prefix such as "error: " for the given severity.
  LLVM_ABI static const char *
  getDiagnosticMessagePrefix(DiagnosticSeverity Severity);

  /// Report a message to the currently installed diagnostic handler.
  ///
  /// This function returns, in particular in the case of error reporting
  /// (DI.Severity == \a DS_Error), so the caller should leave the compilation
  /// process in a self-consistent state, even though the generated code
  /// need not be correct.
  ///
  /// The diagnostic message will be implicitly prefixed with a severity keyword
  /// according to \p DI.getSeverity(), i.e., "error: " for \a DS_Error,
  /// "warning: " for \a DS_Warning, and "note: " for \a DS_Note.
  /// \param DI The diagnostic information to report.
  LLVM_ABI void diagnose(const DiagnosticInfo &DI);

  /// Registers a yield callback with the given context.
  ///
  /// The yield callback function may be called by LLVM to transfer control back
  /// to the client that invoked the LLVM compilation. This can be used to yield
  /// control of the thread, or perform periodic work needed by the client.
  /// There is no guaranteed frequency at which callbacks must occur; in fact,
  /// the client is not guaranteed to ever receive this callback. It is at the
  /// sole discretion of LLVM to do so and only if it can guarantee that
  /// suspending the thread won't block any forward progress in other LLVM
  /// contexts in the same process.
  ///
  /// At a suspend point, the state of the current LLVM context is intentionally
  /// undefined. No assumptions about it can or should be made. Only LLVM
  /// context API calls that explicitly state that they can be used during a
  /// yield callback are allowed to be used. Any other API calls into the
  /// context are not supported until the yield callback function returns
  /// control to LLVM. Other LLVM contexts are unaffected by this restriction.
  /// \param Callback The yield callback to register, or nullptr to clear.
  /// \param OpaqueHandle Opaque client data passed to \p Callback.
  LLVM_ABI void setYieldCallback(YieldCallbackTy Callback, void *OpaqueHandle);

  /// Calls the yield callback (if applicable).
  ///
  /// This transfers control of the current thread back to the client, which may
  /// suspend the current thread. Only call this method when LLVM doesn't hold
  /// any global mutex or cannot block the execution in another LLVM context.
  LLVM_ABI void yield();

  /// Emit an error for instruction \p I with message \p ErrorStr.
  ///
  /// This function returns, so code should be prepared to drop the erroneous
  /// construct on the floor and "not crash". The generated code need not be
  /// correct. The error message will be implicitly prefixed with "error: " and
  /// should not end with a ".".
  /// \param I Optional instruction providing location information.
  /// \param ErrorStr The error message to emit.
  LLVM_ABI void emitError(const Instruction *I, const Twine &ErrorStr);
  /// Emit an error message with no associated instruction location.
  /// \param ErrorStr The error message to emit.
  LLVM_ABI void emitError(const Twine &ErrorStr);

  /// Access the object which can disable optional passes and individual
  /// optimizations at compile time.
  /// \return The optimization pass gate for this context.
  LLVM_ABI OptPassGate &getOptPassGate() const;

  /// Set the object which can disable optional passes and individual
  /// optimizations at compile time.
  ///
  /// The lifetime of the object must be guaranteed to extend as long as the
  /// LLVMContext is used by compilation.
  /// \param Gate The optimization pass gate to use.
  LLVM_ABI void setOptPassGate(OptPassGate &Gate);

  /// Get the current default target CPU (target-cpu function attribute).
  ///
  /// The intent is that compiler frontends will set this to a value that
  /// reflects the attribute that a function would get "by default" without any
  /// specific function attributes, and compiler passes will attach the
  /// attribute to newly created functions that are not associated with a
  /// particular function, such as global initializers.
  /// Function::createWithDefaultAttr() will create functions with this
  /// attribute. This function should only be called by passes that run at
  /// compile time and not by the backend or LTO passes.
  /// \return The current default target CPU name.
  LLVM_ABI StringRef getDefaultTargetCPU();
  /// Set the current default target CPU (target-cpu function attribute).
  /// \param CPU The default target CPU name to use for new functions.
  LLVM_ABI void setDefaultTargetCPU(StringRef CPU);

  /// Similar to {get,set}DefaultTargetCPU() but for default target-features.
  /// \return The current default target-features string.
  LLVM_ABI StringRef getDefaultTargetFeatures();
  /// Set the default target-features string for newly created functions.
  /// \param Features The default target-features string to use for new
  /// functions.
  LLVM_ABI void setDefaultTargetFeatures(StringRef Features);

  /// Key Instructions: update the highest number atom group emitted for any
  /// function.
  /// \param G The atom group number waterline to record.
  LLVM_ABI void updateDILocationAtomGroupWaterline(uint64_t G);

  /// Key Instructions: get the next free atom group number and increment
  /// the global tracker.
  /// \return The next free DI location atom group number.
  LLVM_ABI uint64_t incNextDILocationAtomGroup();

private:
  // Module needs access to the add/removeModule methods.
  friend class Module;

  /// addModule - Register a module as being instantiated in this context.  If
  /// the context is deleted, the module will be deleted as well.
  void addModule(Module*);

  /// removeModule - Unregister a module from this context.
  void removeModule(Module *);
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Opaque C API conversions for \c LLVMContext (see CBindingWrapping.h).
/// \param P The opaque C API context reference to unwrap.
/// \return The \c LLVMContext pointer corresponding to \p P.
inline LLVMContext *unwrap(LLVMContextRef P) {
  return reinterpret_cast<LLVMContext *>(P);
}

/// Wrap a \c LLVMContext pointer as an opaque C API reference.
/// \param P The context pointer to wrap.
/// \return The opaque C API context reference for \p P.
inline LLVMContextRef wrap(const LLVMContext *P) {
  return reinterpret_cast<LLVMContextRef>(const_cast<LLVMContext *>(P));
}

/* Specialized opaque context conversions.
 */
/// Unwrap an array of opaque \c LLVMContextRef values.
/// \param Tys The opaque C API context reference array to unwrap.
/// \return The same array cast as \c LLVMContext pointers.
inline LLVMContext **unwrap(LLVMContextRef* Tys) {
  return reinterpret_cast<LLVMContext**>(Tys);
}

/// Wrap an array of \c LLVMContext pointers for the C API.
/// \param Tys The context pointer array to wrap.
/// \return The same array cast as opaque C API context references.
inline LLVMContextRef *wrap(const LLVMContext **Tys) {
  return reinterpret_cast<LLVMContextRef*>(const_cast<LLVMContext**>(Tys));
}

/// Get the deprecated global context for use by the C API.
/// \return The global LLVM context as an opaque C API reference.
LLVM_ABI LLVMContextRef getGlobalContextForCAPI();

} // end namespace llvm

#endif // LLVM_IR_LLVMCONTEXT_H
