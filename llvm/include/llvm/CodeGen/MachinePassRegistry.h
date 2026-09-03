//===- llvm/CodeGen/MachinePassRegistry.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the mechanics for machine function pass registries.  A
// function pass registry (MachinePassRegistry) is auto filled by the static
// constructors of MachinePassRegistryNode.  Further there is a command line
// parser (RegisterPassParser) which listens to each registry for additions
// and deletions, so that the appropriate command option is updated.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEPASSREGISTRY_H
#define LLVM_CODEGEN_MACHINEPASSREGISTRY_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {

//===----------------------------------------------------------------------===//
///
/// MachinePassRegistryListener - Listener to adds and removals of nodes in
/// registration list.
///
//===----------------------------------------------------------------------===//
template <class PassCtorTy> class MachinePassRegistryListener {
  virtual void anchor() {}

public:
  /// Construct a listener with no associated registry.
  MachinePassRegistryListener() = default;
  /// Destroy this listener.
  virtual ~MachinePassRegistryListener() = default;

  /// Notify that a pass was added to the registry.
  /// \param N Name of the added pass.
  /// \param C Constructor for the added pass.
  /// \param D Description of the added pass.
  virtual void NotifyAdd(StringRef N, PassCtorTy C, StringRef D) = 0;
  /// Notify that a pass was removed from the registry.
  /// \param N Name of the removed pass.
  virtual void NotifyRemove(StringRef N) = 0;
};

//===----------------------------------------------------------------------===//
///
/// MachinePassRegistryNode - Machine pass node stored in registration list.
///
//===----------------------------------------------------------------------===//
template <typename PassCtorTy> class MachinePassRegistryNode {
private:
  MachinePassRegistryNode *Next = nullptr; // Next function pass in list.
  StringRef Name;                       // Name of function pass.
  StringRef Description;                // Description string.
  PassCtorTy Ctor;                      // Pass creator.

public:
  /// Construct a registry node for a named machine pass.
  /// \param N Pass name used on the command line.
  /// \param D Human-readable description of the pass.
  /// \param C Constructor that creates the pass.
  MachinePassRegistryNode(const char *N, const char *D, PassCtorTy C)
      : Name(N), Description(D), Ctor(C) {}

  // Accessors
  /// Return the next node in the registration list.
  /// \return Next node, or null if this is the last node.
  MachinePassRegistryNode *getNext()      const { return Next; }
  /// Return the address of the next-node pointer in this node.
  /// \return Address of the next-node link used when splicing the list.
  MachinePassRegistryNode **getNextAddress()    { return &Next; }
  /// Return the command-line name of this pass.
  /// \return Pass name used on the command line.
  StringRef getName()                   const { return Name; }
  /// Return the human-readable description of this pass.
  /// \return Description string for this pass.
  StringRef getDescription()            const { return Description; }
  /// Return the constructor that creates this pass.
  /// \return Constructor used to create this pass.
  PassCtorTy getCtor() const { return Ctor; }
  /// Set the next node in the registration list.
  /// \param N Node that follows this one, or null.
  void setNext(MachinePassRegistryNode *N)      { Next = N; }
};

//===----------------------------------------------------------------------===//
///
/// MachinePassRegistry - Track the registration of machine passes.
///
//===----------------------------------------------------------------------===//
template <typename PassCtorTy> class MachinePassRegistry {
private:
  MachinePassRegistryNode<PassCtorTy> *List; // List of registry nodes.
  PassCtorTy Default;                        // Default function pass creator.
  MachinePassRegistryListener<PassCtorTy>
      *Listener; // Listener for list adds are removes.

public:
  // NO CONSTRUCTOR - we don't want static constructor ordering to mess
  // with the registry.

  // Accessors.
  //
  /// Return the head of the registration list.
  /// \return Head of the registration list, or null if empty.
  MachinePassRegistryNode<PassCtorTy> *getList() { return List; }
  /// Return the default pass constructor.
  /// \return Default constructor used when no pass name is selected.
  PassCtorTy getDefault() { return Default; }
  /// Set the default pass constructor.
  /// \param C Constructor to use when no pass name is selected.
  void setDefault(PassCtorTy C) { Default = C; }
  /// setDefault - Set the default constructor by name.
  /// \param Name Registered pass name whose constructor becomes the default.
  void setDefault(StringRef Name) {
    PassCtorTy Ctor = nullptr;
    for (MachinePassRegistryNode<PassCtorTy> *R = getList(); R;
         R = R->getNext()) {
      if (R->getName() == Name) {
        Ctor = R->getCtor();
        break;
      }
    }
    assert(Ctor && "Unregistered pass name");
    setDefault(Ctor);
  }
  /// Set the listener notified of registration list changes.
  /// \param L Listener to notify, or null to clear.
  void setListener(MachinePassRegistryListener<PassCtorTy> *L) { Listener = L; }

  /// Add - Adds a function pass to the registration list.
  ///
  /// \param Node Registry node to insert at the head of the list.
  void Add(MachinePassRegistryNode<PassCtorTy> *Node) {
    Node->setNext(List);
    List = Node;
    if (Listener)
      Listener->NotifyAdd(Node->getName(), Node->getCtor(),
                          Node->getDescription());
  }

  /// Remove - Removes a function pass from the registration list.
  ///
  /// \param Node Registry node to remove from the list.
  void Remove(MachinePassRegistryNode<PassCtorTy> *Node) {
    for (MachinePassRegistryNode<PassCtorTy> **I = &List; *I;
         I = (*I)->getNextAddress()) {
      if (*I == Node) {
        if (Listener)
          Listener->NotifyRemove(Node->getName());
        *I = (*I)->getNext();
        break;
      }
    }
  }
};

//===----------------------------------------------------------------------===//
///
/// RegisterPassParser class - Handle the addition of new machine passes.
///
//===----------------------------------------------------------------------===//
template <class RegistryClass>
class RegisterPassParser
    : public MachinePassRegistryListener<
          typename RegistryClass::FunctionPassCtor>,
      public cl::parser<typename RegistryClass::FunctionPassCtor> {
public:
  /// Construct a command-line parser that tracks \p RegistryClass.
  /// \param O Option this parser is bound to.
  RegisterPassParser(cl::Option &O)
      : cl::parser<typename RegistryClass::FunctionPassCtor>(O) {}
  /// Destroy the parser and clear the registry listener.
  ~RegisterPassParser() override { RegistryClass::setListener(nullptr); }

  /// Initialize the option with already-registered passes and listen for more.
  void initialize() {
    cl::parser<typename RegistryClass::FunctionPassCtor>::initialize();

    // Add existing passes to option.
    for (RegistryClass *Node = RegistryClass::getList();
         Node; Node = Node->getNext()) {
      this->addLiteralOption(Node->getName(),
                      (typename RegistryClass::FunctionPassCtor)Node->getCtor(),
                             Node->getDescription());
    }

    // Make sure we listen for list changes.
    RegistryClass::setListener(this);
  }

  // Implement the MachinePassRegistryListener callbacks.
  /// Add a newly registered pass as a literal command-line option.
  /// \param N Name of the added pass.
  /// \param C Constructor for the added pass.
  /// \param D Description of the added pass.
  void NotifyAdd(StringRef N, typename RegistryClass::FunctionPassCtor C,
                 StringRef D) override {
    this->addLiteralOption(N, C, D);
  }
  /// Remove the command-line option for a deregistered pass.
  /// \param N Name of the removed pass.
  void NotifyRemove(StringRef N) override {
    this->removeLiteralOption(N);
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEPASSREGISTRY_H
