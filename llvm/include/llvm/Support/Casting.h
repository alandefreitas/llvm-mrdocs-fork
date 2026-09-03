//===- llvm/Support/Casting.h - Allow flexible, checked, casts --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the isa<X>(), cast<X>(), dyn_cast<X>(),
// cast_if_present<X>(), and dyn_cast_if_present<X>() templates.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CASTING_H
#define LLVM_SUPPORT_CASTING_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <memory>
#include <optional>
#include <type_traits>

namespace llvm {

//===----------------------------------------------------------------------===//
// simplify_type
//===----------------------------------------------------------------------===//

/// Trait that maps a possibly wrapped type to the type used for casting.
///
/// Define a template that can be specialized by smart pointers to reflect the
/// fact that they are automatically dereferenced, and are not involved with the
/// template selection process...  the default implementation is a noop.
// TODO: rename this and/or replace it with other cast traits.
template <typename From> struct simplify_type {
  /// The simplified representation of \c From (identity by default).
  using SimpleType = From; // The real type this represents...

  /// Returns the value in simplified form (passthrough by default).
  ///
  /// \param Val Value to simplify.
  /// \return The simplified value (passthrough by default).
  static SimpleType &getSimplifiedValue(From &Val) { return Val; }
};

/// Const specialization of \c simplify_type that preserves const on the result.
template <typename From> struct simplify_type<const From> {
  /// Non-const simplified type of \c From before const is re-applied.
  using NonConstSimpleType = typename simplify_type<From>::SimpleType;
  /// Simplified type with const applied past pointers as needed.
  using SimpleType = typename add_const_past_pointer<NonConstSimpleType>::type;
  /// Reference or pointer return type for \c getSimplifiedValue.
  using RetType =
      typename add_lvalue_reference_if_not_pointer<SimpleType>::type;

  /// Returns the const value in simplified form.
  ///
  /// \param Val Const value to simplify.
  /// \return The const simplified value.
  static RetType getSimplifiedValue(const From &Val) {
    return simplify_type<From>::getSimplifiedValue(const_cast<From &>(Val));
  }
};

// TODO: add this namespace once everyone is switched to using the new
//       interface.
// namespace detail {

//===----------------------------------------------------------------------===//
// isa_impl
//===----------------------------------------------------------------------===//

// The core of the implementation of isa<X> is here; To and From should be
// the names of classes.  This template can be specialized to customize the
// implementation of isa<> without rewriting it from scratch.
/// Core implementation of \c isa that invokes \c To::classof.
template <typename To, typename From, typename Enabler = void> struct isa_impl {
  /// Returns true if \p Val is an instance of \c To.
  ///
  /// \param Val Value to test.
  /// \return True if \p Val is an instance of \c To.
  static inline bool doit(const From &Val) { return To::classof(&Val); }
};

// Always allow upcasts, and perform no dynamic check for them.
/// Specialization of \c isa_impl that accepts upcasts without a dynamic check.
template <typename To, typename From>
struct isa_impl<To, From, std::enable_if_t<std::is_base_of_v<To, From>>> {
  /// Returns true because \c From is derived from (or is) \c To.
  ///
  /// \param Val Value whose static type already derives from \c To.
  /// \return Always true for this upcast specialization.
  static inline bool doit(const From &Val) { return true; }
};

/// Const-aware wrapper that forwards \c isa checks to \c isa_impl.
template <typename To, typename From> struct isa_impl_cl {
  /// Returns true if \p Val is an instance of \c To.
  ///
  /// \param Val Value to test.
  /// \return True if \p Val is an instance of \c To.
  static inline bool doit(const From &Val) {
    return isa_impl<To, From>::doit(Val);
  }
};

/// \c isa_impl_cl specialization for const \c From values.
template <typename To, typename From> struct isa_impl_cl<To, const From> {
  /// Returns true if \p Val is an instance of \c To.
  ///
  /// \param Val Const value to test.
  /// \return True if \p Val is an instance of \c To.
  static inline bool doit(const From &Val) {
    return isa_impl<To, From>::doit(Val);
  }
};

/// \c isa_impl_cl specialization for const unique_ptr arguments.
template <typename To, typename From>
struct isa_impl_cl<To, const std::unique_ptr<From>> {
  /// Returns true if the pointed-to value is an instance of \c To.
  ///
  /// \param Val Non-null unique_ptr whose pointee is tested.
  /// \return True if the pointee is an instance of \c To.
  static inline bool doit(const std::unique_ptr<From> &Val) {
    assert(Val && "isa<> used on a null pointer");
    return isa_impl_cl<To, From>::doit(*Val);
  }
};

/// \c isa_impl_cl specialization for pointer arguments.
template <typename To, typename From> struct isa_impl_cl<To, From *> {
  /// Returns true if the pointed-to value is an instance of \c To.
  ///
  /// \param Val Non-null pointer to test.
  /// \return True if the pointee is an instance of \c To.
  static inline bool doit(const From *Val) {
    assert(Val && "isa<> used on a null pointer");
    return isa_impl<To, From>::doit(*Val);
  }
};

/// \c isa_impl_cl specialization for const-qualified pointer objects.
template <typename To, typename From> struct isa_impl_cl<To, From *const> {
  /// Returns true if the pointed-to value is an instance of \c To.
  ///
  /// \param Val Non-null pointer to test.
  /// \return True if the pointee is an instance of \c To.
  static inline bool doit(const From *Val) {
    assert(Val && "isa<> used on a null pointer");
    return isa_impl<To, From>::doit(*Val);
  }
};

/// \c isa_impl_cl specialization for pointers to const.
template <typename To, typename From> struct isa_impl_cl<To, const From *> {
  /// Returns true if the pointed-to value is an instance of \c To.
  ///
  /// \param Val Non-null pointer to const to test.
  /// \return True if the pointee is an instance of \c To.
  static inline bool doit(const From *Val) {
    assert(Val && "isa<> used on a null pointer");
    return isa_impl<To, From>::doit(*Val);
  }
};

/// \c isa_impl_cl specialization for const pointers to const.
template <typename To, typename From>
struct isa_impl_cl<To, const From *const> {
  /// Returns true if the pointed-to value is an instance of \c To.
  ///
  /// \param Val Non-null pointer to const to test.
  /// \return True if the pointee is an instance of \c To.
  static inline bool doit(const From *Val) {
    assert(Val && "isa<> used on a null pointer");
    return isa_impl<To, From>::doit(*Val);
  }
};

/// Recursively simplifies \c From via \c simplify_type before the \c isa check.
template <typename To, typename From, typename SimpleFrom>
struct isa_impl_wrap {
  // When From != SimplifiedType, we can simplify the type some more by using
  // the simplify_type template.
  /// Simplifies \p Val and retries the \c isa check on the simplified type.
  ///
  /// \param Val Value to simplify and test.
  /// \return True if the simplified value is an instance of \c To.
  static bool doit(const From &Val) {
    return isa_impl_wrap<To, SimpleFrom,
                         typename simplify_type<SimpleFrom>::SimpleType>::
        doit(simplify_type<const From>::getSimplifiedValue(Val));
  }
};

/// Terminal \c isa_impl_wrap specialization when no further simplification applies.
template <typename To, typename FromTy>
struct isa_impl_wrap<To, FromTy, FromTy> {
  // When From == SimpleType, we are as simple as we are going to get.
  /// Forwards the \c isa check to \c isa_impl_cl.
  ///
  /// \param Val Value to test.
  /// \return True if \p Val is an instance of \c To.
  static bool doit(const FromTy &Val) {
    return isa_impl_cl<To, FromTy>::doit(Val);
  }
};

//===----------------------------------------------------------------------===//
// cast_retty + cast_retty_impl
//===----------------------------------------------------------------------===//

/// Computes the return type of \c cast/\c dyn_cast for \c To and \c From.
template <class To, class From> struct cast_retty;

// Calculate what type the 'cast' function should return, based on a requested
// type of To and a source type of From.
/// Implementation detail that computes the return type of cast for \c To/\c From.
template <class To, class From> struct cast_retty_impl {
  /// Result type of casting \c From to \c To (reference by default).
  using ret_type = To &; // Normal case, return Ty&
};
/// \c cast_retty_impl specialization for const \c From values.
template <class To, class From> struct cast_retty_impl<To, const From> {
  /// Result type of casting const \c From to \c To.
  using ret_type = const To &; // Normal case, return Ty&
};

/// \c cast_retty_impl specialization for pointer arguments.
template <class To, class From> struct cast_retty_impl<To, From *> {
  /// Result type of casting a pointer \c From to \c To.
  using ret_type = To *; // Pointer arg case, return Ty*
};

/// \c cast_retty_impl specialization for pointers to const.
template <class To, class From> struct cast_retty_impl<To, const From *> {
  /// Result type of casting a pointer to const \c From to \c To.
  using ret_type = const To *; // Constant pointer arg case, return const Ty*
};

/// \c cast_retty_impl specialization for const pointers to const.
template <class To, class From> struct cast_retty_impl<To, const From *const> {
  /// Result type of casting a const pointer to const \c From to \c To.
  using ret_type = const To *; // Constant pointer arg case, return const Ty*
};

/// \c cast_retty_impl specialization for \c std::unique_ptr arguments.
template <class To, class From>
struct cast_retty_impl<To, std::unique_ptr<From>> {
private:
  using PointerType = typename cast_retty_impl<To, From *>::ret_type;
  using ResultType = std::remove_pointer_t<PointerType>;

public:
  /// Result type of casting \c unique_ptr<\c From> to \c To.
  using ret_type = std::unique_ptr<ResultType>;
};

/// Selects cast return type after optionally simplifying \c From.
template <class To, class From, class SimpleFrom> struct cast_retty_wrap {
  // When the simplified type and the from type are not the same, use the type
  // simplifier to reduce the type, then reuse cast_retty_impl to get the
  // resultant type.
  /// Cast return type obtained via \c cast_retty on the simplified type.
  using ret_type = typename cast_retty<To, SimpleFrom>::ret_type;
};

/// Terminal \c cast_retty_wrap when \c From needs no further simplification.
template <class To, class FromTy> struct cast_retty_wrap<To, FromTy, FromTy> {
  // When the simplified type is equal to the from type, use it directly.
  /// Cast return type obtained directly from \c cast_retty_impl.
  using ret_type = typename cast_retty_impl<To, FromTy>::ret_type;
};

/// Computes the return type of \c cast/\c dyn_cast for \c To and \c From.
template <class To, class From> struct cast_retty {
  /// The type returned by casting \c From to \c To.
  using ret_type = typename cast_retty_wrap<
      To, From, typename simplify_type<From>::SimpleType>::ret_type;
};

//===----------------------------------------------------------------------===//
// cast_convert_val
//===----------------------------------------------------------------------===//

// Ensure the non-simple values are converted using the simplify_type template
// that may be specialized by smart pointers...
//
/// Performs the actual cast after simplifying non-simple source values.
template <class To, class From, class SimpleFrom> struct cast_convert_val {
  // This is not a simple type, use the template to simplify it...
  /// Simplifies \p Val and continues casting on the simplified type.
  ///
  /// \param Val Value to simplify and cast.
  /// \return The cast result for the simplified value.
  static typename cast_retty<To, From>::ret_type doit(const From &Val) {
    return cast_convert_val<To, SimpleFrom,
                            typename simplify_type<SimpleFrom>::SimpleType>::
        doit(simplify_type<From>::getSimplifiedValue(const_cast<From &>(Val)));
  }
};

/// Terminal \c cast_convert_val specialization for non-pointer values.
template <class To, class FromTy> struct cast_convert_val<To, FromTy, FromTy> {
  // If it's a reference, switch to a pointer to do the cast and then deref it.
  /// Casts reference \p Val to \c To by pointer cast and dereference.
  ///
  /// \param Val Value to cast.
  /// \return A reference to \p Val cast as \c To.
  static typename cast_retty<To, FromTy>::ret_type doit(const FromTy &Val) {
    return *(std::remove_reference_t<typename cast_retty<To, FromTy>::ret_type>
                 *)&const_cast<FromTy &>(Val);
  }
};

/// Terminal \c cast_convert_val specialization for pointer values.
template <class To, class FromTy>
struct cast_convert_val<To, FromTy *, FromTy *> {
  // If it's a pointer, we can use c-style casting directly.
  /// Casts pointer \p Val to the cast return type.
  ///
  /// \param Val Pointer to cast.
  /// \return \p Val cast to the computed cast return type.
  static typename cast_retty<To, FromTy *>::ret_type doit(const FromTy *Val) {
    return (typename cast_retty<To, FromTy *>::ret_type) const_cast<FromTy *>(
        Val);
  }
};

//===----------------------------------------------------------------------===//
// is_simple_type
//===----------------------------------------------------------------------===//

/// Trait that is true when \c X is already a simplified type.
template <class X> struct is_simple_type {
  /// True when \c X equals its \c simplify_type::SimpleType.
  static const bool value =
      std::is_same_v<X, typename simplify_type<X>::SimpleType>;
};

// } // namespace detail

//===----------------------------------------------------------------------===//
// CastIsPossible
//===----------------------------------------------------------------------===//

/// Trait that reports whether a cast from \c From to \c To is possible.
///
/// This struct provides a way to check if a given cast is possible. It provides
/// a static function called isPossible that is used to check if a cast can be
/// performed. It should be overridden like this:
///
/// template<> struct CastIsPossible<foo, bar> {
///   static inline bool isPossible(const bar &b) {
///     return bar.isFoo();
///   }
/// };
template <typename To, typename From, typename Enable = void>
struct CastIsPossible {
  /// Returns true if a cast from \p f to \c To is possible.
  ///
  /// \param f Value to test for a possible cast to \c To.
  /// \return True if a cast from \p f to \c To is possible.
  static inline bool isPossible(const From &f) {
    return isa_impl_wrap<
        To, const From,
        typename simplify_type<const From>::SimpleType>::doit(f);
  }
};

// Needed for optional unwrapping. This could be implemented with isa_impl, but
// we want to implement things in the new method and move old implementations
// over. In fact, some of the isa_impl templates should be moved over to
// CastIsPossible.
/// \c CastIsPossible specialization for \c std::optional sources.
template <typename To, typename From>
struct CastIsPossible<To, std::optional<From>> {
  /// Returns true if the engaged optional value can be cast to \c To.
  ///
  /// \param f Non-empty optional whose value is tested.
  /// \return True if the engaged value can be cast to \c To.
  static inline bool isPossible(const std::optional<From> &f) {
    assert(f && "CastIsPossible::isPossible called on a nullopt!");
    return isa_impl_wrap<
        To, const From,
        typename simplify_type<const From>::SimpleType>::doit(*f);
  }
};

/// Upcasting (from derived to base) and casting from a type to itself should
/// always be possible.
template <typename To, typename From>
struct CastIsPossible<To, From, std::enable_if_t<std::is_base_of_v<To, From>>> {
  /// Returns true because upcasts and self-casts are always possible.
  ///
  /// \param f Value whose static type already derives from \c To.
  /// \return Always true for upcasts and self-casts.
  static inline bool isPossible(const From &f) { return true; }
};

//===----------------------------------------------------------------------===//
// Cast traits
//===----------------------------------------------------------------------===//

/// All of these cast traits are meant to be implementations for useful casts
/// that users may want to use that are outside the standard behavior. An
/// example of how to use a special cast called `CastTrait` is:
///
/// template<> struct CastInfo<foo, bar> : public CastTrait<foo, bar> {};
///
/// Essentially, if your use case falls directly into one of the use cases
/// supported by a given cast trait, simply inherit your special CastInfo
/// directly from one of these to avoid having to reimplement the boilerplate
/// `isPossible/castFailed/doCast/doCastIfPossible`. A cast trait can also
/// provide a subset of those functions.

/// Provides a declarative \c castFailed that constructs a null \c To.
///
/// This cast trait just provides castFailed for the specified `To` type to make
/// CastInfo specializations more declarative. In order to use this, the target
/// result type must be `To` and `To` must be constructible from `nullptr`.
template <typename To> struct NullableValueCastFailed {
  /// Returns a null \c To used when a cast fails.
  ///
  /// \return A null \c To constructed from \c nullptr.
  static To castFailed() { return To(nullptr); }
};

/// Provides a default \c doCastIfPossible that uses \c isPossible and \c doCast.
///
/// This cast trait just provides the default implementation of doCastIfPossible
/// to make CastInfo specializations more declarative. The `Derived` template
/// parameter *must* be provided for forwarding castFailed and doCast.
template <typename To, typename From, typename Derived>
struct DefaultDoCastIfPossible {
  /// Casts \p f to \c To if possible; otherwise returns \c castFailed().
  ///
  /// \param f Value to cast when the cast is possible.
  /// \return The cast result, or \c castFailed() when the cast is not possible.
  static To doCastIfPossible(From f) {
    if (!Derived::isPossible(f))
      return Derived::castFailed();
    return Derived::doCast(f);
  }
};

namespace detail {
/// A helper to derive the type to use with `Self` for cast traits, when the
/// provided CRTP derived type is allowed to be void.
template <typename OptionalDerived, typename Default>
using SelfType = std::conditional_t<std::is_same_v<OptionalDerived, void>,
                                    Default, OptionalDerived>;
} // namespace detail

/// Cast trait for constructing a value-typed \c To from a \c From pointer.
///
/// This cast trait provides casting for the specific case of casting to a
/// value-typed object from a pointer-typed object. Note that `To` must be
/// nullable/constructible from a pointer to `From` to use this cast.
template <typename To, typename From, typename Derived = void>
struct ValueFromPointerCast
    : public CastIsPossible<To, From *>,
      public NullableValueCastFailed<To>,
      public DefaultDoCastIfPossible<
          To, From *,
          detail::SelfType<Derived, ValueFromPointerCast<To, From>>> {
  /// Constructs a \c To from pointer \p f.
  ///
  /// \param f Pointer used to construct the \c To result.
  /// \return A \c To constructed from \p f.
  static inline To doCast(From *f) { return To(f); }
};

/// Cast trait that moves a \c unique_ptr during a successful cast.
///
/// This cast trait provides std::unique_ptr casting. It has the semantics of
/// moving the contents of the input unique_ptr into the output unique_ptr
/// during the cast. It's also a good example of how to implement a move-only
/// cast.
template <typename To, typename From, typename Derived = void>
struct UniquePtrCast : CastIsPossible<To, From *> {
  /// Derived cast type used to call static helpers through CRTP.
  using Self = detail::SelfType<Derived, UniquePtrCast<To, From>>;
  /// unique_ptr result type produced by this unique_ptr cast.
  using CastResultType = std::unique_ptr<
      std::remove_reference_t<typename cast_retty<To, From>::ret_type>>;

  /// Moves \p f into a unique_ptr of the cast result type.
  ///
  /// \param f unique_ptr whose ownership is transferred on success.
  /// \return A unique_ptr holding the cast pointee.
  static inline CastResultType doCast(std::unique_ptr<From> &&f) {
    return CastResultType((typename CastResultType::element_type *)f.release());
  }

  /// Returns a null unique_ptr when the cast fails.
  ///
  /// \return A null unique_ptr of the cast result type.
  static inline CastResultType castFailed() { return CastResultType(nullptr); }

  /// Moves and casts \p f if possible; otherwise returns null.
  ///
  /// \param f unique_ptr to test and possibly move from.
  /// \return The cast unique_ptr on success, or null on failure.
  static inline CastResultType doCastIfPossible(std::unique_ptr<From> &f) {
    if (!Self::isPossible(f.get()))
      return castFailed();
    return doCast(std::move(f));
  }
};

/// Cast trait that wraps value casts in \c std::optional.
///
/// This cast trait provides std::optional<T> casting. This means that if you
/// have a value type, you can cast it to another value type and have dyn_cast
/// return an std::optional<T>.
template <typename To, typename From, typename Derived = void>
struct OptionalValueCast
    : public CastIsPossible<To, From>,
      public DefaultDoCastIfPossible<
          std::optional<To>, From,
          detail::SelfType<Derived, OptionalValueCast<To, From>>> {
  /// Returns an empty optional when the cast fails.
  ///
  /// \return An empty \c std::optional<To>.
  static inline std::optional<To> castFailed() { return std::optional<To>{}; }

  /// Constructs a \c To from \p f and wraps it in \c std::optional.
  ///
  /// \param f Value used to construct the optional result.
  /// \return An engaged optional holding the cast \c To.
  static inline std::optional<To> doCast(const From &f) { return To(f); }
};

/// Forwards const cast sources to a non-const \c CastInfo implementation.
///
/// Provides a cast trait that strips `const` from types to make it easier to
/// implement a const-version of a non-const cast. It just removes boilerplate
/// and reduces the amount of code you as the user need to implement. You can
/// use it like this:
///
/// template<> struct CastInfo<foo, bar> {
///   ...verbose implementation...
/// };
///
/// template<> struct CastInfo<foo, const bar> : public
///        ConstStrippingForwardingCast<foo, const bar, CastInfo<foo, bar>> {};
///
template <typename To, typename From, typename ForwardTo>
struct ConstStrippingForwardingCast {
  /// From with cv-qualifiers and pointer removed.
  using DecayedFrom = std::remove_cv_t<std::remove_pointer_t<From>>;
  // Now if it's a pointer, add it back. Otherwise, we want a ref.
  /// Non-const pointer or reference form of \c From used for forwarding.
  using NonConstFrom =
      std::conditional_t<std::is_pointer_v<From>, DecayedFrom *, DecayedFrom &>;

  /// Forwards the possibility check to the non-const \c ForwardTo trait.
  ///
  /// \param f Const value forwarded after const is stripped.
  /// \return True if the non-const forward cast reports the cast is possible.
  static inline bool isPossible(const From &f) {
    return ForwardTo::isPossible(const_cast<NonConstFrom>(f));
  }

  /// Forwards the failed-cast sentinel to the non-const \c ForwardTo trait.
  ///
  /// \return The failed-cast sentinel from \c ForwardTo.
  static inline decltype(auto) castFailed() { return ForwardTo::castFailed(); }

  /// Forwards the cast to the non-const \c ForwardTo trait.
  ///
  /// \param f Const value forwarded after const is stripped.
  /// \return The result of the non-const forward cast.
  static inline decltype(auto) doCast(const From &f) {
    return ForwardTo::doCast(const_cast<NonConstFrom>(f));
  }

  /// Forwards a fallible cast to the non-const \c ForwardTo trait.
  ///
  /// \param f Const value forwarded after const is stripped.
  /// \return The fallible cast result from \c ForwardTo.
  static inline decltype(auto) doCastIfPossible(const From &f) {
    return ForwardTo::doCastIfPossible(const_cast<NonConstFrom>(f));
  }
};

/// Implements reference casts by forwarding through a pointer \c CastInfo.
///
/// Provides a cast trait that uses a defined pointer to pointer cast as a base
/// for reference-to-reference casts. Note that it does not provide castFailed
/// and doCastIfPossible because a pointer-to-pointer cast would likely just
/// return `nullptr` which could cause nullptr dereference. You can use it like
/// this:
///
///   template <> struct CastInfo<foo, bar *> { ... verbose implementation... };
///
///   template <>
///   struct CastInfo<foo, bar>
///       : public ForwardToPointerCast<foo, bar, CastInfo<foo, bar *>> {};
///
template <typename To, typename From, typename ForwardTo>
struct ForwardToPointerCast {
  /// Return true if casting the address of \p f is possible.
  ///
  /// \param f Reference whose address is tested for a pointer cast.
  /// \return True if the pointer cast of \c &f is possible.
  static inline bool isPossible(const From &f) {
    return ForwardTo::isPossible(&f);
  }

  /// Cast by taking the address of \p f and dereferencing the pointer cast.
  ///
  /// \param f Reference to cast via its address.
  /// \return A reference to the pointer-cast result.
  static inline decltype(auto) doCast(const From &f) {
    return *ForwardTo::doCast(&f);
  }
};

//===----------------------------------------------------------------------===//
// CastInfo
//===----------------------------------------------------------------------===//

/// Customizes how casts from \c From to \c To are performed.
///
/// This struct provides a method for customizing the way a cast is performed.
/// It inherits from CastIsPossible, to support the case of declaring many
/// CastIsPossible specializations without having to specialize the full
/// CastInfo.
///
/// In order to specialize different behaviors, specify different functions in
/// your CastInfo specialization.
/// For isa<> customization, provide:
///
///   `static bool isPossible(const From &f)`
///
/// For cast<> customization, provide:
///
///  `static To doCast(const From &f)`
///
/// For dyn_cast<> and the *_if_present<> variants' customization, provide:
///
///  `static To castFailed()` and `static To doCastIfPossible(const From &f)`
///
/// Your specialization might look something like this:
///
///  template<> struct CastInfo<foo, bar> : public CastIsPossible<foo, bar> {
///    static inline foo doCast(const bar &b) {
///      return foo(const_cast<bar &>(b));
///    }
///    static inline foo castFailed() { return foo(); }
///    static inline foo doCastIfPossible(const bar &b) {
///      if (!CastInfo<foo, bar>::isPossible(b))
///        return castFailed();
///      return doCast(b);
///    }
///  };

// The default implementations of CastInfo don't use cast traits for now because
// we need to specify types all over the place due to the current expected
// casting behavior and the way cast_retty works. New use cases can and should
// take advantage of the cast traits whenever possible!

template <typename To, typename From, typename Enable = void>
struct CastInfo : CastIsPossible<To, From> {
  /// This \c CastInfo specialization, used for CRTP-style calls.
  using Self = CastInfo<To, From, Enable>;

  /// Return type produced by successful casts in this specialization.
  using CastReturnType = typename cast_retty<To, From>::ret_type;

  /// Unconditionally converts \p f to the cast return type.
  ///
  /// \param f Value to cast.
  /// \return \p f converted to the cast return type.
  static inline CastReturnType doCast(const From &f) {
    return cast_convert_val<
        To, From,
        typename simplify_type<From>::SimpleType>::doit(const_cast<From &>(f));
  }

  // This assumes that you can construct the cast return type from `nullptr`.
  // This is largely to support legacy use cases - if you don't want this
  // behavior you should specialize CastInfo for your use case.
  /// Returns a null cast result used when the cast fails.
  ///
  /// \return A null value of the cast return type.
  static inline CastReturnType castFailed() { return CastReturnType(nullptr); }

  /// Casts \p f if possible; otherwise returns \c castFailed().
  ///
  /// \param f Value to cast when the cast is possible.
  /// \return The cast result, or \c castFailed() when not possible.
  static inline CastReturnType doCastIfPossible(const From &f) {
    if (!Self::isPossible(f))
      return castFailed();
    return doCast(f);
  }
};

/// \c CastInfo specialization that forwards through \c simplify_type.
///
/// This struct provides an overload for CastInfo where From has simplify_type
/// defined. This simply forwards to the appropriate CastInfo with the
/// simplified type/value, so you don't have to implement both.
template <typename To, typename From>
struct CastInfo<To, From, std::enable_if_t<!is_simple_type<From>::value>> {
  /// This \c CastInfo specialization, used for CRTP-style calls.
  using Self = CastInfo<To, From>;
  /// Source type after \c simplify_type is applied.
  using SimpleFrom = typename simplify_type<From>::SimpleType;
  /// \c CastInfo for the simplified source type.
  using SimplifiedSelf = CastInfo<To, SimpleFrom>;

  /// Returns true if the simplified form of \p f can be cast to \c To.
  ///
  /// \param f Value to simplify before testing the cast.
  /// \return True if the simplified value can be cast to \c To.
  static inline bool isPossible(From &f) {
    return SimplifiedSelf::isPossible(
        simplify_type<From>::getSimplifiedValue(f));
  }

  /// Casts the simplified form of \p f to \c To.
  ///
  /// \param f Value to simplify before casting.
  /// \return The cast of the simplified value.
  static inline decltype(auto) doCast(From &f) {
    return SimplifiedSelf::doCast(simplify_type<From>::getSimplifiedValue(f));
  }

  /// Returns the failed-cast sentinel from the simplified \c CastInfo.
  ///
  /// \return The failed-cast sentinel from the simplified \c CastInfo.
  static inline decltype(auto) castFailed() {
    return SimplifiedSelf::castFailed();
  }

  /// Casts the simplified form of \p f if possible; otherwise fails.
  ///
  /// \param f Value to simplify before a fallible cast.
  /// \return The fallible cast of the simplified value.
  static inline decltype(auto) doCastIfPossible(From &f) {
    return SimplifiedSelf::doCastIfPossible(
        simplify_type<From>::getSimplifiedValue(f));
  }
};

//===----------------------------------------------------------------------===//
// Pre-specialized CastInfo
//===----------------------------------------------------------------------===//

/// Provide a CastInfo specialized for std::unique_ptr.
template <typename To, typename From>
struct CastInfo<To, std::unique_ptr<From>> : public UniquePtrCast<To, From> {};

/// Provide a CastInfo specialized for casting out of \c std::optional.
///
/// It's assumed that if the input is std::optional<From> that the output can be
/// std::optional<To>. If that's not the case, specialize CastInfo for your use
/// case.
template <typename To, typename From>
struct CastInfo<To, std::optional<From>> : OptionalValueCast<To, From> {};

/// isa<X> - Return true if the parameter to the template is an instance of one
/// of the template type arguments.  Used like this:
///
///  if (isa<Type>(myVal)) { ... }
///  if (isa<Type0, Type1, Type2>(myVal)) { ... }
///
/// \param Val Value to test against the \c To types.
/// \return True if \p Val is an instance of any of the \c To types.
template <typename... To, typename From>
[[nodiscard]] inline bool isa(const From &Val) {
  return (CastInfo<To, const From>::isPossible(Val) || ...);
}

/// Asserting cast of a present const value \p Val to \c To.
///
/// This casting operator asserts that the type is correct, so it does not
/// return null on failure. It does not allow a null argument (use
/// cast_if_present for that). It is typically used like this:
///
///  cast<Instruction>(myVal)->getParent()
///
/// \param Val Present value to cast.
/// \return \p Val cast to \c To.
template <typename To, typename From>
[[nodiscard]] inline decltype(auto) cast(const From &Val) {
  assert(isa<To>(Val) && "cast<Ty>() argument of incompatible type!");
  return CastInfo<To, const From>::doCast(Val);
}

/// Asserting cast of a present non-const value \p Val to \c To.
///
/// \param Val Present non-const value to cast.
/// \return \p Val cast to \c To.
template <typename To, typename From>
[[nodiscard]] inline decltype(auto) cast(From &Val) {
  assert(isa<To>(Val) && "cast<Ty>() argument of incompatible type!");
  return CastInfo<To, From>::doCast(Val);
}

/// Asserting cast of a non-null pointer \p Val to \c To.
///
/// \param Val Non-null pointer to cast.
/// \return \p Val cast to \c To.
template <typename To, typename From>
[[nodiscard]] inline decltype(auto) cast(From *Val) {
  assert(isa<To>(Val) && "cast<Ty>() argument of incompatible type!");
  return CastInfo<To, From *>::doCast(Val);
}

/// Asserting cast that moves a \c unique_ptr<\c From> into \c To.
///
/// \param Val unique_ptr to move and cast.
/// \return A unique_ptr of \c To taking ownership of \p Val.
template <typename To, typename From>
[[nodiscard]] inline decltype(auto) cast(std::unique_ptr<From> &&Val) {
  assert(isa<To>(Val) && "cast<Ty>() argument of incompatible type!");
  return CastInfo<To, std::unique_ptr<From>>::doCast(std::move(Val));
}

//===----------------------------------------------------------------------===//
// ValueIsPresent
//===----------------------------------------------------------------------===//

/// True if \c T is a pointer or constructible from \c nullptr.
template <typename T>
constexpr bool IsNullable =
    std::is_pointer_v<T> || std::is_constructible_v<T, std::nullptr_t>;

/// Trait that tests whether a value is present and can unwrap it.
///
/// ValueIsPresent provides a way to check if a value is, well, present. For
/// pointers, this is the equivalent of checking against nullptr, for Optionals
/// this is the equivalent of checking hasValue(). It also provides a method for
/// unwrapping a value (think calling .value() on an optional).

// Generic values can't *not* be present.
template <typename T, typename Enable = void> struct ValueIsPresent {
  /// Underlying value type after unwrapping presence wrappers.
  using UnwrappedType = T;
  /// Always true for non-nullable value types.
  ///
  /// \param t Value that is always treated as present.
  /// \return Always true for non-nullable value types.
  static inline bool isPresent(const T &t) { return true; }
  /// Returns \p t unchanged.
  ///
  /// \param t Value to return as-is.
  /// \return \p t unchanged.
  static inline decltype(auto) unwrapValue(T &t) { return t; }
};

// Optional provides its own way to check if something is present.
/// \c ValueIsPresent specialization for \c std::optional.
template <typename T> struct ValueIsPresent<std::optional<T>> {
  /// Engaged value type stored in the optional.
  using UnwrappedType = T;
  /// Returns true if the optional currently holds a value.
  ///
  /// \param t Optional to test for engagement.
  /// \return True if the optional currently holds a value.
  static inline bool isPresent(const std::optional<T> &t) {
    return t.has_value();
  }
  /// Returns a reference to the engaged optional value.
  ///
  /// \param t Engaged optional to unwrap.
  /// \return A reference to the engaged value.
  static inline decltype(auto) unwrapValue(std::optional<T> &t) { return *t; }
};

// If something is "nullable" then we just compare it to nullptr to see if it
// exists.
/// \c ValueIsPresent specialization for nullable pointer-like types.
template <typename T>
struct ValueIsPresent<T, std::enable_if_t<IsNullable<T>>> {
  /// Underlying nullable type (unchanged by unwrapping).
  using UnwrappedType = T;
  /// Returns true if \p t is not a null value.
  ///
  /// \param t Nullable value to compare against null.
  /// \return True if \p t is not a null value.
  static inline bool isPresent(const T &t) { return t != T(nullptr); }
  /// Returns \p t unchanged.
  ///
  /// \param t Nullable value to return as-is.
  /// \return \p t unchanged.
  static inline decltype(auto) unwrapValue(T &t) { return t; }
};

namespace detail {
// Convenience function we can use to check if a value is present. Because of
// simplify_type, we have to call it on the simplified type for now.
template <typename T> inline bool isPresent(const T &t) {
  return ValueIsPresent<typename simplify_type<T>::SimpleType>::isPresent(
      simplify_type<T>::getSimplifiedValue(const_cast<T &>(t)));
}

// Convenience function we can use to unwrap a value.
template <typename T> inline decltype(auto) unwrapValue(T &t) {
  return ValueIsPresent<T>::unwrapValue(t);
}
} // namespace detail

/// Casts a present const value \p Val to \c To, or returns null on failure.
///
/// This casting operator returns null if the argument is of the wrong type, so
/// it can be used to test for a type as well as cast if successful. The value
/// passed in must be present; if not, use dyn_cast_if_present. Typically used
/// like:
///
///  if (const Instruction *I = dyn_cast<Instruction>(myVal)) { ... }
///
/// \param Val Present value to cast.
/// \return \p Val cast to \c To, or null if the types are incompatible.
template <typename To, typename From>
[[nodiscard]] inline decltype(auto) dyn_cast(const From &Val) {
  assert(detail::isPresent(Val) && "dyn_cast on a non-existent value");
  return CastInfo<To, const From>::doCastIfPossible(Val);
}

/// Casts a present non-const value \p Val to \c To, or returns null on failure.
///
/// \param Val Present non-const value to cast.
/// \return \p Val cast to \c To, or null if the types are incompatible.
template <typename To, typename From>
[[nodiscard]] inline decltype(auto) dyn_cast(From &Val) {
  assert(detail::isPresent(Val) && "dyn_cast on a non-existent value");
  return CastInfo<To, From>::doCastIfPossible(Val);
}

/// Casts a present pointer \p Val to \c To, or returns null on failure.
///
/// \param Val Present pointer to cast.
/// \return \p Val cast to \c To, or null if the types are incompatible.
template <typename To, typename From>
[[nodiscard]] inline decltype(auto) dyn_cast(From *Val) {
  assert(detail::isPresent(Val) && "dyn_cast on a non-existent value");
  return CastInfo<To, From *>::doCastIfPossible(Val);
}

/// Attempt a dyn_cast of unique_ptr \p Val, moving on success or returning null.
///
/// \param Val Present unique_ptr to cast, moved from on success.
/// \return A unique_ptr of \c To on success, or null on failure.
template <typename To, typename From>
[[nodiscard]] inline decltype(auto) dyn_cast(std::unique_ptr<From> &Val) {
  assert(detail::isPresent(Val) && "dyn_cast on a non-existent value");
  return CastInfo<To, std::unique_ptr<From>>::doCastIfPossible(Val);
}

/// isa_and_present<X> - Functionally identical to isa, except that a null value
/// is accepted.
///
/// \param Val Value that may be missing; returns false when not present.
/// \return True if \p Val is present and matches any of the \c X types.
template <typename... X, class Y>
[[nodiscard]] inline bool isa_and_present(const Y &Val) {
  if (!detail::isPresent(Val))
    return false;
  return isa<X...>(Val);
}

/// Return true if \p Val is present and matches any of \p X.
///
/// \param Val Value that may be missing; returns false when not present.
/// \return True if \p Val is present and matches any of the \c X types.
template <typename... X, class Y>
[[nodiscard]] inline bool isa_and_nonnull(const Y &Val) {
  return isa_and_present<X...>(Val);
}

/// cast_if_present<X> - Functionally identical to cast, except that a null
/// value is accepted.
///
/// \param Val Value that may be missing; returns the failed-cast sentinel then.
/// \return \p Val cast to \c X, or the failed-cast sentinel if \p Val is missing.
template <class X, class Y>
[[nodiscard]] inline auto cast_if_present(const Y &Val) {
  if (!detail::isPresent(Val))
    return CastInfo<X, const Y>::castFailed();
  assert(isa<X>(Val) && "cast_if_present<Ty>() argument of incompatible type!");
  return cast<X>(detail::unwrapValue(Val));
}

/// Asserting cast of non-const value \p Val to \c X, accepting a null argument.
///
/// \param Val Value that may be missing; returns the failed-cast sentinel then.
/// \return \p Val cast to \c X, or the failed-cast sentinel if \p Val is missing.
template <class X, class Y> [[nodiscard]] inline auto cast_if_present(Y &Val) {
  if (!detail::isPresent(Val))
    return CastInfo<X, Y>::castFailed();
  assert(isa<X>(Val) && "cast_if_present<Ty>() argument of incompatible type!");
  return cast<X>(detail::unwrapValue(Val));
}

/// Asserting cast of pointer \p Val to \c X, accepting a null argument.
///
/// \param Val Pointer that may be null; returns the failed-cast sentinel then.
/// \return \p Val cast to \c X, or the failed-cast sentinel if \p Val is null.
template <class X, class Y> [[nodiscard]] inline auto cast_if_present(Y *Val) {
  if (!detail::isPresent(Val))
    return CastInfo<X, Y *>::castFailed();
  assert(isa<X>(Val) && "cast_if_present<Ty>() argument of incompatible type!");
  return cast<X>(detail::unwrapValue(Val));
}

/// Asserting cast of a movable unique_ptr \p Val to \c X, accepting null.
///
/// \param Val unique_ptr that may be null; moved from on success.
/// \return A unique_ptr of \c X on success, or null if \p Val is null.
template <class X, class Y>
[[nodiscard]] inline auto cast_if_present(std::unique_ptr<Y> &&Val) {
  if (!detail::isPresent(Val))
    return UniquePtrCast<X, Y>::castFailed();
  return UniquePtrCast<X, Y>::doCast(std::move(Val));
}

// Provide a forwarding from cast_or_null to cast_if_present for current
// users. This is deprecated and will be removed in a future patch, use
// cast_if_present instead.
/// Deprecated alias for \c cast_if_present on a const value.
///
/// \param Val Value forwarded to \c cast_if_present.
/// \return The result of \c cast_if_present<X>(Val).
template <class X, class Y> auto cast_or_null(const Y &Val) {
  return cast_if_present<X>(Val);
}

/// Deprecated alias for \c cast_if_present on a non-const value.
///
/// \param Val Value forwarded to \c cast_if_present.
/// \return The result of \c cast_if_present<X>(Val).
template <class X, class Y> auto cast_or_null(Y &Val) {
  return cast_if_present<X>(Val);
}

/// Deprecated alias for \c cast_if_present on a pointer.
///
/// \param Val Pointer forwarded to \c cast_if_present.
/// \return The result of \c cast_if_present<X>(Val).
template <class X, class Y> auto cast_or_null(Y *Val) {
  return cast_if_present<X>(Val);
}

/// Deprecated alias for \c cast_if_present on a movable \c unique_ptr.
///
/// \param Val unique_ptr forwarded to \c cast_if_present.
/// \return The result of \c cast_if_present<X>(std::move(Val)).
template <class X, class Y> auto cast_or_null(std::unique_ptr<Y> &&Val) {
  return cast_if_present<X>(std::move(Val));
}

/// dyn_cast_if_present<X> - Functionally identical to dyn_cast, except that a
/// null (or none in the case of optionals) value is accepted.
///
/// \param Val Value that may be missing; returns the failed-cast sentinel then.
/// \return \p Val cast to \c X if possible, or the failed-cast sentinel.
template <class X, class Y> auto dyn_cast_if_present(const Y &Val) {
  if (!detail::isPresent(Val))
    return CastInfo<X, const Y>::castFailed();
  return CastInfo<X, const Y>::doCastIfPossible(detail::unwrapValue(Val));
}

/// Casts non-const \p Val to \c X if possible, accepting a null argument.
///
/// \param Val Value that may be missing; returns the failed-cast sentinel then.
/// \return \p Val cast to \c X if possible, or the failed-cast sentinel.
template <class X, class Y> auto dyn_cast_if_present(Y &Val) {
  if (!detail::isPresent(Val))
    return CastInfo<X, Y>::castFailed();
  return CastInfo<X, Y>::doCastIfPossible(detail::unwrapValue(Val));
}

/// Casts pointer \p Val to \c X if possible, accepting a null argument.
///
/// \param Val Pointer that may be null; returns the failed-cast sentinel then.
/// \return \p Val cast to \c X if possible, or the failed-cast sentinel.
template <class X, class Y> auto dyn_cast_if_present(Y *Val) {
  if (!detail::isPresent(Val))
    return CastInfo<X, Y *>::castFailed();
  return CastInfo<X, Y *>::doCastIfPossible(detail::unwrapValue(Val));
}

// Forwards to dyn_cast_if_present to avoid breaking current users. This is
// deprecated and will be removed in a future patch, use
// dyn_cast_if_present instead.
/// Deprecated alias for \c dyn_cast_if_present on a const value.
///
/// \param Val Value forwarded to \c dyn_cast_if_present.
/// \return The result of \c dyn_cast_if_present<X>(Val).
template <class X, class Y> auto dyn_cast_or_null(const Y &Val) {
  return dyn_cast_if_present<X>(Val);
}

/// Deprecated alias for \c dyn_cast_if_present on a non-const value.
///
/// \param Val Value forwarded to \c dyn_cast_if_present.
/// \return The result of \c dyn_cast_if_present<X>(Val).
template <class X, class Y> auto dyn_cast_or_null(Y &Val) {
  return dyn_cast_if_present<X>(Val);
}

/// Deprecated alias for \c dyn_cast_if_present on a pointer.
///
/// \param Val Pointer forwarded to \c dyn_cast_if_present.
/// \return The result of \c dyn_cast_if_present<X>(Val).
template <class X, class Y> auto dyn_cast_or_null(Y *Val) {
  return dyn_cast_if_present<X>(Val);
}

/// Cast unique_ptr \p Val to \c X, taking ownership only when the cast succeeds.
///
/// unique_dyn_cast<X> - Given a unique_ptr<Y>, try to return a unique_ptr<X>,
/// taking ownership of the input pointer iff isa<X>(Val) is true.  If the
/// cast is successful, From refers to nullptr on exit and the casted value
/// is returned.  If the cast is unsuccessful, the function returns nullptr
/// and From is unchanged.
///
/// \param Val unique_ptr to test; moved from only when the cast succeeds.
/// \return A unique_ptr of \c X on success, or null if the cast fails.
template <class X, class Y>
[[nodiscard]] inline typename CastInfo<X, std::unique_ptr<Y>>::CastResultType
unique_dyn_cast(std::unique_ptr<Y> &Val) {
  if (!isa<X>(Val))
    return nullptr;
  return cast<X>(std::move(Val));
}

/// Rvalue overload that forwards to the lvalue \c unique_dyn_cast.
///
/// \param Val unique_ptr forwarded to the lvalue overload.
/// \return The result of the lvalue \c unique_dyn_cast.
template <class X, class Y>
[[nodiscard]] inline auto unique_dyn_cast(std::unique_ptr<Y> &&Val) {
  return unique_dyn_cast<X, Y>(Val);
}

// unique_dyn_cast_or_null<X> - Functionally identical to unique_dyn_cast,
// except that a null value is accepted.
/// Like \c unique_dyn_cast, but accepts a null \c unique_ptr.
///
/// \param Val unique_ptr that may be null; moved from only on success.
/// \return A unique_ptr of \c X on success, or null if \p Val is null or the cast fails.
template <class X, class Y>
[[nodiscard]] inline typename CastInfo<X, std::unique_ptr<Y>>::CastResultType
unique_dyn_cast_or_null(std::unique_ptr<Y> &Val) {
  if (!Val)
    return nullptr;
  return unique_dyn_cast<X, Y>(Val);
}

/// Like \c unique_dyn_cast, but accepts a null rvalue \c unique_ptr.
///
/// \param Val unique_ptr that may be null; forwarded to the lvalue overload.
/// \return The result of the lvalue \c unique_dyn_cast_or_null.
template <class X, class Y>
[[nodiscard]] inline auto unique_dyn_cast_or_null(std::unique_ptr<Y> &&Val) {
  return unique_dyn_cast_or_null<X, Y>(Val);
}

//===----------------------------------------------------------------------===//
// Isa Predicates
//===----------------------------------------------------------------------===//

/// These are wrappers over isa* function that allow them to be used in generic
/// algorithms such as `llvm:all_of`, `llvm::none_of`, etc. This is accomplished
/// by exposing the isa* functions through function objects with a generic
/// function call operator.

namespace detail {
template <typename... Types> struct IsaCheckPredicate {
  template <typename T> [[nodiscard]] bool operator()(const T &Val) const {
    return isa<Types...>(Val);
  }
};

template <typename... Types> struct IsaAndPresentCheckPredicate {
  template <typename T> [[nodiscard]] bool operator()(const T &Val) const {
    return isa_and_present<Types...>(Val);
  }
};

//===----------------------------------------------------------------------===//
// Casting Function Objects
//===----------------------------------------------------------------------===//

/// Usable in generic algorithms like map_range
template <typename U> struct StaticCastFunc {
  template <typename T> decltype(auto) operator()(T &&Val) const {
    return static_cast<U>(Val);
  }
};

template <typename U> struct DynCastFunc {
  template <typename T> decltype(auto) operator()(T &&Val) const {
    return dyn_cast<U>(Val);
  }
};

template <typename U> struct CastFunc {
  template <typename T> decltype(auto) operator()(T &&Val) const {
    return cast<U>(Val);
  }
};

template <typename U> struct CastIfPresentFunc {
  template <typename T> decltype(auto) operator()(T &&Val) const {
    return cast_if_present<U>(Val);
  }
};

template <typename U> struct DynCastIfPresentFunc {
  template <typename T> decltype(auto) operator()(T &&Val) const {
    return dyn_cast_if_present<U>(Val);
  }
};

} // namespace detail

/// Function object that applies \c isa<\c Types...> in generic algorithms.
///
/// The function call operator returns true when the value can be cast to any
/// type in `Types`. Example:
/// ```
/// SmallVector<Type> myTypes = ...;
/// if (llvm::all_of(myTypes, llvm::IsaPred<VectorType>))
///   ...
/// ```
template <typename... Types>
inline constexpr detail::IsaCheckPredicate<Types...> IsaPred{};

/// Function object that applies \c isa_and_present<\c Types...> in algorithms.
///
/// The function call operator returns true when the value can be cast to any
/// type in `Types`, or if the value is not present (e.g., nullptr). Example:
/// ```
/// SmallVector<Type> myTypes = ...;
/// if (llvm::all_of(myTypes, llvm::IsaAndPresentPred<VectorType>))
///   ...
/// ```
template <typename... Types>
inline constexpr detail::IsaAndPresentCheckPredicate<Types...>
    IsaAndPresentPred{};

/// Function objects corresponding to the Cast types defined above.
template <typename To>
inline constexpr detail::StaticCastFunc<To> StaticCastTo{};

/// Function object that applies \c cast<\c To> in generic algorithms.
template <typename To> inline constexpr detail::CastFunc<To> CastTo{};

/// Function object that applies \c cast_if_present<\c To> in generic algorithms.
template <typename To>
inline constexpr detail::CastIfPresentFunc<To> CastIfPresentTo{};

/// Function object that applies \c dyn_cast_if_present<\c To> in generic algorithms.
template <typename To>
inline constexpr detail::DynCastIfPresentFunc<To> DynCastIfPresentTo{};

/// Function object that applies \c dyn_cast<\c To> in generic algorithms.
template <typename To> inline constexpr detail::DynCastFunc<To> DynCastTo{};

} // end namespace llvm

#endif // LLVM_SUPPORT_CASTING_H
