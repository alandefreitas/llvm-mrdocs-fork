//===- MCExpr.h - Assembly Level Expressions --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCEXPR_H
#define LLVM_MC_MCEXPR_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SMLoc.h"
#include <cstdint>

namespace llvm {

class MCAsmInfo;
class MCAssembler;
class MCContext;
class MCFixup;
class MCFragment;
class MCSection;
class MCStreamer;
class MCSymbol;
class MCValue;
class raw_ostream;
class StringRef;
class MCSymbolRefExpr;

/// Base class for the full range of assembler expressions which are
/// needed for parsing.
class MCExpr {
public:
  // Allow MC classes to access the private `print` function.
  friend class MCAsmInfo;
  friend class MCFragment;
  friend class MCOperand;
  /// Discriminator for concrete MCExpr subclasses.
  enum ExprKind : uint8_t {
    Binary,    ///< Binary expressions.
    Constant,  ///< Constant expressions.
    SymbolRef, ///< References to labels and assigned expressions.
    Unary,     ///< Unary expressions.
    Specifier, ///< Expression with a relocation specifier.
    Target     ///< Target specific expression.
  };

private:
  static const unsigned NumSubclassDataBits = 24;
  static_assert(
      NumSubclassDataBits == CHAR_BIT * (sizeof(unsigned) - sizeof(ExprKind)),
      "ExprKind and SubclassData together should take up one word");

  ExprKind Kind;
  /// Field reserved for use by MCExpr subclasses.
  unsigned SubclassData : NumSubclassDataBits;
  SMLoc Loc;

  void print(raw_ostream &OS, const MCAsmInfo *MAI,
             int SurroundingPrec = 0) const;
  bool evaluateAsAbsolute(int64_t &Res, const MCAssembler *Asm,
                          bool InSet) const;

protected:
  /// Relocation specifier type stored in subclass data.
  using Spec = uint16_t;
  /// Construct an expression of \p Kind at source location \p Loc.
  ///
  /// \param Kind - Expression kind discriminator.
  /// \param Loc - Source location of the expression.
  /// \param SubclassData - Subclass-specific payload packed into this
  /// expression.
  explicit MCExpr(ExprKind Kind, SMLoc Loc, unsigned SubclassData = 0)
      : Kind(Kind), SubclassData(SubclassData), Loc(Loc) {
    assert(SubclassData < (1 << NumSubclassDataBits) &&
           "Subclass data too large");
  }

  /// Evaluate this expression as a relocatable value.
  ///
  /// \param Res - Filled with the relocatable result on success.
  /// \param Asm - Optional assembler used to resolve symbols and fragments.
  /// \param InSet - True when evaluating inside a \c .set / assignment
  /// context.
  /// \return True on success.
  LLVM_ABI bool evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                          bool InSet) const;

  /// Return subclass-specific data stored in this expression.
  ///
  /// \return Subclass-specific payload packed into this expression.
  unsigned getSubclassData() const { return SubclassData; }

public:
  /// Deleted copy constructor.
  ///
  /// \param Other - Unused; copy construction is deleted.
  MCExpr(const MCExpr &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other - Unused; copy assignment is deleted.
  MCExpr &operator=(const MCExpr &Other) = delete;

  /// \name Accessors
  /// @{

  /// Return the kind of this expression.
  ///
  /// \return Expression kind discriminator.
  ExprKind getKind() const { return Kind; }
  /// Return the source location of this expression.
  ///
  /// \return Source location of the expression.
  SMLoc getLoc() const { return Loc; }

  /// @}
  /// \name Utility Methods
  /// @{

  /// Dump this expression to stderr.
  LLVM_ABI void dump() const;

  /// @}
  /// \name Expression Evaluation
  /// @{

  /// Try to evaluate the expression to an absolute value.
  ///
  /// \param Res - The absolute value, if evaluation succeeds.
  /// \return - True on success.
  LLVM_ABI bool evaluateAsAbsolute(int64_t &Res) const;
  /// Try to evaluate the expression to an absolute value using \p Asm.
  ///
  /// \param Res - The absolute value, if evaluation succeeds.
  /// \param Asm - Assembler used to resolve symbols and fragments.
  /// \return - True on success.
  LLVM_ABI bool evaluateAsAbsolute(int64_t &Res, const MCAssembler &Asm) const;
  /// Try to evaluate the expression to an absolute value using \p Asm,
  /// which may be null.
  ///
  /// \param Res - The absolute value, if evaluation succeeds.
  /// \param Asm - Optional assembler used to resolve symbols and fragments.
  /// \return - True on success.
  LLVM_ABI bool evaluateAsAbsolute(int64_t &Res, const MCAssembler *Asm) const;

  /// Aggressive variant of evaluateAsRelocatable when relocations are
  /// unavailable (e.g. .fill). Expects callers to handle errors when true is
  /// returned.
  ///
  /// \param Res - The absolute value, if evaluation succeeds.
  /// \param Asm - Assembler used to resolve symbols and fragments.
  /// \return True on success.
  LLVM_ABI bool evaluateKnownAbsolute(int64_t &Res,
                                      const MCAssembler &Asm) const;

  /// Try to evaluate the expression to a relocatable value, i.e. an
  /// expression of the fixed form (a - b + constant).
  ///
  /// \param Res - The relocatable value, if evaluation succeeds.
  /// \param Asm - The assembler object to use for evaluating values.
  /// \return - True on success.
  LLVM_ABI bool evaluateAsRelocatable(MCValue &Res,
                                      const MCAssembler *Asm) const;

  /// Try to evaluate the expression to the form (a - b + constant) where
  /// neither a nor b are variables.
  ///
  /// This is a more aggressive variant of evaluateAsRelocatable. The intended
  /// use is for when relocations are not available, like the .size directive.
  ///
  /// \param Res - The evaluated value, if evaluation succeeds.
  /// \param Asm - Assembler used to resolve symbols and fragments.
  /// \return True on success.
  LLVM_ABI bool evaluateAsValue(MCValue &Res, const MCAssembler &Asm) const;

  /// Find the fragment associated with this expression.
  ///
  /// The associated section is currently defined as the absolute section for
  /// constants, or otherwise the section associated with the first defined
  /// symbol in the expression.
  ///
  /// \return Associated fragment, or null if none can be determined.
  LLVM_ABI MCFragment *findAssociatedFragment() const;

  /// @}

  /// Add two symbolic relocatable values, folding resolved differences when
  /// possible.
  ///
  /// \param Asm - Optional assembler used to resolve symbols and fragments.
  /// \param InSet - True when evaluating inside a \c .set / assignment
  /// context.
  /// \param LHS - Left-hand relocatable value.
  /// \param RHS - Right-hand relocatable value.
  /// \param Res - Filled with the sum on success.
  /// \return True on success.
  LLVM_ABI static bool evaluateSymbolicAdd(const MCAssembler *Asm, bool InSet,
                                           const MCValue &LHS,
                                           const MCValue &RHS, MCValue &Res);
};

////  Represent a constant integer expression.
class MCConstantExpr : public MCExpr {
  int64_t Value;

  // Subclass data stores SizeInBytes in bits 0..7 and PrintInHex in bit 8.
  static const unsigned SizeInBytesBits = 8;
  static const unsigned SizeInBytesMask = (1 << SizeInBytesBits) - 1;
  static const unsigned PrintInHexBit = 1 << SizeInBytesBits;

  static unsigned encodeSubclassData(bool PrintInHex, unsigned SizeInBytes) {
    assert(SizeInBytes <= sizeof(int64_t) && "Excessive size");
    return SizeInBytes | (PrintInHex ? PrintInHexBit : 0);
  }

  MCConstantExpr(int64_t Value, bool PrintInHex, unsigned SizeInBytes)
      : MCExpr(MCExpr::Constant, SMLoc(),
               encodeSubclassData(PrintInHex, SizeInBytes)), Value(Value) {}

public:
  /// \name Construction
  /// @{

  /// Create a constant integer expression with value \p Value.
  ///
  /// \param Value - Integer value of the expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \param PrintInHex - If true, print the constant in hexadecimal.
  /// \param SizeInBytes - Optional size hint used when printing; 0 means none.
  /// \return Newly created constant expression.
  LLVM_ABI static const MCConstantExpr *create(int64_t Value, MCContext &Ctx,
                                               bool PrintInHex = false,
                                               unsigned SizeInBytes = 0);

  /// @}
  /// \name Accessors
  /// @{

  /// Return the integer value of this constant expression.
  ///
  /// \return Integer value of the expression.
  int64_t getValue() const { return Value; }
  /// Return the optional size-in-bytes printing hint, or 0 if none.
  ///
  /// \return Size-in-bytes printing hint, or 0 if none.
  unsigned getSizeInBytes() const {
    return getSubclassData() & SizeInBytesMask;
  }

  /// Return true if this constant should be printed in hexadecimal.
  ///
  /// \return True if the constant should be printed in hexadecimal.
  bool useHexFormat() const { return (getSubclassData() & PrintInHexBit) != 0; }

  /// @}

  /// Return true if \p E is a constant expression.
  ///
  /// \param E - Expression to test.
  /// \return True if \p E is a constant expression.
  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Constant;
  }
};

///  Represent a reference to a symbol from inside an expression.
///
/// A symbol reference in an expression may be a use of a label, a use of an
/// assembler variable (defined constant), or constitute an implicit definition
/// of the symbol as external.
class MCSymbolRefExpr : public MCExpr {
public:
  // VariantKind isn't ideal for encoding relocation operators because:
  // (a) other expressions, like MCConstantExpr (e.g., 4@l) and MCBinaryExpr
  // (e.g., (a+1)@l), also need it; (b) semantics become unclear (e.g., folding
  // expressions with @). MCSpecifierExpr, as used by AArch64 and RISC-V, offers
  // a cleaner approach.
  /// Relocation or assembler variant applied to a symbol reference.
  enum VariantKind : uint16_t {
    /// COFF image-relative relocation (\c symbol@imgrel).
    VK_COFF_IMGREL32 = 3,

    FirstTargetSpecifier,
  };

private:
  /// The symbol being referenced.
  const MCSymbol *Symbol;

  explicit MCSymbolRefExpr(const MCSymbol *Symbol, Spec specifier, SMLoc Loc);

public:
  /// \name Construction
  /// @{

  /// Create a symbol reference expression for \p Symbol.
  ///
  /// \param Symbol - Symbol being referenced.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \param Loc - Source location of the reference.
  /// \return Newly created symbol reference expression.
  static const MCSymbolRefExpr *create(const MCSymbol *Symbol, MCContext &Ctx,
                                       SMLoc Loc = SMLoc()) {
    return MCSymbolRefExpr::create(Symbol, 0, Ctx, Loc);
  }

  /// Create a symbol reference expression for \p Symbol with relocation
  /// specifier \p specifier.
  ///
  /// \param Symbol - Symbol being referenced.
  /// \param specifier - Relocation or assembler variant specifier.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \param Loc - Source location of the reference.
  /// \return Newly created symbol reference expression.
  LLVM_ABI static const MCSymbolRefExpr *create(const MCSymbol *Symbol,
                                                Spec specifier, MCContext &Ctx,
                                                SMLoc Loc = SMLoc());

  /// @}
  /// \name Accessors
  /// @{

  /// Return the referenced symbol.
  ///
  /// \return Referenced symbol.
  const MCSymbol &getSymbol() const { return *Symbol; }

  // Some targets encode the relocation specifier within SymA using
  // MCSymbolRefExpr::SubclassData, which is copied to MCValue::Specifier,
  // though this method is now deprecated.
  /// Return the relocation variant kind encoded in this symbol reference.
  ///
  /// \return Relocation variant kind encoded in this symbol reference.
  VariantKind getKind() const { return VariantKind(getSubclassData()); }
  /// Return the relocation specifier encoded in this symbol reference.
  ///
  /// \return Relocation specifier encoded in this symbol reference.
  uint16_t getSpecifier() const { return getSubclassData(); }

  /// @}

  /// Return true if \p E is a symbol reference expression.
  ///
  /// \param E - Expression to test.
  /// \return True if \p E is a symbol reference expression.
  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::SymbolRef;
  }
};

/// Unary assembler expressions.
class MCUnaryExpr : public MCExpr {
public:
  /// Unary operator applied to a subexpression.
  enum Opcode {
    LNot,  ///< Logical negation.
    Minus, ///< Unary minus.
    Not,   ///< Bitwise negation.
    Plus   ///< Unary plus.
  };

private:
  const MCExpr *Expr;

  MCUnaryExpr(Opcode Op, const MCExpr *Expr, SMLoc Loc)
      : MCExpr(MCExpr::Unary, Loc, Op), Expr(Expr) {}

public:
  /// \name Construction
  /// @{

  /// Create a unary expression with operator \p Op applied to \p Expr.
  ///
  /// \param Op - Unary operator.
  /// \param Expr - Operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \param Loc - Source location of the expression.
  /// \return Newly created unary expression.
  LLVM_ABI static const MCUnaryExpr *
  create(Opcode Op, const MCExpr *Expr, MCContext &Ctx, SMLoc Loc = SMLoc());

  /// Create a logical-not unary expression of \p Expr.
  ///
  /// \param Expr - Operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \param Loc - Source location of the expression.
  /// \return Newly created logical-not expression.
  static const MCUnaryExpr *createLNot(const MCExpr *Expr, MCContext &Ctx, SMLoc Loc = SMLoc()) {
    return create(LNot, Expr, Ctx, Loc);
  }

  /// Create a unary minus of \p Expr.
  ///
  /// \param Expr - Operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \param Loc - Source location of the expression.
  /// \return Newly created unary-minus expression.
  static const MCUnaryExpr *createMinus(const MCExpr *Expr, MCContext &Ctx, SMLoc Loc = SMLoc()) {
    return create(Minus, Expr, Ctx, Loc);
  }

  /// Create a bitwise-not unary expression of \p Expr.
  ///
  /// \param Expr - Operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \param Loc - Source location of the expression.
  /// \return Newly created bitwise-not expression.
  static const MCUnaryExpr *createNot(const MCExpr *Expr, MCContext &Ctx, SMLoc Loc = SMLoc()) {
    return create(Not, Expr, Ctx, Loc);
  }

  /// Create a unary plus of \p Expr (a no-op in assembly expressions).
  ///
  /// \param Expr - Operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \param Loc - Source location of the expression.
  /// \return Newly created unary-plus expression.
  static const MCUnaryExpr *createPlus(const MCExpr *Expr, MCContext &Ctx, SMLoc Loc = SMLoc()) {
    return create(Plus, Expr, Ctx, Loc);
  }

  /// @}
  /// \name Accessors
  /// @{

  /// Get the kind of this unary expression.
  ///
  /// \return Unary operator of this expression.
  Opcode getOpcode() const { return (Opcode)getSubclassData(); }

  /// Get the child of this unary expression.
  ///
  /// \return Operand subexpression.
  const MCExpr *getSubExpr() const { return Expr; }

  /// @}

  /// Return true if \p E is a unary expression.
  ///
  /// \param E - Expression to test.
  /// \return True if \p E is a unary expression.
  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Unary;
  }
};

/// Binary assembler expressions.
class MCBinaryExpr : public MCExpr {
public:
  /// Binary operation applied to two subexpressions.
  enum Opcode {
    Add,  ///< Addition.
    And,  ///< Bitwise and.
    Div,  ///< Signed division.
    EQ,   ///< Equality comparison.
    GT,   ///< Signed greater than comparison (result is either 0 or some
          ///< target-specific non-zero value)
    GTE,  ///< Signed greater than or equal comparison (result is either 0 or
          ///< some target-specific non-zero value).
    LAnd, ///< Logical and.
    LOr,  ///< Logical or.
    LT,   ///< Signed less than comparison (result is either 0 or
          ///< some target-specific non-zero value).
    LTE,  ///< Signed less than or equal comparison (result is either 0 or
          ///< some target-specific non-zero value).
    Mod,  ///< Signed remainder.
    Mul,  ///< Multiplication.
    NE,   ///< Inequality comparison.
    Or,   ///< Bitwise or.
    OrNot, ///< Bitwise or not.
    Shl,  ///< Shift left.
    AShr, ///< Arithmetic shift right.
    LShr, ///< Logical shift right.
    Sub,  ///< Subtraction.
    Xor   ///< Bitwise exclusive or.
  };

private:
  const MCExpr *LHS, *RHS;

  MCBinaryExpr(Opcode Op, const MCExpr *LHS, const MCExpr *RHS,
               SMLoc Loc = SMLoc())
      : MCExpr(MCExpr::Binary, Loc, Op), LHS(LHS), RHS(RHS) {}

public:
  /// \name Construction
  /// @{

  /// Create a binary expression with operator \p Op applied to \p LHS and
  /// \p RHS.
  ///
  /// \param Op - Binary operator.
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \param Loc - Source location of the expression.
  /// \return Newly created binary expression.
  LLVM_ABI static const MCBinaryExpr *create(Opcode Op, const MCExpr *LHS,
                                             const MCExpr *RHS, MCContext &Ctx,
                                             SMLoc Loc = SMLoc());

  /// Create an addition of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \param Loc - Source location of the expression.
  /// \return Newly created addition expression.
  static const MCBinaryExpr *createAdd(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx, SMLoc Loc = SMLoc()) {
    return create(Add, LHS, RHS, Ctx, Loc);
  }

  /// Create a bitwise AND of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created bitwise AND expression.
  static const MCBinaryExpr *createAnd(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(And, LHS, RHS, Ctx);
  }

  /// Create a signed division of \p LHS by \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created signed division expression.
  static const MCBinaryExpr *createDiv(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Div, LHS, RHS, Ctx);
  }

  /// Create an equality comparison of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created equality comparison expression.
  static const MCBinaryExpr *createEQ(const MCExpr *LHS, const MCExpr *RHS,
                                      MCContext &Ctx) {
    return create(EQ, LHS, RHS, Ctx);
  }

  /// Create a signed greater-than comparison of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created signed greater-than comparison expression.
  static const MCBinaryExpr *createGT(const MCExpr *LHS, const MCExpr *RHS,
                                      MCContext &Ctx) {
    return create(GT, LHS, RHS, Ctx);
  }

  /// Create a signed greater-than-or-equal comparison of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created signed greater-than-or-equal comparison expression.
  static const MCBinaryExpr *createGTE(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(GTE, LHS, RHS, Ctx);
  }

  /// Create a logical AND of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created logical AND expression.
  static const MCBinaryExpr *createLAnd(const MCExpr *LHS, const MCExpr *RHS,
                                        MCContext &Ctx) {
    return create(LAnd, LHS, RHS, Ctx);
  }

  /// Create a logical OR of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created logical OR expression.
  static const MCBinaryExpr *createLOr(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(LOr, LHS, RHS, Ctx);
  }

  /// Create a signed less-than comparison of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created signed less-than comparison expression.
  static const MCBinaryExpr *createLT(const MCExpr *LHS, const MCExpr *RHS,
                                      MCContext &Ctx) {
    return create(LT, LHS, RHS, Ctx);
  }

  /// Create a signed less-than-or-equal comparison of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created signed less-than-or-equal comparison expression.
  static const MCBinaryExpr *createLTE(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(LTE, LHS, RHS, Ctx);
  }

  /// Create a signed remainder of \p LHS divided by \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created signed remainder expression.
  static const MCBinaryExpr *createMod(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Mod, LHS, RHS, Ctx);
  }

  /// Create a multiplication of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created multiplication expression.
  static const MCBinaryExpr *createMul(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Mul, LHS, RHS, Ctx);
  }

  /// Create a not-equal comparison of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created not-equal comparison expression.
  static const MCBinaryExpr *createNE(const MCExpr *LHS, const MCExpr *RHS,
                                      MCContext &Ctx) {
    return create(NE, LHS, RHS, Ctx);
  }

  /// Create a bitwise OR of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created bitwise OR expression.
  static const MCBinaryExpr *createOr(const MCExpr *LHS, const MCExpr *RHS,
                                      MCContext &Ctx) {
    return create(Or, LHS, RHS, Ctx);
  }

  /// Create a left shift of \p LHS by \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created left shift expression.
  static const MCBinaryExpr *createShl(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Shl, LHS, RHS, Ctx);
  }

  /// Create an arithmetic right shift of \p LHS by \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created arithmetic right shift expression.
  static const MCBinaryExpr *createAShr(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(AShr, LHS, RHS, Ctx);
  }

  /// Create a logical right shift of \p LHS by \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created logical right shift expression.
  static const MCBinaryExpr *createLShr(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(LShr, LHS, RHS, Ctx);
  }

  /// Create a subtraction of \p RHS from \p LHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created subtraction expression.
  static const MCBinaryExpr *createSub(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Sub, LHS, RHS, Ctx);
  }

  /// Create a bitwise XOR of \p LHS and \p RHS.
  ///
  /// \param LHS - Left-hand operand expression.
  /// \param RHS - Right-hand operand expression.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \return Newly created bitwise XOR expression.
  static const MCBinaryExpr *createXor(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Xor, LHS, RHS, Ctx);
  }

  /// @}
  /// \name Accessors
  /// @{

  /// Get the kind of this binary expression.
  ///
  /// \return Binary operator of this expression.
  Opcode getOpcode() const { return (Opcode)getSubclassData(); }

  /// Get the left-hand side expression of the binary operator.
  ///
  /// \return Left-hand operand subexpression.
  const MCExpr *getLHS() const { return LHS; }

  /// Get the right-hand side expression of the binary operator.
  ///
  /// \return Right-hand operand subexpression.
  const MCExpr *getRHS() const { return RHS; }

  /// @}

  /// Return true if \p E is a binary expression.
  ///
  /// \param E - Expression to test.
  /// \return True if \p E is a binary expression.
  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Binary;
  }
};

/// Base class for target-specific assembler expressions.
///
/// This can encode a relocation operator, serving as a replacement for
/// MCSymbolRefExpr::VariantKind. Ideally, limit this to
/// top-level use, avoiding its inclusion as a subexpression.
///
/// NOTE: All subclasses are required to have trivial destructors because
/// MCExprs are bump pointer allocated and not destructed.
class LLVM_ABI MCTargetExpr : public MCExpr {
  virtual void anchor();

protected:
  /// Construct a target-specific expression.
  MCTargetExpr() : MCExpr(Target, SMLoc()) {}
  /// Destroy a target-specific expression.
  virtual ~MCTargetExpr() = default;

public:
  /// Print this target expression to \p OS.
  ///
  /// \param OS - Output stream.
  /// \param MAI - Optional assembler info controlling target-specific
  /// printing.
  virtual void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const = 0;
  /// Evaluate this target expression as a relocatable \p Res using \p Asm.
  ///
  /// \param Res - Filled with the relocatable result on success.
  /// \param Asm - Optional assembler used to resolve symbols and fragments.
  /// \return True on success.
  virtual bool evaluateAsRelocatableImpl(MCValue &Res,
                                         const MCAssembler *Asm) const = 0;
  /// Return true if this target expression equals \p x.
  ///
  /// \param x - Expression to compare against.
  /// \return True if the expressions are equal; the default is false.
  virtual bool isEqualTo(const MCExpr *x) const { return false; }
  /// Return true if assigned expressions of this kind must be inlined.
  ///
  /// Set when assigned expressions are not valid ".set" expressions, e.g.
  /// registers, and must be inlined.
  ///
  /// \return True if assigned expressions of this kind must be inlined.
  virtual bool inlineAssignedExpr() const { return false; }
  /// Visit symbols used by this target expression via \p Streamer.
  ///
  /// \param Streamer - Streamer that records used expressions.
  virtual void visitUsedExpr(MCStreamer& Streamer) const = 0;
  /// Find the fragment associated with this target expression.
  ///
  /// \return Associated fragment, or null if none can be determined.
  virtual MCFragment *findAssociatedFragment() const = 0;

  /// Return true if \p E is a target-specific expression.
  ///
  /// \param E - Expression to test.
  /// \return True if \p E is a target-specific expression.
  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
};

/// Expression that applies a relocation specifier to a subexpression.
///
/// Extension point for target-specific MCExpr subclasses with a relocation
/// specifier, serving as a replacement for MCSymbolRefExpr::VariantKind.
/// Limit this to top-level use, avoiding its inclusion as a subexpression.
///
/// NOTE: All subclasses are required to have trivial destructors because
/// MCExprs are bump pointer allocated and not destructed.
class LLVM_ABI MCSpecifierExpr : public MCExpr {
protected:
  /// Subexpression modified by the relocation specifier.
  const MCExpr *Expr;

  /// Construct a specifier expression wrapping \p Expr with specifier \p S.
  ///
  /// \param Expr - Subexpression to wrap.
  /// \param S - Relocation specifier.
  /// \param Loc - Source location of the expression.
  explicit MCSpecifierExpr(const MCExpr *Expr, Spec S, SMLoc Loc = SMLoc())
      : MCExpr(Specifier, Loc, S), Expr(Expr) {}

public:
  /// Create a specifier expression wrapping \p Expr with specifier \p S.
  ///
  /// \param Expr - Subexpression to wrap.
  /// \param S - Relocation specifier.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \param Loc - Source location of the expression.
  /// \return Newly created specifier expression.
  static const MCSpecifierExpr *create(const MCExpr *Expr, Spec S,
                                       MCContext &Ctx, SMLoc Loc = SMLoc());
  /// Create a specifier expression for symbol \p Sym with specifier \p S.
  ///
  /// \param Sym - Symbol to reference.
  /// \param S - Relocation specifier.
  /// \param Ctx - Assembler context used to allocate the expression.
  /// \param Loc - Source location of the expression.
  /// \return Newly created specifier expression.
  static const MCSpecifierExpr *create(const MCSymbol *Sym, Spec S,
                                       MCContext &Ctx, SMLoc Loc = SMLoc());

  /// Return the relocation specifier applied to the subexpression.
  ///
  /// \return Relocation specifier applied to the subexpression.
  Spec getSpecifier() const { return getSubclassData(); }
  /// Return the subexpression modified by the relocation specifier.
  ///
  /// \return Subexpression modified by the relocation specifier.
  const MCExpr *getSubExpr() const { return Expr; }

  /// Return true if \p E is a specifier expression.
  ///
  /// \param E - Expression to test.
  /// \return True if \p E is a specifier expression.
  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Specifier;
  }
};

} // end namespace llvm

#endif // LLVM_MC_MCEXPR_H
