#ifdef GET_ATTR_NAMES
#undef GET_ATTR_NAMES
#ifndef ATTRIBUTE_ALL
#define ATTRIBUTE_ALL(FIRST, SECOND)
#endif

#ifndef ATTRIBUTE_ENUM
#define ATTRIBUTE_ENUM(FIRST, SECOND) ATTRIBUTE_ALL(FIRST, SECOND)
#endif

ATTRIBUTE_ENUM(AllocAlign,allocalign)
ATTRIBUTE_ENUM(AllocatedPointer,allocptr)
ATTRIBUTE_ENUM(AlwaysInline,alwaysinline)
ATTRIBUTE_ENUM(Builtin,builtin)
ATTRIBUTE_ENUM(Cold,cold)
ATTRIBUTE_ENUM(Convergent,convergent)
ATTRIBUTE_ENUM(CoroDestroyOnlyWhenComplete,coro_only_destroy_when_complete)
ATTRIBUTE_ENUM(CoroElideSafe,coro_elide_safe)
ATTRIBUTE_ENUM(DeadOnUnwind,dead_on_unwind)
ATTRIBUTE_ENUM(DisableSanitizerInstrumentation,disable_sanitizer_instrumentation)
ATTRIBUTE_ENUM(Flatten,flatten)
ATTRIBUTE_ENUM(FnRetThunkExtern,fn_ret_thunk_extern)
ATTRIBUTE_ENUM(Hot,hot)
ATTRIBUTE_ENUM(HybridPatchable,hybrid_patchable)
ATTRIBUTE_ENUM(ImmArg,immarg)
ATTRIBUTE_ENUM(InReg,inreg)
ATTRIBUTE_ENUM(InlineHint,inlinehint)
ATTRIBUTE_ENUM(JumpTable,jumptable)
ATTRIBUTE_ENUM(MinSize,minsize)
ATTRIBUTE_ENUM(MustProgress,mustprogress)
ATTRIBUTE_ENUM(Naked,naked)
ATTRIBUTE_ENUM(Nest,nest)
ATTRIBUTE_ENUM(NoAlias,noalias)
ATTRIBUTE_ENUM(NoBuiltin,nobuiltin)
ATTRIBUTE_ENUM(NoCallback,nocallback)
ATTRIBUTE_ENUM(NoCfCheck,nocf_check)
ATTRIBUTE_ENUM(NoCreateUndefOrPoison,nocreateundeforpoison)
ATTRIBUTE_ENUM(NoDivergenceSource,nodivergencesource)
ATTRIBUTE_ENUM(NoDuplicate,noduplicate)
ATTRIBUTE_ENUM(NoExt,noext)
ATTRIBUTE_ENUM(NoFree,nofree)
ATTRIBUTE_ENUM(NoFreeObj,nofreeobj)
ATTRIBUTE_ENUM(NoIPA,noipa)
ATTRIBUTE_ENUM(NoImplicitFloat,noimplicitfloat)
ATTRIBUTE_ENUM(NoInline,noinline)
ATTRIBUTE_ENUM(NoMerge,nomerge)
ATTRIBUTE_ENUM(NoOutline,nooutline)
ATTRIBUTE_ENUM(NoProfile,noprofile)
ATTRIBUTE_ENUM(NoRecurse,norecurse)
ATTRIBUTE_ENUM(NoRedZone,noredzone)
ATTRIBUTE_ENUM(NoReturn,noreturn)
ATTRIBUTE_ENUM(NoSanitizeBounds,nosanitize_bounds)
ATTRIBUTE_ENUM(NoSanitizeCoverage,nosanitize_coverage)
ATTRIBUTE_ENUM(NoSync,nosync)
ATTRIBUTE_ENUM(NoUndef,noundef)
ATTRIBUTE_ENUM(NoUnwind,nounwind)
ATTRIBUTE_ENUM(NonLazyBind,nonlazybind)
ATTRIBUTE_ENUM(NonNull,nonnull)
ATTRIBUTE_ENUM(NullPointerIsValid,null_pointer_is_valid)
ATTRIBUTE_ENUM(OptForFuzzing,optforfuzzing)
ATTRIBUTE_ENUM(OptimizeForDebugging,optdebug)
ATTRIBUTE_ENUM(OptimizeForSize,optsize)
ATTRIBUTE_ENUM(OptimizeNone,optnone)
ATTRIBUTE_ENUM(PresplitCoroutine,presplitcoroutine)
ATTRIBUTE_ENUM(ReadNone,readnone)
ATTRIBUTE_ENUM(ReadOnly,readonly)
ATTRIBUTE_ENUM(Returned,returned)
ATTRIBUTE_ENUM(ReturnsTwice,returns_twice)
ATTRIBUTE_ENUM(SExt,signext)
ATTRIBUTE_ENUM(SafeStack,safestack)
ATTRIBUTE_ENUM(SanitizeAddress,sanitize_address)
ATTRIBUTE_ENUM(SanitizeAllocToken,sanitize_alloc_token)
ATTRIBUTE_ENUM(SanitizeHWAddress,sanitize_hwaddress)
ATTRIBUTE_ENUM(SanitizeMemTag,sanitize_memtag)
ATTRIBUTE_ENUM(SanitizeMemory,sanitize_memory)
ATTRIBUTE_ENUM(SanitizeNumericalStability,sanitize_numerical_stability)
ATTRIBUTE_ENUM(SanitizeRealtime,sanitize_realtime)
ATTRIBUTE_ENUM(SanitizeRealtimeBlocking,sanitize_realtime_blocking)
ATTRIBUTE_ENUM(SanitizeThread,sanitize_thread)
ATTRIBUTE_ENUM(SanitizeType,sanitize_type)
ATTRIBUTE_ENUM(ShadowCallStack,shadowcallstack)
ATTRIBUTE_ENUM(SkipProfile,skipprofile)
ATTRIBUTE_ENUM(Speculatable,speculatable)
ATTRIBUTE_ENUM(SpeculativeLoadHardening,speculative_load_hardening)
ATTRIBUTE_ENUM(StackProtect,ssp)
ATTRIBUTE_ENUM(StackProtectReq,sspreq)
ATTRIBUTE_ENUM(StackProtectStrong,sspstrong)
ATTRIBUTE_ENUM(StrictFP,strictfp)
ATTRIBUTE_ENUM(SwiftAsync,swiftasync)
ATTRIBUTE_ENUM(SwiftError,swifterror)
ATTRIBUTE_ENUM(SwiftSelf,swiftself)
ATTRIBUTE_ENUM(WillReturn,willreturn)
ATTRIBUTE_ENUM(Writable,writable)
ATTRIBUTE_ENUM(WriteOnly,writeonly)
ATTRIBUTE_ENUM(ZExt,zeroext)
ATTRIBUTE_ENUM(ByRef,byref)
ATTRIBUTE_ENUM(ByVal,byval)
ATTRIBUTE_ENUM(ElementType,elementtype)
ATTRIBUTE_ENUM(InAlloca,inalloca)
ATTRIBUTE_ENUM(Preallocated,preallocated)
ATTRIBUTE_ENUM(StructRet,sret)
ATTRIBUTE_ENUM(Alignment,align)
ATTRIBUTE_ENUM(AllocKind,allockind)
ATTRIBUTE_ENUM(AllocSize,allocsize)
ATTRIBUTE_ENUM(Captures,captures)
ATTRIBUTE_ENUM(DeadOnReturn,dead_on_return)
ATTRIBUTE_ENUM(DenormalFPEnv,denormal_fpenv)
ATTRIBUTE_ENUM(Dereferenceable,dereferenceable)
ATTRIBUTE_ENUM(DereferenceableOrNull,dereferenceable_or_null)
ATTRIBUTE_ENUM(Memory,memory)
ATTRIBUTE_ENUM(NoFPClass,nofpclass)
ATTRIBUTE_ENUM(StackAlignment,alignstack)
ATTRIBUTE_ENUM(UWTable,uwtable)
ATTRIBUTE_ENUM(VScaleRange,vscale_range)
ATTRIBUTE_ENUM(Range,range)
ATTRIBUTE_ENUM(Initializes,initializes)
#undef ATTRIBUTE_ENUM

#ifndef ATTRIBUTE_STRBOOL
#define ATTRIBUTE_STRBOOL(FIRST, SECOND) ATTRIBUTE_ALL(FIRST, SECOND)
#endif

ATTRIBUTE_STRBOOL(AllowDirectAccessInHotPatchFunction,allow_direct_access_in_hot_patch_function)
ATTRIBUTE_STRBOOL(LessPreciseFPMAD,less-precise-fpmad)
ATTRIBUTE_STRBOOL(LoaderReplaceable,loader-replaceable)
ATTRIBUTE_STRBOOL(MarkedForWindowsSecureHotPatching,marked_for_windows_hot_patching)
ATTRIBUTE_STRBOOL(NoInlineLineTables,no-inline-line-tables)
ATTRIBUTE_STRBOOL(NoJumpTables,no-jump-tables)
ATTRIBUTE_STRBOOL(NoSignedZerosFPMath,no-signed-zeros-fp-math)
ATTRIBUTE_STRBOOL(ProfileSampleAccurate,profile-sample-accurate)
ATTRIBUTE_STRBOOL(UseSampleProfile,use-sample-profile)
#undef ATTRIBUTE_STRBOOL

#ifndef ATTRIBUTE_COMPLEXSTR
#define ATTRIBUTE_COMPLEXSTR(FIRST, SECOND) ATTRIBUTE_ALL(FIRST, SECOND)
#endif

#undef ATTRIBUTE_COMPLEXSTR

#undef ATTRIBUTE_ALL
#endif

#ifdef GET_ATTR_ENUM
#undef GET_ATTR_ENUM
FirstEnumAttr = 1,
AllocAlign = 1,
AllocatedPointer = 2,
AlwaysInline = 3,
Builtin = 4,
Cold = 5,
Convergent = 6,
CoroDestroyOnlyWhenComplete = 7,
CoroElideSafe = 8,
DeadOnUnwind = 9,
DisableSanitizerInstrumentation = 10,
Flatten = 11,
FnRetThunkExtern = 12,
Hot = 13,
HybridPatchable = 14,
ImmArg = 15,
InReg = 16,
InlineHint = 17,
JumpTable = 18,
MinSize = 19,
MustProgress = 20,
Naked = 21,
Nest = 22,
NoAlias = 23,
NoBuiltin = 24,
NoCallback = 25,
NoCfCheck = 26,
NoCreateUndefOrPoison = 27,
NoDivergenceSource = 28,
NoDuplicate = 29,
NoExt = 30,
NoFree = 31,
NoFreeObj = 32,
NoIPA = 33,
NoImplicitFloat = 34,
NoInline = 35,
NoMerge = 36,
NoOutline = 37,
NoProfile = 38,
NoRecurse = 39,
NoRedZone = 40,
NoReturn = 41,
NoSanitizeBounds = 42,
NoSanitizeCoverage = 43,
NoSync = 44,
NoUndef = 45,
NoUnwind = 46,
NonLazyBind = 47,
NonNull = 48,
NullPointerIsValid = 49,
OptForFuzzing = 50,
OptimizeForDebugging = 51,
OptimizeForSize = 52,
OptimizeNone = 53,
PresplitCoroutine = 54,
ReadNone = 55,
ReadOnly = 56,
Returned = 57,
ReturnsTwice = 58,
SExt = 59,
SafeStack = 60,
SanitizeAddress = 61,
SanitizeAllocToken = 62,
SanitizeHWAddress = 63,
SanitizeMemTag = 64,
SanitizeMemory = 65,
SanitizeNumericalStability = 66,
SanitizeRealtime = 67,
SanitizeRealtimeBlocking = 68,
SanitizeThread = 69,
SanitizeType = 70,
ShadowCallStack = 71,
SkipProfile = 72,
Speculatable = 73,
SpeculativeLoadHardening = 74,
StackProtect = 75,
StackProtectReq = 76,
StackProtectStrong = 77,
StrictFP = 78,
SwiftAsync = 79,
SwiftError = 80,
SwiftSelf = 81,
WillReturn = 82,
Writable = 83,
WriteOnly = 84,
ZExt = 85,
LastEnumAttr = 85,
FirstTypeAttr = 86,
ByRef = 86,
ByVal = 87,
ElementType = 88,
InAlloca = 89,
Preallocated = 90,
StructRet = 91,
LastTypeAttr = 91,
FirstIntAttr = 92,
Alignment = 92,
AllocKind = 93,
AllocSize = 94,
Captures = 95,
DeadOnReturn = 96,
DenormalFPEnv = 97,
Dereferenceable = 98,
DereferenceableOrNull = 99,
Memory = 100,
NoFPClass = 101,
StackAlignment = 102,
UWTable = 103,
VScaleRange = 104,
LastIntAttr = 104,
FirstConstantRangeAttr = 105,
Range = 105,
LastConstantRangeAttr = 105,
FirstConstantRangeListAttr = 106,
Initializes = 106,
LastConstantRangeListAttr = 106,
#endif

#ifdef GET_ATTR_COMPAT_FUNC
#undef GET_ATTR_COMPAT_FUNC
static inline bool hasCompatibleFnAttrs(const Function &Caller,
                                        const Function &Callee) {
  bool Ret = true;

  Ret &= isEqual<SanitizeAddressAttr>(Caller, Callee);
  Ret &= isEqual<SanitizeThreadAttr>(Caller, Callee);
  Ret &= isEqual<SanitizeTypeAttr>(Caller, Callee);
  Ret &= isEqual<SanitizeMemoryAttr>(Caller, Callee);
  Ret &= isEqual<SanitizeHWAddressAttr>(Caller, Callee);
  Ret &= isEqual<SanitizeMemTagAttr>(Caller, Callee);
  Ret &= isEqual<SanitizeNumericalStabilityAttr>(Caller, Callee);
  Ret &= isEqual<SanitizeRealtimeAttr>(Caller, Callee);
  Ret &= isEqual<SanitizeRealtimeBlockingAttr>(Caller, Callee);
  Ret &= isEqual<SafeStackAttr>(Caller, Callee);
  Ret &= isEqual<ShadowCallStackAttr>(Caller, Callee);
  Ret &= isEqual<UseSampleProfileAttr>(Caller, Callee);
  Ret &= isEqual<NoProfileAttr>(Caller, Callee);
  Ret &= checkDenormMode(Caller, Callee);
  Ret &= checkStrictFP(Caller, Callee);
  Ret &= isEqual(Caller, Callee, "sign-return-address");
  Ret &= isEqual(Caller, Callee, "sign-return-address-key");
  Ret &= isEqual(Caller, Callee, "branch-protection-pauth-lr");

  return Ret;
}

static inline void mergeFnAttrs(Function &Caller,
                                const Function &Callee) {
  setAND<LessPreciseFPMADAttr>(Caller, Callee);
  setAND<NoSignedZerosFPMathAttr>(Caller, Callee);
  setOR<NoImplicitFloatAttr>(Caller, Callee);
  setOR<NoJumpTablesAttr>(Caller, Callee);
  setOR<ProfileSampleAccurateAttr>(Caller, Callee);
  setOR<SpeculativeLoadHardeningAttr>(Caller, Callee);
  adjustCallerSSPLevel(Caller, Callee);
  adjustCallerStackProbes(Caller, Callee);
  adjustCallerStackProbeSize(Caller, Callee);
  adjustMinLegalVectorWidth(Caller, Callee);
  adjustNullPointerValidAttr(Caller, Callee);
  setAND<MustProgressAttr>(Caller, Callee);
}

#endif
#ifdef GET_ATTR_PROP_TABLE
#undef GET_ATTR_PROP_TABLE
static const uint8_t AttrPropTable[] = {
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::ParamAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectAnd,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::RetAttr | AttributeProperty::ParamAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::ParamAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectCustom,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectCustom,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectMin,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectMin,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectMin,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectCustom,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectCustom,
0 | AttributeProperty::FnAttr | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::FnAttr | AttributeProperty::IntersectPreserve,
0 | AttributeProperty::ParamAttr | AttributeProperty::RetAttr | AttributeProperty::IntersectCustom,
0 | AttributeProperty::ParamAttr | AttributeProperty::IntersectPreserve,
};
#endif
