/*===- TableGen'erated file -------------------------------------*- C++ -*-===*\
|*                                                                            *|
|* Type and expression traits                                                 *|
|*                                                                            *|
|* Automatically generated file, do not edit!                                 *|
|* From: BuiltinTraits.td                                                     *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#if defined(EMIT_TOKENKEY)
  KEYC99 = 1,
  KEYCXX = 2,
  KEYCXX11 = 4,
  KEYGNU = 8,
  KEYMS = 16,
  BOOLSUPPORT = 32,
  KEYALTIVEC = 64,
  KEYNOCXX = 128,
  KEYBORLAND = 256,
  KEYOPENCLC = 512,
  KEYC23 = 1024,
  KEYNOMS18 = 2048,
  KEYNOOPENCL = 4096,
  WCHARSUPPORT = 8192,
  HALFSUPPORT = 16384,
  CHAR8SUPPORT = 32768,
  KEYOBJC = 65536,
  KEYZVECTOR = 131072,
  KEYCOROUTINES = 262144,
  KEYMODULES = 524288,
  KEYCXX20 = 1048576,
  KEYOPENCLCXX = 2097152,
  KEYMSCOMPAT = 4194304,
  KEYSYCL = 8388608,
  KEYCUDA = 16777216,
  KEYZOS = 33554432,
  KEYNOZOS = 67108864,
  KEYHLSL = 134217728,
  KEYFIXEDPOINT = 268435456,
  KEYDEFERTS = 536870912,
  KEYNOHLSL = 1073741824,
  KEYMAX = 1073741824,
  KEYALLCXX = 1048582,
  KEYALL = 1006626815,
#elif defined(EMIT_ENUMS)
/// Names for traits that operate specifically on types.
enum TypeTrait {
  UTT_IsInterfaceClass,
  UTT_IsSealed,
  UTT_IsDestructible,
  UTT_IsTriviallyDestructible,
  UTT_IsNothrowDestructible,
  UTT_HasNothrowMoveAssign,
  UTT_HasTrivialMoveAssign,
  UTT_HasTrivialMoveConstructor,
  UTT_IsImplicitLifetime,
  UTT_HasNothrowAssign,
  UTT_HasNothrowCopy,
  UTT_HasNothrowConstructor,
  UTT_HasTrivialAssign,
  UTT_HasTrivialCopy,
  UTT_HasTrivialDefaultConstructor,
  UTT_HasTrivialDestructor,
  UTT_HasVirtualDestructor,
  UTT_IsAbstract,
  UTT_IsAggregate,
  UTT_IsClass,
  UTT_IsEmpty,
  UTT_IsEnum,
  UTT_IsFinal,
  UTT_IsLiteral,
  UTT_IsPOD,
  UTT_IsPolymorphic,
  UTT_IsStandardLayout,
  UTT_IsTrivial,
  UTT_IsTriviallyCopyable,
  UTT_IsUnion,
  UTT_HasUniqueObjectRepresentations,
  UTT_IsTriviallyEqualityComparable,
  UTT_IsBoundedArray,
  UTT_IsUnboundedArray,
  UTT_IsScopedEnum,
  UTT_CanPassInRegs,
  UTT_IsCppTriviallyRelocatable,
  UTT_IsTriviallyRelocatable,
  UTT_IsBitwiseCloneable,
  UTT_StructuredBindingSize,
  UTT_IsArithmetic,
  UTT_IsFloatingPoint,
  UTT_IsIntegral,
  UTT_IsCompleteType,
  UTT_IsVoid,
  UTT_IsArray,
  UTT_IsFunction,
  UTT_IsReference,
  UTT_IsLvalueReference,
  UTT_IsRvalueReference,
  UTT_IsFundamental,
  UTT_IsObject,
  UTT_IsScalar,
  UTT_IsCompound,
  UTT_IsPointer,
  UTT_IsMemberObjectPointer,
  UTT_IsMemberFunctionPointer,
  UTT_IsMemberPointer,
  UTT_IsConst,
  UTT_IsVolatile,
  UTT_IsSigned,
  UTT_IsUnsigned,
  UTT_IsIntangibleType,
  UTT_IsTypedResourceElementCompatible,
  UTT_IsConstantBufferElementCompatible,
  UTT_Last = 64, // UTT_Last == last UTT_XX in the enum.
  BTT_TypeCompatible,
  BTT_TypeOrder,
  BTT_IsNothrowAssignable,
  BTT_IsAssignable,
  BTT_IsVirtualBaseOf,
  BTT_IsBaseOf,
  BTT_IsConvertibleTo,
  BTT_IsTriviallyAssignable,
  BTT_IsLayoutCompatible,
  BTT_IsPointerInterconvertibleBaseOf,
  BTT_ReferenceBindsToTemporary,
  BTT_ReferenceConstructsFromTemporary,
  BTT_ReferenceConvertsFromTemporary,
  BTT_LtSynthesizesFromSpaceship,
  BTT_LeSynthesizesFromSpaceship,
  BTT_GtSynthesizesFromSpaceship,
  BTT_GeSynthesizesFromSpaceship,
  BTT_IsDeducible,
  BTT_IsSame,
  BTT_IsConvertible,
  BTT_IsNothrowConvertible,
  BTT_IsScalarizedLayoutCompatible,
  BTT_Last = 86, // BTT_Last == last BTT_XX in the enum.
  TT_IsConstructible,
  TT_IsNothrowConstructible,
  TT_IsTriviallyConstructible,
  TT_Last = 89 // TT_Last == last TT_XX in the enum.
};

/// Names for the array type traits.
enum ArrayTypeTrait {
  ATT_ArrayRank,
  ATT_ArrayExtent,
  ATT_Last = 1 // ATT_Last == last ATT
};

/// Names for the "expression or type" traits.
enum UnaryExprOrTypeTrait {
  UETT_SizeOf,
  UETT_DataSizeOf,
  UETT_CountOf,
  UETT_PreferredAlignOf,
  UETT_PtrAuthTypeDiscriminator,
  UETT_VecStep,
  UETT_OpenMPRequiredSimdAlign,
  UETT_VectorElements,
  UETT_AlignOf,
  UETT_Last = 8 // UETT_Last == last UETT_XX in the enum.
};

/// Names for the expression traits.
enum ExpressionTrait {
  ET_IsLValueExpr,
  ET_IsRValueExpr,
  ET_Last = 1 // ET_Last == last ET_XX in the enum.
};

#elif defined(EMIT_ARRAYS)
static constexpr const char *TypeTraitNames[] = {
  "IsInterfaceClass",
  "IsSealed",
  "IsDestructible",
  "IsTriviallyDestructible",
  "IsNothrowDestructible",
  "HasNothrowMoveAssign",
  "HasTrivialMoveAssign",
  "HasTrivialMoveConstructor",
  "IsImplicitLifetime",
  "HasNothrowAssign",
  "HasNothrowCopy",
  "HasNothrowConstructor",
  "HasTrivialAssign",
  "HasTrivialCopy",
  "HasTrivialDefaultConstructor",
  "HasTrivialDestructor",
  "HasVirtualDestructor",
  "IsAbstract",
  "IsAggregate",
  "IsClass",
  "IsEmpty",
  "IsEnum",
  "IsFinal",
  "IsLiteral",
  "IsPOD",
  "IsPolymorphic",
  "IsStandardLayout",
  "IsTrivial",
  "IsTriviallyCopyable",
  "IsUnion",
  "HasUniqueObjectRepresentations",
  "IsTriviallyEqualityComparable",
  "IsBoundedArray",
  "IsUnboundedArray",
  "IsScopedEnum",
  "CanPassInRegs",
  "IsCppTriviallyRelocatable",
  "IsTriviallyRelocatable",
  "IsBitwiseCloneable",
  "StructuredBindingSize",
  "IsArithmetic",
  "IsFloatingPoint",
  "IsIntegral",
  "IsCompleteType",
  "IsVoid",
  "IsArray",
  "IsFunction",
  "IsReference",
  "IsLvalueReference",
  "IsRvalueReference",
  "IsFundamental",
  "IsObject",
  "IsScalar",
  "IsCompound",
  "IsPointer",
  "IsMemberObjectPointer",
  "IsMemberFunctionPointer",
  "IsMemberPointer",
  "IsConst",
  "IsVolatile",
  "IsSigned",
  "IsUnsigned",
  "IsIntangibleType",
  "IsTypedResourceElementCompatible",
  "IsConstantBufferElementCompatible",
  "TypeCompatible",
  "TypeOrder",
  "IsNothrowAssignable",
  "IsAssignable",
  "IsVirtualBaseOf",
  "IsBaseOf",
  "IsConvertibleTo",
  "IsTriviallyAssignable",
  "IsLayoutCompatible",
  "IsPointerInterconvertibleBaseOf",
  "ReferenceBindsToTemporary",
  "ReferenceConstructsFromTemporary",
  "ReferenceConvertsFromTemporary",
  "LtSynthesizesFromSpaceship",
  "LeSynthesizesFromSpaceship",
  "GtSynthesizesFromSpaceship",
  "GeSynthesizesFromSpaceship",
  "IsDeducible",
  "IsSame",
  "IsConvertible",
  "IsNothrowConvertible",
  "IsScalarizedLayoutCompatible",
  "IsConstructible",
  "IsNothrowConstructible",
  "IsTriviallyConstructible",
};

static constexpr const char *TypeTraitSpellings[] = {
  "__is_interface_class",
  "__is_sealed",
  "__is_destructible",
  "__is_trivially_destructible",
  "__is_nothrow_destructible",
  "__has_nothrow_move_assign",
  "__has_trivial_move_assign",
  "__has_trivial_move_constructor",
  "__builtin_is_implicit_lifetime",
  "__has_nothrow_assign",
  "__has_nothrow_copy",
  "__has_nothrow_constructor",
  "__has_trivial_assign",
  "__has_trivial_copy",
  "__has_trivial_constructor",
  "__has_trivial_destructor",
  "__has_virtual_destructor",
  "__is_abstract",
  "__is_aggregate",
  "__is_class",
  "__is_empty",
  "__is_enum",
  "__is_final",
  "__is_literal",
  "__is_pod",
  "__is_polymorphic",
  "__is_standard_layout",
  "__is_trivial",
  "__is_trivially_copyable",
  "__is_union",
  "__has_unique_object_representations",
  "__is_trivially_equality_comparable",
  "__is_bounded_array",
  "__is_unbounded_array",
  "__is_scoped_enum",
  "__can_pass_in_regs",
  "__builtin_is_cpp_trivially_relocatable",
  "__is_trivially_relocatable",
  "__is_bitwise_cloneable",
  "__builtin_structured_binding_size",
  "__is_arithmetic",
  "__is_floating_point",
  "__is_integral",
  "__is_complete_type",
  "__is_void",
  "__is_array",
  "__is_function",
  "__is_reference",
  "__is_lvalue_reference",
  "__is_rvalue_reference",
  "__is_fundamental",
  "__is_object",
  "__is_scalar",
  "__is_compound",
  "__is_pointer",
  "__is_member_object_pointer",
  "__is_member_function_pointer",
  "__is_member_pointer",
  "__is_const",
  "__is_volatile",
  "__is_signed",
  "__is_unsigned",
  "__builtin_hlsl_is_intangible",
  "__builtin_hlsl_is_typed_resource_element_compatible",
  "__builtin_hlsl_is_constant_buffer_element_compatible",
  "__builtin_types_compatible_p",
  "__builtin_type_order",
  "__is_nothrow_assignable",
  "__is_assignable",
  "__builtin_is_virtual_base_of",
  "__is_base_of",
  "__is_convertible_to",
  "__is_trivially_assignable",
  "__is_layout_compatible",
  "__is_pointer_interconvertible_base_of",
  "__reference_binds_to_temporary",
  "__reference_constructs_from_temporary",
  "__reference_converts_from_temporary",
  "__builtin_lt_synthesizes_from_spaceship",
  "__builtin_le_synthesizes_from_spaceship",
  "__builtin_gt_synthesizes_from_spaceship",
  "__builtin_ge_synthesizes_from_spaceship",
  "",
  "__is_same",
  "__is_convertible",
  "__is_nothrow_convertible",
  "__builtin_hlsl_is_scalarized_layout_compatible",
  "__is_constructible",
  "__is_nothrow_constructible",
  "__is_trivially_constructible",
};

static constexpr const unsigned TypeTraitArities[] = {
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
0, 0, 0
};

static constexpr const char *ArrayTypeTraitNames[] = {
  "ArrayRank",
  "ArrayExtent",
};

static constexpr const char *ArrayTypeTraitSpellings[] = {
  "__array_rank",
  "__array_extent",
};

static constexpr const char *UnaryExprOrTypeTraitNames[] = {
  "SizeOf",
  "DataSizeOf",
  "CountOf",
  "PreferredAlignOf",
  "PtrAuthTypeDiscriminator",
  "VecStep",
  "OpenMPRequiredSimdAlign",
  "VectorElements",
  "AlignOf",
};

static constexpr const char *UnaryExprOrTypeTraitSpellings[] = {
  "sizeof",
  "__datasizeof",
  "_Countof",
  "__alignof",
  "__builtin_ptrauth_type_discriminator",
  "vec_step",
  "__builtin_omp_required_simd_align",
  "__builtin_vectorelements",
  "alignof",
};

static constexpr const char *ExpressionTraitNames[] = {
  "IsLValueExpr",
  "IsRValueExpr",
};

static constexpr const char *ExpressionTraitSpellings[] = {
  "__is_lvalue_expr",
  "__is_rvalue_expr",
};

#elif defined(EMIT_STD_NAME_CASES)
  .Case("is_constructible", TypeTrait::TT_IsConstructible)
  .Case("is_assignable", TypeTrait::BTT_IsAssignable)
  .Case("is_abstract", TypeTrait::UTT_IsAbstract)
  .Case("is_aggregate", TypeTrait::UTT_IsAggregate)
  .Case("is_empty", TypeTrait::UTT_IsEmpty)
  .Case("is_final", TypeTrait::UTT_IsFinal)
  .Case("is_standard_layout", TypeTrait::UTT_IsStandardLayout)
  .Case("is_trivially_copyable", TypeTrait::UTT_IsTriviallyCopyable)
  .Case("is_trivially_relocatable", TypeTrait::UTT_IsCppTriviallyRelocatable)
#else
#ifndef TYPE_TRAIT_1
#define TYPE_TRAIT_1(I,E,K)
#endif
#ifndef TYPE_TRAIT_2
#define TYPE_TRAIT_2(I,E,K)
#endif
#ifndef TYPE_TRAIT_N
#define TYPE_TRAIT_N(I,E,K)
#endif
#ifndef ARRAY_TYPE_TRAIT
#define ARRAY_TYPE_TRAIT(I,E,K)
#endif
#ifndef UNARY_EXPR_OR_TYPE_TRAIT
#define UNARY_EXPR_OR_TYPE_TRAIT(I,E,K)
#endif
#ifndef CXX11_UNARY_EXPR_OR_TYPE_TRAIT
#define CXX11_UNARY_EXPR_OR_TYPE_TRAIT(I,E,K)
#endif
#ifndef EXPRESSION_TRAIT
#define EXPRESSION_TRAIT(I,E,K)
#endif
#ifndef TRANSFORM_TYPE_TRAIT_DEF
#define TRANSFORM_TYPE_TRAIT_DEF(K, Trait)
#endif
#ifndef ALIAS
#define ALIAS(X,Y,Z)
#endif

UNARY_EXPR_OR_TYPE_TRAIT(sizeof, SizeOf, KEYALL)
UNARY_EXPR_OR_TYPE_TRAIT(__datasizeof, DataSizeOf, KEYCXX)
UNARY_EXPR_OR_TYPE_TRAIT(_Countof, CountOf, KEYNOCXX)
CXX11_UNARY_EXPR_OR_TYPE_TRAIT(alignof, AlignOf, KEYC23)
UNARY_EXPR_OR_TYPE_TRAIT(__alignof, PreferredAlignOf, KEYALL)
TYPE_TRAIT_2(__builtin_types_compatible_p, TypeCompatible, KEYNOCXX)
TYPE_TRAIT_2(__builtin_type_order, TypeOrder, KEYCXX20)
TYPE_TRAIT_1(__is_interface_class, IsInterfaceClass, KEYMS)
TYPE_TRAIT_1(__is_sealed, IsSealed, KEYMS)
TYPE_TRAIT_1(__is_destructible, IsDestructible, KEYALL)
TYPE_TRAIT_1(__is_trivially_destructible, IsTriviallyDestructible, KEYCXX)
TYPE_TRAIT_1(__is_nothrow_destructible, IsNothrowDestructible, KEYALL)
TYPE_TRAIT_2(__is_nothrow_assignable, IsNothrowAssignable, KEYCXX)
TYPE_TRAIT_N(__is_constructible, IsConstructible, KEYCXX)
TYPE_TRAIT_N(__is_nothrow_constructible, IsNothrowConstructible, KEYCXX)
TYPE_TRAIT_2(__is_assignable, IsAssignable, KEYCXX)
TYPE_TRAIT_1(__has_nothrow_move_assign, HasNothrowMoveAssign, KEYCXX)
TYPE_TRAIT_1(__has_trivial_move_assign, HasTrivialMoveAssign, KEYCXX)
TYPE_TRAIT_1(__has_trivial_move_constructor, HasTrivialMoveConstructor, KEYCXX)
TYPE_TRAIT_1(__builtin_is_implicit_lifetime, IsImplicitLifetime, KEYCXX)
TYPE_TRAIT_2(__builtin_is_virtual_base_of, IsVirtualBaseOf, KEYCXX)
TYPE_TRAIT_1(__has_nothrow_assign, HasNothrowAssign, KEYCXX)
TYPE_TRAIT_1(__has_nothrow_copy, HasNothrowCopy, KEYCXX)
TYPE_TRAIT_1(__has_nothrow_constructor, HasNothrowConstructor, KEYCXX)
TYPE_TRAIT_1(__has_trivial_assign, HasTrivialAssign, KEYCXX)
TYPE_TRAIT_1(__has_trivial_copy, HasTrivialCopy, KEYCXX)
TYPE_TRAIT_1(__has_trivial_constructor, HasTrivialDefaultConstructor, KEYCXX)
TYPE_TRAIT_1(__has_trivial_destructor, HasTrivialDestructor, KEYCXX)
TYPE_TRAIT_1(__has_virtual_destructor, HasVirtualDestructor, KEYCXX)
TYPE_TRAIT_1(__is_abstract, IsAbstract, KEYCXX)
TYPE_TRAIT_1(__is_aggregate, IsAggregate, KEYCXX)
TYPE_TRAIT_2(__is_base_of, IsBaseOf, KEYCXX)
TYPE_TRAIT_1(__is_class, IsClass, KEYCXX)
TYPE_TRAIT_2(__is_convertible_to, IsConvertibleTo, KEYCXX)
TYPE_TRAIT_1(__is_empty, IsEmpty, KEYCXX)
TYPE_TRAIT_1(__is_enum, IsEnum, KEYCXX)
TYPE_TRAIT_1(__is_final, IsFinal, KEYCXX)
TYPE_TRAIT_1(__is_literal, IsLiteral, KEYCXX)
TYPE_TRAIT_1(__is_pod, IsPOD, KEYCXX)
TYPE_TRAIT_1(__is_polymorphic, IsPolymorphic, KEYCXX)
TYPE_TRAIT_1(__is_standard_layout, IsStandardLayout, KEYCXX)
TYPE_TRAIT_1(__is_trivial, IsTrivial, KEYCXX)
TYPE_TRAIT_2(__is_trivially_assignable, IsTriviallyAssignable, KEYCXX)
TYPE_TRAIT_N(__is_trivially_constructible, IsTriviallyConstructible, KEYCXX)
TYPE_TRAIT_1(__is_trivially_copyable, IsTriviallyCopyable, KEYCXX)
TYPE_TRAIT_1(__is_union, IsUnion, KEYCXX)
TYPE_TRAIT_1(__has_unique_object_representations, HasUniqueObjectRepresentations, KEYCXX)
TYPE_TRAIT_2(__is_layout_compatible, IsLayoutCompatible, KEYCXX)
TYPE_TRAIT_2(__is_pointer_interconvertible_base_of, IsPointerInterconvertibleBaseOf, KEYCXX)
TRANSFORM_TYPE_TRAIT_DEF(AddLvalueReference, add_lvalue_reference)
TRANSFORM_TYPE_TRAIT_DEF(AddPointer, add_pointer)
TRANSFORM_TYPE_TRAIT_DEF(AddRvalueReference, add_rvalue_reference)
TRANSFORM_TYPE_TRAIT_DEF(Decay, decay)
TRANSFORM_TYPE_TRAIT_DEF(MakeSigned, make_signed)
TRANSFORM_TYPE_TRAIT_DEF(MakeUnsigned, make_unsigned)
TRANSFORM_TYPE_TRAIT_DEF(RemoveAllExtents, remove_all_extents)
TRANSFORM_TYPE_TRAIT_DEF(RemoveConst, remove_const)
TRANSFORM_TYPE_TRAIT_DEF(RemoveCV, remove_cv)
TRANSFORM_TYPE_TRAIT_DEF(RemoveCVRef, remove_cvref)
TRANSFORM_TYPE_TRAIT_DEF(RemoveExtent, remove_extent)
TRANSFORM_TYPE_TRAIT_DEF(RemovePointer, remove_pointer)
TRANSFORM_TYPE_TRAIT_DEF(RemoveReference, remove_reference_t)
TRANSFORM_TYPE_TRAIT_DEF(RemoveRestrict, remove_restrict)
TRANSFORM_TYPE_TRAIT_DEF(RemoveVolatile, remove_volatile)
TRANSFORM_TYPE_TRAIT_DEF(EnumUnderlyingType, underlying_type)
TYPE_TRAIT_1(__is_trivially_equality_comparable, IsTriviallyEqualityComparable, KEYCXX)
TYPE_TRAIT_1(__is_bounded_array, IsBoundedArray, KEYCXX)
TYPE_TRAIT_1(__is_unbounded_array, IsUnboundedArray, KEYCXX)
TYPE_TRAIT_1(__is_scoped_enum, IsScopedEnum, KEYCXX)
TYPE_TRAIT_1(__can_pass_in_regs, CanPassInRegs, KEYCXX)
TYPE_TRAIT_2(__reference_binds_to_temporary, ReferenceBindsToTemporary, KEYCXX)
TYPE_TRAIT_2(__reference_constructs_from_temporary, ReferenceConstructsFromTemporary, KEYCXX)
TYPE_TRAIT_2(__reference_converts_from_temporary, ReferenceConvertsFromTemporary, KEYCXX)
TYPE_TRAIT_2(__builtin_lt_synthesizes_from_spaceship, LtSynthesizesFromSpaceship, KEYCXX)
TYPE_TRAIT_2(__builtin_le_synthesizes_from_spaceship, LeSynthesizesFromSpaceship, KEYCXX)
TYPE_TRAIT_2(__builtin_gt_synthesizes_from_spaceship, GtSynthesizesFromSpaceship, KEYCXX)
TYPE_TRAIT_2(__builtin_ge_synthesizes_from_spaceship, GeSynthesizesFromSpaceship, KEYCXX)
TYPE_TRAIT_2(, IsDeducible, KEYCXX)
TYPE_TRAIT_1(__builtin_is_cpp_trivially_relocatable, IsCppTriviallyRelocatable, KEYCXX)
TYPE_TRAIT_1(__is_trivially_relocatable, IsTriviallyRelocatable, KEYCXX)
TYPE_TRAIT_1(__is_bitwise_cloneable, IsBitwiseCloneable, KEYALL)
TYPE_TRAIT_1(__builtin_structured_binding_size, StructuredBindingSize, KEYCXX)
EXPRESSION_TRAIT(__is_lvalue_expr, IsLValueExpr, KEYCXX)
EXPRESSION_TRAIT(__is_rvalue_expr, IsRValueExpr, KEYCXX)
TYPE_TRAIT_1(__is_arithmetic, IsArithmetic, KEYCXX)
TYPE_TRAIT_1(__is_floating_point, IsFloatingPoint, KEYCXX)
TYPE_TRAIT_1(__is_integral, IsIntegral, KEYCXX)
TYPE_TRAIT_1(__is_complete_type, IsCompleteType, KEYCXX)
TYPE_TRAIT_1(__is_void, IsVoid, KEYCXX)
TYPE_TRAIT_1(__is_array, IsArray, KEYCXX)
TYPE_TRAIT_1(__is_function, IsFunction, KEYCXX)
TYPE_TRAIT_1(__is_reference, IsReference, KEYCXX)
TYPE_TRAIT_1(__is_lvalue_reference, IsLvalueReference, KEYCXX)
TYPE_TRAIT_1(__is_rvalue_reference, IsRvalueReference, KEYCXX)
TYPE_TRAIT_1(__is_fundamental, IsFundamental, KEYCXX)
TYPE_TRAIT_1(__is_object, IsObject, KEYCXX)
TYPE_TRAIT_1(__is_scalar, IsScalar, KEYCXX)
TYPE_TRAIT_1(__is_compound, IsCompound, KEYCXX)
TYPE_TRAIT_1(__is_pointer, IsPointer, KEYCXX)
TYPE_TRAIT_1(__is_member_object_pointer, IsMemberObjectPointer, KEYCXX)
TYPE_TRAIT_1(__is_member_function_pointer, IsMemberFunctionPointer, KEYCXX)
TYPE_TRAIT_1(__is_member_pointer, IsMemberPointer, KEYCXX)
TYPE_TRAIT_1(__is_const, IsConst, KEYCXX)
TYPE_TRAIT_1(__is_volatile, IsVolatile, KEYCXX)
TYPE_TRAIT_1(__is_signed, IsSigned, KEYCXX)
TYPE_TRAIT_1(__is_unsigned, IsUnsigned, KEYCXX)
TYPE_TRAIT_2(__is_same, IsSame, KEYCXX)
TYPE_TRAIT_2(__is_convertible, IsConvertible, KEYCXX)
TYPE_TRAIT_2(__is_nothrow_convertible, IsNothrowConvertible, KEYCXX)
ARRAY_TYPE_TRAIT(__array_rank, ArrayRank, KEYCXX)
ARRAY_TYPE_TRAIT(__array_extent, ArrayExtent, KEYCXX)
UNARY_EXPR_OR_TYPE_TRAIT(__builtin_ptrauth_type_discriminator, PtrAuthTypeDiscriminator, KEYALL)
UNARY_EXPR_OR_TYPE_TRAIT(vec_step, VecStep, KEYOPENCLC | KEYOPENCLCXX | KEYALTIVEC | KEYZVECTOR)
TYPE_TRAIT_2(__builtin_hlsl_is_scalarized_layout_compatible, IsScalarizedLayoutCompatible, KEYHLSL)
TYPE_TRAIT_1(__builtin_hlsl_is_intangible, IsIntangibleType, KEYHLSL)
TYPE_TRAIT_1(__builtin_hlsl_is_typed_resource_element_compatible, IsTypedResourceElementCompatible, KEYHLSL)
TYPE_TRAIT_1(__builtin_hlsl_is_constant_buffer_element_compatible, IsConstantBufferElementCompatible, KEYHLSL)
UNARY_EXPR_OR_TYPE_TRAIT(__builtin_omp_required_simd_align, OpenMPRequiredSimdAlign, KEYALL)
UNARY_EXPR_OR_TYPE_TRAIT(__builtin_vectorelements, VectorElements, KEYALL)
ALIAS("__alignof__", __alignof, KEYALL)
ALIAS("__builtin_alignof", __alignof, KEYMS)
ALIAS("_alignof", __alignof, KEYMS)
ALIAS("__is_literal_type", __is_literal, KEYCXX)
ALIAS("__is_same_as", __is_same, KEYCXX)
#undef ALIAS
#undef TRANSFORM_TYPE_TRAIT_DEF
#undef EXPRESSION_TRAIT
#undef CXX11_UNARY_EXPR_OR_TYPE_TRAIT
#undef UNARY_EXPR_OR_TYPE_TRAIT
#undef ARRAY_TYPE_TRAIT
#undef TYPE_TRAIT_N
#undef TYPE_TRAIT_2
#undef TYPE_TRAIT_1
#endif


#undef EMIT_ARRAYS
#undef EMIT_ENUMS
#undef EMIT_STD_NAME_CASES
#undef EMIT_TOKENKEY

