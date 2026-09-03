//===-- Instrumentor.h - Highly configurable instrumentation pass ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Instrumentor, a highly configurable instrumentation pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_INSTRUMENTOR_H
#define LLVM_TRANSFORMS_IPO_INSTRUMENTOR_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/EnumeratedArray.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Transforms/IPO/InstrumentorUtils.h"
#include "llvm/Transforms/Utils/Instrumentation.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>

namespace llvm {
/// Types and helpers for the configurable Instrumentor instrumentation pass.
namespace instrumentor {

struct InstrumentationConfig;
struct InstrumentationOpportunity;

/// Callback that obtains an instrumentation argument value from IR.
using GetterCallbackTy = std::function<Value *(
    Value &, Type &, InstrumentationConfig &, InstrumentorIRBuilderTy &)>;

/// Callback that consumes a replacement value returned by the runtime.
using SetterCallbackTy = std::function<Value *(
    Value &, Value &, InstrumentationConfig &, InstrumentorIRBuilderTy &)>;

/// Helper to represent an argument to an instrumentation runtime function.
struct IRTArg {
  /// Flags describing the possible properties of an argument.
  enum IRArgFlagTy {
    /// No special properties.
    NONE = 0,
    /// Argument is a string (pointer to null-terminated characters).
    STRING = 1 << 0,
    /// Argument value may be replaced by the runtime return value.
    REPLACABLE = 1 << 1,
    /// Argument uses a custom replacement setter callback.
    REPLACABLE_CUSTOM = 1 << 2,
    /// Argument may be passed indirectly through memory.
    POTENTIALLY_INDIRECT = 1 << 3,
    /// Indirect argument includes an explicit size.
    INDIRECT_HAS_SIZE = 1 << 4,
    /// Argument packs multiple values into one runtime parameter.
    VALUE_PACK = 1 << 5,
    /// Argument is a type identifier integer.
    TYPEID = 1 << 6,
    /// Sentinel past the last flag bit; not a real flag.
    LAST,
  };

  /// Construct an instrumentation runtime argument description.
  ///
  /// \param Ty LLVM type of the argument.
  /// \param Name Argument name used in signatures and configs.
  /// \param Description Human-readable description of the argument.
  /// \param Flags Bitmask of \c IRArgFlagTy properties.
  /// \param GetterCB Callback that produces the argument value.
  /// \param SetterCB Optional callback that applies a replacement value.
  /// \param Enabled Whether the argument is included in the call.
  /// \param NoCache Whether the value must not be cached across PRE/POST.
  IRTArg(Type *Ty, StringRef Name, StringRef Description, unsigned Flags,
         GetterCallbackTy GetterCB, SetterCallbackTy SetterCB = nullptr,
         bool Enabled = true, bool NoCache = false)
      : Enabled(Enabled), Ty(Ty), Name(Name), Description(Description),
        Flags(Flags), GetterCB(std::move(GetterCB)),
        SetterCB(std::move(SetterCB)), NoCache(NoCache) {}

  /// Whether the argument is enabled and should be passed to the function call.
  bool Enabled;

  /// The type of the argument.
  Type *Ty;

  /// A string with the name of the argument.
  StringRef Name;

  /// A string with the description of the argument.
  StringRef Description;

  /// The flags that describe the properties of the argument. Multiple flags may
  /// be specified.
  unsigned Flags;

  /// The callback for getting the value of the argument.
  GetterCallbackTy GetterCB;

  /// The callback for consuming the output value of the argument.
  SetterCallbackTy SetterCB;

  /// Whether the argument value can be cached between the PRE and POST calls.
  bool NoCache;
};

/// Helper to represent an instrumentation runtime function that is related to
/// an instrumentation opportunity.
struct IRTCallDescription {
  /// Construct an instrumentation function description.
  ///
  /// \param IO Instrumentation opportunity this call describes.
  /// \param RetTy Return type of the runtime function, or null for void.
  LLVM_ABI IRTCallDescription(InstrumentationOpportunity &IO,
                              Type *RetTy = nullptr);

  /// Create the type of the instrumentation function.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \param DL Data layout used for type sizing.
  /// \param ForceIndirection Force all eligible arguments to be indirect.
  /// \return The LLVM function type of the instrumentation runtime function.
  LLVM_ABI FunctionType *createLLVMSignature(InstrumentationConfig &IConf,
                                             InstrumentorIRBuilderTy &IIRB,
                                             const DataLayout &DL,
                                             bool ForceIndirection);

  /// Create a call instruction that calls to the instrumentation function and
  /// passes the corresponding arguments.
  ///
  /// \param V Value being instrumented; may be updated by replacements.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder used to emit the call.
  /// \param DL Data layout used for type sizing.
  /// \param ICaches Argument value caches shared across PRE/POST calls.
  /// \return The call instruction invoking the instrumentation runtime function.
  LLVM_ABI CallInst *createLLVMCall(Value *&V, InstrumentationConfig &IConf,
                                    InstrumentorIRBuilderTy &IIRB,
                                    const DataLayout &DL,
                                    InstrumentationCaches &ICaches);

  /// Create a string representation of the function declaration in C.
  ///
  /// Two strings are returned: the function definition with direct arguments
  /// and the function with any indirect argument.
  ///
  /// \param IConf Instrumentation configuration used for naming and types.
  /// \return Direct and indirect C declaration strings for the function.
  LLVM_ABI std::pair<std::string, std::string>
  createCSignature(const InstrumentationConfig &IConf) const;

  /// Create C stub definitions that print the passed arguments.
  ///
  /// Two strings are returned: the function definition with direct arguments
  /// and the function with any indirect argument.
  ///
  /// \return Direct and indirect C stub body strings that print arguments.
  LLVM_ABI std::pair<std::string, std::string> createCBodies() const;

  /// Return whether the argument can be replaced.
  ///
  /// \param IRTA Argument description to inspect.
  /// \return True if the argument can be replaced.
  bool isReplacable(IRTArg &IRTA) const {
    return (IRTA.Flags & (IRTArg::REPLACABLE | IRTArg::REPLACABLE_CUSTOM));
  }

  /// Return whether the argument may be passed indirectly.
  ///
  /// \param IRTA Argument description to inspect.
  /// \return True if the argument may be passed indirectly.
  bool isPotentiallyIndirect(IRTArg &IRTA) const {
    return ((IRTA.Flags & IRTArg::POTENTIALLY_INDIRECT) ||
            ((IRTA.Flags & IRTArg::REPLACABLE) && NumReplaceableArgs > 1));
  }

  /// Whether the function requires indirection in some argument.
  bool RequiresIndirection = false;

  /// Whether any argument may require indirection.
  bool MightRequireIndirection = false;

  /// The number of arguments that can be replaced.
  unsigned NumReplaceableArgs = 0;

  /// The instrumentation opportunity which it is linked to.
  InstrumentationOpportunity &IO;

  /// The return type of the instrumentation function.
  Type *RetTy = nullptr;
};

/// Helper to represent an instrumentation location, which is composed of an
/// instrumentation opportunity type and a position.
struct InstrumentationLocation {
  /// Location kinds pairing an opportunity type with a PRE or POST position.
  ///
  /// The PRE position indicates the instrumentation function call is inserted
  /// before the instrumented event occurs. The POST position indicates the
  /// instrumentation call is inserted after the event occurs. Some opportunity
  /// types may only support one position.
  enum KindTy {
    /// Before module-level instrumentation runs.
    MODULE_PRE,
    /// After module-level instrumentation runs.
    MODULE_POST,
    /// Before a global variable is processed.
    GLOBAL_PRE,
    /// After a global variable is processed.
    GLOBAL_POST,
    /// Before a function is entered for instrumentation.
    FUNCTION_PRE,
    /// After a function has been instrumented.
    FUNCTION_POST,
    /// Before a basic block is instrumented.
    BASIC_BLOCK_PRE,
    /// After a basic block is instrumented.
    BASIC_BLOCK_POST,
    /// Before an instruction executes.
    INSTRUCTION_PRE,
    /// After an instruction executes.
    INSTRUCTION_POST,
    /// Special non-instruction value such as a base pointer.
    SPECIAL_VALUE,
    /// Last valid kind; equal to SPECIAL_VALUE.
    Last = SPECIAL_VALUE,
  };

  /// Construct an instrumentation location with the given kind.
  ///
  /// \param Kind Location kind combining opportunity type and position.
  InstrumentationLocation(KindTy Kind) : Kind(Kind) {}

  /// Return the type and position.
  ///
  /// \return The location kind combining opportunity type and position.
  KindTy getKind() const { return Kind; }

  /// Return the string representation given a location kind.
  ///
  /// This is the string used in the configuration file.
  ///
  /// \param Kind Location kind to convert.
  /// \return Configuration-file string for the location kind.
  static StringRef getKindStr(KindTy Kind) {
    switch (Kind) {
    case MODULE_PRE:
      return "module_pre";
    case MODULE_POST:
      return "module_post";
    case GLOBAL_PRE:
      return "global_pre";
    case GLOBAL_POST:
      return "global_post";
    case FUNCTION_PRE:
      return "function_pre";
    case FUNCTION_POST:
      return "function_post";
    case BASIC_BLOCK_PRE:
      return "basic_block_pre";
    case BASIC_BLOCK_POST:
      return "basic_block_post";
    case INSTRUCTION_PRE:
      return "instruction_pre";
    case INSTRUCTION_POST:
      return "instruction_post";
    case SPECIAL_VALUE:
      return "special_value";
    }
    llvm_unreachable("Invalid kind!");
  }

  /// Return the location kind described by a string.
  ///
  /// \param S Configuration string naming a location kind.
  /// \return The location kind named by \p S, or Last if unrecognized.
  static KindTy getKindFromStr(StringRef S) {
    return StringSwitch<KindTy>(S)
        .Case("module_pre", MODULE_PRE)
        .Case("module_post", MODULE_POST)
        .Case("global_pre", GLOBAL_PRE)
        .Case("global_post", GLOBAL_POST)
        .Case("function_pre", FUNCTION_PRE)
        .Case("function_post", FUNCTION_POST)
        .Case("basic_block_pre", BASIC_BLOCK_PRE)
        .Case("basic_block_post", BASIC_BLOCK_POST)
        .Case("instruction_pre", INSTRUCTION_PRE)
        .Case("instruction_post", INSTRUCTION_POST)
        .Case("special_value", SPECIAL_VALUE)
        .Default(Last);
  }

  /// Return whether a location kind is positioned before the event occurs.
  ///
  /// \param Kind Location kind to test.
  /// \return True if \p Kind is a PRE location.
  static bool isPRE(KindTy Kind) {
    switch (Kind) {
    case MODULE_PRE:
    case GLOBAL_PRE:
    case FUNCTION_PRE:
    case BASIC_BLOCK_PRE:
    case INSTRUCTION_PRE:
      return true;
    case MODULE_POST:
    case GLOBAL_POST:
    case FUNCTION_POST:
    case BASIC_BLOCK_POST:
    case INSTRUCTION_POST:
    case SPECIAL_VALUE:
      return false;
    }
    llvm_unreachable("Invalid kind!");
  }

  /// Return whether the instrumentation location is before the event occurs.
  ///
  /// \return True if this location is positioned before the event.
  bool isPRE() const { return isPRE(Kind); }

private:
  /// The kind (type and position) of the instrumentation location.
  const KindTy Kind;
};

/// An option for the base configuration.
struct BaseConfigurationOption {
  /// The possible types of options.
  enum KindTy {
    /// Option stores a string value.
    STRING,
    /// Option stores a boolean value.
    BOOLEAN,
  };

  /// Create a boolean option with the given name, description, and default.
  ///
  /// \param IC Instrumentation configuration that owns the option.
  /// \param Name Option name used in configuration files.
  /// \param Description Human-readable description of the option.
  /// \param DefaultValue Initial boolean value.
  /// \return Newly created boolean configuration option.
  LLVM_ABI static std::unique_ptr<BaseConfigurationOption>
  createBoolOption(InstrumentationConfig &IC, StringRef Name,
                   StringRef Description, bool DefaultValue);

  /// Create a string option with the given name, description, and default.
  ///
  /// \param IC Instrumentation configuration that owns the option.
  /// \param Name Option name used in configuration files.
  /// \param Description Human-readable description of the option.
  /// \param DefaultValue Initial string value.
  /// \return Newly created string configuration option.
  LLVM_ABI static std::unique_ptr<BaseConfigurationOption>
  createStringOption(InstrumentationConfig &IC, StringRef Name,
                     StringRef Description, StringRef DefaultValue);

  /// Helper union that holds any possible option type.
  union ValueTy {
    /// Boolean payload when \c Kind is \c BOOLEAN.
    bool Bool;
    /// String payload when \c Kind is \c STRING.
    StringRef String;
  };

  /// Set the boolean value. Only valid if it is a boolean option.
  ///
  /// \param B New boolean value.
  void setBool(bool B) {
    assert(Kind == BOOLEAN && "Not a boolean!");
    Value.Bool = B;
  }
  /// Get the boolean value. Only valid if it is a boolean option.
  ///
  /// \return The stored boolean option value.
  bool getBool() const {
    assert(Kind == BOOLEAN && "Not a boolean!");
    return Value.Bool;
  }

  /// Set the string value. Only valid if it is a string option.
  ///
  /// \param S New string value.
  void setString(StringRef S) {
    assert(Kind == STRING && "Not a string!");
    Value.String = S;
  }
  /// Get the string value. Only valid if it is a string option.
  ///
  /// \return The stored string option value.
  StringRef getString() const {
    assert(Kind == STRING && "Not a string!");
    return Value.String;
  }

  /// The name of the option.
  StringRef Name;
  /// Human-readable description of the option.
  StringRef Description;
  /// Discriminator selecting the active member of \c Value.
  KindTy Kind;
  /// Stored option payload.
  ValueTy Value = {0};

  /// Construct a base configuration option.
  ///
  /// \param Name Option name used in configuration files.
  /// \param Desc Human-readable description of the option.
  /// \param Kind Payload kind (string or boolean).
  BaseConfigurationOption(StringRef Name, StringRef Desc, KindTy Kind)
      : Name(Name), Description(Desc), Kind(Kind) {}
};

/// Configuration for the instrumentor pass.
///
/// It holds the information for each instrumented opportunity, including the
/// base configuration options. Another class may inherit from this one to
/// modify the default behavior.
struct LLVM_ABI InstrumentationConfig {
  /// Destroy the instrumentation configuration.
  virtual ~InstrumentationConfig() {}

  /// Construct an instrumentation configuration with the base options.
  InstrumentationConfig() : SS(StringAllocator) {}

  /// Initialize the config to a clean base state without losing cached values.
  ///
  /// Cached values that can be reused across configurations are preserved.
  ///
  /// \param IIRB Instrumentor IR builder used while populating opportunities.
  void init(InstrumentorIRBuilderTy &IIRB) {
    // Clear previous configurations but not the caches.
    BaseConfigurationOptions.clear();
    for (auto &Map : IChoices)
      Map.clear();

    RuntimePrefix = BaseConfigurationOption::createStringOption(
        *this, "runtime_prefix", "The runtime API prefix.", "__instrumentor_");
    RuntimeStubsFile = BaseConfigurationOption::createStringOption(
        *this, "runtime_stubs_file",
        "The file into which runtime stubs should be written.", "");
    TargetRegex = BaseConfigurationOption::createStringOption(
        *this, "target_regex",
        "Regular expression to be matched against the module target. "
        "Only targets that match this regex will be instrumented.",
        "");
    FunctionRegex = BaseConfigurationOption::createStringOption(
        *this, "function_regex",
        "Regular expression to be matched against a function name. "
        "Only functions that match this regex will be instrumented.",
        "");
    DemangleFunctionNames = BaseConfigurationOption::createBoolOption(
        *this, "demangle_function_names",
        "Demangle functions names passed to the runtime.", true);
    HostEnabled = BaseConfigurationOption::createBoolOption(
        *this, "host_enabled", "Instrument non-GPU targets", true);
    GPUEnabled = BaseConfigurationOption::createBoolOption(
        *this, "gpu_enabled", "Instrument GPU targets", true);
    RuntimeBitcode = BaseConfigurationOption::createStringOption(
        *this, "runtime_bitcode", "Link runtime bitcode", "");
    InlineRuntimeEagerly = BaseConfigurationOption::createBoolOption(
        *this, "inline_runtime", "Inline runtime function calls eagerly", true);
    populate(IIRB);
  }

  /// Populate the instrumentation opportunities.
  ///
  /// \param IIRB Instrumentor IR builder used to create opportunity state.
  virtual void populate(InstrumentorIRBuilderTy &IIRB);

  /// Get the runtime prefix for the instrumentation runtime functions.
  ///
  /// \return The runtime API name prefix.
  StringRef getRTName() const { return RuntimePrefix->getString(); }

  /// Get the instrumentation function name.
  ///
  /// \param Prefix Name prefix inserted after the runtime prefix.
  /// \param Name Primary opportunity or symbol name.
  /// \param Suffix1 Optional first suffix.
  /// \param Suffix2 Optional second suffix.
  /// \return Fully qualified instrumentation function name.
  std::string getRTName(StringRef Prefix, StringRef Name,
                        StringRef Suffix1 = "", StringRef Suffix2 = "") const {
    return (getRTName() + Prefix + Name + Suffix1 + Suffix2).str();
  }

  /// Add a base configuration option into the list of base options.
  ///
  /// \param BCO Option to register; ownership remains with the caller.
  void addBaseChoice(BaseConfigurationOption *BCO) {
    BaseConfigurationOptions.push_back(BCO);
  }

  /// Register an instrumentation opportunity.
  ///
  /// \param IO Opportunity to register.
  /// \param Ctx LLVM context used while finishing registration.
  void addChoice(InstrumentationOpportunity &IO, LLVMContext &Ctx);

  /// Allocate an object of type \p Ty using a bump allocator.
  ///
  /// Constructs it with the given arguments. The object may not be freed
  /// manually.
  ///
  /// \param Args Constructor arguments forwarded to \p Ty.
  /// \return Newly allocated and constructed object of type \p Ty.
  template <typename Ty, typename... ArgsTy>
  static Ty *allocate(ArgsTy &&...Args) {
    static SpecificBumpPtrAllocator<Ty> Allocator;
    Ty *Obj = Allocator.Allocate();
    new (Obj) Ty(std::forward<ArgsTy>(Args)...);
    return Obj;
  }

  /// Map to remember underlying objects for pointers.
  DenseMap<Value *, Value *> UnderlyingObjsMap;

  /// Map to remember base pointer info for values in a specific function.
  DenseMap<std::pair<Value *, Function *>, Value *> BasePointerInfoMap;

  /// Return the base pointer info for a value.
  ///
  /// \param V Value whose base pointer info is requested.
  /// \param IIRB Instrumentor IR builder used to materialize info if needed.
  /// \return Base-pointer info value for \p V.
  Value *getBasePointerInfo(Value &V, InstrumentorIRBuilderTy &IIRB);

  /// Mapping to remember global strings passed to the runtime.
  DenseMap<StringRef, Constant *> GlobalStringsMap;

  /// Mapping from constants to globals with the constant as initializer.
  DenseMap<Constant *, GlobalVariable *> ConstantGlobalsCache;

  /// Return a global string constant for \p S, creating it if needed.
  ///
  /// \param S String contents to materialize as a global.
  /// \param IIRB Instrumentor IR builder used to create the global.
  /// \return Global string constant for \p S.
  Constant *getGlobalString(StringRef S, InstrumentorIRBuilderTy &IIRB) {
    Constant *&V = GlobalStringsMap[SS.save(S)];
    if (!V) {
      auto &M = *IIRB.IRB.GetInsertBlock()->getModule();
      V = IIRB.IRB.CreateGlobalString(
          S, getRTName() + ".str",
          M.getDataLayout().getDefaultGlobalsAddressSpace(), &M);
      if (V->getType() != IIRB.IRB.getPtrTy())
        V = ConstantExpr::getAddrSpaceCast(V, IIRB.IRB.getPtrTy());
    }
    return V;
  }
  /// The list of enabled base configuration options.
  SmallVector<BaseConfigurationOption *> BaseConfigurationOptions;

  /// The base configuration options.
  std::unique_ptr<BaseConfigurationOption> RuntimePrefix;
  /// File path where runtime stub declarations should be written.
  std::unique_ptr<BaseConfigurationOption> RuntimeStubsFile;
  /// Whether function names passed to the runtime are demangled.
  std::unique_ptr<BaseConfigurationOption> DemangleFunctionNames;
  /// Regex matched against the module target triple.
  std::unique_ptr<BaseConfigurationOption> TargetRegex;
  /// Regex matched against function names to select instrumentation.
  std::unique_ptr<BaseConfigurationOption> FunctionRegex;
  /// Whether non-GPU (host) targets are instrumented.
  std::unique_ptr<BaseConfigurationOption> HostEnabled;
  /// Whether GPU targets are instrumented.
  std::unique_ptr<BaseConfigurationOption> GPUEnabled;
  /// Path to runtime bitcode to link, if any.
  std::unique_ptr<BaseConfigurationOption> RuntimeBitcode;
  /// Whether runtime calls should be inlined eagerly.
  std::unique_ptr<BaseConfigurationOption> InlineRuntimeEagerly;

  /// Registered instrumentation opportunities by location and name.
  ///
  /// The map is indexed by the instrumentation location kind and then by the
  /// opportunity name. An instrumentation location may have more than one
  /// instrumentation opportunity registered.
  EnumeratedArray<MapVector<StringRef, InstrumentationOpportunity *>,
                  InstrumentationLocation::KindTy>
      IChoices;

  /// Allocator used for configuration and opportunity strings.
  BumpPtrAllocator StringAllocator;
  /// String saver backed by \c StringAllocator.
  StringSaver SS;
};

/// Base class for instrumentation opportunities. All opportunities should
/// inherit from this class and implement the virtual class members.
struct InstrumentationOpportunity {
  /// Destroy the instrumentation opportunity.
  virtual ~InstrumentationOpportunity() {}

  /// Construct an opportunity with the given instrumentation location.
  ///
  /// \param IP Instrumentation location for this opportunity.
  InstrumentationOpportunity(const InstrumentationLocation IP) : IP(IP) {}

  /// The instrumentation location of the opportunity.
  InstrumentationLocation IP;

  /// The list of possible arguments for the instrumentation runtime function.
  ///
  /// The order within the array determines the order of arguments. Arguments may be disabled and will not be passed to the function call.
  SmallVector<IRTArg> IRTArgs;

  /// Flag names and their integer bitmask values.
  StringMap<int32_t> FlagNames;

  /// Whether the opportunity is enabled.
  bool Enabled = true;

  /// A filter expression to be matched against runtime property values.
  ///
  /// If the filter is non-empty, only instrumentations matching the filter will be executed. The filter syntax supports: - Integer comparisons: ==, !=, <, >, <=, >= - String comparisons: ==, != (with quoted strings) - String prefix check: startswith("prefix") - Logical operators: &&, || Examples: "sync_scope_id==3 && atomicity_ordering>0" "name==\"foo\" || name.startswith(\"test_\")" If a property value is dynamic (not a constant), the filter is assumed to pass (true).
  StringRef Filter;

  /// Force-cast \p V to \p Ty using the instrumentor IR builder.
  ///
  /// \param V Value to cast.
  /// \param Ty Destination type.
  /// \param IIRB Instrumentor IR builder used to emit casts.
  /// \return \p V cast to \p Ty.
  LLVM_ABI static Value *forceCast(Value &V, Type &Ty,
                                   InstrumentorIRBuilderTy &IIRB);
  /// Pass \p V to the runtime after casting it to \p Ty.
  ///
  /// \param V Value to pass.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return \p V cast to \p Ty for passing to the runtime.
  static Value *getValue(Value &V, Type &Ty, InstrumentationConfig &IConf,
                         InstrumentorIRBuilderTy &IIRB) {
    return forceCast(V, Ty, IIRB);
  }
  /// Replace uses of \p V with \p NewV according to the opportunity rules.
  ///
  /// \param V Original value being replaced.
  /// \param NewV Replacement value from the runtime.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Replacement value after applying opportunity rules.
  LLVM_ABI static Value *replaceValue(Value &V, Value &NewV,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);

  /// Instrument a value by emitting the corresponding runtime call.
  ///
  /// \param V Value being instrumented; may be updated by replacements.
  /// \param Changed Set to true when instrumentation modifies the IR.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder used to emit the call.
  /// \param ICaches Argument value caches shared across PRE/POST calls.
  /// \return The emitted instrumentation call, or null if skipped.
  virtual Value *instrument(Value *&V, bool &Changed,
                            InstrumentationConfig &IConf,
                            InstrumentorIRBuilderTy &IIRB,
                            InstrumentationCaches &ICaches) {
    if (CB && !CB(*V))
      return nullptr;

    // Check if the filter matches before instrumenting
    if (!evaluateFilter(*V, Changed, *this, IConf, IIRB))
      return nullptr;

    Changed = true;
    const DataLayout &DL = IIRB.IRB.GetInsertBlock()->getDataLayout();
    IRTCallDescription IRTCallDesc(*this, getRetTy(V->getContext()));
    auto *CI = IRTCallDesc.createLLVMCall(V, IConf, IIRB, DL, ICaches);
    return CI;
  }

  /// Get the return type for the instrumentation runtime function.
  ///
  /// \param Ctx LLVM context used to construct the return type.
  /// \return Return type of the instrumentation runtime function, or null for void.
  virtual Type *getRetTy(LLVMContext &Ctx) const { return nullptr; }

  /// Get the name of the instrumentation opportunity.
  ///
  /// \return Name of the instrumentation opportunity.
  virtual StringRef getName() const = 0;

  /// Return the opcodes handled by this opportunity.
  ///
  /// For non-instruction opportunities, returns an empty array. For
  /// instruction opportunities, returns an array of all opcodes this IO
  /// handles.
  ///
  /// \return Opcodes handled by this opportunity, or empty otherwise.
  virtual ArrayRef<unsigned> getAllOpcodes() const { return {}; }

  /// Get the location kind of the instrumentation opportunity.
  ///
  /// \return Location kind of this instrumentation opportunity.
  InstrumentationLocation::KindTy getLocationKind() const {
    return IP.getKind();
  }

  /// Callback type that may skip instrumentation for a value.
  using CallbackTy = std::function<bool(Value &)>;
  /// Optional callback; return false to skip instrumenting a value.
  CallbackTy CB = nullptr;

  /// Add arguments available in all instrumentation opportunities.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param Ctx LLVM context used to build argument types.
  /// \param PassId Whether to pass the opportunity identifier argument.
  void addCommonArgs(InstrumentationConfig &IConf, LLVMContext &Ctx,
                     bool PassId) {
    const auto CB = IP.isPRE() ? getIdPre : getIdPost;
    if (PassId) {
      IRTArgs.push_back(
          IRTArg(IntegerType::getInt32Ty(Ctx), "id",
                 "A unique ID associated with the given instrumentor call",
                 IRTArg::NONE, CB, nullptr, true, true));
    }
  }

  /// Get the opportunity identifier for a PRE instrumentation call.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected identifier type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Opportunity identifier value for a PRE call.
  LLVM_ABI static Value *getIdPre(Value &V, Type &Ty,
                                  InstrumentationConfig &IConf,
                                  InstrumentorIRBuilderTy &IIRB);
  /// Get the opportunity identifier for a POST instrumentation call.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected identifier type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Opportunity identifier value for a POST call.
  LLVM_ABI static Value *getIdPost(Value &V, Type &Ty,
                                   InstrumentationConfig &IConf,
                                   InstrumentorIRBuilderTy &IIRB);

  /// Compute the opportunity identifier for the current instrumentation epoch.
  ///
  /// The identifiers are assigned consecutively as the epoch advances. Epochs
  /// may have no identifier assigned (e.g., because no id was requested). This
  /// function always returns the same identifier when called multiple times
  /// with the same epoch.
  ///
  /// \param CurrentEpoch Instrumentation epoch whose identifier is requested.
  /// \return Stable identifier assigned to \p CurrentEpoch.
  static int32_t getIdFromEpoch(uint32_t CurrentEpoch) {
    static DenseMap<uint32_t, int32_t> EpochIdMap;
    static int32_t GlobalId = 0;
    int32_t &EpochId = EpochIdMap[CurrentEpoch];
    if (EpochId == 0)
      EpochId = ++GlobalId;
    return EpochId;
  }
};

/// The base class that implements basic logic for any instruction
/// instrumentation opportunity that inherits from InstructionIO.
struct BaseInstructionIO : public InstrumentationOpportunity {
  /// Destroy the base instruction opportunity.
  virtual ~BaseInstructionIO() {}

  /// Construct a base instruction opportunity at the given location kind.
  ///
  /// \param Kind PRE or POST instruction location kind.
  BaseInstructionIO(InstrumentationLocation::KindTy Kind)
      : InstrumentationOpportunity(InstrumentationLocation(Kind)) {}

  /// Return the opcode of the instrumented instruction as an integer value.
  ///
  /// \param V Instruction being instrumented.
  /// \param Ty Expected result type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Opcode of the instrumented instruction as an integer value.
  LLVM_ABI static Value *getOpcode(Value &V, Type &Ty,
                                   InstrumentationConfig &IConf,
                                   InstrumentorIRBuilderTy &IIRB);
  /// Return the type size of the instrumented instruction result.
  ///
  /// \param V Instruction being instrumented.
  /// \param Ty Expected result type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Type size of the instrumented instruction result.
  LLVM_ABI static Value *getTypeSize(Value &V, Type &Ty,
                                     InstrumentationConfig &IConf,
                                     InstrumentorIRBuilderTy &IIRB);
  /// Return the left-hand operand of a binary instruction.
  ///
  /// \param V Instruction being instrumented.
  /// \param Ty Expected result type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Left-hand operand of the binary instruction.
  LLVM_ABI static Value *getLeftOperand(Value &V, Type &Ty,
                                        InstrumentationConfig &IConf,
                                        InstrumentorIRBuilderTy &IIRB);
  /// Return the right-hand operand of a binary instruction.
  ///
  /// \param V Instruction being instrumented.
  /// \param Ty Expected result type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Right-hand operand of the binary instruction.
  LLVM_ABI static Value *getRightOperand(Value &V, Type &Ty,
                                         InstrumentationConfig &IConf,
                                         InstrumentorIRBuilderTy &IIRB);
  /// Return the primary type id of the instrumented instruction.
  ///
  /// \param V Instruction being instrumented.
  /// \param Ty Expected result type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Primary type id of the instrumented instruction.
  LLVM_ABI static Value *getTypeId(Value &V, Type &Ty,
                                   InstrumentationConfig &IConf,
                                   InstrumentorIRBuilderTy &IIRB);
  /// Return the subtype id of the instrumented instruction.
  ///
  /// \param V Instruction being instrumented.
  /// \param Ty Expected result type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Subtype id of the instrumented instruction.
  LLVM_ABI static Value *getSubTypeId(Value &V, Type &Ty,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);
};

/// Common base for instruction instrumentation opportunities.
///
/// Each instruction opportunity should inherit from this class and implement
/// the virtual class members. If multiple opcodes are provided, all of them
/// are instrumented using the same logic, and a name must be explicitly
/// provided by overriding getName().
template <unsigned... Opcodes> struct InstructionIO : public BaseInstructionIO {
  /// Destroy the instruction opportunity.
  virtual ~InstructionIO() {}

  /// Construct an instruction opportunity at the given location kind.
  ///
  /// \param Kind PRE or POST instruction location kind.
  InstructionIO(InstrumentationLocation::KindTy Kind)
      : BaseInstructionIO(Kind) {
    static_assert(sizeof...(Opcodes) >= 1,
                  "InstructionIO must have at least one opcode");
  }

  /// Compile-time array of opcodes handled by this opportunity.
  static constexpr std::array<unsigned, sizeof...(Opcodes)> OpcodesArray = {
      Opcodes...};

  /// Get all opcodes for this instrumentation opportunity (override).
  ///
  /// \return Compile-time opcode array for this opportunity.
  ArrayRef<unsigned> getAllOpcodes() const override { return OpcodesArray; }

  /// Get the number of opcodes.
  ///
  /// \return Number of opcodes handled by this opportunity.
  static constexpr size_t getNumOpcodes() { return OpcodesArray.size(); }

  /// Get the instruction opportunity name.
  ///
  /// For single-opcode IOs, this defaults to the opcode name. For multi-opcode
  /// IOs, getName() MUST be overridden to provide an explicit name identifying
  /// the whole group of opcodes.
  ///
  /// \return Opcode name for single-opcode opportunities.
  virtual StringRef getName() const override {
    // This method should not be called for multi-opcode IOs.
    // Multi-opcode IOs must override getName().
    assert(sizeof...(Opcodes) == 1 &&
           "Multi-opcode InstructionIO must override getName() to provide an "
           "explicit name instead of using the first opcode");
    // Get the first opcode from the opcodes array.
    return Instruction::getOpcodeName(OpcodesArray[0]);
  }
};

/// The instrumentation opportunity for functions.
struct FunctionIO final : public InstrumentationOpportunity {
  /// Construct a function instrumentation opportunity.
  ///
  /// \param Kind PRE or POST function location kind.
  FunctionIO(InstrumentationLocation::KindTy Kind)
      : InstrumentationOpportunity(InstrumentationLocation(Kind)) {}

  /// Selector of arguments for function opportunities.
  enum ConfigKind {
    /// Pass the function address to the runtime.
    PassAddress = 0,
    /// Pass the function name to the runtime.
    PassName,
    /// Pass the number of arguments to the runtime.
    PassNumArguments,
    /// Pass the function arguments to the runtime.
    PassArguments,
    /// Allow the runtime to replace function arguments.
    ReplaceArguments,
    /// Pass whether the function is main.
    PassIsMain,
    /// Pass the opportunity identifier.
    PassId,
    /// Number of configuration options.
    NumConfig,
  };

  /// Per-opportunity configuration for function instrumentation.
  struct ConfigTy final : public BaseConfigTy<ConfigKind> {
    /// Optional filter selecting which arguments are passed.
    std::function<bool(Argument &)> ArgFilter;

    /// Construct function configuration with options enabled by default.
    ///
    /// \param Enable Initial enablement for all config bits.
    ConfigTy(bool Enable = true) : BaseConfigTy(Enable) {}
  } Config; ///< Active configuration for this function opportunity.

  /// Return the opportunity name ("function").
  ///
  /// \return Opportunity name ("function").
  StringRef getName() const override { return "function"; }

  /// Initialize the function opportunity and its runtime arguments.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \param UserConfig Optional per-opportunity configuration; defaults when null.
  LLVM_ABI void init(InstrumentationConfig &IConf,
                     InstrumentorIRBuilderTy &IIRB,
                     ConfigTy *UserConfig = nullptr);

  /// Return the address of the instrumented function.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Address of the instrumented function.
  LLVM_ABI static Value *getFunctionAddress(Value &V, Type &Ty,
                                            InstrumentationConfig &IConf,
                                            InstrumentorIRBuilderTy &IIRB);
  /// Return the name of the instrumented function.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Name of the instrumented function.
  LLVM_ABI static Value *getFunctionName(Value &V, Type &Ty,
                                         InstrumentationConfig &IConf,
                                         InstrumentorIRBuilderTy &IIRB);
  /// Return the number of arguments of the instrumented function.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Number of arguments of the instrumented function.
  LLVM_ABI Value *getNumArguments(Value &V, Type &Ty,
                                  InstrumentationConfig &IConf,
                                  InstrumentorIRBuilderTy &IIRB);
  /// Return a packed representation of the function arguments.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Packed representation of the function arguments.
  LLVM_ABI Value *getArguments(Value &V, Type &Ty, InstrumentationConfig &IConf,
                               InstrumentorIRBuilderTy &IIRB);
  /// Apply runtime-provided replacements to function arguments.
  ///
  /// \param V Value being instrumented.
  /// \param NewV Replacement value from the runtime.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Value after applying argument replacements.
  LLVM_ABI Value *setArguments(Value &V, Value &NewV,
                               InstrumentationConfig &IConf,
                               InstrumentorIRBuilderTy &IIRB);
  /// Return whether the instrumented function is main.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Whether the instrumented function is main.
  LLVM_ABI static Value *isMainFunction(Value &V, Type &Ty,
                                        InstrumentationConfig &IConf,
                                        InstrumentorIRBuilderTy &IIRB);

  /// Register PRE and POST function opportunities on the configuration.
  ///
  /// \param IConf Instrumentation configuration to populate.
  /// \param IIRB Instrumentor IR builder.
  static void populate(InstrumentationConfig &IConf,
                       InstrumentorIRBuilderTy &IIRB) {
    auto *PreIO =
        IConf.allocate<FunctionIO>(InstrumentationLocation::FUNCTION_PRE);
    PreIO->init(IConf, IIRB);
    auto *PostIO =
        IConf.allocate<FunctionIO>(InstrumentationLocation::FUNCTION_POST);
    PostIO->init(IConf, IIRB);
  }
};

/// The instrumentation opportunity for alloca instructions.
struct AllocaIO final : public InstructionIO<Instruction::Alloca> {
  /// Construct an alloca instrumentation opportunity.
  ///
  /// \param Kind PRE or POST instruction location kind.
  AllocaIO(InstrumentationLocation::KindTy Kind) : InstructionIO(Kind) {}

  /// Selector of arguments for alloca opportunities.
  enum ConfigKind {
    /// Pass the alloca address to the runtime.
    PassAddress = 0,
    /// Allow the runtime to replace the alloca address.
    ReplaceAddress,
    /// Pass the allocation size to the runtime.
    PassSize,
    /// Allow the runtime to replace the allocation size.
    ReplaceSize,
    /// Pass the alloca alignment to the runtime.
    PassAlignment,
    /// Pass the opportunity identifier.
    PassId,
    /// Number of configuration options.
    NumConfig,
  };

  /// Configuration bitset type for alloca opportunities.
  using ConfigTy = BaseConfigTy<ConfigKind>;
  /// Per-opportunity configuration for alloca instrumentation.
  ConfigTy Config;

  /// Initialize the alloca opportunity and its runtime arguments.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \param UserConfig Optional per-opportunity configuration; defaults when null.
  LLVM_ABI void init(InstrumentationConfig &IConf,
                     InstrumentorIRBuilderTy &IIRB,
                     ConfigTy *UserConfig = nullptr);

  /// Return the allocation size of the alloca.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Allocation size of the alloca.
  LLVM_ABI static Value *getSize(Value &V, Type &Ty,
                                 InstrumentationConfig &IConf,
                                 InstrumentorIRBuilderTy &IIRB);
  /// Replace the allocation size with a runtime-provided value.
  ///
  /// \param V Value being instrumented.
  /// \param NewV Replacement value from the runtime.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Value after replacing the allocation size.
  LLVM_ABI static Value *setSize(Value &V, Value &NewV,
                                 InstrumentationConfig &IConf,
                                 InstrumentorIRBuilderTy &IIRB);
  /// Return the alignment of the alloca.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Alignment of the alloca.
  LLVM_ABI static Value *getAlignment(Value &V, Type &Ty,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);

  /// Register PRE and POST alloca opportunities on the configuration.
  ///
  /// \param IConf Instrumentation configuration to populate.
  /// \param IIRB Instrumentor IR builder.
  static void populate(InstrumentationConfig &IConf,
                       InstrumentorIRBuilderTy &IIRB) {
    auto *PreIO =
        IConf.allocate<AllocaIO>(InstrumentationLocation::INSTRUCTION_PRE);
    PreIO->init(IConf, IIRB);
    auto *PostIO =
        IConf.allocate<AllocaIO>(InstrumentationLocation::INSTRUCTION_POST);
    PostIO->init(IConf, IIRB);
  }
};

/// Instrumentation opportunity for unreachable instructions.
struct UnreachableIO final : public InstructionIO<Instruction::Unreachable> {
  /// Construct an unreachable opportunity at the PRE instruction location.
  UnreachableIO()
      : InstructionIO<Instruction::Unreachable>(
            InstrumentationLocation::INSTRUCTION_PRE) {}

  /// Selector of arguments for unreachable opportunities.
  enum ConfigKind {
    /// Pass the opportunity identifier.
    PassId,
    /// Number of configuration options.
    NumConfig,
  };

  /// Configuration bitset type for unreachable opportunities.
  using ConfigTy = BaseConfigTy<ConfigKind>;
  /// Per-opportunity configuration for unreachable instrumentation.
  ConfigTy Config;

  /// Initialize the unreachable opportunity and its runtime arguments.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \param UserConfig Optional per-opportunity configuration; defaults when null.
  LLVM_ABI void init(InstrumentationConfig &IConf,
                     InstrumentorIRBuilderTy &IIRB,
                     ConfigTy *UserConfig = nullptr);

  /// Register the unreachable opportunity on the configuration.
  ///
  /// \param IConf Instrumentation configuration to populate.
  /// \param IIRB Instrumentor IR builder.
  static void populate(InstrumentationConfig &IConf,
                       InstrumentorIRBuilderTy &IIRB) {
    auto *PreIO = IConf.allocate<UnreachableIO>();
    PreIO->init(IConf, IIRB);
  }
};

/// Instrumentation opportunity for base pointers of memory operations.
struct BasePointerIO final : public InstrumentationOpportunity {
  /// Construct a base-pointer opportunity at the special-value location.
  BasePointerIO()
      : InstrumentationOpportunity(
            InstrumentationLocation(InstrumentationLocation::SPECIAL_VALUE)) {}
  /// Destroy the base-pointer opportunity.
  virtual ~BasePointerIO() {};

  /// Selector of arguments for base-pointer opportunities.
  enum ConfigKind {
    /// Pass the base pointer to the runtime.
    PassPointer = 0,
    /// Pass the kind of the base pointer to the runtime.
    PassPointerKind,
    /// Pass the opportunity identifier.
    PassId,
    /// Number of configuration options.
    NumConfig,
  };

  /// Configuration bitset type for base-pointer opportunities.
  using ConfigTy = BaseConfigTy<ConfigKind>;
  /// Per-opportunity configuration for base-pointer instrumentation.
  ConfigTy Config;

  /// Return the opportunity name ("base_pointer_info").
  ///
  /// \return Opportunity name ("base_pointer_info").
  StringRef getName() const override { return "base_pointer_info"; }

  /// Initialize the base-pointer opportunity and its runtime arguments.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \param UserConfig Optional per-opportunity configuration; defaults when null.
  LLVM_ABI void init(InstrumentationConfig &IConf,
                     InstrumentorIRBuilderTy &IIRB,
                     ConfigTy *UserConfig = nullptr);

  /// Return the kind of the base pointer.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Kind of the base pointer.
  LLVM_ABI static Value *getPointerKind(Value &V, Type &Ty,
                                        InstrumentationConfig &IConf,
                                        InstrumentorIRBuilderTy &IIRB);

  /// Return \p NewV unchanged so other IOs can consume a replacement slot.
  ///
  /// No replacement of \p V is performed.
  ///
  /// \param V Value being instrumented.
  /// \param NewV Replacement value from the runtime.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return \p NewV unchanged for use as a replacement slot.
  static Value *setValueNoop(Value &V, Value &NewV,
                             InstrumentationConfig &IConf,
                             InstrumentorIRBuilderTy &IIRB) {
    return &NewV;
  }

  /// Register the base-pointer opportunity on the configuration.
  ///
  /// \param IConf Instrumentation configuration to populate.
  /// \param IIRB Instrumentor IR builder.
  static void populate(InstrumentationConfig &IConf,
                       InstrumentorIRBuilderTy &IIRB) {
    auto *BPIO = IConf.allocate<BasePointerIO>();
    BPIO->init(IConf, IIRB);
  }
};

/// Instrumentation opportunity for modules.
struct ModuleIO final : public InstrumentationOpportunity {
  /// Construct a module instrumentation opportunity.
  ///
  /// \param Kind PRE or POST module location kind.
  ModuleIO(InstrumentationLocation::KindTy Kind)
      : InstrumentationOpportunity(InstrumentationLocation(Kind)) {}

  /// Selector of arguments for module opportunities.
  enum ConfigKind {
    /// Pass the opportunity identifier.
    PassId,
    /// Pass the module name to the runtime.
    PassName,
    /// Pass the target triple to the runtime.
    PassTargetTriple,
    /// Number of configuration options.
    NumConfig,
  };

  /// Configuration bitset type for module opportunities.
  using ConfigTy = BaseConfigTy<ConfigKind>;
  /// Per-opportunity configuration for module instrumentation.
  ConfigTy Config;

  /// Return the opportunity name ("module").
  ///
  /// \return Opportunity name ("module").
  StringRef getName() const override { return "module"; }

  /// Initialize the module opportunity and its runtime arguments.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \param UserConfig Optional per-opportunity configuration; defaults when null.
  LLVM_ABI void init(InstrumentationConfig &IConf,
                     InstrumentorIRBuilderTy &IIRB,
                     ConfigTy *UserConfig = nullptr);

  /// Return the module name.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Module name.
  LLVM_ABI static Value *getModuleName(Value &V, Type &Ty,
                                       InstrumentationConfig &IConf,
                                       InstrumentorIRBuilderTy &IIRB);
  /// Return the module target triple.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Module target triple.
  LLVM_ABI static Value *getTargetTriple(Value &V, Type &Ty,
                                         InstrumentationConfig &IConf,
                                         InstrumentorIRBuilderTy &IIRB);

  /// Register PRE and POST module opportunities on the configuration.
  ///
  /// \param IConf Instrumentation configuration to populate.
  /// \param IIRB Instrumentor IR builder.
  static void populate(InstrumentationConfig &IConf,
                       InstrumentorIRBuilderTy &IIRB) {
    auto *PreIO = IConf.allocate<ModuleIO>(InstrumentationLocation::MODULE_PRE);
    PreIO->init(IConf, IIRB);
    auto *PostIO =
        IConf.allocate<ModuleIO>(InstrumentationLocation::MODULE_POST);
    PostIO->init(IConf, IIRB);
  }
};

/// Instrumentation opportunity for global variables.
struct GlobalVarIO final : public InstrumentationOpportunity {
  /// Construct a global-variable instrumentation opportunity.
  ///
  /// \param Kind PRE or POST global location kind.
  GlobalVarIO(InstrumentationLocation::KindTy Kind)
      : InstrumentationOpportunity(InstrumentationLocation(Kind)) {}

  /// Selector of arguments for global-variable opportunities.
  enum ConfigKind {
    /// Pass the global address to the runtime.
    PassAddress = 0,
    /// Allow the runtime to replace the global address.
    ReplaceAddress,
    /// Pass the address space to the runtime.
    PassAS,
    /// Pass the declared size to the runtime.
    PassDeclaredSize,
    /// Pass the alignment to the runtime.
    PassAlignment,
    /// Pass the symbol name to the runtime.
    PassName,
    /// Pass the initial value to the runtime.
    PassInitialValue,
    /// Pass whether the global is constant.
    PassIsConstant,
    /// Pass whether the global is a definition.
    PassIsDefinition,
    /// Pass the opportunity identifier.
    PassId,
    /// Number of configuration options.
    NumConfig,
  };

  /// Configuration bitset type for global-variable opportunities.
  using ConfigTy = BaseConfigTy<ConfigKind>;
  /// Per-opportunity configuration for global-variable instrumentation.
  ConfigTy Config;

  /// Return the opportunity name ("global").
  ///
  /// \return Opportunity name ("global").
  StringRef getName() const override { return "global"; }

  /// Initialize the global opportunity and its runtime arguments.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \param UserConfig Optional per-opportunity configuration; defaults when null.
  LLVM_ABI void init(InstrumentationConfig &IConf,
                     InstrumentorIRBuilderTy &IIRB,
                     ConfigTy *UserConfig = nullptr);

  /// Return the address of the global variable.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Address of the global variable.
  LLVM_ABI static Value *getAddress(Value &V, Type &Ty,
                                    InstrumentationConfig &IConf,
                                    InstrumentorIRBuilderTy &IIRB);
  /// Replace the global address with a runtime-provided value.
  ///
  /// \param V Value being instrumented.
  /// \param NewV Replacement value from the runtime.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Value after replacing the global address.
  LLVM_ABI static Value *setAddress(Value &V, Value &NewV,
                                    InstrumentationConfig &IConf,
                                    InstrumentorIRBuilderTy &IIRB);
  /// Return the address space of the global variable.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Address space of the global variable.
  LLVM_ABI static Value *getAS(Value &V, Type &Ty, InstrumentationConfig &IConf,
                               InstrumentorIRBuilderTy &IIRB);
  /// Return the declared size of the global variable.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Declared size of the global variable.
  LLVM_ABI static Value *getDeclaredSize(Value &V, Type &Ty,
                                         InstrumentationConfig &IConf,
                                         InstrumentorIRBuilderTy &IIRB);
  /// Return the alignment of the global variable.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Alignment of the global variable.
  LLVM_ABI static Value *getAlignment(Value &V, Type &Ty,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);
  /// Return the symbol name of the global variable.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Symbol name of the global variable.
  LLVM_ABI static Value *getSymbolName(Value &V, Type &Ty,
                                       InstrumentationConfig &IConf,
                                       InstrumentorIRBuilderTy &IIRB);
  /// Return the initial value of the global variable.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Initial value of the global variable.
  LLVM_ABI static Value *getInitialValue(Value &V, Type &Ty,
                                         InstrumentationConfig &IConf,
                                         InstrumentorIRBuilderTy &IIRB);
  /// Return whether the global variable is constant.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Whether the global variable is constant.
  LLVM_ABI static Value *isConstant(Value &V, Type &Ty,
                                    InstrumentationConfig &IConf,
                                    InstrumentorIRBuilderTy &IIRB);
  /// Return whether the global variable is a definition.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Whether the global variable is a definition.
  LLVM_ABI static Value *isDefinition(Value &V, Type &Ty,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);

  /// Register PRE and POST global opportunities on the configuration.
  ///
  /// \param IConf Instrumentation configuration to populate.
  /// \param IIRB Instrumentor IR builder.
  static void populate(InstrumentationConfig &IConf,
                       InstrumentorIRBuilderTy &IIRB) {
    auto *PreIO =
        IConf.allocate<GlobalVarIO>(InstrumentationLocation::GLOBAL_PRE);
    PreIO->init(IConf, IIRB);
    auto *PostIO =
        IConf.allocate<GlobalVarIO>(InstrumentationLocation::GLOBAL_POST);
    PostIO->init(IConf, IIRB);
  }
};

/// The instrumentation opportunity for store instructions.
struct StoreIO : public InstructionIO<Instruction::Store> {
  /// Destroy the store opportunity.
  virtual ~StoreIO() {};

  /// Construct a store instruction opportunity.
  ///
  /// \param Kind PRE or POST instruction location kind.
  StoreIO(InstrumentationLocation::KindTy Kind) : InstructionIO(Kind) {}

  /// Selector of arguments for store opportunities.
  enum ConfigKind {
    /// Pass the store pointer to the runtime.
    PassPointer = 0,
    /// Allow the runtime to replace the store pointer.
    ReplacePointer,
    /// Pass the pointer address space to the runtime.
    PassPointerAS,
    /// Pass base-pointer info for the store pointer.
    PassBasePointerInfo,
    /// Pass the stored value to the runtime.
    PassStoredValue,
    /// Pass the stored value size to the runtime.
    PassStoredValueSize,
    /// Pass the store alignment to the runtime.
    PassAlignment,
    /// Pass the stored value type id to the runtime.
    PassValueTypeId,
    /// Pass the stored value subtype id to the runtime.
    PassValueSubTypeId,
    /// Pass the atomicity ordering to the runtime.
    PassAtomicityOrdering,
    /// Pass the sync scope id to the runtime.
    PassSyncScopeId,
    /// Pass whether the store is volatile.
    PassIsVolatile,
    /// Pass the opportunity identifier.
    PassId,
    /// Number of configuration options.
    NumConfig,
  };

  /// Configuration bitset type for store opportunities.
  using ConfigTy = BaseConfigTy<ConfigKind>;
  /// Per-opportunity configuration for store instrumentation.
  ConfigTy Config;

  /// Get the type of the stored value.
  ///
  /// \param IIRB Instrumentor IR builder providing common types.
  /// \return Type used to pass the stored value to the runtime.
  virtual Type *getValueType(InstrumentorIRBuilderTy &IIRB) const {
    return IIRB.Int64Ty;
  }

  /// Initialize the store opportunity and its runtime arguments.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \param UserConfig Optional per-opportunity configuration; defaults when null.
  LLVM_ABI void init(InstrumentationConfig &IConf,
                     InstrumentorIRBuilderTy &IIRB,
                     ConfigTy *UserConfig = nullptr);

  /// Return the pointer operand of the store.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Pointer operand of the store.
  LLVM_ABI static Value *getPointer(Value &V, Type &Ty,
                                    InstrumentationConfig &IConf,
                                    InstrumentorIRBuilderTy &IIRB);
  /// Replace the store pointer with a runtime-provided value.
  ///
  /// \param V Value being instrumented.
  /// \param NewV Replacement value from the runtime.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Value after replacing the store pointer.
  LLVM_ABI static Value *setPointer(Value &V, Value &NewV,
                                    InstrumentationConfig &IConf,
                                    InstrumentorIRBuilderTy &IIRB);
  /// Return the address space of the store pointer.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Address space of the store pointer.
  LLVM_ABI static Value *getPointerAS(Value &V, Type &Ty,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);
  /// Return base-pointer info for the store pointer.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Base-pointer info for the store pointer.
  LLVM_ABI static Value *getBasePointerInfo(Value &V, Type &Ty,
                                            InstrumentationConfig &IConf,
                                            InstrumentorIRBuilderTy &IIRB);
  /// Return the value being stored.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Value being stored.
  LLVM_ABI static Value *getValue(Value &V, Type &Ty,
                                  InstrumentationConfig &IConf,
                                  InstrumentorIRBuilderTy &IIRB);
  /// Return the size of the stored value.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Size of the stored value.
  LLVM_ABI static Value *getValueSize(Value &V, Type &Ty,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);
  /// Return the alignment of the store.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Alignment of the store.
  LLVM_ABI static Value *getAlignment(Value &V, Type &Ty,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);
  /// Return the type id of the stored value.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Type id of the stored value.
  LLVM_ABI static Value *getValueTypeId(Value &V, Type &Ty,
                                        InstrumentationConfig &IConf,
                                        InstrumentorIRBuilderTy &IIRB);
  /// Return the subtype id of the stored value.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Subtype id of the stored value.
  LLVM_ABI static Value *getValueSubTypeId(Value &V, Type &Ty,
                                           InstrumentationConfig &IConf,
                                           InstrumentorIRBuilderTy &IIRB);
  /// Return the atomicity ordering of the store.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Atomicity ordering of the store.
  LLVM_ABI static Value *getAtomicityOrdering(Value &V, Type &Ty,
                                              InstrumentationConfig &IConf,
                                              InstrumentorIRBuilderTy &IIRB);
  /// Return the sync scope id of the store.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Sync scope id of the store.
  LLVM_ABI static Value *getSyncScopeId(Value &V, Type &Ty,
                                        InstrumentationConfig &IConf,
                                        InstrumentorIRBuilderTy &IIRB);
  /// Return whether the store is volatile.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Whether the store is volatile.
  LLVM_ABI static Value *isVolatile(Value &V, Type &Ty,
                                    InstrumentationConfig &IConf,
                                    InstrumentorIRBuilderTy &IIRB);

  /// Create store opportunities for PRE and POST positions.
  ///
  /// Opportunities are also initialized with the arguments for their
  /// instrumentation calls.
  ///
  /// \param IConf Instrumentation configuration to populate.
  /// \param IIRB Instrumentor IR builder.
  static void populate(InstrumentationConfig &IConf,
                       InstrumentorIRBuilderTy &IIRB) {
    auto *PreIO =
        IConf.allocate<StoreIO>(InstrumentationLocation::INSTRUCTION_PRE);
    PreIO->init(IConf, IIRB);
    auto *PostIO =
        IConf.allocate<StoreIO>(InstrumentationLocation::INSTRUCTION_POST);
    PostIO->init(IConf, IIRB);
  }
};

/// The instrumentation opportunity for load instructions.
struct LoadIO : public InstructionIO<Instruction::Load> {
  /// Destroy the load opportunity.
  virtual ~LoadIO() {};

  /// Construct a load opportunity.
  ///
  /// \param Kind PRE or POST instruction location kind.
  LoadIO(InstrumentationLocation::KindTy Kind) : InstructionIO(Kind) {}

  /// Selector of arguments for load opportunities.
  enum ConfigKind {
    /// Pass the load pointer to the runtime.
    PassPointer = 0,
    /// Allow the runtime to replace the load pointer.
    ReplacePointer,
    /// Pass the pointer address space to the runtime.
    PassPointerAS,
    /// Pass base-pointer info for the load pointer.
    PassBasePointerInfo,
    /// Pass the loaded value to the runtime.
    PassValue,
    /// Allow the runtime to replace the loaded value.
    ReplaceValue,
    /// Pass the loaded value size to the runtime.
    PassValueSize,
    /// Pass the load alignment to the runtime.
    PassAlignment,
    /// Pass the loaded value type id to the runtime.
    PassValueTypeId,
    /// Pass the loaded value subtype id to the runtime.
    PassValueSubTypeId,
    /// Pass the atomicity ordering to the runtime.
    PassAtomicityOrdering,
    /// Pass the sync scope id to the runtime.
    PassSyncScopeId,
    /// Pass whether the load is volatile.
    PassIsVolatile,
    /// Pass the opportunity identifier.
    PassId,
    /// Number of configuration options.
    NumConfig,
  };

  /// Configuration bitset type for load opportunities.
  using ConfigTy = BaseConfigTy<ConfigKind>;
  /// Per-opportunity configuration for load instrumentation.
  ConfigTy Config;

  /// Get the type of the loaded value.
  ///
  /// \param IIRB Instrumentor IR builder providing common types.
  /// \return Type used to pass the loaded value to the runtime.
  virtual Type *getValueType(InstrumentorIRBuilderTy &IIRB) const {
    return IIRB.Int64Ty;
  }

  /// Initialize the load opportunity and its runtime arguments.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \param UserConfig Optional per-opportunity configuration; defaults when null.
  LLVM_ABI void init(InstrumentationConfig &IConf,
                     InstrumentorIRBuilderTy &IIRB,
                     ConfigTy *UserConfig = nullptr);

  /// Return the pointer operand of the load.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Pointer operand of the load.
  LLVM_ABI static Value *getPointer(Value &V, Type &Ty,
                                    InstrumentationConfig &IConf,
                                    InstrumentorIRBuilderTy &IIRB);
  /// Replace the load pointer with a runtime-provided value.
  ///
  /// \param V Value being instrumented.
  /// \param NewV Replacement value from the runtime.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Value after replacing the load pointer.
  LLVM_ABI static Value *setPointer(Value &V, Value &NewV,
                                    InstrumentationConfig &IConf,
                                    InstrumentorIRBuilderTy &IIRB);
  /// Return the address space of the load pointer.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Address space of the load pointer.
  LLVM_ABI static Value *getPointerAS(Value &V, Type &Ty,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);
  /// Return base-pointer info for the load pointer.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Base-pointer info for the load pointer.
  LLVM_ABI static Value *getBasePointerInfo(Value &V, Type &Ty,
                                            InstrumentationConfig &IConf,
                                            InstrumentorIRBuilderTy &IIRB);
  /// Return the loaded value.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Loaded value.
  LLVM_ABI static Value *getValue(Value &V, Type &Ty,
                                  InstrumentationConfig &IConf,
                                  InstrumentorIRBuilderTy &IIRB);
  /// Return the size of the loaded value.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Size of the loaded value.
  LLVM_ABI static Value *getValueSize(Value &V, Type &Ty,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);
  /// Return the alignment of the load.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Alignment of the load.
  LLVM_ABI static Value *getAlignment(Value &V, Type &Ty,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);
  /// Return the type id of the loaded value.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Type id of the loaded value.
  LLVM_ABI static Value *getValueTypeId(Value &V, Type &Ty,
                                        InstrumentationConfig &IConf,
                                        InstrumentorIRBuilderTy &IIRB);
  /// Return the subtype id of the loaded value.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Subtype id of the loaded value.
  LLVM_ABI static Value *getValueSubTypeId(Value &V, Type &Ty,
                                           InstrumentationConfig &IConf,
                                           InstrumentorIRBuilderTy &IIRB);
  /// Return the atomicity ordering of the load.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Atomicity ordering of the load.
  LLVM_ABI static Value *getAtomicityOrdering(Value &V, Type &Ty,
                                              InstrumentationConfig &IConf,
                                              InstrumentorIRBuilderTy &IIRB);
  /// Return the sync scope id of the load.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Sync scope id of the load.
  LLVM_ABI static Value *getSyncScopeId(Value &V, Type &Ty,
                                        InstrumentationConfig &IConf,
                                        InstrumentorIRBuilderTy &IIRB);
  /// Return whether the load is volatile.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Whether the load is volatile.
  LLVM_ABI static Value *isVolatile(Value &V, Type &Ty,
                                    InstrumentationConfig &IConf,
                                    InstrumentorIRBuilderTy &IIRB);

  /// Create load opportunities for PRE and POST positions.
  ///
  /// \param IConf Instrumentation configuration to populate.
  /// \param IIRB Instrumentor IR builder.
  static void populate(InstrumentationConfig &IConf,
                       InstrumentorIRBuilderTy &IIRB) {
    auto *PreIO =
        IConf.allocate<LoadIO>(InstrumentationLocation::INSTRUCTION_PRE);
    PreIO->init(IConf, IIRB);
    auto *PostIO =
        IConf.allocate<LoadIO>(InstrumentationLocation::INSTRUCTION_POST);
    PostIO->init(IConf, IIRB);
  }
};

/// The instrumentation opportunity for type cast instructions.
///
/// This includes PtrToInt, IntToPtr, Trunc, ZExt, SExt, FPToUI, FPToSI, UIToFP, SIToFP, FPTrunc, FPExt, AddrSpaceCast, and BitCast.
struct CastIO final
    : public InstructionIO<
          Instruction::PtrToInt, Instruction::IntToPtr, Instruction::Trunc,
          Instruction::ZExt, Instruction::SExt, Instruction::FPToUI,
          Instruction::FPToSI, Instruction::UIToFP, Instruction::SIToFP,
          Instruction::FPTrunc, Instruction::FPExt, Instruction::AddrSpaceCast,
          Instruction::BitCast> {
  /// Construct a cast instrumentation opportunity.
  ///
  /// \param Kind PRE or POST instruction location kind.
  CastIO(InstrumentationLocation::KindTy Kind) : InstructionIO(Kind) {}

  /// Selector of arguments for cast opportunities.
  enum ConfigKind {
    /// Pass the cast input value to the runtime.
    PassInput,
    /// Pass the input type id to the runtime.
    PassInputTypeId,
    /// Pass the input subtype id to the runtime.
    PassInputSubTypeId,
    /// Pass the input size to the runtime.
    PassInputSize,
    /// Pass the cast result to the runtime.
    PassResult,
    /// Allow the runtime to replace the cast result.
    ReplaceResult,
    /// Pass the result type id to the runtime.
    PassResultTypeId,
    /// Pass the result subtype id to the runtime.
    PassResultSubTypeId,
    /// Pass the result size to the runtime.
    PassResultSize,
    /// Pass the cast opcode to the runtime.
    PassOpcode,
    /// Pass the opportunity identifier.
    PassId,
    /// Number of configuration options.
    NumConfig,
  };

  /// Configuration bitset type for cast opportunities.
  using ConfigTy = BaseConfigTy<ConfigKind>;
  /// Per-opportunity configuration for cast instrumentation.
  ConfigTy Config;

  /// Return the opportunity name ("cast").
  ///
  /// \return Opportunity name ("cast").
  StringRef getName() const override { return "cast"; }

  /// Initialize the cast opportunity and its runtime arguments.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \param UserConfig Optional per-opportunity configuration; defaults when null.
  LLVM_ABI void init(InstrumentationConfig &IConf,
                     InstrumentorIRBuilderTy &IIRB,
                     ConfigTy *UserConfig = nullptr);

  /// Return the input operand of the cast.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Input operand of the cast.
  LLVM_ABI static Value *getInput(Value &V, Type &Ty,
                                  InstrumentationConfig &IConf,
                                  InstrumentorIRBuilderTy &IIRB);
  /// Return the type id of the cast input.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Type id of the cast input.
  LLVM_ABI static Value *getInputTypeId(Value &V, Type &Ty,
                                        InstrumentationConfig &IConf,
                                        InstrumentorIRBuilderTy &IIRB);
  /// Return the subtype id of the cast input.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Subtype id of the cast input.
  LLVM_ABI static Value *getInputSubTypeId(Value &V, Type &Ty,
                                           InstrumentationConfig &IConf,
                                           InstrumentorIRBuilderTy &IIRB);
  /// Return the size of the cast input.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Size of the cast input.
  LLVM_ABI static Value *getInputSize(Value &V, Type &Ty,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);
  /// Return the type id of the cast result.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Type id of the cast result.
  LLVM_ABI static Value *getResultTypeId(Value &V, Type &Ty,
                                         InstrumentationConfig &IConf,
                                         InstrumentorIRBuilderTy &IIRB);
  /// Return the subtype id of the cast result.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Subtype id of the cast result.
  LLVM_ABI static Value *getResultSubTypeId(Value &V, Type &Ty,
                                            InstrumentationConfig &IConf,
                                            InstrumentorIRBuilderTy &IIRB);
  /// Return the size of the cast result.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Size of the cast result.
  LLVM_ABI static Value *getResultSize(Value &V, Type &Ty,
                                       InstrumentationConfig &IConf,
                                       InstrumentorIRBuilderTy &IIRB);

  /// Register PRE and POST cast opportunities on the configuration.
  ///
  /// \param IConf Instrumentation configuration to populate.
  /// \param IIRB Instrumentor IR builder.
  static void populate(InstrumentationConfig &IConf,
                       InstrumentorIRBuilderTy &IIRB) {
    auto *PreIO =
        IConf.allocate<CastIO>(InstrumentationLocation::INSTRUCTION_PRE);
    PreIO->init(IConf, IIRB);
    auto *PostIO =
        IConf.allocate<CastIO>(InstrumentationLocation::INSTRUCTION_POST);
    PostIO->init(IConf, IIRB);
  }
};

/// Instrumentation opportunity for numeric operations.
///
/// This includes Add, FAdd, Sub, FSub, Mul, FMul, UDiv, FDiv, SDiv, URem, SRem, FRem, Shl, LShr, AShr, And, Or, Xor, and FNeg.
struct NumericIO final
    : public InstructionIO<
          Instruction::Add, Instruction::FAdd, Instruction::Sub,
          Instruction::FSub, Instruction::Mul, Instruction::FMul,
          Instruction::UDiv, Instruction::FDiv, Instruction::SDiv,
          Instruction::URem, Instruction::SRem, Instruction::FRem,
          Instruction::Shl, Instruction::LShr, Instruction::AShr,
          Instruction::And, Instruction::Or, Instruction::Xor,
          Instruction::FNeg> {
  /// Construct a numeric instrumentation opportunity.
  ///
  /// \param Kind PRE or POST instruction location kind.
  NumericIO(InstrumentationLocation::KindTy Kind) : InstructionIO(Kind) {}

  /// Selector of arguments for numeric opportunities.
  enum ConfigKind {
    /// Pass the result type id to the runtime.
    PassTypeId,
    /// Pass the result subtype id to the runtime.
    PassSubTypeId,
    /// Pass the result size to the runtime.
    PassSize,
    /// Pass the numeric opcode to the runtime.
    PassOpcode,
    /// Pass the numeric result to the runtime.
    PassResult,
    /// Allow the runtime to replace the numeric result.
    ReplaceResult,
    /// Pass the left operand to the runtime.
    PassLeft,
    /// Pass the right operand to the runtime.
    PassRight,
    /// Pass operation flags to the runtime.
    PassFlags,
    /// Pass the opportunity identifier.
    PassId,
    /// Number of configuration options.
    NumConfig,
  };

  /// Configuration bitset type for numeric opportunities.
  using ConfigTy = BaseConfigTy<ConfigKind>;
  /// Per-opportunity configuration for numeric instrumentation.
  ConfigTy Config;

  /// Return the opportunity name ("numeric").
  ///
  /// \return Opportunity name ("numeric").
  StringRef getName() const override { return "numeric"; }

  /// Initialize the numeric opportunity and its runtime arguments.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \param UserConfig Optional per-opportunity configuration; defaults when null.
  LLVM_ABI void init(InstrumentationConfig &IConf,
                     InstrumentorIRBuilderTy &IIRB,
                     ConfigTy *UserConfig = nullptr);
  /// Register human-readable names for numeric operation flags.
  LLVM_ABI void addFlagNames();

  /// Return the flags of the numeric instruction.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Flags of the numeric instruction.
  LLVM_ABI static Value *getFlags(Value &V, Type &Ty,
                                  InstrumentationConfig &IConf,
                                  InstrumentorIRBuilderTy &IIRB);

  /// Register PRE and POST numeric opportunities on the configuration.
  ///
  /// \param IConf Instrumentation configuration to populate.
  /// \param IIRB Instrumentor IR builder.
  static void populate(InstrumentationConfig &IConf,
                       InstrumentorIRBuilderTy &IIRB) {
    auto *PreIO =
        IConf.allocate<NumericIO>(InstrumentationLocation::INSTRUCTION_PRE);
    PreIO->init(IConf, IIRB);
    auto *PostIO =
        IConf.allocate<NumericIO>(InstrumentationLocation::INSTRUCTION_POST);
    PostIO->init(IConf, IIRB);
  }
};

/// Instrumentation opportunity for compare instructions.
struct CompareIO final
    : public InstructionIO<Instruction::ICmp, Instruction::FCmp> {
  /// Construct a compare instrumentation opportunity.
  ///
  /// \param Kind PRE or POST instruction location kind.
  CompareIO(InstrumentationLocation::KindTy Kind) : InstructionIO(Kind) {}

  /// Selector of arguments for compare opportunities.
  enum ConfigKind {
    /// Pass the operand type id to the runtime.
    PassOpTypeId,
    /// Pass the operand size to the runtime.
    PassOpSize,
    /// Pass the compare opcode to the runtime.
    PassOpcode,
    /// Pass the compare predicate to the runtime.
    PassPredicate,
    /// Pass the left operand to the runtime.
    PassLeft,
    /// Pass the right operand to the runtime.
    PassRight,
    /// Pass the result type id to the runtime.
    PassResultTypeId,
    /// Pass the result size to the runtime.
    PassResultSize,
    /// Pass the compare result to the runtime.
    PassResult,
    /// Allow the runtime to replace the compare result.
    ReplaceResult,
    /// Pass compare flags to the runtime.
    PassFlags,
    /// Pass the opportunity identifier.
    PassId,
    /// Number of configuration options.
    NumConfig,
  };

  /// Configuration bitset type for compare opportunities.
  using ConfigTy = BaseConfigTy<ConfigKind>;
  /// Per-opportunity configuration for compare instrumentation.
  ConfigTy Config;

  /// Return the opportunity name ("compare").
  ///
  /// \return Opportunity name ("compare").
  StringRef getName() const override { return "compare"; }

  /// Initialize the compare opportunity and its runtime arguments.
  ///
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \param UserConfig Optional per-opportunity configuration; defaults when null.
  LLVM_ABI void init(InstrumentationConfig &IConf,
                     InstrumentorIRBuilderTy &IIRB,
                     ConfigTy *UserConfig = nullptr);
  /// Register human-readable names for compare flags.
  LLVM_ABI void addFlagNames();

  /// Return the type id of the compare operands.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Type id of the compare operands.
  LLVM_ABI static Value *getOperandTypeId(Value &V, Type &Ty,
                                          InstrumentationConfig &IConf,
                                          InstrumentorIRBuilderTy &IIRB);
  /// Return the size of the compare operands.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Size of the compare operands.
  LLVM_ABI static Value *getOperandSize(Value &V, Type &Ty,
                                        InstrumentationConfig &IConf,
                                        InstrumentorIRBuilderTy &IIRB);
  /// Return the compare predicate.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Compare predicate.
  LLVM_ABI static Value *getPredicate(Value &V, Type &Ty,
                                      InstrumentationConfig &IConf,
                                      InstrumentorIRBuilderTy &IIRB);
  /// Return the flags of the compare instruction.
  ///
  /// \param V Value being instrumented.
  /// \param Ty Expected argument type.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder.
  /// \return Flags of the compare instruction.
  LLVM_ABI static Value *getFlags(Value &V, Type &Ty,
                                  InstrumentationConfig &IConf,
                                  InstrumentorIRBuilderTy &IIRB);

  /// Register PRE and POST compare opportunities on the configuration.
  ///
  /// \param IConf Instrumentation configuration to populate.
  /// \param IIRB Instrumentor IR builder.
  static void populate(InstrumentationConfig &IConf,
                       InstrumentorIRBuilderTy &IIRB) {
    auto *PreIO =
        IConf.allocate<CompareIO>(InstrumentationLocation::INSTRUCTION_PRE);
    PreIO->init(IConf, IIRB);
    auto *PostIO =
        IConf.allocate<CompareIO>(InstrumentationLocation::INSTRUCTION_POST);
    PostIO->init(IConf, IIRB);
  }
};

} // namespace instrumentor

/// The Instrumentor pass.
class InstrumentorPass : public RequiredPassInfoMixin<InstrumentorPass> {
  using InstrumentationConfig = instrumentor::InstrumentationConfig;
  using InstrumentorIRBuilderTy = instrumentor::InstrumentorIRBuilderTy;

  /// File system to be used for read operations.
  IntrusiveRefCntPtr<vfs::FileSystem> FS;

  /// The configuration and IR builder provided by the user.
  InstrumentationConfig *UserIConf;
  InstrumentorIRBuilderTy *UserIIRB;

  PreservedAnalyses run(Module &M, InstrumentationConfig &IConf,
                        InstrumentorIRBuilderTy &IIRB, bool ReadConfig);

public:
  /// Construct an instrumentor pass.
  ///
  /// Uses the instrumentation configuration \p IC and the IR builder \p IIRB.
  /// If an IR builder is not provided, a default builder is used. When the
  /// configuration is not provided, it is read from the config file if
  /// available and otherwise a default configuration is used.
  ///
  /// \param FS File system used for configuration and stub file I/O.
  /// \param IC Optional instrumentation configuration; read or defaulted if null.
  /// \param IIRB Optional instrumentor IR builder; a default is used if null.
  LLVM_ABI InstrumentorPass(IntrusiveRefCntPtr<vfs::FileSystem> FS = nullptr,
                            InstrumentationConfig *IC = nullptr,
                            InstrumentorIRBuilderTy *IIRB = nullptr);

  /// Run the instrumentor over the given module.
  ///
  /// \param M Module to instrument.
  /// \param MAM Module analysis manager.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_INSTRUMENTOR_H
