//===- OMPConstants.h - OpenMP related constants and helpers ------ C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines constans and helpers used when dealing with OpenMP.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_OPENMP_OMPCONSTANTS_H
#define LLVM_FRONTEND_OPENMP_OMPCONSTANTS_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Frontend/OpenMP/OMP.h"

namespace llvm {
namespace omp {
// Expanded (instead of the macro) so MrDocs can attach docs to each using.
/// Bring bitmask enum bitwise NOT into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator~;
/// Bring bitmask enum bitwise OR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator|;
/// Bring bitmask enum bitwise AND into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator&;
/// Bring bitmask enum bitwise XOR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator^;
/// Bring bitmask enum left-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator<<;
/// Bring bitmask enum right-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator>>;
/// Bring bitmask enum in-place OR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator|=;
/// Bring bitmask enum in-place AND into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator&=;
/// Bring bitmask enum in-place XOR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator^=;
/// Bring bitmask enum in-place left-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator<<=;
/// Bring bitmask enum in-place right-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator>>=;
/// Bring bitmask enum logical-not into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator!;
/// Bring bitmask enum any-bits-set test into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::any;

/// IDs for all Internal Control Variables (ICVs).
enum class InternalControlVar {
#define ICV_DATA_ENV(Enum, ...) Enum,
#include "llvm/Frontend/OpenMP/OMPKinds.def"
};

#define ICV_DATA_ENV(Enum, ...)                                                \
  constexpr auto Enum = omp::InternalControlVar::Enum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"

/// Initial values that OpenMP Internal Control Variables may take.
enum class ICVInitValue {
#define ICV_INIT_VALUE(Enum, Name) Enum,
#include "llvm/Frontend/OpenMP/OMPKinds.def"
};

#define ICV_INIT_VALUE(Enum, Name)                                             \
  constexpr auto Enum = omp::ICVInitValue::Enum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"

/// IDs for all omp runtime library (RTL) functions.
enum class RuntimeFunction {
#define OMP_RTL(Enum, ...) Enum,
#include "llvm/Frontend/OpenMP/OMPKinds.def"
};

#define OMP_RTL(Enum, ...) constexpr auto Enum = omp::RuntimeFunction::Enum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"

/// IDs for the different default kinds.
enum class DefaultKind {
#define OMP_DEFAULT_KIND(Enum, Str) Enum,
#include "llvm/Frontend/OpenMP/OMPKinds.def"
};

#define OMP_DEFAULT_KIND(Enum, ...)                                            \
  constexpr auto Enum = omp::DefaultKind::Enum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"

/// IDs for all omp runtime library ident_t flag encodings (see
/// their defintion in openmp/runtime/src/kmp.h).
enum class IdentFlag {
#define OMP_IDENT_FLAG(Enum, Str, Value) Enum = Value,
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(0x7FFFFFFF)
};

#define OMP_IDENT_FLAG(Enum, ...) constexpr auto Enum = omp::IdentFlag::Enum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"

// Version of the kernel argument format used by the omp runtime.
#define OMP_KERNEL_ARG_VERSION 5

// Minimum version of the compiler that generates a kernel dynamic pointer.
#define OMP_KERNEL_ARG_MIN_VERSION_WITH_DYN_PTR 3

/// Schedule kinds for OpenMP worksharing loops, matching kmp.h sched_type.
///
/// \note This needs to be kept in sync with kmp.h enum sched_type.
/// Todo: Update kmp.h to include this file, and remove the enums in kmp.h
enum class OMPScheduleType {
  /// Sentinel for typed comparisons; not a valid schedule.
  None = 0,

  /// Static schedule with an explicit chunk size.
  BaseStaticChunked = 1,
  /// Static schedule without an explicit chunk size.
  BaseStatic = 2,
  /// Dynamic schedule with chunking.
  BaseDynamicChunked = 3,
  /// Guided schedule with chunking.
  BaseGuidedChunked = 4,
  /// Runtime-selected schedule.
  BaseRuntime = 5,
  /// Auto schedule chosen by the runtime.
  BaseAuto = 6,
  /// Trapezoidal schedule algorithm.
  BaseTrapezoidal = 7,
  /// Greedy schedule algorithm.
  BaseGreedy = 8,
  /// Balanced schedule algorithm.
  BaseBalanced = 9,
  /// Guided iterative chunked schedule.
  BaseGuidedIterativeChunked = 10,
  /// Guided analytical chunked schedule.
  BaseGuidedAnalyticalChunked = 11,
  /// Work-stealing schedule algorithm.
  BaseSteal = 12,

  /// Static balanced schedule with chunk adjustment (e.g. simd).
  BaseStaticBalancedChunked = 13,
  /// Guided schedule with simd chunk adjustment.
  BaseGuidedSimd = 14,
  /// Runtime schedule with simd chunk adjustment.
  BaseRuntimeSimd = 15,

  /// Static distribute schedule with chunking.
  BaseDistributeChunked = 27,
  /// Static distribute schedule without chunking.
  BaseDistribute = 28,

  /// Modifier flag for unordered iterations.
  ModifierUnordered = (1 << 5),
  /// Modifier flag for ordered iterations.
  ModifierOrdered = (1 << 6),
  /// Modifier flag to disable schedule merging.
  ModifierNomerge = (1 << 7),
  /// Modifier flag for monotonic scheduling.
  ModifierMonotonic = (1 << 29),
  /// Modifier flag for nonmonotonic scheduling.
  ModifierNonmonotonic = (1 << 30),

  /// Mask of ordering-related modifier flags.
  OrderingMask = ModifierUnordered | ModifierOrdered | ModifierNomerge,
  /// Mask of monotonicity-related modifier flags.
  MonotonicityMask = ModifierMonotonic | ModifierNonmonotonic,
  /// Mask of all schedule modifier flags.
  ModifierMask = OrderingMask | MonotonicityMask,

  /// Unordered static chunked schedule.
  UnorderedStaticChunked = BaseStaticChunked | ModifierUnordered,        //  33
  /// Unordered static schedule.
  UnorderedStatic = BaseStatic | ModifierUnordered,                      //  34
  /// Unordered dynamic chunked schedule.
  UnorderedDynamicChunked = BaseDynamicChunked | ModifierUnordered,      //  35
  /// Unordered guided chunked schedule.
  UnorderedGuidedChunked = BaseGuidedChunked | ModifierUnordered,        //  36
  /// Unordered runtime schedule.
  UnorderedRuntime = BaseRuntime | ModifierUnordered,                    //  37
  /// Unordered auto schedule.
  UnorderedAuto = BaseAuto | ModifierUnordered,                          //  38
  /// Unordered trapezoidal schedule.
  UnorderedTrapezoidal = BaseTrapezoidal | ModifierUnordered,            //  39
  /// Unordered greedy schedule.
  UnorderedGreedy = BaseGreedy | ModifierUnordered,                      //  40
  /// Unordered balanced schedule.
  UnorderedBalanced = BaseBalanced | ModifierUnordered,                  //  41
  /// Unordered guided iterative chunked schedule.
  UnorderedGuidedIterativeChunked =
      BaseGuidedIterativeChunked | ModifierUnordered,                    //  42
  /// Unordered guided analytical chunked schedule.
  UnorderedGuidedAnalyticalChunked =
      BaseGuidedAnalyticalChunked | ModifierUnordered,                   //  43
  /// Unordered work-stealing schedule.
  UnorderedSteal = BaseSteal | ModifierUnordered,                        //  44

  /// Unordered static balanced chunked schedule.
  UnorderedStaticBalancedChunked =
      BaseStaticBalancedChunked | ModifierUnordered,                     //  45
  /// Unordered guided simd schedule.
  UnorderedGuidedSimd = BaseGuidedSimd | ModifierUnordered,              //  46
  /// Unordered runtime simd schedule.
  UnorderedRuntimeSimd = BaseRuntimeSimd | ModifierUnordered,            //  47

  /// Ordered static chunked schedule.
  OrderedStaticChunked = BaseStaticChunked | ModifierOrdered,            //  65
  /// Ordered static schedule.
  OrderedStatic = BaseStatic | ModifierOrdered,                          //  66
  /// Ordered dynamic chunked schedule.
  OrderedDynamicChunked = BaseDynamicChunked | ModifierOrdered,          //  67
  /// Ordered guided chunked schedule.
  OrderedGuidedChunked = BaseGuidedChunked | ModifierOrdered,            //  68
  /// Ordered runtime schedule.
  OrderedRuntime = BaseRuntime | ModifierOrdered,                        //  69
  /// Ordered auto schedule.
  OrderedAuto = BaseAuto | ModifierOrdered,                              //  70
  /// Ordered trapezoidal schedule (historical spelling OrderdTrapezoidal).
  OrderdTrapezoidal = BaseTrapezoidal | ModifierOrdered,                 //  71

  /// Ordered distribute chunked schedule.
  OrderedDistributeChunked = BaseDistributeChunked | ModifierOrdered,    //  91
  /// Ordered distribute schedule.
  OrderedDistribute = BaseDistribute | ModifierOrdered,                  //  92

  /// Nomerge unordered static chunked schedule.
  NomergeUnorderedStaticChunked =
      BaseStaticChunked | ModifierUnordered | ModifierNomerge,           // 161
  /// Nomerge unordered static schedule.
  NomergeUnorderedStatic =
      BaseStatic | ModifierUnordered | ModifierNomerge,                  // 162
  /// Nomerge unordered dynamic chunked schedule.
  NomergeUnorderedDynamicChunked =
      BaseDynamicChunked | ModifierUnordered | ModifierNomerge,          // 163
  /// Nomerge unordered guided chunked schedule.
  NomergeUnorderedGuidedChunked =
      BaseGuidedChunked | ModifierUnordered | ModifierNomerge,           // 164
  /// Nomerge unordered runtime schedule.
  NomergeUnorderedRuntime =
      BaseRuntime | ModifierUnordered | ModifierNomerge,                 // 165
  /// Nomerge unordered auto schedule.
  NomergeUnorderedAuto = BaseAuto | ModifierUnordered | ModifierNomerge, // 166
  /// Nomerge unordered trapezoidal schedule.
  NomergeUnorderedTrapezoidal =
      BaseTrapezoidal | ModifierUnordered | ModifierNomerge,             // 167
  /// Nomerge unordered greedy schedule.
  NomergeUnorderedGreedy =
      BaseGreedy | ModifierUnordered | ModifierNomerge,                  // 168
  /// Nomerge unordered balanced schedule.
  NomergeUnorderedBalanced =
      BaseBalanced | ModifierUnordered | ModifierNomerge,                // 169
  /// Nomerge unordered guided iterative chunked schedule.
  NomergeUnorderedGuidedIterativeChunked =
      BaseGuidedIterativeChunked | ModifierUnordered | ModifierNomerge,  // 170
  /// Nomerge unordered guided analytical chunked schedule.
  NomergeUnorderedGuidedAnalyticalChunked =
      BaseGuidedAnalyticalChunked | ModifierUnordered | ModifierNomerge, // 171
  /// Nomerge unordered work-stealing schedule.
  NomergeUnorderedSteal =
      BaseSteal | ModifierUnordered | ModifierNomerge,                   // 172

  /// Nomerge ordered static chunked schedule.
  NomergeOrderedStaticChunked =
      BaseStaticChunked | ModifierOrdered | ModifierNomerge,             // 193
  /// Nomerge ordered static schedule.
  NomergeOrderedStatic = BaseStatic | ModifierOrdered | ModifierNomerge, // 194
  /// Nomerge ordered dynamic chunked schedule.
  NomergeOrderedDynamicChunked =
      BaseDynamicChunked | ModifierOrdered | ModifierNomerge,            // 195
  /// Nomerge ordered guided chunked schedule.
  NomergeOrderedGuidedChunked =
      BaseGuidedChunked | ModifierOrdered | ModifierNomerge,             // 196
  /// Nomerge ordered runtime schedule.
  NomergeOrderedRuntime =
      BaseRuntime | ModifierOrdered | ModifierNomerge,                   // 197
  /// Nomerge ordered auto schedule.
  NomergeOrderedAuto = BaseAuto | ModifierOrdered | ModifierNomerge,     // 198
  /// Nomerge ordered trapezoidal schedule.
  NomergeOrderedTrapezoidal =
      BaseTrapezoidal | ModifierOrdered | ModifierNomerge,               // 199

  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue */ ModifierMask)
};

/// The fallback types for the dyn_groupprivate clause.
enum class OMPDynGroupprivateFallbackType : uint64_t {
  /// Abort the execution.
  Abort = 0,
  /// Return null pointer.
  Null = 1,
  /// Allocate from a implementation defined memory space.
  DefaultMem = 2
};

/// Default OpenMP mapper name suffix.
inline constexpr const char *OmpDefaultMapperName = "_omp_default_mapper";

/// Values for bit flags used to specify the mapping type for
/// offloading.
enum class OpenMPOffloadMappingFlags : uint64_t {
  /// No flags
  OMP_MAP_NONE = 0x0,
  /// Allocate memory on the device and move data from host to device.
  OMP_MAP_TO = 0x01,
  /// Allocate memory on the device and move data from device to host.
  OMP_MAP_FROM = 0x02,
  /// Always perform the requested mapping action on the element, even
  /// if it was already mapped before.
  OMP_MAP_ALWAYS = 0x04,
  /// Delete the element from the device environment, ignoring the
  /// current reference count associated with the element.
  OMP_MAP_DELETE = 0x08,
  /// The element being mapped is a pointer-pointee pair; both the
  /// pointer and the pointee should be mapped.
  OMP_MAP_PTR_AND_OBJ = 0x10,
  /// This flags signals that the base address of an entry should be
  /// passed to the target kernel as an argument.
  OMP_MAP_TARGET_PARAM = 0x20,
  /// Signal that the runtime library has to return the device pointer
  /// in the current position for the data being mapped. Used when we have the
  /// use_device_ptr or use_device_addr clause.
  OMP_MAP_RETURN_PARAM = 0x40,
  /// This flag signals that the reference being passed is a pointer to
  /// private data.
  OMP_MAP_PRIVATE = 0x80,
  /// Pass the element to the device by value.
  OMP_MAP_LITERAL = 0x100,
  /// Implicit map
  OMP_MAP_IMPLICIT = 0x200,
  /// Close is a hint to the runtime to allocate memory close to
  /// the target device.
  OMP_MAP_CLOSE = 0x400,
  /// 0x800 is reserved for compatibility with XLC.
  /// Produce a runtime error if the data is not already allocated.
  OMP_MAP_PRESENT = 0x1000,
  // Increment and decrement a separate reference counter so that the data
  // cannot be unmapped within the associated region.  Thus, this flag is
  // intended to be used on 'target' and 'target data' directives because they
  // are inherently structured.  It is not intended to be used on 'target
  // enter data' and 'target exit data' directives because they are inherently
  // dynamic.
  // This is an OpenMP extension for the sake of OpenACC support.
  OMP_MAP_OMPX_HOLD = 0x2000,
  // Attach pointer and pointee, after processing all other maps.
  // Applicable to map-entering directives. Does not change ref-count.
  OMP_MAP_ATTACH = 0x4000,
  // When a lookup fails, fall back to using null as the translated pointer,
  // instead of preserving the original pointer's value. Currently only
  // useful in conjunction with RETURN_PARAM.
  OMP_MAP_FB_NULLIFY = 0x8000,
  /// Signal that the runtime library should use args as an array of
  /// descriptor_dim pointers and use args_size as dims. Used when we have
  /// non-contiguous list items in target update directive
  OMP_MAP_NON_CONTIG = 0x100000000000,
  /// The 16 MSBs of the flags indicate whether the entry is member of some
  /// struct/class.
  OMP_MAP_MEMBER_OF = 0xffff000000000000,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestFlag = */ OMP_MAP_MEMBER_OF)
};

/// Reserved device IDs used for OpenMP offloading.
enum OpenMPOffloadingReservedDeviceIDs {
  /// Device ID if the device was not defined, runtime should get it
  /// from environment variables in the spec.
  OMP_DEVICEID_UNDEF = -1
};

/// OpenMP target address spaces used in device code generation.
enum class AddressSpace : unsigned {
  /// Generic (flat) address space.
  Generic = 0,
  /// Global device memory address space.
  Global = 1,
  /// Shared (work-group local) address space.
  Shared = 3,
  /// Constant (read-only) address space.
  Constant = 4,
  /// Local (private per-lane) address space.
  Local = 5,
};

/// OpenMP interop object kinds, matching interop.h kmp_interop_type_t.
///
/// \note This needs to be kept in sync with interop.h enum kmp_interop_type_t.:
enum class OMPInteropType {
  /// Unknown or unspecified interop type.
  Unknown,
  /// Target interop object.
  Target,
  /// Target synchronization interop object.
  TargetSync
};

/// Atomic compare operations. Currently OpenMP only supports ==, >, and <.
enum class OMPAtomicCompareOp : unsigned {
  /// Equality comparison (==).
  EQ,
  /// Minimum comparison (<).
  MIN,
  /// Maximum comparison (>).
  MAX
};

/// Fields ids in kmp_depend_info record.
enum class RTLDependInfoFields {
  /// Base address of the dependence object.
  BaseAddr,
  /// Length of the dependence object in bytes.
  Len,
  /// Dependence flags for the object.
  Flags
};

/// Dependence kind for RTL.
enum class RTLDependenceKindTy {
  /// Unknown or unspecified dependence kind.
  DepUnknown = 0x0,
  /// Input dependence.
  DepIn = 0x01,
  /// Input-output dependence.
  DepInOut = 0x3,
  /// Mutexinoutset dependence.
  DepMutexInOutSet = 0x4,
  /// Inoutset dependence.
  DepInOutSet = 0x8,
  /// Dependence on all memory.
  DepOmpAllMem = 0x80,
};

/// A type of worksharing loop construct
enum class WorksharingLoopType {
  /// Worksharing `for`-loop.
  ForStaticLoop,
  /// Worksharing `distribute`-loop.
  DistributeStaticLoop,
  /// Worksharing `distribute parallel for`-loop.
  DistributeForStaticLoop
};

} // end namespace omp

} // end namespace llvm

#include "OMPDeviceConstants.h"

#endif // LLVM_FRONTEND_OPENMP_OMPCONSTANTS_H
