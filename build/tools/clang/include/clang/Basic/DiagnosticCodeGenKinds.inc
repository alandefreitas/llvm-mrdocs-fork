#ifdef CODEGENSTART
__CODEGENSTART = DIAG_START_CODEGEN,
#undef CODEGENSTART
#endif

DIAG(err_cir_mlir_diagnostic, CLASS_ERROR, (unsigned)diag::Severity::Error, "%0", 0, SFINAE_SubstitutionFailure, false, true, true, false, 0, 26013, 0)
DIAG(err_cir_to_cir_transform_failed, CLASS_ERROR, (unsigned)diag::Severity::Fatal, "CIR-to-CIR transformation failed", 0, SFINAE_SubstitutionFailure, false, true, true, false, 0, 26037, 0)
DIAG(err_cir_verification_failed_pre_passes, CLASS_ERROR, (unsigned)diag::Severity::Fatal, "CIR module verification error before running CIR-to-CIR passes", 0, SFINAE_SubstitutionFailure, false, true, true, false, 0, 26069, 0)
DIAG(note_cir_mlir_diagnostic, CLASS_NOTE, (unsigned)diag::Severity::Fatal, "%0", 0, SFINAE_Suppress, false, false, true, false, 0, 149732, 0)
DIAG(remark_cir_mlir_diagnostic, CLASS_REMARK, (unsigned)diag::Severity::Ignored, "%0", 929, SFINAE_Suppress, false, false, true, false, 0, 183622, 0)
DIAG(warn_cir_mlir_diagnostic, CLASS_WARNING, (unsigned)diag::Severity::Warning, "%0", 191, SFINAE_Suppress, false, false, true, false, 0, 192918, 0)
