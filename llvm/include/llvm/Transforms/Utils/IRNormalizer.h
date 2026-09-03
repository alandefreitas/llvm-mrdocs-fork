#ifndef LLVM_TRANSFORMS_UTILS_IRNORMALIZER_H
#define LLVM_TRANSFORMS_UTILS_IRNORMALIZER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Options controlling how IRNormalizerPass rewrites a function.
struct IRNormalizerOptions {
  /// Preserves original instruction order.
  bool PreserveOrder = false;

  /// Renames all instructions (including user-named)
  bool RenameAll = true;

  /// Folds all regular instructions (including pre-outputs)
  bool FoldPreOutputs = true;

  /// Sorts and reorders operands in commutative instructions
  bool ReorderOperands = true;
};

/// IRNormalizer aims to transform LLVM IR into normal form.
struct IRNormalizerPass : public OptionalPassInfoMixin<IRNormalizerPass> {
private:
  const IRNormalizerOptions Options;

public:
  /// Construct an IR normalizer pass with the given options.
  /// @param Options Controls ordering, renaming, folding, and operand reorder.
  IRNormalizerPass(IRNormalizerOptions Options = IRNormalizerOptions())
      : Options(Options) {}

  /// Run the IR normalizer pass over the function.
  /// @param F Function whose IR should be normalized.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F,
                                 FunctionAnalysisManager &AM) const;
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_IRNORMALIZER_H
