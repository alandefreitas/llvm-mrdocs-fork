//===- PassManager.h --- Pass management for CodeGen ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines the pass manager interface for codegen. The codegen
// pipeline consists of only machine function passes. There is no container
// relationship between IR module/function and machine function in terms of pass
// manager organization. So there is no need for adaptor classes (for example
// ModuleToMachineFunctionAdaptor). Since invalidation could only happen among
// machine function passes, there is no proxy classes to handle cross-IR-unit
// invalidation. IR analysis results are provided for machine function passes by
// their respective analysis managers such as ModuleAnalysisManager and
// FunctionAnalysisManager.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEPASSMANAGER_H
#define LLVM_CODEGEN_MACHINEPASSMANAGER_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PassManagerInternal.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
class Module;
class Function;

/// RAII helper that updates MachineFunctionProperties around a pass run.
///
/// Define an MFPropsModifier in PassT::run to set MachineFunctionProperties
/// properly.
template <typename PassT> class MFPropsModifier {
public:
  /// Begin updating MachineFunctionProperties for pass \p P_ on \p MF_.
  /// @param P_ Pass whose property traits are applied.
  /// @param MF_ Machine function whose properties are updated.
  MFPropsModifier(const PassT &P_, MachineFunction &MF_) : P(P_), MF(MF_) {
    auto &MFProps = MF.getProperties();
#ifndef NDEBUG
    if constexpr (has_get_required_properties_v<PassT>) {
      auto &MFProps = MF.getProperties();
      auto RequiredProperties = P.getRequiredProperties();
      if (!MFProps.verifyRequiredProperties(RequiredProperties)) {
        errs() << "MachineFunctionProperties required by " << PassT::name()
               << " pass are not met by function " << MF.getName() << ".\n"
               << "Required properties: ";
        RequiredProperties.print(errs());
        errs() << "\nCurrent properties: ";
        MFProps.print(errs());
        errs() << '\n';
        reportFatalUsageError("MachineFunctionProperties check failed");
      }
    }
#endif // NDEBUG
    if constexpr (has_get_cleared_properties_v<PassT>)
      MFProps.reset(P.getClearedProperties());
  }

  /// Restore set properties on the machine function when the modifier ends.
  ~MFPropsModifier() {
    if constexpr (has_get_set_properties_v<PassT>) {
      auto &MFProps = MF.getProperties();
      MFProps.set(P.getSetProperties());
    }
  }

private:
  const PassT &P;
  MachineFunction &MF;

  template <typename T>
  using has_get_required_properties_t =
      decltype(std::declval<T &>().getRequiredProperties());

  template <typename T>
  using has_get_set_properties_t =
      decltype(std::declval<T &>().getSetProperties());

  template <typename T>
  using has_get_cleared_properties_t =
      decltype(std::declval<T &>().getClearedProperties());

  template <typename T>
  static constexpr bool has_get_required_properties_v =
      is_detected<has_get_required_properties_t, T>::value;

  template <typename T>
  static constexpr bool has_get_set_properties_v =
      is_detected<has_get_set_properties_t, T>::value;

  template <typename T>
  static constexpr bool has_get_cleared_properties_v =
      is_detected<has_get_cleared_properties_t, T>::value;
};

/// Deduction guide that suppresses unused-parameter warnings for MFPropsModifier.
/// @param P Pass whose property traits are applied.
/// @param MF Machine function whose properties are updated.
template <typename PassT>
MFPropsModifier(PassT &P, MachineFunction &MF) -> MFPropsModifier<PassT>;

/// Provide the \c MachineFunctionAnalysisManager to \c Module proxy.
using MachineFunctionAnalysisManagerModuleProxy =
    InnerAnalysisManagerProxy<MachineFunctionAnalysisManager, Module>;

/// Specialization of the invalidate method for the \c
/// MachineFunctionAnalysisManagerModuleProxy's result.
/// @param M Module being invalidated.
/// @param PA Set of analyses preserved by the transform.
/// @param Inv Invalidator for resolving analysis dependencies.
/// @return True if the proxy result itself should be invalidated.
template <>
LLVM_ABI bool MachineFunctionAnalysisManagerModuleProxy::Result::invalidate(
    Module &M, const PreservedAnalyses &PA,
    ModuleAnalysisManager::Invalidator &Inv);
extern template class InnerAnalysisManagerProxy<MachineFunctionAnalysisManager,
                                                Module>;
/// Provide the \c MachineFunctionAnalysisManager to \c Function proxy.
using MachineFunctionAnalysisManagerFunctionProxy =
    InnerAnalysisManagerProxy<MachineFunctionAnalysisManager, Function>;

/// Specialization of the invalidate method for the \c
/// MachineFunctionAnalysisManagerFunctionProxy's result.
/// @param F Function being invalidated.
/// @param PA Set of analyses preserved by the transform.
/// @param Inv Invalidator for resolving analysis dependencies.
/// @return True if the proxy result itself should be invalidated.
template <>
LLVM_ABI bool MachineFunctionAnalysisManagerFunctionProxy::Result::invalidate(
    Function &F, const PreservedAnalyses &PA,
    FunctionAnalysisManager::Invalidator &Inv);
extern template class InnerAnalysisManagerProxy<MachineFunctionAnalysisManager,
                                                Function>;

/// Explicit instantiation of the module-to-machine-function outer analysis proxy.
extern template class LLVM_TEMPLATE_ABI
    OuterAnalysisManagerProxy<ModuleAnalysisManager, MachineFunction>;
/// Provide the \c ModuleAnalysisManager to \c MachineFunction proxy.
using ModuleAnalysisManagerMachineFunctionProxy =
    OuterAnalysisManagerProxy<ModuleAnalysisManager, MachineFunction>;

/// A proxy from a \c FunctionAnalysisManager to a \c MachineFunction.
class FunctionAnalysisManagerMachineFunctionProxy
    : public AnalysisInfoMixin<FunctionAnalysisManagerMachineFunctionProxy> {
public:
  /// Proxy result owning invalidation responsibility for a function analysis manager.
  class Result {
  public:
    /// Construct a result that proxies \p FAM.
    /// @param FAM Function analysis manager to expose.
    explicit Result(FunctionAnalysisManager &FAM) : FAM(&FAM) {}

    /// Move-construct a result, taking ownership from \p Arg.
    /// @param Arg Result to move from; its manager pointer is cleared.
    Result(Result &&Arg) : FAM(std::move(Arg.FAM)) {
      // We have to null out the analysis manager in the moved-from state
      // because we are taking ownership of the responsibility to clear the
      // analysis state.
      Arg.FAM = nullptr;
    }

    /// Move-assign proxy ownership of the function analysis manager from \p RHS.
    /// @param RHS Result to move from; its manager pointer is cleared.
    /// @return A reference to this result.
    Result &operator=(Result &&RHS) {
      FAM = RHS.FAM;
      // We have to null out the analysis manager in the moved-from state
      // because we are taking ownership of the responsibility to clear the
      // analysis state.
      RHS.FAM = nullptr;
      return *this;
    }

    /// Accessor for the analysis manager.
    /// @return The function analysis manager exposed by this result.
    FunctionAnalysisManager &getManager() { return *FAM; }

    /// Handler for invalidation of the outer IR unit, \c IRUnitT.
    ///
    /// If the proxy analysis itself is not preserved, we assume that the set of
    /// inner IR objects contained in IRUnit may have changed.  In this case,
    /// we have to call \c clear() on the inner analysis manager, as it may now
    /// have stale pointers to its inner IR objects.
    ///
    /// Regardless of whether the proxy analysis is marked as preserved, all of
    /// the analyses in the inner analysis manager are potentially invalidated
    /// based on the set of preserved analyses.
    /// @param IR Machine function being invalidated.
    /// @param PA Set of analyses preserved by the transform.
    /// @param Inv Invalidator for resolving analysis dependencies.
    /// @return True if the proxy result itself should be invalidated.
    LLVM_ABI bool invalidate(MachineFunction &IR, const PreservedAnalyses &PA,
                             MachineFunctionAnalysisManager::Invalidator &Inv);

  private:
    FunctionAnalysisManager *FAM;
  };

  /// Construct a proxy over the given function analysis manager.
  /// @param FAM Function analysis manager to expose through this proxy.
  explicit FunctionAnalysisManagerMachineFunctionProxy(
      FunctionAnalysisManager &FAM)
      : FAM(&FAM) {}

  /// Run the analysis pass and create our proxy result object.
  ///
  /// This doesn't do any interesting work; it is primarily used to insert our
  /// proxy result object into the outer analysis cache so that we can proxy
  /// invalidation to the inner analysis manager.
  /// @param MF Machine function (unused).
  /// @param MFAM Machine function analysis manager (unused).
  /// @return A result that proxies the function analysis manager.
  Result run(MachineFunction &MF, MachineFunctionAnalysisManager &MFAM) {
    return Result(*FAM);
  }

  /// Analysis key used to identify FunctionAnalysisManagerMachineFunctionProxy.
  LLVM_ABI static AnalysisKey Key;

private:
  FunctionAnalysisManager *FAM;
};

/// Trivial adaptor that maps from a function to its machine function.
///
/// Designed to allow composition of a MachineFunctionPass(Manager) and a
/// FunctionPassManager by running the MachineFunctionPass(Manager) over the
/// machine function corresponding to each IR function.
class FunctionToMachineFunctionPassAdaptor
    : public RequiredPassInfoMixin<FunctionToMachineFunctionPassAdaptor> {
public:
  /// Type-erased machine function pass concept used by this adaptor.
  using PassConceptT =
      detail::PassConcept<MachineFunction, MachineFunctionAnalysisManager>;

  /// Construct an adaptor that runs \p Pass over the machine function.
  /// @param Pass Machine function pass to run.
  explicit FunctionToMachineFunctionPassAdaptor(PassConceptT::unique_ptr Pass)
      : Pass(std::move(Pass)) {}

  /// Runs the machine function pass on the machine function for \p F.
  /// @param F Function whose machine function is processed.
  /// @param FAM Function analysis manager.
  /// @return The analyses preserved after running the machine function pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  /// Print this adaptor and its nested machine function pass as a pipeline.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  PassConceptT::unique_ptr Pass;
};

/// A function to deduce a machine function pass type and wrap it in the
/// templated adaptor.
/// @param Pass Machine function pass to wrap.
/// @return A function-to-machine-function adaptor wrapping \p Pass.
template <typename MachineFunctionPassT>
FunctionToMachineFunctionPassAdaptor
createFunctionToMachineFunctionPassAdaptor(MachineFunctionPassT &&Pass) {
  using PassModelT = detail::PassModel<MachineFunction, MachineFunctionPassT,
                                       MachineFunctionAnalysisManager>;
  return FunctionToMachineFunctionPassAdaptor(
      PassModelT::create(std::move(Pass)));
}

/// Run all machine function passes in this manager over \p MF.
/// @param MF Machine function to run passes over.
/// @param MFAM Analysis manager propagated to each pass.
/// @return The analyses preserved after running all passes.
template <>
LLVM_ABI PreservedAnalyses PassManager<MachineFunction>::run(
    MachineFunction &MF, AnalysisManager<MachineFunction> &MFAM);
extern template class PassManager<MachineFunction>;

/// Convenience typedef for a pass manager over functions.
using MachineFunctionPassManager = PassManager<MachineFunction>;

/// Returns the minimum set of Analyses that all machine function passes must
/// preserve.
/// @return The minimum set of analyses that all machine function passes must
/// preserve.
LLVM_ABI PreservedAnalyses getMachineFunctionPassPreservedAnalyses();

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEPASSMANAGER_H
