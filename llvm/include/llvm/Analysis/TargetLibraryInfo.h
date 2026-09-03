//===-- TargetLibraryInfo.h - Library information ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TARGETLIBRARYINFO_H
#define LLVM_ANALYSIS_TARGETLIBRARYINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringTable.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/SystemLibraries.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <bitset>
#include <optional>

namespace llvm {

template <typename T> class ArrayRef;

/// Describes a scalar-to-vector function mapping for vectorization.
///
/// Function 'VectorFnName' is equivalent to 'ScalarFnName'
/// vectorized by a factor 'VectorizationFactor'.
/// The VABIPrefix string holds information about isa, mask, vlen,
/// and vparams so a scalar-to-vector mapping of the form:
///    _ZGV<isa><mask><vlen><vparams>_<scalarname>(<vectorname>)
/// can be constructed where:
///
/// <isa> = "_LLVM_"
/// <mask> = "M" if masked, "N" if no mask.
/// <vlen> = Number of concurrent lanes, stored in the `VectorizationFactor`
///          field of the `VecDesc` struct. If the number of lanes is scalable
///          then 'x' is printed instead.
/// <vparams> = "v", as many as are the numArgs.
/// <scalarname> = the name of the scalar function.
/// <vectorname> = the name of the vector function.
class VecDesc {
  StringRef ScalarFnName;
  StringRef VectorFnName;
  ElementCount VectorizationFactor;
  bool Masked;
  StringRef VABIPrefix;
  /// Encoded calling convention: 0 means absent (std::nullopt), otherwise
  /// stores CallingConv::ID + 1 so an explicit C (0) remains representable.
  /// TODO: Since C++20 standard becomes default in LLVM we can return back to
  /// use std::optional<CallingConv::ID> instead of unsigned and value_or()
  /// in default constructor.
  unsigned CC;

public:
  /// Deleted default constructor; a VecDesc requires explicit mapping fields.
  VecDesc() = delete;
  /// Construct a scalar-to-vector function mapping descriptor.
  /// @param ScalarFnName Name of the scalar function.
  /// @param VectorFnName Name of the equivalent vector function.
  /// @param VectorizationFactor Vectorization factor for the mapping.
  /// @param Masked Whether the vector function is a masked variant.
  /// @param VABIPrefix Vector function ABI variant prefix string.
  /// @param Conv Optional calling convention for the vector function.
  constexpr VecDesc(StringRef ScalarFnName, StringRef VectorFnName,
                    ElementCount VectorizationFactor, bool Masked,
                    StringRef VABIPrefix, std::optional<CallingConv::ID> Conv)
      : ScalarFnName(ScalarFnName), VectorFnName(VectorFnName),
        VectorizationFactor(VectorizationFactor), Masked(Masked),
        VABIPrefix(VABIPrefix),
        CC(Conv ? static_cast<unsigned>(*Conv) + 1u : 0u) {}

  /// Return the name of the scalar function.
  /// @return Name of the scalar function.
  StringRef getScalarFnName() const { return ScalarFnName; }
  /// Return the name of the vector function.
  /// @return Name of the vector function.
  StringRef getVectorFnName() const { return VectorFnName; }
  /// Return the vectorization factor for this mapping.
  /// @return Vectorization factor for this mapping.
  ElementCount getVectorizationFactor() const { return VectorizationFactor; }
  /// Return true if this mapping describes a masked vector function.
  /// @return True if this mapping describes a masked vector function.
  bool isMasked() const { return Masked; }
  /// Return the vector function ABI variant prefix.
  /// @return Vector function ABI variant prefix.
  StringRef getVABIPrefix() const { return VABIPrefix; }
  /// Return the optional calling convention for the vector function.
  /// @return Optional calling convention for the vector function.
  std::optional<CallingConv::ID> getCallingConv() const {
    if (CC == 0)
      return std::nullopt;
    return static_cast<CallingConv::ID>(CC - 1);
  }

  /// Returns a vector function ABI variant string on the form:
  ///    _ZGV<isa><mask><vlen><vparams>_<scalarname>(<vectorname>)
  /// @return Vector function ABI variant string.
  LLVM_ABI std::string getVectorFunctionABIVariantString() const;
};

#define GET_TARGET_LIBRARY_INFO_ENUM
#include "llvm/Analysis/TargetLibraryInfo.inc"

/// Implementation of the target library information.
///
/// This class constructs tables that hold the target library information and
/// make it available. However, it is somewhat expensive to compute and only
/// depends on the triple. So users typically interact with the \c
/// TargetLibraryInfo wrapper below.
class TargetLibraryInfoImpl {
  friend class TargetLibraryInfo;

  unsigned char AvailableArray[(NumLibFuncs+3)/4];
  DenseMap<unsigned, std::string> CustomNames;
#define GET_TARGET_LIBRARY_INFO_IMPL_DECL
#include "llvm/Analysis/TargetLibraryInfo.inc"
  bool ShouldExtI32Param, ShouldExtI32Return, ShouldSignExtI32Param, ShouldSignExtI32Return;
  unsigned SizeOfInt;
  bool IsErrnoFunctionCall;

  enum AvailabilityState {
    StandardName = 3, // (memset to all ones)
    CustomName = 1,
    Unavailable = 0  // (memset to all zeros)
  };
  void setState(LibFunc F, AvailabilityState State) {
    AvailableArray[F/4] &= ~(3 << 2*(F&3));
    AvailableArray[F/4] |= State << 2*(F&3);
  }
  AvailabilityState getState(LibFunc F) const {
    if (F == NotLibFunc)
      return Unavailable;
    return static_cast<AvailabilityState>((AvailableArray[F/4] >> 2*(F&3)) & 3);
  }

  /// Vectorization descriptors - sorted by ScalarFnName.
  std::vector<VecDesc> VectorDescs;
  /// Scalarization descriptors - same content as VectorDescs but sorted based
  /// on VectorFnName rather than ScalarFnName.
  std::vector<VecDesc> ScalarDescs;

  /// Return true if the function type FTy is valid for the library function
  /// F, regardless of whether the function is available.
  LLVM_ABI bool isValidProtoForLibFunc(const FunctionType &FTy, LibFunc F,
                                       const Module &M) const;

public:
  /// Deleted default constructor; a TargetLibraryInfoImpl requires a triple.
  TargetLibraryInfoImpl() = delete;
  /// Construct target library info for triple \p T.
  /// @param T Target triple used to select available library functions.
  /// @param VecLib Optional vector library preset to register mappings for.
  LLVM_ABI explicit TargetLibraryInfoImpl(
      const Triple &T, VectorLibrary VecLib = VectorLibrary::NoLibrary);

  // Provide value semantics.
  /// Copy-construct target library info from \p TLI.
  /// @param TLI Target library info to copy.
  LLVM_ABI TargetLibraryInfoImpl(const TargetLibraryInfoImpl &TLI);
  /// Move-construct target library info from \p TLI.
  /// @param TLI Target library info to move from.
  LLVM_ABI TargetLibraryInfoImpl(TargetLibraryInfoImpl &&TLI);
  /// Copy-assign from \p TLI.
  /// @param TLI Target library info to copy.
  /// @return Reference to this target library info.
  LLVM_ABI TargetLibraryInfoImpl &operator=(const TargetLibraryInfoImpl &TLI);
  /// Move-assign from \p TLI.
  /// @param TLI Target library info to move from.
  /// @return Reference to this target library info.
  LLVM_ABI TargetLibraryInfoImpl &operator=(TargetLibraryInfoImpl &&TLI);

  /// Searches for a particular function name.
  ///
  /// Returns the corresponding LibFunc if it is one of the known library
  /// functions, and NotLibFunc otherwise.
  /// @param funcName Function name to look up.
  /// @return Matching LibFunc, or NotLibFunc if unknown.
  LLVM_ABI LibFunc getLibFunc(StringRef funcName) const;

  /// Searches for a particular function name, also checking that its type is
  /// valid for the library function matching that name.
  ///
  /// Returns the corresponding LibFunc if it is one of the known library
  /// functions, and NotLibFunc otherwise.
  ///
  /// FDecl is assumed to have a parent Module when using this function.
  /// @param FDecl Function whose name and type are checked.
  /// @return Matching LibFunc, or NotLibFunc if unknown or invalid.
  LLVM_ABI LibFunc getLibFunc(const Function &FDecl) const;

  /// Searches for a function name using an Instruction \p Opcode.
  /// Currently, only the frem instruction is supported.
  ///
  /// Returns NotLibFunc if there is no matching library function.
  /// @param Opcode Instruction opcode to map to a library function.
  /// @param Ty Operand type used to select the matching library function.
  /// @return Matching LibFunc, or NotLibFunc if none matches.
  LLVM_ABI LibFunc getLibFunc(unsigned int Opcode, Type *Ty) const;

  /// Forces a function to be marked as unavailable.
  /// @param F Library function to mark unavailable.
  void setUnavailable(LibFunc F) {
    setState(F, Unavailable);
  }

  /// Forces a function to be marked as available.
  /// @param F Library function to mark available.
  void setAvailable(LibFunc F) {
    setState(F, StandardName);
  }

  /// Forces a function to be marked as available and provide an alternate name
  /// that must be used.
  /// @param F Library function to mark available.
  /// @param Name Alternate name that must be used for \p F.
  void setAvailableWithName(LibFunc F, StringRef Name) {
    if (StringRef(StandardNamesStrTable.getCString(StandardNamesOffsets[F]),
                  StandardNamesSizeTable[F]) != Name) {
      setState(F, CustomName);
      CustomNames[F] = std::string(Name);
      assert(CustomNames.contains(F));
    } else {
      setState(F, StandardName);
    }
  }

  /// Disables all builtins.
  ///
  /// This can be used for options like -fno-builtin.
  LLVM_ABI void disableAllFunctions();

  /// Add a set of scalar -> vector mappings, queryable via
  /// getVectorizedFunction and getScalarizedFunction.
  /// @param Fns Scalar-to-vector mapping descriptors to register.
  LLVM_ABI void addVectorizableFunctions(ArrayRef<VecDesc> Fns);

  /// Calls addVectorizableFunctions with a known preset of functions for the
  /// given vector library.
  /// @param VecLib Vector library whose preset mappings should be registered.
  /// @param TargetTriple Triple used to select the appropriate mappings.
  LLVM_ABI void
  addVectorizableFunctionsFromVecLib(enum VectorLibrary VecLib,
                                     const llvm::Triple &TargetTriple);

  /// Return true if the function F has a vector equivalent with vectorization
  /// factor VF.
  /// @param F Scalar function name to query.
  /// @param VF Vectorization factor to look up.
  /// @return True if \p F has a vector equivalent with factor \p VF.
  bool isFunctionVectorizable(StringRef F, const ElementCount &VF) const {
    return !(getVectorizedFunction(F, VF, false).empty() &&
             getVectorizedFunction(F, VF, true).empty());
  }

  /// Return true if the function F has a vector equivalent with any
  /// vectorization factor.
  /// @param F Scalar function name to query.
  /// @return True if \p F has a vector equivalent with any vectorization factor.
  LLVM_ABI bool isFunctionVectorizable(StringRef F) const;

  /// Return the name of the equivalent of F, vectorized with factor VF. If no
  /// such mapping exists, return the empty string.
  /// @param F Scalar function name to look up.
  /// @param VF Vectorization factor for the mapping.
  /// @param Masked Whether to look up a masked vector function.
  /// @return Name of the vectorized equivalent, or the empty string if none.
  LLVM_ABI StringRef getVectorizedFunction(StringRef F, const ElementCount &VF,
                                           bool Masked) const;

  /// Return VecDesc info for the vectorized equivalent of \p F with factor \p VF.
  ///
  /// Holds all scalar-to-vector mapping info in TLI for that equivalence.
  /// If no such mapping exists, return nullptr.
  /// @param F Scalar function name to look up.
  /// @param VF Vectorization factor for the mapping.
  /// @param Masked Whether to look up a masked vector function.
  /// @return Mapping descriptor, or nullptr if none exists.
  LLVM_ABI const VecDesc *
  getVectorMappingInfo(StringRef F, const ElementCount &VF, bool Masked) const;

  /// Set to true iff i32 parameters to library functions should have signext
  /// or zeroext attributes if they correspond to C-level int or unsigned int,
  /// respectively.
  /// @param Val Whether i32 parameters should receive extension attributes.
  void setShouldExtI32Param(bool Val) {
    ShouldExtI32Param = Val;
  }

  /// Set to true iff i32 results from library functions should have signext
  /// or zeroext attributes if they correspond to C-level int or unsigned int,
  /// respectively.
  /// @param Val Whether i32 returns should receive extension attributes.
  void setShouldExtI32Return(bool Val) {
    ShouldExtI32Return = Val;
  }

  /// Set to true iff i32 parameters to library functions should have signext
  /// attribute if they correspond to C-level int or unsigned int.
  /// @param Val Whether i32 parameters should receive signext.
  void setShouldSignExtI32Param(bool Val) {
    ShouldSignExtI32Param = Val;
  }

  /// Set to true iff i32 results from library functions should have signext
  /// attribute if they correspond to C-level int or unsigned int.
  /// @param Val Whether i32 returns should receive signext.
  void setShouldSignExtI32Return(bool Val) {
    ShouldSignExtI32Return = Val;
  }

  /// Returns the size of the wchar_t type in bytes.
  /// This queries the 'wchar_size' metadata.
  /// @param M Module whose wchar_size metadata is queried.
  /// @return Size of wchar_t in bytes.
  LLVM_ABI unsigned getWCharSize(const Module &M) const;

  /// Returns the size of the size_t type in bits.
  /// @param M Module used to determine the size_t width.
  /// @return Size of size_t in bits.
  LLVM_ABI unsigned getSizeTSize(const Module &M) const;

  /// Get size of a C-level int or unsigned int, in bits.
  /// @return Size of a C-level int in bits.
  unsigned getIntSize() const {
    return SizeOfInt;
  }

  /// Initialize the C-level size of an integer.
  /// @param Bits Size of a C-level int in bits.
  void setIntSize(unsigned Bits) {
    SizeOfInt = Bits;
  }

  /// Returns the largest vectorization factor used in the list of
  /// vector functions.
  /// @param ScalarF Scalar function name whose mappings are inspected.
  /// @param FixedVF Set to the widest fixed vectorization factor found.
  /// @param Scalable Set to the widest scalable vectorization factor found.
  LLVM_ABI void getWidestVF(StringRef ScalarF, ElementCount &FixedVF,
                            ElementCount &Scalable) const;

  /// Returns true if call site / callee has cdecl-compatible calling
  /// conventions.
  /// @param CI Call site whose calling convention is checked.
  /// @return True if the call site has a cdecl-compatible calling convention.
  LLVM_ABI static bool isCallingConvCCompatible(CallBase *CI);
  /// Returns true if \p Callee has a cdecl-compatible calling convention.
  /// @param Callee Function whose calling convention is checked.
  /// @return True if \p Callee has a cdecl-compatible calling convention.
  LLVM_ABI static bool isCallingConvCCompatible(Function *Callee);

  /// Return true if errno is defined as a function call on known environments.
  /// @return True if errno is defined as a function call on known environments.
  bool isErrnoFunctionCall() const { return IsErrnoFunctionCall; }
};

/// Provides information about what library functions are available for
/// the current target.
///
/// This both allows optimizations to handle them specially and frontends to
/// disable such optimizations through -fno-builtin etc.
class TargetLibraryInfo {
  friend class TargetLibraryAnalysis;
  friend class TargetLibraryInfoWrapperPass;

  /// The global (module level) TLI info.
  const TargetLibraryInfoImpl *Impl;

  /// Support for -fno-builtin* options as function attributes, overrides
  /// information in global TargetLibraryInfoImpl.
  std::bitset<NumLibFuncs> OverrideAsUnavailable;

public:
  /// Deleted default constructor; a TargetLibraryInfo requires an impl.
  TargetLibraryInfo() = delete;

  /// Construct target library info from module-level impl \p Impl.
  ///
  /// When \p F is provided, apply function-level -fno-builtin* overrides.
  /// @param Impl Module-level target library info implementation.
  /// @param F Optional function whose no-builtin attributes override \p Impl.
  explicit TargetLibraryInfo(const TargetLibraryInfoImpl &Impl,
                             std::optional<const Function *> F = std::nullopt)
      : Impl(&Impl) {
    if (!F)
      return;
    if ((*F)->hasFnAttribute("no-builtins"))
      disableAllFunctions();
    else {
      // Disable individual libc/libm calls in TargetLibraryInfo.
      AttributeSet FnAttrs = (*F)->getAttributes().getFnAttrs();
      for (const Attribute &Attr : FnAttrs) {
        if (!Attr.isStringAttribute())
          continue;
        auto AttrStr = Attr.getKindAsString();
        if (!AttrStr.consume_front("no-builtin-"))
          continue;
        if (LibFunc LF = getLibFunc(AttrStr))
          setUnavailable(LF);
      }
    }
  }

  // Provide value semantics.
  /// Copy-construct target library info from \p TLI.
  /// @param TLI Target library info to copy.
  TargetLibraryInfo(const TargetLibraryInfo &TLI) = default;
  /// Move-construct target library info from \p TLI.
  /// @param TLI Target library info to move from.
  TargetLibraryInfo(TargetLibraryInfo &&TLI) = default;
  /// Copy-assign from \p TLI.
  /// @param TLI Target library info to copy.
  /// @return Reference to this target library info.
  TargetLibraryInfo &operator=(const TargetLibraryInfo &TLI) = default;
  /// Move-assign from \p TLI.
  /// @param TLI Target library info to move from.
  /// @return Reference to this target library info.
  TargetLibraryInfo &operator=(TargetLibraryInfo &&TLI) = default;

  /// Determine whether a callee with the given TLI can be inlined into this
  /// caller.
  ///
  /// Compatibility is based on 'nobuiltin' attributes. When requested, allow
  /// inlining into a caller with a superset of the callee's nobuiltin
  /// attributes, which is conservatively correct.
  /// @param CalleeTLI Target library info of the callee.
  /// @param AllowCallerSuperset When true, allow the caller to have a
  ///        superset of the callee's nobuiltin attributes.
  /// @return True if the callee can be inlined into this caller.
  bool areInlineCompatible(const TargetLibraryInfo &CalleeTLI,
                           bool AllowCallerSuperset) const {
    if (!AllowCallerSuperset)
      return OverrideAsUnavailable == CalleeTLI.OverrideAsUnavailable;
    // We can inline if the callee's nobuiltin attributes are no stricter than
    // the caller's.
    return (CalleeTLI.OverrideAsUnavailable & ~OverrideAsUnavailable).none();
  }

  /// Return true if the function type FTy is valid for the library function
  /// F, regardless of whether the function is available.
  /// @param FTy Function type to validate.
  /// @param F Library function whose prototype is checked.
  /// @param M Module providing context for the check.
  /// @return True if \p FTy is a valid prototype for library function \p F.
  bool isValidProtoForLibFunc(const FunctionType &FTy, LibFunc F,
                              const Module &M) const {
    return Impl->isValidProtoForLibFunc(FTy, F, M);
  }

  /// Searches for a particular function name.
  ///
  /// Returns the corresponding LibFunc if it is one of the known library
  /// functions, and NotLibFunc otherwise.
  /// @param funcName Function name to look up.
  /// @return Matching LibFunc, or NotLibFunc if unknown.
  LibFunc getLibFunc(StringRef funcName) const {
    return Impl->getLibFunc(funcName);
  }

  /// Searches for a particular function name, also checking that its type is
  /// valid for the library function matching that name.
  ///
  /// Returns the corresponding LibFunc if it is one of the known library
  /// functions, and NotLibFunc otherwise.
  /// @param FDecl Function whose name and type are checked.
  /// @return Matching LibFunc, or NotLibFunc if unknown or invalid.
  LibFunc getLibFunc(const Function &FDecl) const {
    return Impl->getLibFunc(FDecl);
  }

  /// If a callbase does not have the 'nobuiltin' attribute, return the library
  /// function the callee is, and NotLibFunc otherwise.
  /// @param CB Call site whose callee is looked up.
  /// @return Matching LibFunc for the callee, or NotLibFunc otherwise.
  LibFunc getLibFunc(const CallBase &CB) const {
    if (CB.isNoBuiltin() || !CB.getCalledFunction())
      return NotLibFunc;
    return getLibFunc(*CB.getCalledFunction());
  }

  /// Searches for a function name using an Instruction \p Opcode.
  /// Currently, only the frem instruction is supported.
  ///
  /// Returns NotLibFunc if there is no matching library function.
  /// @param Opcode Instruction opcode to map to a library function.
  /// @param Ty Operand type used to select the matching library function.
  /// @return Matching LibFunc, or NotLibFunc if none matches.
  LibFunc getLibFunc(unsigned int Opcode, Type *Ty) const {
    return Impl->getLibFunc(Opcode, Ty);
  }

  /// Disables all builtins.
  ///
  /// This can be used for options like -fno-builtin.
  [[maybe_unused]] void disableAllFunctions() { OverrideAsUnavailable.set(); }

  /// Forces a function to be marked as unavailable.
  /// @param F Library function to mark unavailable.
  [[maybe_unused]] void setUnavailable(LibFunc F) {
    assert(F < OverrideAsUnavailable.size() && "out-of-bounds LibFunc");
    OverrideAsUnavailable.set(F);
  }

  /// Return the availability state of library function \p F.
  /// @param F Library function whose availability is queried.
  /// @return Availability state of library function \p F.
  TargetLibraryInfoImpl::AvailabilityState getState(LibFunc F) const {
    assert(F < OverrideAsUnavailable.size() && "out-of-bounds LibFunc");
    if (OverrideAsUnavailable[F])
      return TargetLibraryInfoImpl::Unavailable;
    return Impl->getState(F);
  }

  /// Tests whether a library function is available.
  /// @param F Library function to test.
  /// @return True if library function \p F is available.
  bool has(LibFunc F) const {
    return getState(F) != TargetLibraryInfoImpl::Unavailable;
  }
  /// Return true if the function F has a vector equivalent with vectorization
  /// factor VF.
  /// @param F Scalar function name to query.
  /// @param VF Vectorization factor to look up.
  /// @return True if \p F has a vector equivalent with factor \p VF.
  bool isFunctionVectorizable(StringRef F, const ElementCount &VF) const {
    return Impl->isFunctionVectorizable(F, VF);
  }
  /// Return true if the function F has a vector equivalent with any
  /// vectorization factor.
  /// @param F Scalar function name to query.
  /// @return True if \p F has a vector equivalent with any vectorization factor.
  bool isFunctionVectorizable(StringRef F) const {
    return Impl->isFunctionVectorizable(F);
  }
  /// Return the name of the equivalent of F, vectorized with factor VF.
  ///
  /// If no such mapping exists, return the empty string.
  /// @param F Scalar function name to look up.
  /// @param VF Vectorization factor for the mapping.
  /// @param Masked Whether to look up a masked vector function.
  /// @return Name of the vectorized equivalent, or the empty string if none.
  StringRef getVectorizedFunction(StringRef F, const ElementCount &VF,
                                  bool Masked = false) const {
    return Impl->getVectorizedFunction(F, VF, Masked);
  }
  /// Return VecDesc info for the vectorized equivalent of \p F with factor \p VF.
  ///
  /// If no such mapping exists, return nullptr.
  /// @param F Scalar function name to look up.
  /// @param VF Vectorization factor for the mapping.
  /// @param Masked Whether to look up a masked vector function.
  /// @return Mapping descriptor, or nullptr if none exists.
  const VecDesc *getVectorMappingInfo(StringRef F, const ElementCount &VF,
                                      bool Masked) const {
    return Impl->getVectorMappingInfo(F, VF, Masked);
  }

  /// Tests if the function is both available and a candidate for optimized code
  /// generation.
  /// @param F Library function to test.
  /// @return True if \p F is available and a candidate for optimized code
  ///         generation.
  bool hasOptimizedCodeGen(LibFunc F) const {
    if (getState(F) == TargetLibraryInfoImpl::Unavailable)
      return false;
    switch (F) {
    default: break;
      // clang-format off
    case LibFunc_acos:         case LibFunc_acosf:      case LibFunc_acosl:
    case LibFunc_asin:         case LibFunc_asinf:      case LibFunc_asinl:
    case LibFunc_atan2:        case LibFunc_atan2f:     case LibFunc_atan2l:
    case LibFunc_atan:         case LibFunc_atanf:      case LibFunc_atanl:
    case LibFunc_copysign:     case LibFunc_copysignf:  case LibFunc_copysignl:
    case LibFunc_cos:          case LibFunc_cosf:       case LibFunc_cosl:
    case LibFunc_cosh:         case LibFunc_coshf:      case LibFunc_coshl:
    case LibFunc_exp2:         case LibFunc_exp2f:      case LibFunc_exp2l:
    case LibFunc_exp10:        case LibFunc_exp10f:     case LibFunc_exp10l:
    case LibFunc_ldexp:        case LibFunc_ldexpf:     case LibFunc_ldexpl:
    case LibFunc_log2:         case LibFunc_log2f:      case LibFunc_log2l:
    case LibFunc_memcmp:       case LibFunc_bcmp:       case LibFunc_strcmp:
    case LibFunc_memcpy:       case LibFunc_memset:     case LibFunc_memmove:
    case LibFunc_sin:          case LibFunc_sinf:       case LibFunc_sinl:
    case LibFunc_sinh:         case LibFunc_sinhf:      case LibFunc_sinhl:
    case LibFunc_sqrt:         case LibFunc_sqrtf:      case LibFunc_sqrtl:
    case LibFunc_sqrt_finite:  case LibFunc_sqrtf_finite:
                                                   case LibFunc_sqrtl_finite:
    case LibFunc_strcpy:       case LibFunc_stpcpy:     case LibFunc_strlen:
    case LibFunc_strnlen:      case LibFunc_strstr:     case LibFunc_memchr:
    case LibFunc_memccpy:      case LibFunc_mempcpy:    case LibFunc_tan:
    case LibFunc_tanf:         case LibFunc_tanl:       case LibFunc_tanh:
    case LibFunc_tanhf:        case LibFunc_tanhl:
      // clang-format on
      return true;
    }
    return false;
  }

  /// Return the canonical name for a LibFunc. This should not be used for
  /// semantic purposes, use getName instead.
  /// @param F Library function whose standard name is requested.
  /// @return Canonical name for library function \p F.
  static StringRef getStandardName(LibFunc F) {
    return StringRef(TargetLibraryInfoImpl::StandardNamesStrTable.getCString(
                         TargetLibraryInfoImpl::StandardNamesOffsets[F]),
                     TargetLibraryInfoImpl::StandardNamesSizeTable[F]);
  }

  /// Return the name to use for library function \p F on this target.
  ///
  /// Returns an empty string when \p F is unavailable.
  /// @param F Library function whose name is requested.
  /// @return Target-specific name for \p F, or an empty string if unavailable.
  StringRef getName(LibFunc F) const {
    auto State = getState(F);
    if (State == TargetLibraryInfoImpl::Unavailable)
      return StringRef();
    if (State == TargetLibraryInfoImpl::StandardName)
      return StringRef(
          Impl->StandardNamesStrTable.getCString(Impl->StandardNamesOffsets[F]),
          Impl->StandardNamesSizeTable[F]);
    assert(State == TargetLibraryInfoImpl::CustomName);
    return Impl->CustomNames.find(F)->second;
  }

  /// Initialize i32 extension attribute flags for target triple \p T.
  /// @param ShouldExtI32Param Set when i32 params need signext/zeroext.
  /// @param ShouldExtI32Return Set when i32 returns need signext/zeroext.
  /// @param ShouldSignExtI32Param Set when i32 params need signext.
  /// @param ShouldSignExtI32Return Set when i32 returns need signext.
  /// @param T Target triple used to select extension conventions.
  static void initExtensionsForTriple(bool &ShouldExtI32Param,
                                      bool &ShouldExtI32Return,
                                      bool &ShouldSignExtI32Param,
                                      bool &ShouldSignExtI32Return,
                                      const Triple &T) {
    ShouldExtI32Param     = ShouldExtI32Return     = false;
    ShouldSignExtI32Param = ShouldSignExtI32Return = false;

    // PowerPC64, Sparc64, SystemZ need signext/zeroext on i32 parameters and
    // returns corresponding to C-level ints and unsigned ints.
    if (T.isPPC64() || T.getArch() == Triple::sparcv9 ||
        T.getArch() == Triple::systemz) {
      ShouldExtI32Param = true;
      ShouldExtI32Return = true;
    }
    // LoongArch, Mips, and riscv64, on the other hand, need signext on i32
    // parameters corresponding to both signed and unsigned ints.
    if (T.isLoongArch() || T.isMIPS() || T.isRISCV64()) {
      ShouldSignExtI32Param = true;
    }
    // LoongArch and riscv64 need signext on i32 returns corresponding to both
    // signed and unsigned ints.
    if (T.isLoongArch() || T.isRISCV64()) {
      ShouldSignExtI32Return = true;
    }
  }

  /// Returns extension attribute kind to be used for i32 parameters
  /// corresponding to C-level int or unsigned int.  May be zeroext, signext,
  /// or none.
private:
  /// Return the i32 parameter extension attribute for the given flags.
  /// @param ShouldExtI32Param_ Whether signedness-dependent extension applies.
  /// @param ShouldSignExtI32Param_ Whether signext always applies.
  /// @param Signed Whether the C-level parameter is signed.
  static Attribute::AttrKind getExtAttrForI32Param(bool ShouldExtI32Param_,
                                                   bool ShouldSignExtI32Param_,
                                                   bool Signed = true) {
    if (ShouldExtI32Param_)
      return Signed ? Attribute::SExt : Attribute::ZExt;
    if (ShouldSignExtI32Param_)
      return Attribute::SExt;
    return Attribute::None;
  }

public:
  /// Return the i32 parameter extension attribute for target triple \p T.
  /// @param T Target triple used to select extension conventions.
  /// @param Signed Whether the C-level parameter is signed.
  /// @return Extension attribute kind for i32 parameters.
  static Attribute::AttrKind getExtAttrForI32Param(const Triple &T,
                                                   bool Signed = true) {
    bool ShouldExtI32Param, ShouldExtI32Return;
    bool ShouldSignExtI32Param, ShouldSignExtI32Return;
    initExtensionsForTriple(ShouldExtI32Param, ShouldExtI32Return,
                            ShouldSignExtI32Param, ShouldSignExtI32Return, T);
    return getExtAttrForI32Param(ShouldExtI32Param, ShouldSignExtI32Param,
                                 Signed);
  }

  /// Return the i32 parameter extension attribute for this target.
  /// @param Signed Whether the C-level parameter is signed.
  /// @return Extension attribute kind for i32 parameters.
  Attribute::AttrKind getExtAttrForI32Param(bool Signed = true) const {
    return getExtAttrForI32Param(Impl->ShouldExtI32Param,
                                 Impl->ShouldSignExtI32Param, Signed);
  }

  /// Returns extension attribute kind to be used for i32 return values
  /// corresponding to C-level int or unsigned int.  May be zeroext, signext,
  /// or none.
private:
  /// Return the i32 return extension attribute for the given flags.
  /// @param ShouldExtI32Return_ Whether signedness-dependent extension applies.
  /// @param ShouldSignExtI32Return_ Whether signext always applies.
  /// @param Signed Whether the C-level return value is signed.
  static Attribute::AttrKind getExtAttrForI32Return(bool ShouldExtI32Return_,
                                                    bool ShouldSignExtI32Return_,
                                                    bool Signed) {
    if (ShouldExtI32Return_)
      return Signed ? Attribute::SExt : Attribute::ZExt;
    if (ShouldSignExtI32Return_)
      return Attribute::SExt;
    return Attribute::None;
  }

public:
  /// Return the i32 return extension attribute for target triple \p T.
  /// @param T Target triple used to select extension conventions.
  /// @param Signed Whether the C-level return value is signed.
  /// @return Extension attribute kind for i32 return values.
  static Attribute::AttrKind getExtAttrForI32Return(const Triple &T,
                                                   bool Signed = true) {
    bool ShouldExtI32Param, ShouldExtI32Return;
    bool ShouldSignExtI32Param, ShouldSignExtI32Return;
    initExtensionsForTriple(ShouldExtI32Param, ShouldExtI32Return,
                            ShouldSignExtI32Param, ShouldSignExtI32Return, T);
    return getExtAttrForI32Return(ShouldExtI32Return, ShouldSignExtI32Return,
                                  Signed);
  }

  /// Return the i32 return extension attribute for this target.
  /// @param Signed Whether the C-level return value is signed.
  /// @return Extension attribute kind for i32 return values.
  Attribute::AttrKind getExtAttrForI32Return(bool Signed = true) const {
    return getExtAttrForI32Return(Impl->ShouldExtI32Return,
                                  Impl->ShouldSignExtI32Return, Signed);
  }

  /// Create an AttributeList with matching i32 extension attributes.
  ///
  /// Applies the same signedness-based extension attributes to the listed
  /// argument indices and, optionally, the return value. Attributes already
  /// present in \p AL are preserved in the returned list.
  /// @param C LLVM context used to build attributes.
  /// @param ArgNos Argument indices that receive the extension attribute.
  /// @param Signed Whether the C-level values are signed.
  /// @param Ret When true, also extend the return value attribute.
  /// @param AL Optional existing attribute list to extend.
  /// @return Attribute list with i32 extension attributes applied.
  AttributeList getAttrList(LLVMContext *C, ArrayRef<unsigned> ArgNos,
                            bool Signed, bool Ret = false,
                            AttributeList AL = AttributeList()) const {
    if (auto AK = getExtAttrForI32Param(Signed))
      for (auto ArgNo : ArgNos)
        AL = AL.addParamAttribute(*C, ArgNo, AK);
    if (Ret)
      if (auto AK = getExtAttrForI32Return(Signed))
        AL = AL.addRetAttribute(*C, AK);
    return AL;
  }

  /// \copydoc TargetLibraryInfoImpl::getWCharSize()
  /// @param M Module whose wchar_size metadata is queried.
  /// @return Size of wchar_t in bytes.
  unsigned getWCharSize(const Module &M) const {
    return Impl->getWCharSize(M);
  }

  /// \copydoc TargetLibraryInfoImpl::getSizeTSize()
  /// @param M Module used to determine the size_t width.
  /// @return Size of size_t in bits.
  unsigned getSizeTSize(const Module &M) const { return Impl->getSizeTSize(M); }

  /// Returns an IntegerType corresponding to size_t.
  /// @param M Module used to determine the size_t width.
  /// @return IntegerType corresponding to size_t.
  IntegerType *getSizeTType(const Module &M) const {
    return IntegerType::get(M.getContext(), getSizeTSize(M));
  }

  /// Returns a constant materialized as a size_t type.
  /// @param V Value to materialize as size_t.
  /// @param M Module used to determine the size_t type.
  /// @return ConstantInt of size_t type with value \p V.
  ConstantInt *getAsSizeT(uint64_t V, const Module &M) const {
    return ConstantInt::get(getSizeTType(M), V);
  }

  /// \copydoc TargetLibraryInfoImpl::getIntSize()
  /// @return Size of a C-level int in bits.
  unsigned getIntSize() const {
    return Impl->getIntSize();
  }

  /// Handle invalidation from the pass manager.
  ///
  /// If we try to invalidate this info, just return false. It cannot become
  /// invalid even if the module or function changes.
  /// @param M Module that may have changed.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator used to check dependent analyses.
  /// @return Always false; this analysis is immutable.
  bool invalidate(Module &M, const PreservedAnalyses &PA,
                  ModuleAnalysisManager::Invalidator &Inv) {
    return false;
  }
  /// Handle invalidation from the pass manager for a function.
  ///
  /// If we try to invalidate this info, just return false. It cannot become
  /// invalid even if the function changes.
  /// @param F Function that may have changed.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator used to check dependent analyses.
  /// @return Always false; this analysis is immutable.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv) {
    return false;
  }
  /// Returns the largest vectorization factor used in the list of
  /// vector functions.
  /// @param ScalarF Scalar function name whose mappings are inspected.
  /// @param FixedVF Set to the widest fixed vectorization factor found.
  /// @param ScalableVF Set to the widest scalable vectorization factor found.
  void getWidestVF(StringRef ScalarF, ElementCount &FixedVF,
                   ElementCount &ScalableVF) const {
    Impl->getWidestVF(ScalarF, FixedVF, ScalableVF);
  }

  /// Check if the function "F" is listed in a library known to LLVM.
  /// @param F Function name to look up among known vectorizable library funcs.
  /// @return True if \p F is listed in a known vectorizable library.
  bool isKnownVectorFunctionInLibrary(StringRef F) const {
    return this->isFunctionVectorizable(F);
  }

  /// Returns whether `errno` is defined as a function call on known
  /// environments.
  /// @return True if errno is defined as a function call on known environments.
  bool isErrnoFunctionCall() const { return Impl->isErrnoFunctionCall(); }
};

/// Analysis pass providing the \c TargetLibraryInfo.
///
/// Note that this pass's result cannot be invalidated, it is immutable for the
/// life of the module.
class TargetLibraryAnalysis : public AnalysisInfoMixin<TargetLibraryAnalysis> {
public:
  /// The analysis result type; target library info for a function.
  typedef TargetLibraryInfo Result;

  /// Default construct the library analysis.
  ///
  /// This will use the module's triple to construct the library info for that
  /// module.
  TargetLibraryAnalysis() = default;

  /// Construct a library analysis with baseline Module-level info.
  ///
  /// This will be supplemented with Function-specific info in the Result.
  /// @param BaselineInfoImpl Module-level baseline target library info.
  TargetLibraryAnalysis(TargetLibraryInfoImpl BaselineInfoImpl)
      : BaselineInfoImpl(std::move(BaselineInfoImpl)) {}

  /// Run the analysis over \p F and produce TargetLibraryInfo.
  /// @param F Function to analyze.
  /// @param FAM Function analysis manager providing dependencies.
  /// @return Target library info for \p F.
  LLVM_ABI TargetLibraryInfo run(const Function &F,
                                 FunctionAnalysisManager &FAM);

private:
  friend AnalysisInfoMixin<TargetLibraryAnalysis>;
  LLVM_ABI static AnalysisKey Key;

  std::optional<TargetLibraryInfoImpl> BaselineInfoImpl;
};

/// Legacy immutable wrapper pass around TargetLibraryAnalysis.
class LLVM_ABI TargetLibraryInfoWrapperPass : public ImmutablePass {
  TargetLibraryAnalysis TLA;
  std::optional<TargetLibraryInfo> TLI;

  virtual void anchor();

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// The default constructor should not be used and is only for pass manager
  /// initialization purposes.
  TargetLibraryInfoWrapperPass();

  /// Construct the wrapper pass for target triple \p T.
  /// @param T Target triple used to build library info.
  explicit TargetLibraryInfoWrapperPass(const Triple &T);
  /// Construct the wrapper pass from module-level info \p TLI.
  /// @param TLI Module-level target library info implementation.
  explicit TargetLibraryInfoWrapperPass(const TargetLibraryInfoImpl &TLI);

  // FIXME: This should be removed when PlaceSafepoints is fixed to not create a
  // PassManager inside a pass.
  /// Construct the wrapper pass from existing target library info \p TLI.
  /// @param TLI Target library info to wrap.
  explicit TargetLibraryInfoWrapperPass(const TargetLibraryInfo &TLI);

  /// Return the TargetLibraryInfo for function \p F.
  /// @param F Function whose target library info is requested.
  /// @return TargetLibraryInfo for function \p F.
  TargetLibraryInfo &getTLI(const Function &F) {
    FunctionAnalysisManager DummyFAM;
    TLI = TLA.run(F, DummyFAM);
    return *TLI;
  }
};

} // end namespace llvm

#endif
