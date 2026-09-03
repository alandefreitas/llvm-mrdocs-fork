//===- llvm/ADT/SmallVectorExtras.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines less commonly used SmallVector utilities.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SMALLVECTOREXTRAS_H
#define LLVM_ADT_SMALLVECTOREXTRAS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

/// Filter a range to a SmallVector with the element types deduced.
///
/// \param C Range whose elements are filtered.
/// \param Pred Predicate that selects which elements are copied.
/// \return A SmallVector containing the elements of \p C for which \p Pred
/// is true.
template <unsigned Size, class ContainerTy, class PredicateFn>
auto filter_to_vector(ContainerTy &&C, PredicateFn &&Pred) {
  return to_vector<Size>(make_filter_range(std::forward<ContainerTy>(C),
                                           std::forward<PredicateFn>(Pred)));
}

/// Filter a range to a SmallVector with the element types deduced.
///
/// \param C Range whose elements are filtered.
/// \param Pred Predicate that selects which elements are copied.
/// \return A SmallVector containing the elements of \p C for which \p Pred
/// is true.
template <class ContainerTy, class PredicateFn>
auto filter_to_vector(ContainerTy &&C, PredicateFn &&Pred) {
  return to_vector(make_filter_range(std::forward<ContainerTy>(C),
                                     std::forward<PredicateFn>(Pred)));
}

/// Map a range to a SmallVector with element types deduced from the mapping.
/// \p F can be a function, lambda, or member pointer.
///
/// \param C Container or range to map over.
/// \param F Mapping function applied to each element.
/// \return A SmallVector of the results of applying \p F to each element of
/// \p C.
template <unsigned Size, class ContainerTy, class FuncTy>
auto map_to_vector(ContainerTy &&C, FuncTy &&F) {
  return to_vector<Size>(
      map_range(std::forward<ContainerTy>(C), std::forward<FuncTy>(F)));
}

/// Map a range to a SmallVector with element types deduced from the mapping.
/// \p F can be a function, lambda, or member pointer.
///
/// \param C Container or range to map over.
/// \param F Mapping function applied to each element.
/// \return A SmallVector of the results of applying \p F to each element of
/// \p C.
template <class ContainerTy, class FuncTy>
auto map_to_vector(ContainerTy &&C, FuncTy &&F) {
  return to_vector(
      map_range(std::forward<ContainerTy>(C), std::forward<FuncTy>(F)));
}

} // namespace llvm

#endif // LLVM_ADT_SMALLVECTOREXTRAS_H
