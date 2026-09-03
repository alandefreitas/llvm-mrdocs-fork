//===- llvm/ADT/ADL.h - Argument dependent lookup utilities -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_ADL_H
#define LLVM_ADT_ADL_H

#include <type_traits>
#include <iterator>
#include <utility>

namespace llvm {

/// SFINAE helper that is only defined when both template arguments match.
template <typename T, T> struct SameType;

/// Helpers that invoke range algorithms via argument-dependent lookup.
namespace adl_detail {

/// Bring `std::begin` into scope for ADL fallback.
using std::begin;

/// Call `begin` on \p range, including ADL candidates.
///
/// \param range Range to iterate.
/// \return Begin iterator for \p range.
template <typename RangeT>
constexpr auto begin_impl(RangeT &&range)
    -> decltype(begin(std::forward<RangeT>(range))) {
  return begin(std::forward<RangeT>(range));
}

/// Bring `std::end` into scope for ADL fallback.
using std::end;

/// Call `end` on \p range, including ADL candidates.
///
/// \param range Range to iterate.
/// \return End iterator for \p range.
template <typename RangeT>
constexpr auto end_impl(RangeT &&range)
    -> decltype(end(std::forward<RangeT>(range))) {
  return end(std::forward<RangeT>(range));
}

/// Bring `std::rbegin` into scope for ADL fallback.
using std::rbegin;

/// Call `rbegin` on \p range, including ADL candidates.
///
/// \param range Range to iterate.
/// \return Reverse-begin iterator for \p range.
template <typename RangeT>
constexpr auto rbegin_impl(RangeT &&range)
    -> decltype(rbegin(std::forward<RangeT>(range))) {
  return rbegin(std::forward<RangeT>(range));
}

/// Bring `std::rend` into scope for ADL fallback.
using std::rend;

/// Call `rend` on \p range, including ADL candidates.
///
/// \param range Range to iterate.
/// \return Reverse-end iterator for \p range.
template <typename RangeT>
constexpr auto rend_impl(RangeT &&range)
    -> decltype(rend(std::forward<RangeT>(range))) {
  return rend(std::forward<RangeT>(range));
}

/// Bring `std::swap` into scope for ADL fallback.
using std::swap;

/// Swap \p lhs and \p rhs using `std::swap` and ADL candidates.
///
/// \param lhs First value to swap.
/// \param rhs Second value to swap.
template <typename T>
constexpr void swap_impl(T &&lhs,
                         T &&rhs) noexcept(noexcept(swap(std::declval<T>(),
                                                         std::declval<T>()))) {
  swap(std::forward<T>(lhs), std::forward<T>(rhs));
}

/// Bring `std::size` into scope for ADL fallback.
using std::size;

/// Call `size` on \p range, including ADL candidates.
///
/// \param range Range whose size is computed.
/// \return Size of \p range.
template <typename RangeT>
constexpr auto size_impl(RangeT &&range)
    -> decltype(size(std::forward<RangeT>(range))) {
  return size(std::forward<RangeT>(range));
}

} // end namespace adl_detail

/// Returns the begin iterator to \p range using `std::begin` and
/// function found through Argument-Dependent Lookup (ADL).
///
/// \param range Range to iterate.
/// \return Begin iterator for \p range.
template <typename RangeT>
constexpr auto adl_begin(RangeT &&range)
    -> decltype(adl_detail::begin_impl(std::forward<RangeT>(range))) {
  return adl_detail::begin_impl(std::forward<RangeT>(range));
}

/// Returns the end iterator to \p range using `std::end` and
/// functions found through Argument-Dependent Lookup (ADL).
///
/// \param range Range to iterate.
/// \return End iterator for \p range.
template <typename RangeT>
constexpr auto adl_end(RangeT &&range)
    -> decltype(adl_detail::end_impl(std::forward<RangeT>(range))) {
  return adl_detail::end_impl(std::forward<RangeT>(range));
}

/// Returns the reverse-begin iterator to \p range using `std::rbegin` and
/// function found through Argument-Dependent Lookup (ADL).
///
/// \param range Range to iterate.
/// \return Reverse-begin iterator for \p range.
template <typename RangeT>
constexpr auto adl_rbegin(RangeT &&range)
    -> decltype(adl_detail::rbegin_impl(std::forward<RangeT>(range))) {
  return adl_detail::rbegin_impl(std::forward<RangeT>(range));
}

/// Returns the reverse-end iterator to \p range using `std::rend` and
/// functions found through Argument-Dependent Lookup (ADL).
///
/// \param range Range to iterate.
/// \return Reverse-end iterator for \p range.
template <typename RangeT>
constexpr auto adl_rend(RangeT &&range)
    -> decltype(adl_detail::rend_impl(std::forward<RangeT>(range))) {
  return adl_detail::rend_impl(std::forward<RangeT>(range));
}

/// Swaps \p lhs with \p rhs using `std::swap` and functions found through
/// Argument-Dependent Lookup (ADL).
///
/// \param lhs First value to swap.
/// \param rhs Second value to swap.
template <typename T>
constexpr void adl_swap(T &&lhs, T &&rhs) noexcept(
    noexcept(adl_detail::swap_impl(std::declval<T>(), std::declval<T>()))) {
  adl_detail::swap_impl(std::forward<T>(lhs), std::forward<T>(rhs));
}

/// Returns the size of \p range using `std::size` and functions found through
/// Argument-Dependent Lookup (ADL).
///
/// \param range Range whose size is computed.
/// \return Size of \p range.
template <typename RangeT>
constexpr auto adl_size(RangeT &&range)
    -> decltype(adl_detail::size_impl(std::forward<RangeT>(range))) {
  return adl_detail::size_impl(std::forward<RangeT>(range));
}

namespace detail {

template <typename RangeT>
using IterOfRange = decltype(adl_begin(std::declval<RangeT &>()));

template <typename RangeT>
using ValueOfRange =
    std::remove_reference_t<decltype(*adl_begin(std::declval<RangeT &>()))>;

} // namespace detail
} // namespace llvm

#endif // LLVM_ADT_ADL_H
