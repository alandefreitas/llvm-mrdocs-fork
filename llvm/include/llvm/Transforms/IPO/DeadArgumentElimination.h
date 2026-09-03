//===- DeadArgumentElimination.h - Eliminate Dead Args ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass deletes dead arguments from internal functions.  Dead argument
// elimination removes arguments which are directly dead, as well as arguments
// only passed into function calls as dead arguments of other functions.  This
// pass also deletes dead return values in a similar way.
//
// This pass is often useful as a cleanup pass to run after aggressive
// interprocedural passes, which add possibly-dead arguments or return values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_DEADARGUMENTELIMINATION_H
#define LLVM_TRANSFORMS_IPO_DEADARGUMENTELIMINATION_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include <map>
#include <set>
#include <string>
#include <tuple>

namespace llvm {

class Module;
class Use;
class Value;

/// Eliminate dead arguments (and return values) from functions.
class DeadArgumentEliminationPass
    : public OptionalPassInfoMixin<DeadArgumentEliminationPass> {
public:
  /// Struct that represents (part of) either a return value or a function
  /// argument.  Used so that arguments and return values can be used
  /// interchangeably.
  struct RetOrArg {
    /// Function that owns this return value or argument.
    const Function *F;
    /// Index of the return value or argument within \p F.
    unsigned Idx;
    /// True if this refers to an argument; false if it refers to a return value.
    bool IsArg;

    /// Construct a reference to return value or argument \p Idx of \p F.
    ///
    /// \param F Function that owns the return or argument.
    /// \param Idx Index of the return value or argument.
    /// \param IsArg True if this refers to an argument; false for a return.
    RetOrArg(const Function *F, unsigned Idx, bool IsArg)
        : F(F), Idx(Idx), IsArg(IsArg) {}

    /// Make RetOrArg comparable, so we can put it into a map.
    ///
    /// \param O Other return-or-argument to compare against.
    /// \return True if this precedes \p O in lexicographic order of F, Idx, and
    /// IsArg.
    bool operator<(const RetOrArg &O) const {
      return std::tie(F, Idx, IsArg) < std::tie(O.F, O.Idx, O.IsArg);
    }

    /// Make RetOrArg comparable, so we can easily iterate the multimap.
    ///
    /// \param O Other return-or-argument to compare against.
    /// \return True if this and \p O refer to the same return or argument.
    bool operator==(const RetOrArg &O) const {
      return F == O.F && Idx == O.Idx && IsArg == O.IsArg;
    }

    /// Return a human-readable description of this return or argument.
    ///
    /// \return A string identifying the argument or return value and its
    /// function.
    std::string getDescription() const {
      return (Twine(IsArg ? "Argument #" : "Return value #") + Twine(Idx) +
              " of function " + F->getName())
          .str();
    }
  };

  /// Liveness of a return value or argument during dead-argument analysis.
  ///
  /// During our initial pass over the program, we determine that things are
  /// either alive or maybe alive. We don't mark anything explicitly dead (even
  /// if we know they are), since anything not alive with no registered uses
  /// (in Uses) will never be marked alive and will thus become dead in the end.
  enum Liveness {
    Live,      ///< Definitely used and must be preserved.
    MaybeLive, ///< Not yet proven live; may become live via Uses.
  };

  /// Run dead-argument elimination over the given module.
  ///
  /// \param M Module whose dead arguments and return values are eliminated.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  /// Convenience wrapper
  ///
  /// \param F Function whose return value is referenced.
  /// \param Idx Index of the return value (for multi-value returns).
  /// \return A RetOrArg referring to return value \p Idx of \p F.
  RetOrArg createRet(const Function *F, unsigned Idx) {
    return RetOrArg(F, Idx, false);
  }

  /// Convenience wrapper
  ///
  /// \param F Function whose argument is referenced.
  /// \param Idx Index of the argument.
  /// \return A RetOrArg referring to argument \p Idx of \p F.
  RetOrArg createArg(const Function *F, unsigned Idx) {
    return RetOrArg(F, Idx, true);
  }

  /// Multimap from a return or argument to MaybeLive values it uses.
  using UseMap = std::multimap<RetOrArg, RetOrArg>;

  /// Multimap from each return or argument to the MaybeLive values it uses.
  ///
  /// This maps a return value or argument to any MaybeLive return values or
  /// arguments it uses. This allows the MaybeLive values to be marked live
  /// when any of its users is marked live.
  /// For example (indices are left out for clarity):
  ///  - Uses[ret F] = ret G
  ///    This means that F calls G, and F returns the value returned by G.
  ///  - Uses[arg F] = ret G
  ///    This means that some function calls G and passes its result as an
  ///    argument to F.
  ///  - Uses[ret F] = arg F
  ///    This means that F returns one of its own arguments.
  ///  - Uses[arg F] = arg G
  ///    This means that G calls F and passes one of its own (G's) arguments
  ///    directly to F.
  UseMap Uses;

  /// Set of returns and arguments known to be live.
  using LiveSet = std::set<RetOrArg>;
  /// Set of functions that must not be modified by this pass.
  using FuncSet = std::set<const Function *>;

  /// This set contains all values that have been determined to be live.
  LiveSet LiveValues;

  /// This set contains all functions that cannot be changed in any way.
  FuncSet FrozenFunctions;

  /// This set contains all functions that cannot change return type;
  FuncSet FrozenRetTyFunctions;

  /// Small vector of returns and arguments collected while surveying uses.
  using UseVector = SmallVector<RetOrArg, 5>;

private:
  Liveness markIfNotLive(RetOrArg Use, UseVector &MaybeLiveUses);
  Liveness surveyUse(const Use *U, UseVector &MaybeLiveUses,
                     unsigned RetValNum = -1U);
  Liveness surveyUses(const Value *V, UseVector &MaybeLiveUses);

  void surveyFunction(const Function &F);
  bool isLive(const RetOrArg &RA);
  void markValue(const RetOrArg &RA, Liveness L,
                 const UseVector &MaybeLiveUses);
  void markLive(const RetOrArg &RA);
  void markFrozen(const Function &F);
  void markRetTyFrozen(const Function &F);
  bool markFnOrRetTyFrozenOnMusttail(const Function &F);
  void propagateLiveness(const RetOrArg &RA);
  bool removeDeadStuffFromFunction(Function *F);
  bool deleteDeadVarargs(Function &F);
  bool removeDeadArgumentsFromCallers(Function &F);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_DEADARGUMENTELIMINATION_H
