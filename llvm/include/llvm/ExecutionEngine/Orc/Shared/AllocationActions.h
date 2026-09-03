//===- AllocationActions.h -- JITLink allocation support calls  -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Structures for making memory allocation support calls.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_ALLOCATIONACTIONS_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_ALLOCATIONACTIONS_H

#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Memory.h"

#include <vector>

namespace llvm {
namespace orc {
namespace shared {

/// A pair of WrapperFunctionCalls, one to be run at finalization time, one to
/// be run at deallocation time.
///
/// AllocActionCallPairs should be constructed for paired operations (e.g.
/// __register_ehframe and __deregister_ehframe for eh-frame registration).
/// See comments for AllocActions for execution ordering.
///
/// For unpaired operations one or the other member can be left unused, as
/// AllocationActionCalls with an FnAddr of zero will be skipped.
struct AllocActionCallPair {
  /// Wrapper function call to run at finalization time.
  WrapperFunctionCall Finalize;
  /// Wrapper function call to run at deallocation time.
  WrapperFunctionCall Dealloc;
};

/// A vector of allocation actions to be run for this allocation.
///
/// Finalize allocations will be run in order at finalize time. Dealloc
/// actions will be run in reverse order at deallocation time.
using AllocActions = std::vector<AllocActionCallPair>;

/// Returns the number of deallocaton actions in the given AllocActions array.
///
/// This can be useful if clients want to pre-allocate room for deallocation
/// actions with the rest of their memory.
/// @param AAs Allocation actions to inspect for non-empty dealloc calls.
/// @return Number of allocation actions with a non-empty dealloc call.
inline size_t numDeallocActions(const AllocActions &AAs) {
  return llvm::count_if(
      AAs, [](const AllocActionCallPair &P) { return !!P.Dealloc; });
}

/// Run finalize actions.
///
/// If any finalize action fails then the corresponding dealloc actions will be
/// run in reverse order (not including the deallocation action for the failed
/// finalize action), and the error for the failing action will be returned.
///
/// If all finalize actions succeed then a vector of deallocation actions will
/// be returned. The dealloc actions should be run by calling
/// runDeallocationActions. If this function succeeds then the AA argument will
/// be cleared before the function returns.
/// @param AAs Allocation actions whose finalize calls should be run.
/// @return Deallocation actions on success, or an error if a finalize action
/// fails.
LLVM_ABI Expected<std::vector<WrapperFunctionCall>>
runFinalizeActions(AllocActions &AAs);

/// Run deallocation actions.
/// Dealloc actions will be run in reverse order (from last element of DAs to
/// first).
/// @param DAs Deallocation wrapper function calls to run in reverse order.
/// @return Success, or the first error encountered while running dealloc
/// actions.
LLVM_ABI Error runDeallocActions(ArrayRef<WrapperFunctionCall> DAs);

/// SPS tag type for AllocActionCallPair.
using SPSAllocActionCallPair =
    SPSTuple<SPSWrapperFunctionCall, SPSWrapperFunctionCall>;

/// SPS serializer for AllocActionCallPair.
template <>
class SPSSerializationTraits<SPSAllocActionCallPair,
                             AllocActionCallPair> {
  using AL = SPSAllocActionCallPair::AsArgList;

public:
  /// Return the serialized size of \p AAP.
  /// @param AAP Allocation action call pair to measure.
  /// @return Number of bytes needed to serialize \p AAP.
  static size_t size(const AllocActionCallPair &AAP) {
    return AL::size(AAP.Finalize, AAP.Dealloc);
  }

  /// Serialize \p AAP into \p OB.
  /// @param OB Output buffer.
  /// @param AAP Allocation action call pair to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &OB,
                        const AllocActionCallPair &AAP) {
    return AL::serialize(OB, AAP.Finalize, AAP.Dealloc);
  }

  /// Deserialize an AllocActionCallPair from \p IB into \p AAP.
  /// @param IB Input buffer.
  /// @param AAP Destination allocation action call pair.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &IB,
                          AllocActionCallPair &AAP) {
    return AL::deserialize(IB, AAP.Finalize, AAP.Dealloc);
  }
};

} // end namespace shared
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_ALLOCATIONACTIONS_H
