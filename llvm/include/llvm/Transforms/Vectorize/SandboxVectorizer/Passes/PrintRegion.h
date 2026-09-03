#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_PRINTREGION_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_PRINTREGION_H

#include "llvm/SandboxIR/Pass.h"
#include "llvm/SandboxIR/Region.h"

namespace llvm::sandboxir {

/// A Region pass that does nothing, for use as a placeholder in tests.
class PrintRegion final : public RegionPass {
public:
  /// Construct a PrintRegion pass.
  /// \param AuxArg Unused; must be empty.
  PrintRegion(StringRef AuxArg) : RegionPass("print-region") {
    assert(AuxArg.empty() && "This pass ignores aux arg!");
  }
  /// Print the region to stdout.
  /// \param R Region to print.
  /// \param A Analyses available to the pass.
  /// \returns False; this pass never modifies the IR.
  bool runOnRegion(Region &R, const Analyses &A) final {
    raw_ostream &OS = outs();
#ifndef NDEBUG
    OS << "-- Region --\n";
    OS << R << "\n";
#else
    // TODO: Make this available in all builds, depends on enabling SandboxIR
    // dumps in non-debug builds.
    OS << "Region dump only available in DEBUG build!";
#endif
    return false;
  }
};

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_PRINTREGION_H
