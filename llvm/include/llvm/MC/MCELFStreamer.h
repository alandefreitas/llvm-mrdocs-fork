//===- MCELFStreamer.h - MCStreamer ELF Object File Interface ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCELFSTREAMER_H
#define LLVM_MC_MCELFSTREAMER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCObjectStreamer.h"

namespace llvm {

class ELFObjectWriter;
class MCContext;
class MCFragment;
class MCObjectWriter;
class MCSection;
class MCSubtargetInfo;
class MCSymbol;
class MCSymbolRefExpr;
class MCAsmBackend;
class MCCodeEmitter;
class MCExpr;
class MCInst;

/// Streaming ELF object file generation interface.
class LLVM_ABI MCELFStreamer : public MCObjectStreamer {
public:
  /// Construct an ELF object streamer.
  ///
  /// \param Context - MC context that owns symbols and sections.
  /// \param TAB - Assembler backend used for relaxation and object writing.
  /// \param OW - Object writer that emits the ELF object.
  /// \param Emitter - Code emitter used to encode instructions.
  MCELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                std::unique_ptr<MCObjectWriter> OW,
                std::unique_ptr<MCCodeEmitter> Emitter);

  /// Destroy the ELF object streamer.
  ~MCELFStreamer() override = default;

  /// state management
  void reset() override {
    SeenIdent = false;
    MCObjectStreamer::reset();
  }

  /// Return the ELF object writer used by this streamer.
  ///
  /// \return Reference to the ELF object writer.
  ELFObjectWriter &getWriter();

  /// \name MCStreamer Interface
  /// @{

  /// Create the default sections and set the initial one.
  ///
  /// \param STI - Subtarget info used to initialize sections.
  void initSections(const MCSubtargetInfo &STI) override;
  /// Update streamer state for a new active section.
  ///
  /// \param Section - Section being switched to.
  /// \param Subsection - Subsection index within \p Section.
  void changeSection(MCSection *Section, uint32_t Subsection = 0) override;
  /// Emit \p Symbol as a label at the current position.
  ///
  /// \param Symbol - Symbol to define as a label.
  /// \param Loc - Source location for diagnostics.
  void emitLabel(MCSymbol *Symbol, SMLoc Loc = SMLoc()) override;
  /// Emit \p Symbol as a label at \p Offset within fragment \p F.
  ///
  /// \param Symbol - Symbol to define as a label.
  /// \param Loc - Source location for diagnostics.
  /// \param F - Fragment that contains the label.
  /// \param Offset - Byte offset of the label within \p F.
  void emitLabelAtPos(MCSymbol *Symbol, SMLoc Loc, MCFragment &F,
                      uint64_t Offset) override;
  /// Emit a weak reference from \p Alias to \p Target.
  ///
  /// \param Alias - Alias symbol being created.
  /// \param Target - Symbol being weakly referenced.
  void emitWeakReference(MCSymbol *Alias, const MCSymbol *Target) override;
  /// Add the given \p Attribute to \p Symbol.
  ///
  /// \param Symbol - Symbol to attribute.
  /// \param Attribute - Attribute to add.
  /// \return True if the attribute was applied.
  bool emitSymbolAttribute(MCSymbol *Symbol, MCSymbolAttr Attribute) override;
  /// Emit a common symbol.
  ///
  /// \param Symbol - Common symbol to emit.
  /// \param Size - Size of the common symbol in bytes.
  /// \param ByteAlignment - Alignment of the symbol.
  void emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                        Align ByteAlignment) override;

  /// Emit an ELF `.size` directive.
  ///
  /// \param Symbol - Symbol whose size is set.
  /// \param Value - Expression giving the symbol size.
  void emitELFSize(MCSymbol *Symbol, const MCExpr *Value) override;
  /// Emit an ELF `.symver` directive.
  ///
  /// \param OriginalSym - Original symbol being versioned.
  /// \param Name - Versioned name, such as `"foo@@SOME_VERSION"`.
  /// \param KeepOriginalSym - True to also keep the original symbol.
  void emitELFSymverDirective(const MCSymbol *OriginalSym, StringRef Name,
                              bool KeepOriginalSym) override;

  /// Emit a local common (`.lcomm`) symbol.
  ///
  /// \param Symbol - Local common symbol to emit.
  /// \param Size - Size of the symbol in bytes.
  /// \param ByteAlignment - Alignment of the symbol.
  void emitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                             Align ByteAlignment) override;

  /// Emit an `.ident` directive.
  ///
  /// \param IdentString - Identification string to emit.
  void emitIdent(StringRef IdentString) override;

  /// Emit a call-graph profile edge.
  ///
  /// \param From - Caller symbol.
  /// \param To - Callee symbol.
  /// \param Count - Number of calls from \p From to \p To.
  void emitCGProfileEntry(const MCSymbolRefExpr *From,
                          const MCSymbolRefExpr *To, uint64_t Count) override;

  /// Enable aligned instruction bundling with the given bundle size.
  ///
  /// \param Alignment - Bundle size in bytes.
  void emitBundleAlignMode(Align Alignment) override;
  /// Begin a bundle-locked instruction group.
  ///
  /// \param AlignToEnd - True to align the group to the end of a bundle.
  /// \param STI - Subtarget info in effect for the locked group.
  void emitBundleLock(bool AlignToEnd, const MCSubtargetInfo &STI) override;
  /// End a bundle-locked instruction group.
  ///
  /// \param STI - Subtarget info in effect for the locked group.
  void emitBundleUnlock(const MCSubtargetInfo &STI) override;

  /// Perform ELF-specific finalization before finishing the object.
  ///
  /// This override is final. Override \c MCTargetStreamer::finish instead for
  /// target-specific code.
  void finishImpl() final;

  /// ELF object attributes section emission support.
  ///
  /// Holds attributes with their string and/or numeric values so they can be
  /// emitted later in declaration order from a single vector.
  struct AttributeItem {
    /// Encoding kind of the attribute value.
    enum Types {
      HiddenAttribute = 0,      ///< Attribute present but not emitted.
      NumericAttribute,         ///< ULEB128-encoded integer value.
      TextAttribute,            ///< Null-terminated byte string value.
      NumericAndTextAttributes  ///< Both an integer and a string value.
    } Type;                     ///< Value encoding for this attribute.
    unsigned Tag;               ///< Attribute tag number.
    unsigned IntValue;          ///< Integer value for numeric attributes.
    std::string StringValue;    ///< String value for text attributes.
    /// Construct an attribute item with the given type, tag, and values.
    ///
    /// \param Ty - Value encoding for the attribute.
    /// \param Tg - Attribute tag number.
    /// \param IV - Integer value (used for numeric attributes).
    /// \param SV - String value (used for text attributes).
    AttributeItem(Types Ty, unsigned Tg, unsigned IV, std::string SV)
        : Type(Ty), Tag(Tg), IntValue(IV), StringValue(std::move(SV)) {}
  };

  /// ELF object attributes subsection support.
  struct AttributeSubSection {
    bool IsActive; ///< True if this is the currently active subsection.
    StringRef VendorName; ///< Vendor / subsection name (NTBS).
    unsigned IsOptional; ///< 0 = required, 1 = optional.
    unsigned ParameterType; ///< 0 = ULEB128 values, 1 = NTBS values.
    SmallVector<AttributeItem, 64> Content; ///< Attributes in this subsection.
  };

  /// Attributes added and managed entirely by the target.
  SmallVector<AttributeItem, 64> Contents;
  /// Set or overwrite a numeric attribute in \c Contents.
  ///
  /// \param Attribute - Attribute tag number.
  /// \param Value - Integer value of the attribute.
  /// \param OverwriteExisting - True to replace an existing item with the same
  ///        tag.
  void setAttributeItem(unsigned Attribute, unsigned Value,
                        bool OverwriteExisting);
  /// Set or overwrite a text attribute in \c Contents.
  ///
  /// \param Attribute - Attribute tag number.
  /// \param Value - String value of the attribute.
  /// \param OverwriteExisting - True to replace an existing item with the same
  ///        tag.
  void setAttributeItem(unsigned Attribute, StringRef Value,
                        bool OverwriteExisting);
  /// Set or overwrite a numeric-and-text attribute in \c Contents.
  ///
  /// \param Attribute - Attribute tag number.
  /// \param IntValue - Integer value of the attribute.
  /// \param StringValue - String value of the attribute.
  /// \param OverwriteExisting - True to replace an existing item with the same
  ///        tag.
  void setAttributeItems(unsigned Attribute, unsigned IntValue,
                         StringRef StringValue, bool OverwriteExisting);
  /// Emit a compact attributes section from \c Contents for \p Vendor.
  ///
  /// \param Vendor - Vendor name stored in the attributes section.
  /// \param Section - Section name to create or switch to.
  /// \param Type - ELF section type for the attributes section.
  /// \param AttributeSection - Existing attributes section, or null to create
  ///        one; updated on return.
  void emitAttributesSection(StringRef Vendor, const Twine &Section,
                             unsigned Type, MCSection *&AttributeSection) {
    createAttributesSection(Vendor, Section, Type, AttributeSection, Contents);
  }
  /// Emit an attributes section built from vendor subsections.
  ///
  /// \param AttributeSection - Existing attributes section, or null to create
  ///        one; updated on return.
  /// \param Section - Section name to create or switch to.
  /// \param Type - ELF section type for the attributes section.
  /// \param SubSectionVec - Vendor subsections to emit; cleared after emission.
  void
  emitAttributesSection(MCSection *&AttributeSection, const Twine &Section,
                        unsigned Type,
                        SmallVector<AttributeSubSection, 64> &SubSectionVec) {
    createAttributesWithSubsection(AttributeSection, Section, Type,
                                   SubSectionVec);
  }

private:
  AttributeItem *getAttributeItem(unsigned Attribute);
  size_t calculateContentSize(SmallVector<AttributeItem, 64> &AttrsVec) const;
  void createAttributesSection(StringRef Vendor, const Twine &Section,
                               unsigned Type, MCSection *&AttributeSection,
                               SmallVector<AttributeItem, 64> &AttrsVec);
  void createAttributesWithSubsection(
      MCSection *&AttributeSection, const Twine &Section, unsigned Type,
      SmallVector<AttributeSubSection, 64> &SubSectionVec);

  // GNU attributes that will get emitted at the end of the asm file.
  SmallVector<AttributeItem, 64> GNUAttributes;
  MCBoundaryAlignFragment *BundleBA = nullptr;

public:
  /// Emit a `.gnu_attribute` directive.
  ///
  /// \param Tag - GNU attribute tag.
  /// \param Value - Integer value of the attribute.
  void emitGNUAttribute(unsigned Tag, unsigned Value) override {
    AttributeItem Item = {AttributeItem::NumericAttribute, Tag, Value,
                          std::string(StringRef(""))};
    GNUAttributes.push_back(Item);
  }

private:
  void finalizeCGProfileEntry(const MCSymbolRefExpr *Sym, uint64_t Offset,
                              const MCSymbolRefExpr *&S);
  void finalizeCGProfile();

  bool SeenIdent = false;
};

/// Create an ARM ELF object streamer.
///
/// \param Context - MC context that owns symbols and sections.
/// \param TAB - Assembler backend used for relaxation and object writing.
/// \param OW - Object writer that emits the ELF object.
/// \param Emitter - Code emitter used to encode instructions.
/// \param IsThumb - True if assembling Thumb code by default.
/// \param IsAndroid - True if targeting the Android ELF ABI.
/// \return Newly created ARM ELF object streamer.
LLVM_ABI MCELFStreamer *
createARMELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                     std::unique_ptr<MCObjectWriter> OW,
                     std::unique_ptr<MCCodeEmitter> Emitter, bool IsThumb,
                     bool IsAndroid);

} // end namespace llvm

#endif // LLVM_MC_MCELFSTREAMER_H
