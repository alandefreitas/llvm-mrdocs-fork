//===--- TrailingObjects.h - Variable-length classes ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This header defines support for implementing classes that have
/// some trailing object (or arrays of objects) appended to them. The
/// main purpose is to make it obvious where this idiom is being used,
/// and to make the usage more idiomatic and more difficult to get
/// wrong.
///
/// The TrailingObject template abstracts away the reinterpret_cast,
/// pointer arithmetic, and size calculations used for the allocation
/// and access of appended arrays of objects, and takes care that they
/// are all allocated at their required alignment. Additionally, it
/// ensures that the base type is final -- deriving from a class that
/// expects data appended immediately after it is typically not safe.
///
/// Users are expected to derive from this template, and provide
/// numTrailingObjects implementations for each trailing type except
/// the last, e.g. like this sample:
///
/// \code
/// class VarLengthObj : private TrailingObjects<VarLengthObj, int, double> {
///   friend TrailingObjects;
///
///   unsigned NumInts, NumDoubles;
///   size_t numTrailingObjects(OverloadToken<int>) const { return NumInts; }
///  };
/// \endcode
///
/// You can access the appended arrays via 'getTrailingObjects', and
/// determine the size needed for allocation via
/// 'additionalSizeToAlloc' and 'totalSizeToAlloc'.
///
/// All the methods implemented by this class are intended for use
/// by the implementation of the class, not as part of its interface
/// (thus, private inheritance is suggested).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TRAILINGOBJECTS_H
#define LLVM_SUPPORT_TRAILINGOBJECTS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/type_traits.h"
#include <new>
#include <type_traits>

namespace llvm {

/// Implementation details supporting TrailingObjects.
namespace trailing_objects_internal {

/// The maximum alignment among the given types.
template <typename... T>
inline constexpr size_t MaxAlignment = std::max({alignof(T)...});

/// The base class for TrailingObjects* classes.
class TrailingObjectsBase {
protected:
  /// Empty tag type used to select overloads by trailing object type.
  ///
  /// OverloadToken's purpose is to allow specifying function overloads
  /// for different types, without actually taking the types as
  /// parameters. (Necessary because member function templates cannot
  /// be specialized, so overloads must be used instead of
  /// specialization.)
  template <typename T> struct OverloadToken {};
};

/// Maps each type in a pack to another type (\c Ty2).
///
/// Just a little helper for transforming a type pack into the same
/// number of a different type. e.g.:
///   ExtractSecondType<Foo..., int>::type
template <typename Ty1, typename Ty2> struct ExtractSecondType {
  /// Alias for \c Ty2; expands a type pack into another type of equal length.
  using type = Ty2;
};

// TrailingObjectsImpl is somewhat complicated, because it is a
// recursively inheriting template, in order to handle the template
// varargs. Each level of inheritance picks off a single trailing type
// then recurses on the rest. The "Align", "BaseTy", and
// "TopTrailingObj" arguments are passed through unchanged through the
// recursion. "PrevTy" is, at each level, the type handled by the
// level right above it.

/// Primary TrailingObjectsImpl template; only the specializations are used.
template <int Align, typename BaseTy, typename TopTrailingObj, typename PrevTy,
          typename... MoreTys>
class TrailingObjectsImpl {
  // The main template definition is never used -- the two
  // specializations cover all possibilities.
};

/// Recursive specialization that handles one trailing type then recurses.
template <int Align, typename BaseTy, typename TopTrailingObj, typename PrevTy,
          typename NextTy, typename... MoreTys>
class TrailingObjectsImpl<Align, BaseTy, TopTrailingObj, PrevTy, NextTy,
                          MoreTys...>
    : public TrailingObjectsImpl<Align, BaseTy, TopTrailingObj, NextTy,
                                 MoreTys...> {

  using ParentType =
      TrailingObjectsImpl<Align, BaseTy, TopTrailingObj, NextTy, MoreTys...>;

  struct RequiresRealignment {
    static const bool value = alignof(PrevTy) < alignof(NextTy);
  };

  static constexpr bool requiresRealignment() {
    return RequiresRealignment::value;
  }

protected:
  // Ensure the inherited getTrailingObjectsImpl is not hidden.
  /// Bring base-class getTrailingObjectsImpl overloads into scope.
  using ParentType::getTrailingObjectsImpl;

  // These two functions are helper functions for
  // TrailingObjects::getTrailingObjects. They recurse to the left --
  // the result for each type in the list of trailing types depends on
  // the result of calling the function on the type to the
  // left. However, the function for the type to the left is
  // implemented by a *subclass* of this class, so we invoke it via
  // the TopTrailingObj, which is, via the
  // curiously-recurring-template-pattern, the most-derived type in
  // this recursion, and thus, contains all the overloads.
  /// Return a const pointer to the \c NextTy trailing array in \p Obj.
  ///
  /// Selected by an \c OverloadToken<\c NextTy> argument.
  ///
  /// \param Obj Base object whose trailing storage is being addressed.
  /// \param Token Tag selecting the \c NextTy trailing array overload.
  /// \return A const pointer to the \c NextTy trailing array.
  static const NextTy *
  getTrailingObjectsImpl(const BaseTy *Obj,
                         TrailingObjectsBase::OverloadToken<NextTy> Token) {
    auto *Ptr = TopTrailingObj::getTrailingObjectsImpl(
                    Obj, TrailingObjectsBase::OverloadToken<PrevTy>()) +
                TopTrailingObj::callNumTrailingObjects(
                    Obj, TrailingObjectsBase::OverloadToken<PrevTy>());

    if (requiresRealignment())
      return reinterpret_cast<const NextTy *>(
          alignAddr(Ptr, Align::Of<NextTy>()));
    else
      return reinterpret_cast<const NextTy *>(Ptr);
  }

  /// Return a pointer to the \c NextTy trailing array in \p Obj.
  ///
  /// Selected by an \c OverloadToken<\c NextTy> argument.
  ///
  /// \param Obj Base object whose trailing storage is being addressed.
  /// \param Token Tag selecting the \c NextTy trailing array overload.
  /// \return A pointer to the \c NextTy trailing array.
  static NextTy *
  getTrailingObjectsImpl(BaseTy *Obj,
                         TrailingObjectsBase::OverloadToken<NextTy> Token) {
    auto *Ptr = TopTrailingObj::getTrailingObjectsImpl(
                    Obj, TrailingObjectsBase::OverloadToken<PrevTy>()) +
                TopTrailingObj::callNumTrailingObjects(
                    Obj, TrailingObjectsBase::OverloadToken<PrevTy>());

    if (requiresRealignment())
      return reinterpret_cast<NextTy *>(alignAddr(Ptr, Align::Of<NextTy>()));
    else
      return reinterpret_cast<NextTy *>(Ptr);
  }

  // Helper function for TrailingObjects::additionalSizeToAlloc: this
  // function recurses to superclasses, each of which requires one
  // fewer size_t argument, and adds its own size.
  /// Accumulate trailing allocation size for \c NextTy and remaining types.
  ///
  /// \param SizeSoFar Size accounted for so far (may be realigned).
  /// \param Count1 Number of \c NextTy trailing elements.
  /// \param MoreCounts Element counts for the remaining trailing types.
  /// \return The total additional size including \c NextTy and remaining types.
  static constexpr size_t additionalSizeToAllocImpl(
      size_t SizeSoFar, size_t Count1,
      typename ExtractSecondType<MoreTys, size_t>::type... MoreCounts) {
    return ParentType::additionalSizeToAllocImpl(
        (requiresRealignment() ? llvm::alignTo<alignof(NextTy)>(SizeSoFar)
                               : SizeSoFar) +
            sizeof(NextTy) * Count1,
        MoreCounts...);
  }
};

// The base case of the TrailingObjectsImpl inheritance recursion,
// when there's no more trailing types.
/// Base case of TrailingObjectsImpl when no trailing types remain.
template <int Align, typename BaseTy, typename TopTrailingObj, typename PrevTy>
class alignas(Align) TrailingObjectsImpl<Align, BaseTy, TopTrailingObj, PrevTy>
    : public TrailingObjectsBase {
protected:
  // This is a dummy method, only here so the "using" doesn't fail --
  // it will never be called, because this function recurses backwards
  // up the inheritance chain to subclasses.
  /// Dummy overload so derived using-declarations remain valid.
  static void getTrailingObjectsImpl();

  /// Return \p SizeSoFar unchanged when no further trailing types remain.
  ///
  /// \param SizeSoFar Accumulated trailing allocation size.
  /// \return \p SizeSoFar, with no further trailing size added.
  static constexpr size_t additionalSizeToAllocImpl(size_t SizeSoFar) {
    return SizeSoFar;
  }
};

} // end namespace trailing_objects_internal

// Finally, the main type defined in this file, the one intended for users...

/// See the file comment for details on the usage of the
/// TrailingObjects type.
template <typename BaseTy, typename... TrailingTys>
class TrailingObjects
    : private trailing_objects_internal::TrailingObjectsImpl<
          trailing_objects_internal::MaxAlignment<TrailingTys...>, BaseTy,
          TrailingObjects<BaseTy, TrailingTys...>, BaseTy, TrailingTys...> {

  template <int A, typename B, typename T, typename P, typename... M>
  friend class trailing_objects_internal::TrailingObjectsImpl;

  template <typename... Tys> class Foo {};

  using ParentType = typename TrailingObjects::TrailingObjectsImpl;
  using TrailingObjectsBase = trailing_objects_internal::TrailingObjectsBase;

  using ParentType::getTrailingObjectsImpl;

  template <bool Strict> static void verifyTrailingObjectsAssertions() {
    // The static_assert for BaseTy must be in a function, and not at
    // class-level  because BaseTy isn't complete at class instantiation time,
    // but will be by the time this function is instantiated.
    static_assert(std::is_final<BaseTy>(), "BaseTy must be final.");

    // Verify that templated getTrailingObjects() is used only with multiple
    // trailing types. Use getTrailingObjectsNonStrict() which does not check
    // this.
    static_assert(!Strict || sizeof...(TrailingTys) > 1,
                  "Use templated getTrailingObjects() only when there are "
                  "multiple trailing types");
  }

  // These two methods are the base of the recursion for this method.
  static const BaseTy *
  getTrailingObjectsImpl(const BaseTy *Obj,
                         TrailingObjectsBase::OverloadToken<BaseTy>) {
    return Obj;
  }

  static BaseTy *
  getTrailingObjectsImpl(BaseTy *Obj,
                         TrailingObjectsBase::OverloadToken<BaseTy>) {
    return Obj;
  }

  // callNumTrailingObjects simply calls numTrailingObjects on the
  // provided Obj -- except when the type being queried is BaseTy
  // itself. There is always only one of the base object, so that case
  // is handled here. (An additional benefit of indirecting through
  // this function is that consumers only say "friend
  // TrailingObjects", and thus, only this class itself can call the
  // numTrailingObjects function.)
  static size_t
  callNumTrailingObjects(const BaseTy *Obj,
                         TrailingObjectsBase::OverloadToken<BaseTy>) {
    return 1;
  }

  template <typename T>
  static size_t callNumTrailingObjects(const BaseTy *Obj,
                                       TrailingObjectsBase::OverloadToken<T>) {
    return Obj->numTrailingObjects(TrailingObjectsBase::OverloadToken<T>());
  }

public:
  // Make this (privately inherited) member public.
#ifndef _MSC_VER
  /// Public alias of the base OverloadToken tag type.
  using ParentType::OverloadToken;
#else
  // An MSVC bug prevents the above from working, (last tested at CL version
  // 19.28). "Class5" in TrailingObjectsTest.cpp tests the problematic case.
  /// Public alias of the base OverloadToken tag type (MSVC workaround).
  template <typename T>
  using OverloadToken = typename ParentType::template OverloadToken<T>;
#endif

  /// Return a const pointer to the trailing object array of type \c T.
  ///
  /// \c T must be one of those specified in the class template. The
  /// array may have zero or more elements in it.
  ///
  /// \return A const pointer to the trailing object array of type \c T.
  template <typename T> const T *getTrailingObjects() const {
    verifyTrailingObjectsAssertions<true>();
    // Forwards to an impl function with overloads, since member
    // function templates can't be specialized.
    return this->getTrailingObjectsImpl(
        static_cast<const BaseTy *>(this),
        TrailingObjectsBase::OverloadToken<T>());
  }

  /// Return a pointer to the trailing object array of type \c T.
  ///
  /// \c T must be one of those specified in the class template. The
  /// array may have zero or more elements in it.
  ///
  /// \return A pointer to the trailing object array of type \c T.
  template <typename T> T *getTrailingObjects() {
    return const_cast<T *>(
        static_cast<const TrailingObjects *>(this)->getTrailingObjects<T>());
  }

  // getTrailingObjects() specialization for a single trailing type.
  /// The first (and possibly only) trailing object type.
  using FirstTrailingType =
      typename std::tuple_element_t<0, std::tuple<TrailingTys...>>;

  /// Return a const pointer to the single trailing object array.
  ///
  /// \return A const pointer to the single trailing object array.
  const FirstTrailingType *getTrailingObjects() const {
    static_assert(sizeof...(TrailingTys) == 1,
                  "Can use non-templated getTrailingObjects() only when there "
                  "is a single trailing type");
    verifyTrailingObjectsAssertions<false>();
    return this->getTrailingObjectsImpl(
        static_cast<const BaseTy *>(this),
        TrailingObjectsBase::OverloadToken<FirstTrailingType>());
  }

  /// Return a pointer to the single trailing object array (non-const).
  ///
  /// \return A pointer to the single trailing object array.
  FirstTrailingType *getTrailingObjects() {
    return const_cast<FirstTrailingType *>(
        static_cast<const TrailingObjects *>(this)->getTrailingObjects());
  }

  // Functions that return the trailing objects as ArrayRefs.
  /// Return a mutable ArrayRef to \p N trailing objects of type \c T.
  ///
  /// \param N Number of trailing elements in the view.
  /// \return A mutable ArrayRef over the trailing objects of type \c T.
  template <typename T> MutableArrayRef<T> getTrailingObjects(size_t N) {
    return MutableArrayRef(getTrailingObjects<T>(), N);
  }

  /// Return a const ArrayRef to \p N trailing objects of type \c T.
  ///
  /// \param N Number of trailing elements in the view.
  /// \return A const ArrayRef over the trailing objects of type \c T.
  template <typename T> ArrayRef<T> getTrailingObjects(size_t N) const {
    return ArrayRef(getTrailingObjects<T>(), N);
  }

  /// Return a mutable ArrayRef to \p N elements of the single trailing type.
  ///
  /// \param N Number of trailing elements in the view.
  /// \return A mutable ArrayRef over the single trailing object array.
  MutableArrayRef<FirstTrailingType> getTrailingObjects(size_t N) {
    return MutableArrayRef(getTrailingObjects(), N);
  }

  /// Return a const ArrayRef to \p N elements of the single trailing type.
  ///
  /// \param N Number of trailing elements in the view.
  /// \return A const ArrayRef over the single trailing object array.
  ArrayRef<FirstTrailingType> getTrailingObjects(size_t N) const {
    return ArrayRef(getTrailingObjects(), N);
  }

  // Non-strict forms of templated `getTrailingObjects` that work with single
  // trailing type.
  /// Like \c getTrailingObjects, but allowed when there is only one trailing type.
  ///
  /// \return A const pointer to the trailing object array of type \c T.
  template <typename T> const T *getTrailingObjectsNonStrict() const {
    verifyTrailingObjectsAssertions<false>();
    return this->getTrailingObjectsImpl(
        static_cast<const BaseTy *>(this),
        TrailingObjectsBase::OverloadToken<T>());
  }

  /// Non-const overload of \c getTrailingObjectsNonStrict().
  ///
  /// \return A pointer to the trailing object array of type \c T.
  template <typename T> T *getTrailingObjectsNonStrict() {
    return const_cast<T *>(static_cast<const TrailingObjects *>(this)
                               ->getTrailingObjectsNonStrict<T>());
  }

  /// Return a mutable view of \p N trailing objects of type \p T.
  ///
  /// \param N Number of trailing elements in the view.
  /// \return A mutable ArrayRef over the trailing objects of type \c T.
  template <typename T>
  MutableArrayRef<T> getTrailingObjectsNonStrict(size_t N) {
    return MutableArrayRef(getTrailingObjectsNonStrict<T>(), N);
  }

  /// Return a const view of \p N trailing objects of type \p T.
  ///
  /// \param N Number of trailing elements in the view.
  /// \return A const ArrayRef over the trailing objects of type \c T.
  template <typename T>
  ArrayRef<T> getTrailingObjectsNonStrict(size_t N) const {
    return ArrayRef(getTrailingObjectsNonStrict<T>(), N);
  }

  /// Return the size of trailing data for the given element counts.
  ///
  /// The counts are in the same order as the template arguments. This
  /// does not include the size of the base object. The template
  /// arguments must be the same as those used in the class; they are
  /// supplied here redundantly only so that it's clear what the counts
  /// are counting in callers.
  ///
  /// \param Counts Element counts for each trailing type, in template order.
  /// \return The number of additional bytes needed for the trailing objects.
  template <typename... Tys>
  static constexpr std::enable_if_t<
      std::is_same_v<Foo<TrailingTys...>, Foo<Tys...>>, size_t>
  additionalSizeToAlloc(typename trailing_objects_internal::ExtractSecondType<
                        TrailingTys, size_t>::type... Counts) {
    return ParentType::additionalSizeToAllocImpl(0, Counts...);
  }

  /// Return the total allocation size including the base object.
  ///
  /// This is the same as additionalSizeToAlloc, except it *does* include the
  /// size of the base object.
  ///
  /// \param Counts Element counts for each trailing type, in template order.
  /// \return The total bytes needed for the base object plus trailing data.
  template <typename... Tys>
  static constexpr std::enable_if_t<
      std::is_same_v<Foo<TrailingTys...>, Foo<Tys...>>, size_t>
  totalSizeToAlloc(typename trailing_objects_internal::ExtractSecondType<
                   TrailingTys, size_t>::type... Counts) {
    return sizeof(BaseTy) + ParentType::additionalSizeToAllocImpl(0, Counts...);
  }

  /// Default-construct the trailing-objects mixin.
  TrailingObjects() = default;
  /// Trailing-object mixins are not copy-constructible.
  ///
  /// \param Other Unused; copy construction is deleted.
  TrailingObjects(const TrailingObjects &Other) = delete;
  /// Trailing-object mixins are not move-constructible.
  ///
  /// \param Other Unused; move construction is deleted.
  TrailingObjects(TrailingObjects &&Other) = delete;
  /// Copy-assignment is deleted; derived types own the storage.
  ///
  /// \param Other Unused; copy assignment is deleted.
  TrailingObjects &operator=(const TrailingObjects &Other) = delete;
  /// Move-assignment is deleted; derived types own the storage.
  ///
  /// \param Other Unused; move assignment is deleted.
  TrailingObjects &operator=(TrailingObjects &&Other) = delete;

  /// Uninitialized storage type sized for given trailing object counts.
  ///
  /// A type where its ::with_counts template member has a ::type member
  /// suitable for use as uninitialized storage for an object with the given
  /// trailing object counts. The template arguments are similar to those
  /// of additionalSizeToAlloc.
  ///
  /// Use with FixedSizeStorageOwner, e.g.:
  ///
  /// \code{.cpp}
  ///
  /// MyObj::FixedSizeStorage<void *>::with_counts<1u>::type myStackObjStorage;
  /// MyObj::FixedSizeStorageOwner
  ///     myStackObjOwner(new ((void *)&myStackObjStorage) MyObj);
  /// MyObj *const myStackObjPtr = myStackObjOwner.get();
  ///
  /// \endcode
  template <typename... Tys> struct FixedSizeStorage {
    /// Selects storage sized for the given trailing object counts.
    template <size_t... Counts> struct with_counts {
      /// Byte size of storage for the selected trailing counts.
      enum {
        /// Total bytes needed for the base plus trailing objects.
        Size = totalSizeToAlloc<Tys...>(Counts...)
      };
      /// Aligned uninitialized storage for an object with fixed trailing counts.
      struct type {
        /// Raw bytes providing aligned storage for the object.
        alignas(BaseTy) char buffer[Size];
      };
    };
  };

  /// A type that acts as the owner for an object placed into fixed storage.
  class FixedSizeStorageOwner {
  public:
    /// Take ownership of an object constructed in fixed storage.
    ///
    /// \param p Pointer to the object placed in fixed storage.
    FixedSizeStorageOwner(BaseTy *p) : p(p) {}
    /// Destroy the owned object in place.
    ~FixedSizeStorageOwner() {
      assert(p && "FixedSizeStorageOwner owns null?");
      p->~BaseTy();
    }

    /// Return a pointer to the owned object.
    ///
    /// \return A pointer to the owned object.
    BaseTy *get() { return p; }
    /// Return a const pointer to the owned object in fixed storage.
    ///
    /// \return A const pointer to the owned object.
    const BaseTy *get() const { return p; }

  private:
    FixedSizeStorageOwner(const FixedSizeStorageOwner &) = delete;
    FixedSizeStorageOwner(FixedSizeStorageOwner &&) = delete;
    FixedSizeStorageOwner &operator=(const FixedSizeStorageOwner &) = delete;
    FixedSizeStorageOwner &operator=(FixedSizeStorageOwner &&) = delete;

    BaseTy *const p;
  };
};

} // end namespace llvm

#endif
