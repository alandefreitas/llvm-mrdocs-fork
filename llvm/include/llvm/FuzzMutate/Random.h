//===--- Random.h - Utilities for random sampling -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities for random sampling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_RANDOM_H
#define LLVM_FUZZMUTATE_RANDOM_H

#include "llvm/Support/raw_ostream.h"
#include <random>
namespace llvm {

/// Return a uniformly distributed random value between \c Min and \c Max.
/// \param Gen Pseudo-random number generator to draw from.
/// \param Min Inclusive lower bound of the range.
/// \param Max Inclusive upper bound of the range.
/// \return A uniformly distributed random value in [\p Min, \p Max].
template <typename T, typename GenT> T uniform(GenT &Gen, T Min, T Max) {
  return std::uniform_int_distribution<T>(Min, Max)(Gen);
}

/// Return a uniformly distributed random value of type \c T.
/// \param Gen Pseudo-random number generator to draw from.
/// \return A uniformly distributed random value of type \c T.
template <typename T, typename GenT> T uniform(GenT &Gen) {
  return uniform<T>(Gen, std::numeric_limits<T>::min(),
                    std::numeric_limits<T>::max());
}

/// Randomly selects an item by sampling into a set with an unknown number of
/// elements, which may each be weighted to be more likely choices.
template <typename T, typename GenT> class ReservoirSampler {
  GenT &RandGen;
  std::remove_const_t<T> Selection = {};
  uint64_t TotalWeight = 0;

public:
  /// Construct a sampler that draws randomness from \p RandGen.
  /// \param RandGen Pseudo-random number generator used for sampling.
  ReservoirSampler(GenT &RandGen) : RandGen(RandGen) {}

  /// Return the total weight of all items sampled so far.
  /// \return The sum of weights of all items sampled so far.
  uint64_t totalWeight() const { return TotalWeight; }
  /// Return true if no items have been sampled yet.
  /// \return True if no items have been sampled yet.
  bool isEmpty() const { return TotalWeight == 0; }

  /// Return the currently selected item.
  /// \return The currently selected item.
  const T &getSelection() const {
    assert(!isEmpty() && "Nothing selected");
    return Selection;
  }

  /// Return true if a selection has been made.
  /// \return True if a selection has been made.
  explicit operator bool() const { return !isEmpty(); }
  /// Return the currently selected item.
  /// \return The currently selected item.
  const T &operator*() const { return getSelection(); }

  /// Sample each item in \c Items with unit weight.
  /// \param Items Range of items to sample with weight one each.
  /// \return A reference to this sampler.
  template <typename RangeT> ReservoirSampler &sample(RangeT &&Items) {
    for (auto &I : Items)
      sample(I, 1);
    return *this;
  }

  /// Sample a single item with the given weight.
  /// \param Item Candidate value to consider for selection.
  /// \param Weight Relative weight of this item; zero is ignored.
  /// \return A reference to this sampler.
  ReservoirSampler &sample(const T &Item, uint64_t Weight) {
    if (!Weight)
      // If the weight is zero, do nothing.
      return *this;
    TotalWeight += Weight;
    // Consider switching from the current element to this one.
    if (uniform<uint64_t>(RandGen, 1, TotalWeight) <= Weight)
      Selection = Item;
    return *this;
  }
};

/// Create a reservoir sampler and sample all items in \p Items with unit weight.
/// \param RandGen Pseudo-random number generator used for sampling.
/// \param Items Range of items to sample with weight one each.
/// \return A reservoir sampler containing a selection from \p Items.
template <typename GenT, typename RangeT,
          typename ElT = std::remove_reference_t<
              decltype(*std::begin(std::declval<RangeT>()))>>
ReservoirSampler<ElT, GenT> makeSampler(GenT &RandGen, RangeT &&Items) {
  ReservoirSampler<ElT, GenT> RS(RandGen);
  RS.sample(Items);
  return RS;
}

/// Create a reservoir sampler and sample a single weighted item.
/// \param RandGen Pseudo-random number generator used for sampling.
/// \param Item Candidate value to consider for selection.
/// \param Weight Relative weight of this item; zero is ignored.
/// \return A reservoir sampler that has sampled \p Item with \p Weight.
template <typename GenT, typename T>
ReservoirSampler<T, GenT> makeSampler(GenT &RandGen, const T &Item,
                                      uint64_t Weight) {
  ReservoirSampler<T, GenT> RS(RandGen);
  RS.sample(Item, Weight);
  return RS;
}

/// Create an empty reservoir sampler over values of type \c T.
/// \param RandGen Pseudo-random number generator used for sampling.
/// \return An empty reservoir sampler over values of type \c T.
template <typename T, typename GenT>
ReservoirSampler<T, GenT> makeSampler(GenT &RandGen) {
  return ReservoirSampler<T, GenT>(RandGen);
}

} // namespace llvm

#endif // LLVM_FUZZMUTATE_RANDOM_H
