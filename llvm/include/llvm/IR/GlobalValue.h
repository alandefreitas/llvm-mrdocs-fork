//===-- llvm/GlobalValue.h - Class to represent a global value --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a common base class of all globally definable objects.  As such,
// it is subclassed by GlobalVariable, GlobalAlias and by Function.  This is
// used because you can do certain things with these global objects that you
// can't do to anything else.  For example, use the address of one as a
// constant.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GLOBALVALUE_H
#define LLVM_IR_GLOBALVALUE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <string>

namespace llvm {

/// Represents a COMDAT group for global objects.
class Comdat;
class ConstantRange;
class DataLayout;
class Error;
class GlobalObject;
class Module;

/// Module pass that assigns a globally unique identifier to every global value.
class AssignGUIDPass;

namespace Intrinsic {
typedef unsigned ID;
} // end namespace Intrinsic

/// Delimiter used when constructing a global identifier from name and filename.
///
/// ';' is used because ':' appears in Objective-C function names.
inline constexpr char GlobalIdentifierDelimiter = ';';

/// Base class of globally definable objects such as functions, variables, and
/// aliases.
///
/// Global values can do things other LLVM values cannot, for example using the
/// address of one as a constant. This class is subclassed by GlobalVariable,
/// GlobalAlias, GlobalIFunc, and Function.
class GlobalValue : public Constant {
public:
  /// An enumeration for the kinds of linkage for global values.
  enum LinkageTypes {
    ExternalLinkage = 0,///< Externally visible function
    AvailableExternallyLinkage, ///< Available for inspection, not emission.
    LinkOnceAnyLinkage, ///< Keep one copy of function when linking (inline)
    LinkOnceODRLinkage, ///< Same, but only replaced by something equivalent.
    WeakAnyLinkage,     ///< Keep one copy of named function when linking (weak)
    WeakODRLinkage,     ///< Same, but only replaced by something equivalent.
    AppendingLinkage,   ///< Special purpose, only applies to global arrays
    InternalLinkage,    ///< Rename collisions when linking (static functions).
    PrivateLinkage,     ///< Like Internal, but omit from symbol table.
    ExternalWeakLinkage,///< ExternalWeak linkage description.
    CommonLinkage       ///< Tentative definitions.
  };

  /// An enumeration for the kinds of visibility of global values.
  enum VisibilityTypes {
    DefaultVisibility = 0,  ///< The GV is visible
    HiddenVisibility,       ///< The GV is hidden
    ProtectedVisibility     ///< The GV is protected
  };

  /// Storage classes of global values for PE targets.
  enum DLLStorageClassTypes {
    DefaultStorageClass   = 0, ///< No DLL import or export storage class.
    DLLImportStorageClass = 1, ///< Function to be imported from DLL
    DLLExportStorageClass = 2  ///< Function to be accessible from DLL.
  };

protected:
  /// Construct a global value with the given type, value kind, linkage, and
  /// name.
  /// \param Ty The type of the value (pointee type).
  /// \param VTy The ValueTy subclass identifier.
  /// \param AllocInfo Operand allocation information for User.
  /// \param Linkage The linkage type of this global.
  /// \param Name The name of this global.
  /// \param AddressSpace The address space of this global's pointer.
  GlobalValue(Type *Ty, ValueTy VTy, AllocInfo AllocInfo, LinkageTypes Linkage,
              const Twine &Name, unsigned AddressSpace)
      : Constant(PointerType::get(Ty->getContext(), AddressSpace), VTy,
                 AllocInfo),
        ValueType(Ty), Visibility(DefaultVisibility),
        UnnamedAddrVal(unsigned(UnnamedAddr::None)),
        DllStorageClass(DefaultStorageClass), ThreadLocal(NotThreadLocal),
        HasLLVMReservedName(false), IsDSOLocal(false), HasPartition(false),
        HasSanitizerMetadata(false) {
    setLinkage(Linkage);
    setName(Name);
  }

  /// The type of the value this global points to (the pointee type).
  Type *ValueType;

  /// Number of bits reserved for subclass-specific data in GlobalValue.
  static const unsigned GlobalValueSubClassDataBits = 15;

  // All bitfields use unsigned as the underlying type so that MSVC will pack
  // them.
  /// Linkage type of this global (see \ref LinkageTypes).
  unsigned Linkage : 4;
  unsigned Visibility : 2; ///< The visibility of this global (see \ref VisibilityTypes).
  unsigned UnnamedAddrVal : 2; ///< This value's unnamed address mode (see \ref UnnamedAddr).
  unsigned DllStorageClass : 2; ///< DLL storage class of this global (see \ref DLLStorageClassTypes).

  unsigned ThreadLocal : 3; ///< Is this symbol "Thread Local", if so, what is
                            ///< the desired model?

  /// True if this value's name starts with "llvm.".
  ///
  /// This corresponds to the value of Function::isIntrinsic(), which may be
  /// true even if Function::intrinsicID() returns Intrinsic::not_intrinsic.
  unsigned HasLLVMReservedName : 1;

  /// If true then there is a definition within the same linkage unit and that
  /// definition cannot be runtime preempted.
  unsigned IsDSOLocal : 1;

  /// True if this symbol has a partition name assigned (see
  /// https://lld.llvm.org/Partitions.html).
  unsigned HasPartition : 1;

  /// True if this symbol has sanitizer metadata available. Should only happen
  /// if sanitizers were enabled when building the translation unit which
  /// contains this GV.
  unsigned HasSanitizerMetadata : 1;

private:
  // Give subclasses access to what otherwise would be wasted padding.
  // (15 + 4 + 2 + 2 + 2 + 3 + 1 + 1 + 1 + 1) == 32.
  unsigned SubClassData : GlobalValueSubClassDataBits;

  friend class Constant;

  void destroyConstantImpl();
  Value *handleOperandChangeImpl(Value *From, Value *To);

  /// Returns true if the definition of this global may be replaced by a
  /// differently optimized variant of the same source level function at link
  /// time.
  bool mayBeDerefined() const {
    switch (getLinkage()) {
    case WeakODRLinkage:
    case LinkOnceODRLinkage:
    case AvailableExternallyLinkage:
      return true;

    case WeakAnyLinkage:
    case LinkOnceAnyLinkage:
    case CommonLinkage:
    case ExternalWeakLinkage:
    case ExternalLinkage:
    case AppendingLinkage:
    case InternalLinkage:
    case PrivateLinkage:
      // Optimizations may assume builtin semantics for functions defined as
      // nobuiltin due to attributes at call-sites. To avoid applying IPO based
      // on nobuiltin semantics, treat such function definitions as maybe
      // derefined.
      return isInterposable() || isNobuiltinFnDef();
    }

    llvm_unreachable("Fully covered switch above!");
  }

  /// Returns true if the global is a function definition with the nobuiltin
  /// attribute.
  LLVM_ABI bool isNobuiltinFnDef() const;

  /// Returns true if the global is a function definition with the noipa
  /// attribute.
  LLVM_ABI bool isNoipaFnDef() const;

protected:
  /// The intrinsic ID for this subclass (which must be a Function).
  ///
  /// This member is defined by this class, but not used for anything.
  /// Subclasses can use it to store their intrinsic ID, if they have one.
  ///
  /// This is stored here to save space in Function on 64-bit hosts.
  Intrinsic::ID IntID = (Intrinsic::ID)0U;

  /// Return subclass-specific data packed into this global value.
  /// \return The subclass-specific data packed into this global value.
  unsigned getGlobalValueSubClassData() const {
    return SubClassData;
  }
  /// Set subclass-specific data packed into this global value.
  /// \param V The subclass data bits to store.
  void setGlobalValueSubClassData(unsigned V) {
    assert(V < (1 << GlobalValueSubClassDataBits) && "It will not fit");
    SubClassData = V;
  }

  Module *Parent = nullptr; ///< The module containing this global value.

  // Used by SymbolTableListTraits.
  /// Set the module that contains this global value.
  /// \param parent The containing module, or null.
  void setParent(Module *parent) {
    Parent = parent;
  }

  /// Destroy this global and remove dead constant users.
  ~GlobalValue() {
    removeDeadConstantUsers();   // remove any dead constants using this.
  }

public:
  /// Thread-local storage models for global variables.
  enum ThreadLocalMode {
    NotThreadLocal = 0,       ///< Not a thread-local symbol.
    GeneralDynamicTLSModel,   ///< General-dynamic TLS model.
    LocalDynamicTLSModel,     ///< Local-dynamic TLS model.
    InitialExecTLSModel,      ///< Initial-exec TLS model.
    LocalExecTLSModel         ///< Local-exec TLS model.
  };

  /// Copy construction is deleted; GlobalValue is non-copyable.
  /// \param Other The global value that would be copied (deleted).
  GlobalValue(const GlobalValue &Other) = delete;

  /// Return the address space of this global value's pointer type.
  /// \return The address space of this global value's pointer type.
  unsigned getAddressSpace() const {
    return getType()->getAddressSpace();
  }

  /// How significant this global's address is when merging or linking.
  enum class UnnamedAddr {
    /// Address is significant; no unnamed_addr attribute.
    None,
    /// Address is insignificant within this module only.
    Local,
    /// Address is insignificant across all linked modules.
    Global,
  };

  /// Return true if this global has the global unnamed_addr attribute.
  /// \return True if this global has the global unnamed_addr attribute.
  bool hasGlobalUnnamedAddr() const {
    return getUnnamedAddr() == UnnamedAddr::Global;
  }

  /// Returns true if this value's address is not significant in this module.
  ///
  /// This attribute is intended to be used only by the code generator and LTO
  /// to allow the linker to decide whether the global needs to be in the symbol
  /// table. It should probably not be used in optimizations, as the value may
  /// have uses outside the module; use hasGlobalUnnamedAddr() instead.
  /// \return True if this value's address is not significant in this module.
  bool hasAtLeastLocalUnnamedAddr() const {
    return getUnnamedAddr() != UnnamedAddr::None;
  }

  /// Return how significant this global's address is for merging/linking.
  /// \return How significant this global's address is for merging/linking.
  UnnamedAddr getUnnamedAddr() const {
    return UnnamedAddr(UnnamedAddrVal);
  }
  /// Set how significant this global's address is for merging and linking.
  /// \param Val The unnamed address mode to assign.
  void setUnnamedAddr(UnnamedAddr Val) { UnnamedAddrVal = unsigned(Val); }

  /// Return the more significant unnamed_addr of \p A and \p B.
  /// \param A First unnamed address mode.
  /// \param B Second unnamed address mode.
  /// \return The more restrictive unnamed_addr of \p A and \p B.
  static UnnamedAddr getMinUnnamedAddr(UnnamedAddr A, UnnamedAddr B) {
    if (A == UnnamedAddr::None || B == UnnamedAddr::None)
      return UnnamedAddr::None;
    if (A == UnnamedAddr::Local || B == UnnamedAddr::Local)
      return UnnamedAddr::Local;
    return UnnamedAddr::Global;
  }

  /// Return true if this global is in a COMDAT group.
  /// \return True if this global is in a COMDAT group.
  bool hasComdat() const { return getComdat() != nullptr; }
  /// Return the Comdat object for this global value, or null if none.
  /// \return The Comdat object, or null if none.
  LLVM_ABI const Comdat *getComdat() const;
  /// Return the Comdat object for this global value, or null if none.
  /// \return The Comdat object, or null if none.
  Comdat *getComdat() {
    return const_cast<Comdat *>(
                           static_cast<const GlobalValue *>(this)->getComdat());
  }

  /// Return the visibility of this global value.
  /// \return The visibility of this global value.
  VisibilityTypes getVisibility() const { return VisibilityTypes(Visibility); }
  /// Return true if this global has default visibility.
  /// \return True if this global has default visibility.
  bool hasDefaultVisibility() const { return Visibility == DefaultVisibility; }
  /// Return true if this global has hidden visibility.
  /// \return True if this global has hidden visibility.
  bool hasHiddenVisibility() const { return Visibility == HiddenVisibility; }
  /// Return true if this global has protected visibility.
  /// \return True if this global has protected visibility.
  bool hasProtectedVisibility() const {
    return Visibility == ProtectedVisibility;
  }
  /// Set the visibility of this global value.
  /// \param V The visibility to assign.
  void setVisibility(VisibilityTypes V) {
    assert((!hasLocalLinkage() || V == DefaultVisibility) &&
           "local linkage requires default visibility");
    Visibility = V;
    if (isImplicitDSOLocal())
      setDSOLocal(true);
  }

  /// If the value is "Thread Local", its value isn't shared by the threads.
  /// \return True if this global is thread-local.
  bool isThreadLocal() const { return getThreadLocalMode() != NotThreadLocal; }
  /// Set whether this global is thread-local using the general-dynamic model.
  /// \param Val True to make the global thread-local.
  void setThreadLocal(bool Val) {
    setThreadLocalMode(Val ? GeneralDynamicTLSModel : NotThreadLocal);
  }
  /// Set the thread-local storage model of this global.
  /// \param Val The TLS model to assign.
  void setThreadLocalMode(ThreadLocalMode Val) {
    assert(Val == NotThreadLocal || getValueID() != Value::FunctionVal);
    ThreadLocal = Val;
  }
  /// Return the thread-local storage model of this global.
  /// \return The thread-local storage model of this global.
  ThreadLocalMode getThreadLocalMode() const {
    return static_cast<ThreadLocalMode>(ThreadLocal);
  }

  /// Return the DLL storage class of this global value.
  /// \return The DLL storage class of this global value.
  DLLStorageClassTypes getDLLStorageClass() const {
    return DLLStorageClassTypes(DllStorageClass);
  }
  /// Return true if this global is imported from a DLL.
  /// \return True if this global is imported from a DLL.
  bool hasDLLImportStorageClass() const {
    return DllStorageClass == DLLImportStorageClass;
  }
  /// Return true if this global is exported from a DLL.
  /// \return True if this global is exported from a DLL.
  bool hasDLLExportStorageClass() const {
    return DllStorageClass == DLLExportStorageClass;
  }
  /// Set the DLL storage class of this global value.
  /// \param C The DLL storage class to assign.
  void setDLLStorageClass(DLLStorageClassTypes C) {
    assert((!hasLocalLinkage() || C == DefaultStorageClass) &&
           "local linkage requires DefaultStorageClass");
    DllStorageClass = C;
  }

  /// Return true if this global has a non-empty section name.
  /// \return True if this global has a non-empty section name.
  bool hasSection() const { return !getSection().empty(); }
  /// Return the object-file section name for this global, or empty if none.
  /// \return The section name, or empty if none.
  LLVM_ABI StringRef getSection() const;

  /// Global values are always pointers.
  /// \return The pointer type of this global value.
  PointerType *getType() const { return cast<PointerType>(User::getType()); }

  /// Return the type of the value (the pointee type for pointer globals).
  /// \return The pointee type of this global value.
  Type *getValueType() const { return ValueType; }

  /// Return true if linkage or visibility implies this symbol is dso_local.
  /// \return True if linkage or visibility implies this symbol is dso_local.
  bool isImplicitDSOLocal() const {
    return hasLocalLinkage() ||
           (!hasDefaultVisibility() && !hasExternalWeakLinkage());
  }

  /// Set whether this symbol is known to be local to the linkage unit.
  /// \param Local True if the symbol cannot be preempted at runtime.
  void setDSOLocal(bool Local) { IsDSOLocal = Local; }

  /// Return true if this symbol is known to be local to the DSO.
  /// \return True if this symbol is known to be local to the DSO.
  bool isDSOLocal() const {
    return IsDSOLocal;
  }

  /// Return true if this global has a linker partition name.
  /// \return True if this global has a linker partition name.
  bool hasPartition() const {
    return HasPartition;
  }
  /// Return the linker partition name for this global, or empty if none is set.
  /// \return The linker partition name, or empty if none is set.
  LLVM_ABI StringRef getPartition() const;
  /// Set the linker partition name for this global.
  /// \param Part The partition name, or empty to clear it.
  LLVM_ABI void setPartition(StringRef Part);

  /// Per-global sanitizer instrumentation flags for ASan, HWASan, and Memtag.
  ///
  /// These bits record whether sanitizer instrumentation was explicitly
  /// disabled for a global so that IR passes such as GlobalMerge preserve the
  /// decision when replacing or merging globals.
  struct SanitizerMetadata {
    /// Construct sanitizer metadata with all flags cleared.
    SanitizerMetadata()
        : NoAddress(false), NoHWAddress(false),
          Memtag(false), IsDynInit(false) {}
    // For ASan and HWASan, this instrumentation is implicitly applied to all
    // global variables when built with -fsanitize=*. What we need is a way to
    // persist the information that a certain global variable should *not* have
    // sanitizers applied, which occurs if:
    //   1. The global variable is in the sanitizer ignore list, or
    //   2. The global variable is created by the sanitizers itself for internal
    //      usage, or
    //   3. The global variable has __attribute__((no_sanitize("..."))) or
    //      __attribute__((disable_sanitizer_instrumentation)).
    //
    // This is important, a some IR passes like GlobalMerge can delete global
    // variables and replace them with new ones. If the old variables were
    // marked to be unsanitized, then the new ones should also be.
    unsigned NoAddress : 1; ///< Do not apply address sanitizer instrumentation.
    unsigned NoHWAddress : 1; ///< Do not apply HWAddressSanitizer instrumentation.

    // Memtag sanitization works differently: sanitization is requested by clang
    // when `-fsanitize=memtag-globals` is provided, and the request can be
    // denied (and the attribute removed) by the AArch64 global tagging pass if
    // it can't be fulfilled (e.g. the global variable is a TLS variable).
    // Memtag sanitization has to interact with other parts of LLVM (like
    // supressing certain optimisations, emitting assembly directives, or
    // creating special relocation sections).
    //
    // Use `GlobalValue::isTagged()` to check whether tagging should be enabled
    // for a global variable.
    unsigned Memtag : 1; ///< Request memtag sanitization for this global.

    /// True if this global is dynamically initialized (C++ sense) and should be
    /// checked for ODR violations by AddressSanitizer.
    unsigned IsDynInit : 1;
  };

  /// Return true if this global has sanitizer metadata attached.
  /// \return True if this global has sanitizer metadata attached.
  bool hasSanitizerMetadata() const { return HasSanitizerMetadata; }
  /// Return the sanitizer instrumentation flags for this global.
  /// \return The sanitizer instrumentation flags for this global.
  LLVM_ABI const SanitizerMetadata &getSanitizerMetadata() const;
  // Note: Not byref as it's a POD and otherwise it's too easy to call
  // G.setSanitizerMetadata(G2.getSanitizerMetadata()), and the argument becomes
  // dangling when the backing storage allocates the metadata for `G`, as the
  // storage is shared between `G1` and `G2`.
  /// Set sanitizer instrumentation flags for this global value.
  /// \param Meta The sanitizer flags to attach.
  LLVM_ABI void setSanitizerMetadata(SanitizerMetadata Meta);
  /// Remove sanitizer metadata from this global value.
  LLVM_ABI void removeSanitizerMetadata();
  /// Disable address and HWAddress sanitizer instrumentation for this global.
  LLVM_ABI void setNoSanitizeMetadata();

  /// Return true if memtag sanitization is requested for this global.
  /// \return True if memtag sanitization is requested for this global.
  bool isTagged() const {
    return hasSanitizerMetadata() && getSanitizerMetadata().Memtag;
  }

  /// Return linkonce or linkonce_odr linkage depending on \p ODR.
  /// \param ODR True to select linkonce_odr, false for linkonce.
  /// \return LinkOnceODRLinkage if \p ODR is true, otherwise LinkOnceAnyLinkage.
  static LinkageTypes getLinkOnceLinkage(bool ODR) {
    return ODR ? LinkOnceODRLinkage : LinkOnceAnyLinkage;
  }
  /// Return weak or weak_odr linkage depending on \p ODR.
  /// \param ODR True to select weak_odr, false for weak.
  /// \return WeakODRLinkage if \p ODR is true, otherwise WeakAnyLinkage.
  static LinkageTypes getWeakLinkage(bool ODR) {
    return ODR ? WeakODRLinkage : WeakAnyLinkage;
  }

  /// Return true if \p Linkage is external linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is external linkage.
  static bool isExternalLinkage(LinkageTypes Linkage) {
    return Linkage == ExternalLinkage;
  }
  /// Return true if \p Linkage is available_externally linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is available_externally linkage.
  static bool isAvailableExternallyLinkage(LinkageTypes Linkage) {
    return Linkage == AvailableExternallyLinkage;
  }
  /// Return true if \p Linkage is linkonce-any linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is linkonce-any linkage.
  static bool isLinkOnceAnyLinkage(LinkageTypes Linkage) {
    return Linkage == LinkOnceAnyLinkage;
  }
  /// Return true if \p Linkage is linkonce_odr linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is linkonce_odr linkage.
  static bool isLinkOnceODRLinkage(LinkageTypes Linkage) {
    return Linkage == LinkOnceODRLinkage;
  }
  /// Return true if \p Linkage is linkonce or linkonce ODR linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is linkonce or linkonce ODR linkage.
  static bool isLinkOnceLinkage(LinkageTypes Linkage) {
    return isLinkOnceAnyLinkage(Linkage) || isLinkOnceODRLinkage(Linkage);
  }
  /// Return true if \p Linkage is weak_any linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is weak_any linkage.
  static bool isWeakAnyLinkage(LinkageTypes Linkage) {
    return Linkage == WeakAnyLinkage;
  }
  /// Return true if \p Linkage is weak ODR linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is weak ODR linkage.
  static bool isWeakODRLinkage(LinkageTypes Linkage) {
    return Linkage == WeakODRLinkage;
  }
  /// Return true if \p Linkage is weak or weak ODR linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is weak or weak ODR linkage.
  static bool isWeakLinkage(LinkageTypes Linkage) {
    return isWeakAnyLinkage(Linkage) || isWeakODRLinkage(Linkage);
  }
  /// Return true if \p Linkage is appending linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is appending linkage.
  static bool isAppendingLinkage(LinkageTypes Linkage) {
    return Linkage == AppendingLinkage;
  }
  /// Return true if \p Linkage is internal linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is internal linkage.
  static bool isInternalLinkage(LinkageTypes Linkage) {
    return Linkage == InternalLinkage;
  }
  /// Return true if \p Linkage is private linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is private linkage.
  static bool isPrivateLinkage(LinkageTypes Linkage) {
    return Linkage == PrivateLinkage;
  }
  /// Return true if \p Linkage is internal or private linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is internal or private linkage.
  static bool isLocalLinkage(LinkageTypes Linkage) {
    return isInternalLinkage(Linkage) || isPrivateLinkage(Linkage);
  }
  /// Return true if \p Linkage is external weak linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is external weak linkage.
  static bool isExternalWeakLinkage(LinkageTypes Linkage) {
    return Linkage == ExternalWeakLinkage;
  }
  /// Return true if \p Linkage is common linkage.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is common linkage.
  static bool isCommonLinkage(LinkageTypes Linkage) {
    return Linkage == CommonLinkage;
  }
  /// Return true if \p Linkage is valid for a declaration without a definition.
  /// \param Linkage The linkage to test.
  /// \return True if \p Linkage is valid for a declaration without a definition.
  static bool isValidDeclarationLinkage(LinkageTypes Linkage) {
    return isExternalWeakLinkage(Linkage) || isExternalLinkage(Linkage);
  }

  /// Return true if a definition with \p Linkage may be replaced
  /// non-equivalently at link time.
  ///
  /// For example, if a function has weak linkage then the code defining it may
  /// be replaced by different code.
  /// \param Linkage The linkage to test.
  /// \return True if a definition with \p Linkage may be replaced non-equivalently.
  static bool isInterposableLinkage(LinkageTypes Linkage) {
    switch (Linkage) {
    case WeakAnyLinkage:
    case LinkOnceAnyLinkage:
    case CommonLinkage:
    case ExternalWeakLinkage:
      return true;

    case AvailableExternallyLinkage:
    case LinkOnceODRLinkage:
    case WeakODRLinkage:
    // The above three cannot be overridden but can be de-refined.

    case ExternalLinkage:
    case AppendingLinkage:
    case InternalLinkage:
    case PrivateLinkage:
      return false;
    }
    llvm_unreachable("Fully covered switch above!");
  }

  /// Whether the definition of this global may be discarded if it is not used
  /// in its compilation unit.
  /// \param Linkage The linkage to test.
  /// \return True if a definition with \p Linkage may be discarded if unused.
  static bool isDiscardableIfUnused(LinkageTypes Linkage) {
    return isLinkOnceLinkage(Linkage) || isLocalLinkage(Linkage) ||
           isAvailableExternallyLinkage(Linkage);
  }

  /// Return true if a definition with \p Linkage may be replaced at link time.
  ///
  /// Using this method outside of the code generators is almost always a
  /// mistake: when working at the IR level use isInterposable instead as it
  /// knows about ODR semantics.
  /// \param Linkage The linkage to test.
  /// \return True if a definition with \p Linkage may be replaced at link time.
  static bool isWeakForLinker(LinkageTypes Linkage)  {
    return Linkage == WeakAnyLinkage || Linkage == WeakODRLinkage ||
           Linkage == LinkOnceAnyLinkage || Linkage == LinkOnceODRLinkage ||
           Linkage == CommonLinkage || Linkage == ExternalWeakLinkage;
  }

  /// Return true if the currently visible definition of this global (if any) is
  /// exactly the definition we will see at runtime.
  ///
  /// Non-exact linkage types inhibits most non-inlining IPO, since a
  /// differently optimized variant of the same function can have different
  /// observable or undefined behavior than in the variant currently visible.
  /// For instance, we could have started with
  ///
  ///   void foo(int *v) {
  ///     int t = 5 / v[0];
  ///     (void) t;
  ///   }
  ///
  /// and "refined" it to
  ///
  ///   void foo(int *v) { }
  ///
  /// However, we cannot infer readnone for `foo`, since that would justify
  /// DSE'ing a store to `v[0]` across a call to `foo`, which can cause
  /// undefined behavior if the linker replaces the actual call destination with
  /// the unoptimized `foo`.
  ///
  /// Inlining is okay across non-exact linkage types as long as they're not
  /// interposable (see \c isInterposable), since in such cases the currently
  /// visible variant is *a* correct implementation of the original source
  /// function; it just isn't the *only* correct implementation.
  /// \return True if the visible definition matches the runtime definition.
  bool isDefinitionExact() const {
    return !mayBeDerefined();
  }

  /// Return true if this global has an exact defintion.
  /// \return True if this global has an exact definition.
  bool hasExactDefinition() const {
    // While this computes exactly the same thing as
    // isStrongDefinitionForLinker, the intended uses are different.  This
    // function is intended to help decide if specific inter-procedural
    // transforms are correct, while isStrongDefinitionForLinker's intended use
    // is in low level code generation.
    return !isDeclaration() && isDefinitionExact();
  }

  /// Return true if this global's definition can be substituted arbitrarily.
  ///
  /// We cannot do any IPO or inlining across interposable call edges, since the
  /// callee can be replaced with something arbitrary. For most IPO passes, the
  /// `noipa` attribute on a function definition is also treated as if it were
  /// interposable (and thus blocking interprocedural analysis). Passes which
  /// already have their own distinct control attributes (e.g. inlining) may set
  /// `CheckNoIPA = false` when calling this.
  /// \param CheckNoIPA If true, treat the noipa attribute as interposable.
  /// \return True if this global's definition can be substituted arbitrarily.
  LLVM_ABI bool isInterposable(bool CheckNoIPA = true) const;
  /// Return true if this global may use a local alias during code generation.
  /// \return True if this global may use a local alias during code generation.
  LLVM_ABI bool canBenefitFromLocalAlias() const;

  /// Return true if this global has external linkage.
  /// \return True if this global has external linkage.
  bool hasExternalLinkage() const { return isExternalLinkage(getLinkage()); }
  /// Return true if this global has available_externally linkage.
  /// \return True if this global has available_externally linkage.
  bool hasAvailableExternallyLinkage() const {
    return isAvailableExternallyLinkage(getLinkage());
  }
  /// Return true if this global has linkonce or linkonce_odr linkage.
  /// \return True if this global has linkonce or linkonce_odr linkage.
  bool hasLinkOnceLinkage() const { return isLinkOnceLinkage(getLinkage()); }
  /// Return true if this global has linkonce-any linkage.
  /// \return True if this global has linkonce-any linkage.
  bool hasLinkOnceAnyLinkage() const {
    return isLinkOnceAnyLinkage(getLinkage());
  }
  /// Return true if this global has linkonce_odr linkage.
  /// \return True if this global has linkonce_odr linkage.
  bool hasLinkOnceODRLinkage() const {
    return isLinkOnceODRLinkage(getLinkage());
  }
  /// Return true if this global has weak or weak ODR linkage.
  /// \return True if this global has weak or weak ODR linkage.
  bool hasWeakLinkage() const { return isWeakLinkage(getLinkage()); }
  /// Return true if this global has weak_any linkage.
  /// \return True if this global has weak_any linkage.
  bool hasWeakAnyLinkage() const { return isWeakAnyLinkage(getLinkage()); }
  /// Return true if this global has weak_odr linkage.
  /// \return True if this global has weak_odr linkage.
  bool hasWeakODRLinkage() const { return isWeakODRLinkage(getLinkage()); }
  /// Return true if this global has appending linkage.
  /// \return True if this global has appending linkage.
  bool hasAppendingLinkage() const { return isAppendingLinkage(getLinkage()); }
  /// Return true if this global has internal linkage.
  /// \return True if this global has internal linkage.
  bool hasInternalLinkage() const { return isInternalLinkage(getLinkage()); }
  /// Return true if this global has private linkage.
  /// \return True if this global has private linkage.
  bool hasPrivateLinkage() const { return isPrivateLinkage(getLinkage()); }
  /// Return true if this global has internal or private linkage.
  /// \return True if this global has internal or private linkage.
  bool hasLocalLinkage() const { return isLocalLinkage(getLinkage()); }
  /// Return true if this global has external weak linkage.
  /// \return True if this global has external weak linkage.
  bool hasExternalWeakLinkage() const {
    return isExternalWeakLinkage(getLinkage());
  }
  /// Return true if this global has common linkage.
  /// \return True if this global has common linkage.
  bool hasCommonLinkage() const { return isCommonLinkage(getLinkage()); }
  /// Return true if this global has a linkage valid for a declaration.
  /// \return True if this global has a linkage valid for a declaration.
  bool hasValidDeclarationLinkage() const {
    return isValidDeclarationLinkage(getLinkage());
  }

  /// Set the linkage type of this global value.
  /// \param LT The linkage type to assign.
  void setLinkage(LinkageTypes LT) {
    if (isLocalLinkage(LT)) {
      Visibility = DefaultVisibility;
      DllStorageClass = DefaultStorageClass;
    }
    Linkage = LT;
    if (isImplicitDSOLocal())
      setDSOLocal(true);
  }
  /// Return the linkage type of this global value.
  /// \return The linkage type of this global value.
  LinkageTypes getLinkage() const { return LinkageTypes(Linkage); }

  /// Return true if this global's definition may be discarded if unused.
  /// \return True if this global's definition may be discarded if unused.
  bool isDiscardableIfUnused() const {
    return isDiscardableIfUnused(getLinkage());
  }

  /// Return true if this global's definition may be replaced at link time.
  /// \return True if this global's definition may be replaced at link time.
  bool isWeakForLinker() const { return isWeakForLinker(getLinkage()); }

protected:
  /// Copy all additional attributes (those not needed to create a GlobalValue)
  /// from the GlobalValue Src to this one.
  /// \param Src The global value to copy attributes from.
  LLVM_ABI void copyAttributesFrom(const GlobalValue *Src);

public:
  /// If the given string begins with the GlobalValue name mangling escape
  /// character '\1', drop it.
  ///
  /// This function applies a specific mangling that is used in PGO profiles,
  /// among other things. If you're trying to get a symbol name for an
  /// arbitrary GlobalValue, this is not the function you're looking for; see
  /// Mangler.h.
  /// \param Name The possibly escaped name to strip.
  /// \return \p Name without a leading mangling escape character, if present.
  static StringRef dropLLVMManglingEscape(StringRef Name) {
    Name.consume_front("\1");
    return Name;
  }

  /// A 64-bit global unique identifier used by PGO and ThinLTO.
  ///
  /// This is a 64-bit hash that provides a compact unique way to identify a
  /// symbol.
  using GUID = uint64_t;

  /// Return a lookup key for a global value used by PGO and ThinLTO.
  ///
  /// The value's original name is \c Name and has linkage of type
  /// \c Linkage. The value is defined in module \c FileName.
  /// \param Name The original name of the global value.
  /// \param Linkage The linkage of the global value.
  /// \param FileName The name of the module defining the value.
  /// \return The global identifier string used as a PGO/ThinLTO lookup key.
  LLVM_ABI static std::string
  getGlobalIdentifier(StringRef Name, GlobalValue::LinkageTypes Linkage,
                      StringRef FileName);

private:
  /// Return the modified name for this global value suitable to be
  /// used as the key for a global lookup (e.g. profile or ThinLTO).
  LLVM_ABI std::string getGlobalIdentifier() const;

  /// Assign a GUID to this value based on its current name and linkage.
  /// This GUID will remain the same even if those change. This method is
  /// idempotent -- if a GUID has already been assigned, calling it again
  /// will do nothing.
  ///
  /// This is private (exposed only to \c AssignGUIDPass), as users don't need
  /// to call it. GUIDs are assigned only by \c AssignGUIDPass. The pass
  /// pipeline should be set up such that GUIDs are always available when
  /// needed. If not, the GUID assignment pass should be moved (or run again)
  /// such that they are.
  void assignGUID();

  // assignGUID needs to be accessible from AssignGUIDPass, which is called
  // early in the pipeline to make GUIDs available to later passes. But we'd
  // rather not expose it publicly, as no-one else should call it.
  /// Module pass that assigns a globally unique identifier to every global value.
  friend class AssignGUIDPass;

  MDNode *getGUIDMetadata() const;

public:
  /// Return a 64-bit GUID for a global symbol name, assuming external linkage.
  ///
  /// Since this call doesn't supply the linkage or defining filename, the GUID
  /// computation will assume that the global has external linkage.
  /// \param GlobalName The name of the global symbol.
  /// \return The 64-bit GUID for \p GlobalName under external linkage.
  LLVM_ABI static GUID getGUIDAssumingExternalLinkage(StringRef GlobalName);

  /// Recompute and assign a GUID to this value, replacing the existing GUID.
  LLVM_ABI void reassignGUID();

  /// Return a 64-bit global unique ID for this value.
  ///
  /// It is based on the "original" name and linkage of this value (i.e. whenever
  /// its GUID was assigned). This might not match the current name and linkage.
  ///
  /// The \c AssignGUIDPass must be run before this is called, otherwise
  /// GUIDs won't be available. This pass can be run multiple times as it does
  /// nothing if GUID metadata is already present.
  /// \return The 64-bit GUID assigned to this value.
  LLVM_ABI GUID getGUID() const;

  /// Return this value's GUID if one has been assigned, or nullopt otherwise.
  ///
  /// This should only need to be used in some exceptional situations. If
  /// possible whatever code needs access to a GUID should be set up to run
  /// after AssignGUIDPass, in which case it will always be available.
  /// \return The assigned GUID, or \c std::nullopt if none.
  LLVM_ABI std::optional<GUID> getGUIDIfAssigned() const;

  /// Return the GUID for this value if it has been assigned, otherwise fall
  /// back to computing it based on its current name and linkage.
  ///
  /// This is to be used in situations where we need a GUID but can't guarantee
  /// that it's been computed. Notably, if we're reading from bitcode files
  /// that might pre-date the storage of GUIDs in metadata.
  /// \return The assigned GUID, or one computed from the current name and linkage.
  LLVM_ABI GUID getGUIDOrFallback() const;

  /// @name Materialization
  /// Materialization is used to construct functions only as they're needed.
  /// This
  /// is useful to reduce memory usage in LLVM or parsing work done by the
  /// BitcodeReader to load the Module.
  /// @{

  /// Return true if this global has not yet been fully read from its source.
  ///
  /// If this function's Module is being lazily streamed in functions from disk
  /// or some other source, this method can be used to check to see if the
  /// function has been read in yet or not.
  /// \return True if this global has not yet been fully materialized.
  LLVM_ABI bool isMaterializable() const;

  /// Make sure this GlobalValue is fully read.
  /// \return Success, or an error if materialization fails.
  LLVM_ABI Error materialize();

  /// @}

  /// Return true if the primary definition of this global value is outside of
  /// the current translation unit.
  /// \return True if the primary definition is outside this translation unit.
  LLVM_ABI bool isDeclaration() const;

  /// Return true if the linker should treat this global as a declaration.
  /// \return True if the linker should treat this global as a declaration.
  bool isDeclarationForLinker() const {
    if (hasAvailableExternallyLinkage())
      return true;

    return isDeclaration();
  }

  /// Returns true if this global's definition will be the one chosen by the
  /// linker.
  ///
  /// NB! Ideally this should not be used at the IR level at all.  If you're
  /// interested in optimization constraints implied by the linker's ability to
  /// choose an implementation, prefer using \c hasExactDefinition.
  /// \return True if the linker will choose this global's definition.
  bool isStrongDefinitionForLinker() const {
    return !(isDeclarationForLinker() || isWeakForLinker());
  }

  /// Return the underlying GlobalObject this value ultimately refers to, if any
  /// (following aliases).
  /// \return The underlying GlobalObject, or null if none.
  LLVM_ABI const GlobalObject *getAliaseeObject() const;
  /// Return the underlying GlobalObject this value ultimately refers to, if any
  /// (following aliases).
  /// \return The underlying GlobalObject, or null if none.
  GlobalObject *getAliaseeObject() {
    return const_cast<GlobalObject *>(
        static_cast<const GlobalValue *>(this)->getAliaseeObject());
  }

  /// Returns whether this is a reference to an absolute symbol.
  /// \return True if this is a reference to an absolute symbol.
  LLVM_ABI bool isAbsoluteSymbolRef() const;

  /// If this is an absolute symbol reference, returns the range of the symbol,
  /// otherwise returns std::nullopt.
  /// \return The absolute symbol range, or \c std::nullopt if not absolute.
  LLVM_ABI std::optional<ConstantRange> getAbsoluteSymbolRange() const;

  /// This method unlinks 'this' from the containing module, but does not delete
  /// it.
  LLVM_ABI void removeFromParent();

  /// This method unlinks 'this' from the containing module and deletes it.
  LLVM_ABI void eraseFromParent();

  /// Return the module that contains this global value.
  /// \return The parent module, or null if none.
  Module *getParent() { return Parent; }
  /// Return the module that contains this global value.
  /// \return The parent module, or null if none.
  const Module *getParent() const { return Parent; }

  /// Get the data layout of the module this global belongs to.
  ///
  /// Requires the global to have a parent module.
  /// \return The data layout of the parent module.
  LLVM_ABI const DataLayout &getDataLayout() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// \return True if \p V is a Function, GlobalVariable, GlobalAlias, or GlobalIFunc.
  static bool classof(const Value *V) {
    return V->getValueID() == Value::FunctionVal ||
           V->getValueID() == Value::GlobalVariableVal ||
           V->getValueID() == Value::GlobalAliasVal ||
           V->getValueID() == Value::GlobalIFuncVal;
  }

  /// Return true if this global can be left out of the object symbol table.
  ///
  /// This is the case for linkonce_odr values whose address is not significant.
  /// While legal, it is not normally profitable to omit them from the .o symbol
  /// table. Using this analysis makes sense when the information can be passed
  /// down to the linker or we are in LTO.
  /// \return True if this global can be omitted from the object symbol table.
  LLVM_ABI bool canBeOmittedFromSymbolTable() const;
};

} // end namespace llvm

#endif // LLVM_IR_GLOBALVALUE_H
