//===--- Types.h - Swift Language Type ASTs ---------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file defines the TypeBase class and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_TYPES_H
#define SWIFT_TYPES_H

#include "swift/AST/DeclContext.h"
#include "swift/AST/DefaultArgumentKind.h"
#include "swift/AST/Ownership.h"
#include "swift/AST/Requirement.h"
#include "swift/AST/Type.h"
#include "swift/AST/Identifier.h"
#include "swift/Basic/ArrayRefView.h"
#include "swift/Basic/Fixnum.h"
#include "swift/Basic/Optional.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm {
  struct fltSemantics;
}
namespace swift {
  enum class AllocationArena;
  class ArchetypeType;
  class AssociatedTypeDecl;
  class ASTContext;
  class ClassDecl;
  class ExprHandle;
  class GenericTypeParamDecl;
  class GenericTypeParamType;
  class GenericParam;
  class GenericParamList;
  class GenericSignature;
  class Identifier;
  class SILModule;
  class SILType;
  class TypeAliasDecl;
  class TypeDecl;
  class NominalTypeDecl;
  class EnumDecl;
  class EnumElementDecl;
  class StructDecl;
  class ProtocolDecl;
  class TypeVariableType;
  class ValueDecl;
  class Module;
  class ProtocolConformance;
  class Substitution;
  enum OptionalTypeKind : unsigned;

  enum class TypeKind {
#define TYPE(id, parent) id,
#define TYPE_RANGE(Id, FirstId, LastId) \
  First_##Id##Type = FirstId, Last_##Id##Type = LastId,
#include "swift/AST/TypeNodes.def"
  };


/// Various properties of types that are primarily defined recursively
/// on structural types.
class RecursiveTypeProperties {
public:
  enum { BitWidth = 3 };

  /// A single property.
  ///
  /// Note that the property polarities should be chosen so that 0 is
  /// the correct default value and bitwise-or correctly merges things.
  enum Property : unsigned {
    /// This type expression contains a TypeVariableType.
    HasTypeVariable     = 0x01,

    /// This type expression contains a GenericTypeParamType.
    IsDependent         = 0x02,

    /// This type expression contains an LValueType or InOutType,
    /// other than as a function input.
    IsNotMaterializable = 0x04,
  };

private:
  unsigned Bits : BitWidth;

public:
  RecursiveTypeProperties() : Bits(0) {}
  RecursiveTypeProperties(Property prop) : Bits(prop) {}
  explicit RecursiveTypeProperties(unsigned bits) : Bits(bits) {}

  /// Return these properties as a bitfield.
  unsigned getBits() const { return Bits; }

  /// Does a type with these properties structurally contain a type
  /// variable?
  bool hasTypeVariable() const { return Bits & HasTypeVariable; }

  /// Is a type with these properties dependent, in the sense of being
  /// expressed in terms of a generic type parameter or a dependent
  /// member thereof?
  bool isDependent() const { return Bits & IsDependent; }

  /// Is a type with these properties materializable: that is, is it a
  /// first-class value type?
  bool isMaterializable() const { return !(Bits & IsNotMaterializable); }

  /// Returns the set of properties present in either set.
  friend RecursiveTypeProperties operator+(Property lhs, Property rhs) {
    return RecursiveTypeProperties(lhs | rhs);
  }
  friend RecursiveTypeProperties operator+(RecursiveTypeProperties lhs,
                                           RecursiveTypeProperties rhs) {
    return RecursiveTypeProperties(lhs.Bits | rhs.Bits);
  }

  /// Add any properties in the right-hand set to this set.
  RecursiveTypeProperties &operator+=(RecursiveTypeProperties other) {
    Bits |= other.Bits;
    return *this;
  }

  /// Returns the set of properties present in the left-hand set but
  /// missing in the right-hand set.
  RecursiveTypeProperties operator-(RecursiveTypeProperties other) {
    return RecursiveTypeProperties(Bits & ~other.Bits);
  }

  /// Remove any properties in the right-hand set from this set.
  RecursiveTypeProperties &operator-=(RecursiveTypeProperties other) {
    Bits &= ~other.Bits;
    return *this;
  }

  /// Test for a particular property in this set.
  bool operator&(Property prop) const {
    return Bits & prop;
  }
};
  
/// The result of a type trait check.
enum class TypeTraitResult {
  /// The type cannot have the trait.
  IsNot,
  /// The generic type can be bound to a type that has the trait.
  CanBe,
  /// The type has the trait irrespective of generic substitutions.
  Is,
};

/// TypeBase - Base class for all types in Swift.
class alignas(8) TypeBase {
  // alignas(8) because we need three tag bits on Type.
  
  friend class ASTContext;
  TypeBase(const TypeBase&) = delete;
  void operator=(const TypeBase&) = delete;
  
  /// CanonicalType - This field is always set to the ASTContext for canonical
  /// types, and is otherwise lazily populated by ASTContext when the canonical
  /// form of a non-canonical type is requested.
  llvm::PointerUnion<TypeBase *, const ASTContext *> CanonicalType;

  /// Kind - The discriminator that indicates what subclass of type this is.
  const TypeKind Kind;

  struct TypeBaseBitfields {
    unsigned Properties : RecursiveTypeProperties::BitWidth;
  };

  enum { NumTypeBaseBits = RecursiveTypeProperties::BitWidth };
  static_assert(NumTypeBaseBits <= 32, "fits in an unsigned");

protected:
  struct AnyFunctionTypeBitfields {
    unsigned : NumTypeBaseBits;

    /// Extra information which affects how the function is called, like
    /// regparm and the calling convention.
    unsigned ExtInfo : 8;
  };
  enum { NumAnyFunctionTypeBits = NumTypeBaseBits + 8 };
  static_assert(NumAnyFunctionTypeBits <= 32, "fits in an unsigned");

  struct TypeVariableTypeBitfields {
    unsigned : NumTypeBaseBits;

    /// \brief The unique number assigned to this type variable.
    unsigned ID : 32 - NumTypeBaseBits;
  };
  enum { NumTypeVariableTypeBits = NumTypeBaseBits + (32 - NumTypeBaseBits) };
  static_assert(NumTypeVariableTypeBits <= 32, "fits in an unsigned");

  struct SILFunctionTypeBitfields {
    unsigned : NumTypeBaseBits;
    unsigned ExtInfo : 8;
    unsigned CalleeConvention : 3;
    unsigned NumParameters : 32 - 11 - NumTypeBaseBits;
  };

  struct AnyMetatypeTypeBitfields {
    unsigned : NumTypeBaseBits;
    /// The representation of the metatype.
    ///
    /// Zero indicates that no representation has been set; otherwise,
    /// the value is the representation + 1
    unsigned Representation : 2;
  };
  enum { NumAnyMetatypeTypeBits = NumTypeBaseBits + 2 };
  static_assert(NumAnyMetatypeTypeBits <= 32, "fits in an unsigned");
  
  union {
    TypeBaseBitfields TypeBaseBits;
    AnyFunctionTypeBitfields AnyFunctionTypeBits;
    TypeVariableTypeBitfields TypeVariableTypeBits;
    SILFunctionTypeBitfields SILFunctionTypeBits;
    AnyMetatypeTypeBitfields AnyMetatypeTypeBits;
  };

protected:
  TypeBase(TypeKind kind, const ASTContext *CanTypeCtx,
           RecursiveTypeProperties properties)
    : CanonicalType((TypeBase*)nullptr), Kind(kind) {
    // If this type is canonical, switch the CanonicalType union to ASTContext.
    if (CanTypeCtx)
      CanonicalType = CanTypeCtx;
    setRecursiveProperties(properties);
  }

  void setRecursiveProperties(RecursiveTypeProperties properties) {
    TypeBaseBits.Properties = properties.getBits();
  }

public:
  /// getKind - Return what kind of type this is.
  TypeKind getKind() const { return Kind; }

  /// isCanonical - Return true if this is a canonical type.
  bool isCanonical() const { return CanonicalType.is<const ASTContext *>(); }
  
  /// hasCanonicalTypeComputed - Return true if we've already computed a
  /// canonical version of this type.
  bool hasCanonicalTypeComputed() const { return !CanonicalType.isNull(); }
  
  /// getCanonicalType - Return the canonical version of this type, which has
  /// sugar from all levels stripped off.
  CanType getCanonicalType();

  /// getASTContext - Return the ASTContext that this type belongs to.
  ASTContext &getASTContext() {
    // If this type is canonical, it has the ASTContext in it.
    if (CanonicalType.is<const ASTContext *>())
      return const_cast<ASTContext&>(*CanonicalType.get<const ASTContext *>());
    // If not, canonicalize it to get the Context.
    return const_cast<ASTContext&>(*getCanonicalType()->
                                   CanonicalType.get<const ASTContext *>());
  }
  
  /// isEqual - Return true if these two types are equal, ignoring sugar.
  bool isEqual(Type Other);
  
  /// isSpelledLike - Return true if these two types share a sugared spelling.
  bool isSpelledLike(Type Other);
  
  /// getDesugaredType - If this type is a sugared type, remove all levels of
  /// sugar until we get down to a non-sugar type.
  TypeBase *getDesugaredType();
  
  /// If this type is a (potentially sugared) type of the specified kind, remove
  /// the minimal amount of sugar required to get a pointer to the type.
  template <typename T>
  T *getAs() {
    return dyn_cast<T>(getDesugaredType());
  }

  template <typename T>
  bool is() {
    return isa<T>(getDesugaredType());
  }
  
  template <typename T>
  T *castTo() {
    return cast<T>(getDesugaredType());
  }

  /// getRecursiveProperties - Returns the properties defined on the
  /// structure of this type.
  RecursiveTypeProperties getRecursiveProperties() const {
    return RecursiveTypeProperties(TypeBaseBits.Properties);
  }

  /// isMaterializable - Is this type 'materializable' according to
  /// the rules of the language?  Basically, does it not contain any
  /// l-value types?
  bool isMaterializable() const {
    return getRecursiveProperties().isMaterializable();
  }

  /// hasReferenceSemantics() - Do objects of this type have reference
  /// semantics?
  bool hasReferenceSemantics();
  
  /// allowsOwnership() - Are variables of this type permitted to have
  /// ownership attributes?
  bool allowsOwnership();  

  /// \brief Determine whether this type involves a type variable.
  bool hasTypeVariable() const {
    return getRecursiveProperties().hasTypeVariable();
  }

  /// \brief Compute and return the set of type variables that occur within this
  /// type.
  ///
  /// \param typeVariables This vector is populated with the set of
  /// type variables referenced by this type.
  void getTypeVariables(SmallVectorImpl<TypeVariableType *> &typeVariables);

  /// Determine whether the type is directly dependent on a generic type
  /// parameter.
  ///
  /// Unlike the C++ notion of "dependent", for which nearly any occurrence of
  /// a generic parameter within the type causes the type to be dependent,
  /// the Swift definition of "dependent" is fairly shallow: we either have
  /// a generic parameter or a member of that generic parameter. Types such
  /// as X<T>, where T is a generic parameter, are not considered "dependent".
  bool isDependentType() {
    return getRecursiveProperties().isDependent();
  }

  /// isExistentialType - Determines whether this type is an existential type,
  /// whose real (runtime) type is unknown but which is known to conform to
  /// some set of protocols. Protocol and protocol-conformance types are
  /// existential types.
  bool isExistentialType();

  /// isAnyExistentialType - Determines whether this type is any kind of
  /// existential type: a protocol type, a protocol composition type, or
  /// an existential metatype.
  bool isAnyExistentialType();

  /// isObjCExistentialType - Determines whether this type is an
  /// class-bounded existential type whose required conformances are
  /// all @objc.  Such types are compatible with ObjC.
  bool isObjCExistentialType();
  
  /// Determines whether this type is an existential type with a class protocol
  /// bound.
  bool isClassExistentialType();

  /// isExistentialType - Determines whether this type is an existential type,
  /// whose real (runtime) type is unknown but which is known to conform to
  /// some set of protocols. Protocol and protocol-conformance types are
  /// existential types.
  ///
  /// \param Protocols If the type is an existential type, this vector is
  /// populated with the set of protocols
  bool isExistentialType(SmallVectorImpl<ProtocolDecl *> &Protocols);

  /// isAnyExistentialType - Determines whether this type is any kind of
  /// existential type: a protocol type, a protocol composition type, or
  /// an existential metatype.
  ///
  /// \param protocols If the type is an existential type, this vector is
  /// populated with the set of protocols.
  bool isAnyExistentialType(SmallVectorImpl<ProtocolDecl *> &protocols);

  /// Given that this type is any kind of existential type, produce
  /// its list of protocols.
  void getAnyExistentialTypeProtocols(SmallVectorImpl<ProtocolDecl *> &protocols);

  /// \brief Determine whether the given type is "specialized", meaning that
  /// it involves generic types for which generic arguments have been provided.
  /// For example, the types Vector<Int> and Vector<Int>.Element are both
  /// specialized, but the type Vector is not.
  bool isSpecialized();

  /// Gather all of the substitutions used to produce the given specialized type
  /// from its unspecialized type.
  ///
  /// \param scratchSpace The substitutions will be written into this scratch
  /// space if a single substitutions array cannot be returned.
  ArrayRef<Substitution> gatherAllSubstitutions(
                           Module *module,
                           SmallVectorImpl<Substitution> &scratchSpace,
                           LazyResolver *resolver);

  /// Gather all of the substitutions used to produce the given specialized type
  /// from its unspecialized type.
  ///
  /// \returns ASTContext-allocated substitutions.
  ArrayRef<Substitution> gatherAllSubstitutions(
                           Module *module,
                           LazyResolver *resolver);

  /// \brief Determine whether the given type is "generic", meaning that
  /// it involves generic types for which generic arguments have not been
  /// provided.
  /// For example, the type Vector and Vector<Int>.InnerGeneric are both
  /// unspecialized generic, but the type Vector<Int> is not.
  bool isUnspecializedGeneric();

  /// \brief Determine whether this type is a legal, lowered SIL type.
  ///
  /// A type is SIL-illegal if it is:
  ///   - an l-value type,
  ///   - an AST function type (i.e. subclasses of AnyFunctionType), or
  ///   - a tuple type with a SIL-illegal element type.
  bool isLegalSILType();

  /// \brief Check if this type is equal to the empty tuple type.
  bool isVoid();

  /// \brief Check if this type is equal to Builtin.IntN.
  bool isBuiltinIntegerType(unsigned bitWidth);

  /// \brief If this is a class type or a bound generic class type, returns the
  /// (possibly generic) class.
  ClassDecl *getClassOrBoundGenericClass();
  
  /// \brief If this is a struct type or a bound generic struct type, returns
  /// the (possibly generic) class.
  StructDecl *getStructOrBoundGenericStruct();
  
  /// \brief If this is an enum or a bound generic enum type, returns the
  /// (possibly generic) enum.
  EnumDecl *getEnumOrBoundGenericEnum();
  
  /// \brief Determine whether this type may have a superclass, which holds for
  /// classes, bound generic classes, and archetypes that are only instantiable
  /// with a class type.
  bool mayHaveSuperclass();

  /// \brief Retrieve the superclass of this type.
  ///
  /// \param resolver The resolver for lazy type checking, or null if the
  ///                 AST is already type-checked.
  ///
  /// \returns The superclass of this type, or a null type if it has no
  ///          superclass.
  Type getSuperclass(LazyResolver *resolver);
  
  /// \brief True if this type is a superclass of another type.
  ///
  /// \param ty       The potential subclass.
  /// \param resolver The resolver for lazy type checking, or null if the
  ///                 AST is already type-checked.
  ///
  /// \returns True if this type is \c ty or a superclass of \c ty.
  bool isSuperclassOf(Type ty, LazyResolver *resolver);

  /// \brief Determines whether this type is a subtype of the \p other,
  /// guaranteed to have the same representation, and is permitted in an
  /// override.
  bool canOverride(Type other, bool allowUnsafeParameterOverride,
                   LazyResolver *resolver);

  /// \brief Determines whether this type has a retainable pointer
  /// representation, i.e. whether it is representable as a single,
  /// possibly nil pointer that can be unknown-retained and
  /// unknown-released.
  bool hasRetainablePointerRepresentation();

  /// \brief If this is a nominal type or a bound generic nominal type,
  /// returns the (possibly generic) nominal type declaration.
  NominalTypeDecl *getNominalOrBoundGenericNominal();

  /// \brief If this is a nominal type, bound generic nominal type, or
  /// unbound generic nominal type, return the (possibly generic) nominal type
  /// declaration.
  NominalTypeDecl *getAnyNominal();

  /// getUnlabeledType - Retrieve a version of this type with all labels
  /// removed at every level. For example, given a tuple type 
  /// \code
  /// (p : (x : int, y : int))
  /// \endcode
  /// the result would be the (parenthesized) type ((int, int)).
  Type getUnlabeledType(ASTContext &Context);

  /// \brief Retrieve the type without any default arguments.
  Type getWithoutDefaultArgs(const ASTContext &Context);

  /// Replace the base type of the result type of the given function
  /// type with a new result type, as per a DynamicSelf or other
  /// covariant return transformation.  The optionality of the
  /// existing result will be preserved.
  ///
  /// \param newResultType The new result type.
  ///
  /// \param uncurryLevel The number of uncurry levels to apply before
  /// replacing the type. With uncurry level == 0, this simply
  /// replaces the current type with the new result type.
  Type replaceCovariantResultType(Type newResultType,
                                  unsigned uncurryLevel = 1);

  /// Returns a function type that is not 'noreturn', but is otherwis the same
  /// as this type.
  Type getWithoutNoReturn(unsigned UncurryLevel);

  /// getRValueType - For an @lvalue type, retrieves the underlying object type.
  /// Otherwise, returns the type itself.
  Type getRValueType();

  /// getInOutObjectType - For an inout type, retrieves the underlying object
  /// type.  Otherwise, returns the type itself.
  Type getInOutObjectType();

  /// getLValueOrInOutObjectType - For an @lvalue or inout type, retrieves the
  /// underlying object type. Otherwise, returns the type itself.
  Type getLValueOrInOutObjectType();

  /// Retrieves the rvalue instance type, looking through single-element
  /// tuples, lvalue types, and metatypes.
  Type getRValueInstanceType();

  /// For a ReferenceStorageType like @unowned, this returns the referent.
  /// Otherwise, it returns the type itself.
  Type getReferenceStorageReferent();

  /// Retrieve the type of the given member as seen through the given base
  /// type, substituting generic arguments where necessary.
  ///
  /// This routine allows one to take a concrete type (the "this" type) and
  /// and a member of that type (or one of its superclasses), then determine
  /// what type an access to that member through the base type will have.
  /// For example, given:
  ///
  /// \code
  /// class Vector<T> {
  ///   func add(value : T) { }
  /// }
  /// \endcode
  ///
  /// Given the type \c Vector<Int> and the member \c add, the resulting type
  /// of the member will be \c (self : Vector<Int>) -> (value : Int) -> ().
  ///
  /// \param module The module in which the substitution occurs.
  ///
  /// \param member The member whose type we are substituting.
  ///
  /// \param resolver The resolver for lazy type checking, which may be null for
  /// a fully-type-checked AST.
  ///
  /// \param memberType The type of the member, in which archetypes will be
  /// replaced by the generic arguments provided by the base type. If null,
  /// the member's type will be used.
  ///
  /// \returns the resulting member type.
  Type getTypeOfMember(Module *module, const ValueDecl *member,
                       LazyResolver *resolver, Type memberType = Type());


  /// Return T if this type is Optional<T>; otherwise, return the null type.
  Type getOptionalObjectType();

  /// Return T if this type is UncheckedOptional<T>; otherwise, return
  /// the null type.
  Type getUncheckedOptionalObjectType();

  /// Return T if this type is Optional<T> or UncheckedOptional<T>;
  /// otherwise, return the null type.
  Type getAnyOptionalObjectType(OptionalTypeKind &kind);
  Type getAnyOptionalObjectType() {
    OptionalTypeKind ignored;
    return getAnyOptionalObjectType(ignored);
  }

  void dump() const;
  void print(raw_ostream &OS,
             const PrintOptions &PO = PrintOptions()) const;
  void print(ASTPrinter &Printer, const PrintOptions &PO) const;

  /// Return the name of the type as a string, for use in diagnostics only.
  std::string getString(const PrintOptions &PO = PrintOptions()) const;
  
  /// Return whether this type is or can be substituted for an @objc class type.
  TypeTraitResult canBeObjCClass();
private:
  // Make vanilla new/delete illegal for Types.
  void *operator new(size_t Bytes) throw() = delete;
  void operator delete(void *Data) throw() = delete;
public:
  // Only allow allocation of Types using the allocator in ASTContext
  // or by doing a placement new.
  void *operator new(size_t bytes, const ASTContext &ctx,
                     AllocationArena arena, unsigned alignment = 8);
  void *operator new(size_t Bytes, void *Mem) throw() { return Mem; }
};

/// ErrorType - This represents a type that was erroneously constructed.  This
/// is produced when parsing types and when name binding type aliases, and is
/// installed in declaration that use these erroneous types.  All uses of a
/// declaration of invalid type should be ignored and not re-diagnosed.
class ErrorType : public TypeBase {
  friend class ASTContext;
  // The Error type is always canonical.
  ErrorType(ASTContext &C) 
    : TypeBase(TypeKind::Error, &C, RecursiveTypeProperties()) { }
public:
  static Type get(const ASTContext &C);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Error;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(ErrorType, Type)

/// BuiltinType - An abstract class for all the builtin types.
class BuiltinType : public TypeBase {
protected:
  BuiltinType(TypeKind kind, const ASTContext &canTypeCtx)
  : TypeBase(kind, &canTypeCtx, RecursiveTypeProperties()) {}
public:
  static bool classof(const TypeBase *T) {
    return T->getKind() >= TypeKind::First_BuiltinType &&
           T->getKind() <= TypeKind::Last_BuiltinType;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(BuiltinType, Type)

/// BuiltinRawPointerType - The builtin raw (and dangling) pointer type.  This
/// pointer is completely unmanaged and is equivalent to i8* in LLVM IR.
class BuiltinRawPointerType : public BuiltinType {
  friend class ASTContext;
  BuiltinRawPointerType(const ASTContext &C)
    : BuiltinType(TypeKind::BuiltinRawPointer, C) {}
public:
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::BuiltinRawPointer;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(BuiltinRawPointerType, BuiltinType);

/// BuiltinObjectPointerType - The builtin opaque object-pointer type.
/// Useful for keeping an object alive when it is otherwise being
/// manipulated via an unsafe pointer type.
class BuiltinObjectPointerType : public BuiltinType {
  friend class ASTContext;
  BuiltinObjectPointerType(const ASTContext &C)
    : BuiltinType(TypeKind::BuiltinObjectPointer, C) {}
public:
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::BuiltinObjectPointer;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(BuiltinObjectPointerType, BuiltinType);

/// BuiltinObjCPointerType - The builtin opaque Objective-C pointer type.
/// Useful for pushing an Objective-C type through swift.
class BuiltinObjCPointerType : public BuiltinType {
  friend class ASTContext;
  BuiltinObjCPointerType(const ASTContext &C)
    : BuiltinType(TypeKind::BuiltinObjCPointer, C) {}
public:
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::BuiltinObjCPointer;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(BuiltinObjCPointerType, BuiltinType);

/// \brief A builtin vector type.
class BuiltinVectorType : public BuiltinType, public llvm::FoldingSetNode {
  Type elementType;
  unsigned numElements;

  friend class ASTContext;

  BuiltinVectorType(const ASTContext &context, Type elementType,
                    unsigned numElements)
    : BuiltinType(TypeKind::BuiltinVector, context),
      elementType(elementType), numElements(numElements) { }

public:
  static BuiltinVectorType *get(const ASTContext &context, Type elementType,
                                unsigned numElements);

  /// \brief Retrieve the type of this vector's elements.
  Type getElementType() const { return elementType; }

  /// \brief Retrieve the number of elements in this vector.
  unsigned getNumElements() const { return numElements; }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getElementType(), getNumElements());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, Type elementType,
                      unsigned numElements) {
    ID.AddPointer(elementType.getPointer());
    ID.AddInteger(numElements);
  }

  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::BuiltinVector;
  }
};
BEGIN_CAN_TYPE_WRAPPER(BuiltinVectorType, BuiltinType)
  PROXY_CAN_TYPE_SIMPLE_GETTER(getElementType)
END_CAN_TYPE_WRAPPER(BuiltinVectorType, BuiltinType)

/// Size descriptor for a builtin integer type. This is either a fixed bit
/// width or an abstract target-dependent value such as "size of a pointer".
class BuiltinIntegerWidth {
  /// Tag values for abstract integer sizes.
  enum : unsigned {
    Least_SpecialValue = ~2U,
    /// The size of a pointer on the target system.
    PointerWidth = ~0U,
    
    /// Inhabitants stolen for use as DenseMap special values.
    DenseMapEmpty = ~1U,
    DenseMapTombstone = ~2U,
  };
  
  unsigned RawValue;
  
  friend struct llvm::DenseMapInfo<swift::BuiltinIntegerWidth>;
  
  /// Private constructor from a raw symbolic value.
  explicit BuiltinIntegerWidth(unsigned RawValue) : RawValue(RawValue) {}
public:
  BuiltinIntegerWidth() : RawValue(0) {}
  
  static BuiltinIntegerWidth fixed(unsigned bitWidth) {
    assert(bitWidth < Least_SpecialValue && "invalid bit width");
    return BuiltinIntegerWidth(bitWidth);
  }
  
  static BuiltinIntegerWidth pointer() {
    return BuiltinIntegerWidth(PointerWidth);
  }
  
  /// Is this a fixed width?
  bool isFixedWidth() const { return RawValue < Least_SpecialValue; }

  /// Get the fixed width value. Fails if the width is abstract.
  unsigned getFixedWidth() const {
    assert(isFixedWidth() && "not fixed-width");
    return RawValue;
  }
  
  /// Is this the abstract target pointer width?
  bool isPointerWidth() const { return RawValue == PointerWidth; }
  
  /// Get the least supported value for the width.
  ///
  /// FIXME: This should be build-configuration-dependent.
  unsigned getLeastWidth() const {
    if (isFixedWidth())
      return getFixedWidth();
    if (isPointerWidth())
      return 32;
    llvm_unreachable("impossible width value");
  }
  
  /// Get the greatest supported value for the width.
  ///
  /// FIXME: This should be build-configuration-dependent.
  unsigned getGreatestWidth() const {
    if (isFixedWidth())
      return getFixedWidth();
    if (isPointerWidth())
      return 64;
    llvm_unreachable("impossible width value");
  }
  
  friend bool operator==(BuiltinIntegerWidth a, BuiltinIntegerWidth b) {
    return a.RawValue == b.RawValue;
  }
  friend bool operator!=(BuiltinIntegerWidth a, BuiltinIntegerWidth b) {
    return a.RawValue != b.RawValue;
  }
};

/// The builtin integer types.  These directly correspond
/// to LLVM IR integer types.  They lack signedness and have an arbitrary
/// bitwidth.
class BuiltinIntegerType : public BuiltinType {
  friend class ASTContext;
private:
  BuiltinIntegerWidth Width;
  BuiltinIntegerType(BuiltinIntegerWidth BitWidth, const ASTContext &C)
    : BuiltinType(TypeKind::BuiltinInteger, C), Width(BitWidth) {}
  
public:
  /// Get a builtin integer type.
  static BuiltinIntegerType *get(BuiltinIntegerWidth BitWidth,
                                 const ASTContext &C);
  
  /// Get a builtin integer type of fixed width.
  static BuiltinIntegerType *get(unsigned BitWidth, const ASTContext &C) {
    return get(BuiltinIntegerWidth::fixed(BitWidth), C);
  }
  
  /// Get the target-pointer-width builtin integer type.
  static BuiltinIntegerType *getWordType(const ASTContext &C) {
    return get(BuiltinIntegerWidth::pointer(), C);
  }
  
  /// Return the bit width of the integer.
  BuiltinIntegerWidth getWidth() const {
    return Width;
  }
  
  /// Is the integer fixed-width?
  bool isFixedWidth() const {
    return Width.isFixedWidth();
  }
  
  /// Is the integer fixed-width with the given width?
  bool isFixedWidth(unsigned width) const {
    return Width.isFixedWidth() && Width.getFixedWidth() == width;
  }
  
  /// Get the fixed integer width. Fails if the integer has abstract width.
  unsigned getFixedWidth() const {
    return Width.getFixedWidth();
  }
  
  /// Return the least supported width of the integer.
  ///
  /// FIXME: This should be build-configuration-dependent.
  unsigned getLeastWidth() const {
    return Width.getLeastWidth();
  }
  
  /// Return the greatest supported width of the integer.
  ///
  /// FIXME: This should be build-configuration-dependent.
  unsigned getGreatestWidth() const {
    return Width.getGreatestWidth();
  }

  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::BuiltinInteger;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(BuiltinIntegerType, BuiltinType)
  
class BuiltinFloatType : public BuiltinType {
  friend class ASTContext;
public:
  enum FPKind {
    IEEE16, IEEE32, IEEE64, IEEE80, IEEE128, /// IEEE floating point types.
    PPC128   /// PowerPC "double double" type.
  };
private:
  FPKind Kind;
  
  BuiltinFloatType(FPKind Kind, const ASTContext &C)
    : BuiltinType(TypeKind::BuiltinFloat, C), Kind(Kind) {}
public:
  
  /// getFPKind - Get the 
  FPKind getFPKind() const {
    return Kind;
  }

  const llvm::fltSemantics &getAPFloatSemantics() const;

  unsigned getBitWidth() const {
    switch (Kind) {
    case IEEE16: return 16;
    case IEEE32: return 32;
    case IEEE64: return 64;
    case IEEE80: return 80;
    case IEEE128:
    case PPC128: return 128;
    }
  }

  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::BuiltinFloat;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(BuiltinFloatType, BuiltinType)
  
/// NameAliasType - An alias type is a name for another type, just like a
/// typedef in C.
class NameAliasType : public TypeBase {
  friend class TypeAliasDecl;
  // NameAliasType are never canonical.
  NameAliasType(TypeAliasDecl *d) 
    : TypeBase(TypeKind::NameAlias, nullptr, RecursiveTypeProperties()),
      TheDecl(d) {}
  TypeAliasDecl *const TheDecl;

public:
  TypeAliasDecl *getDecl() const { return TheDecl; }

  using TypeBase::setRecursiveProperties;
   
  /// Remove one level of top-level sugar from this type.
  TypeBase *getSinglyDesugaredType();

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::NameAlias;
  }
};

/// ParenType - A paren type is a type that's been written in parentheses.
class ParenType : public TypeBase {
  Type UnderlyingType;

  friend class ASTContext;
  ParenType(Type UnderlyingType, RecursiveTypeProperties properties)
    : TypeBase(TypeKind::Paren, nullptr, properties),
      UnderlyingType(UnderlyingType) {}
public:
  Type getUnderlyingType() const { return UnderlyingType; }

  static ParenType *get(const ASTContext &C, Type underlying);
   
  /// Remove one level of top-level sugar from this type.
  TypeBase *getSinglyDesugaredType();

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Paren;
  }
};

/// TupleTypeElt - This represents a single element of a tuple.
class TupleTypeElt {
  /// An optional name for the field.
  Identifier Name;

  /// Describes whether this element is variadic or what kind of default
  /// argument it stores.
  enum class DefaultArgOrVarArg : uint8_t {
    /// Neither variadic nor a default argument.
    None,
    /// Variadic.
    VarArg,
    /// It has a normal default argument.
    DefaultArgument,
    /// It has an inherited default argument.
    InheritedDefaultArgument,
    /// It has a caller-provided __FILE__ default argument.
    FileArgument,
    /// It has a caller-provided __LINE__ default argument.
    LineArgument,
    /// It has a caller-provided __COLUMN__ default argument.
    ColumnArgument,
    /// It has a caller-provided __FUNCTION__ default argument.
    FunctionArgument,
  };

  /// \brief This is the type of the field, which is mandatory, along with a bit
  /// indicating whether this is a vararg.
  llvm::PointerIntPair<Type, 3, DefaultArgOrVarArg> TyAndDefaultOrVarArg;

  friend class TupleType;

public:
  TupleTypeElt() = default;
  inline /*implicit*/ TupleTypeElt(Type ty,
                                   Identifier name = Identifier(),
                                   DefaultArgumentKind defaultArg =
                                     DefaultArgumentKind::None,
                                   bool isVarArg = false);

  /*implicit*/ TupleTypeElt(TypeBase *Ty)
    : Name(Identifier()), TyAndDefaultOrVarArg(Ty, DefaultArgOrVarArg::None) { }

  bool hasName() const { return !Name.empty(); }
  Identifier getName() const { return Name; }

  Type getType() const { return TyAndDefaultOrVarArg.getPointer(); }

  /// Determine whether this field is variadic.
  bool isVararg() const {
    return TyAndDefaultOrVarArg.getInt() == DefaultArgOrVarArg::VarArg;
  }

  /// Retrieve the kind of default argument available on this field.
  DefaultArgumentKind getDefaultArgKind() const {
    switch (TyAndDefaultOrVarArg.getInt()) {
    case DefaultArgOrVarArg::None:
    case DefaultArgOrVarArg::VarArg:
      return DefaultArgumentKind::None;
    case DefaultArgOrVarArg::DefaultArgument:
      return DefaultArgumentKind::Normal;
    case DefaultArgOrVarArg::InheritedDefaultArgument:
      return DefaultArgumentKind::Inherited;
    case DefaultArgOrVarArg::FileArgument:
      return DefaultArgumentKind::File;
    case DefaultArgOrVarArg::LineArgument:
      return DefaultArgumentKind::Line;
    case DefaultArgOrVarArg::ColumnArgument:
      return DefaultArgumentKind::Column;
    case DefaultArgOrVarArg::FunctionArgument:
      return DefaultArgumentKind::Function;
    }
  }

  inline Type getVarargBaseTy() const;

  /// Retrieve a copy of this tuple type element with the type replaced.
  TupleTypeElt getWithType(Type T) const {
    return TupleTypeElt(T, getName(), getDefaultArgKind(), isVararg());
  }

  /// Determine whether this tuple element has an initializer.
  bool hasInit() const {
    return getDefaultArgKind() != DefaultArgumentKind::None;
  }
};

inline Type getTupleEltType(const TupleTypeElt &elt) {
  return elt.getType();
}
typedef ArrayRefView<TupleTypeElt,Type,getTupleEltType> TupleEltTypeArrayRef;

inline CanType getCanTupleEltType(const TupleTypeElt &elt) {
  return CanType(elt.getType());
}
typedef ArrayRefView<TupleTypeElt,CanType,getCanTupleEltType>
  CanTupleEltTypeArrayRef;

/// TupleType - A tuple is a parenthesized list of types where each name has an
/// optional name.
///
class TupleType : public TypeBase, public llvm::FoldingSetNode {
  const ArrayRef<TupleTypeElt> Fields;
  
public:
  /// get - Return the uniqued tuple type with the specified elements.
  /// Returns a ParenType instead if there is exactly one element which
  /// is unlabeled and not varargs, so it doesn't accidentally construct
  /// a tuple which is impossible to write.
  static Type get(ArrayRef<TupleTypeElt> Fields, const ASTContext &C);

  /// getEmpty - Return the empty tuple type '()'.
  static CanTypeWrapper<TupleType> getEmpty(const ASTContext &C);

  /// getFields - Return the fields of this tuple.
  ArrayRef<TupleTypeElt> getFields() const { return Fields; }

  unsigned getNumElements() const { return Fields.size(); }
  
  /// getElementType - Return the type of the specified field.
  Type getElementType(unsigned FieldNo) const {
    return Fields[FieldNo].getType();
  }

  TupleEltTypeArrayRef getElementTypes() const {
    return TupleEltTypeArrayRef(getFields());
  }
  
  /// getNamedElementId - If this tuple has a field with the specified name,
  /// return the field index, otherwise return -1.
  int getNamedElementId(Identifier I) const;
  
  /// hasAnyDefaultValues - Return true if any of our elements has a default
  /// value.
  bool hasAnyDefaultValues() const;
  
  /// getFieldForScalarInit - If a tuple of this type can be initialized with a
  /// scalar, return the field number that the scalar is assigned to.  If not,
  /// return -1.
  int getFieldForScalarInit() const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Tuple;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, Fields);
  }
  static void Profile(llvm::FoldingSetNodeID &ID, 
                      ArrayRef<TupleTypeElt> Fields);
  
private:
  TupleType(ArrayRef<TupleTypeElt> fields, const ASTContext *CanCtx,
            RecursiveTypeProperties properties)
     : TypeBase(TypeKind::Tuple, CanCtx, properties), Fields(fields) {
  }
};
BEGIN_CAN_TYPE_WRAPPER(TupleType, Type)
  CanType getElementType(unsigned fieldNo) const {
    return CanType(getPointer()->getElementType(fieldNo));
  }
  CanTupleEltTypeArrayRef getElementTypes() const {
    return CanTupleEltTypeArrayRef(getPointer()->getFields());
  }
END_CAN_TYPE_WRAPPER(TupleType, Type)

/// UnboundGenericType - Represents a generic nominal type where the
/// type arguments have not yet been resolved.
class UnboundGenericType : public TypeBase, public llvm::FoldingSetNode {
  NominalTypeDecl *TheDecl;

  /// \brief The type of the parent, in which this type is nested.
  Type Parent;

private:
  UnboundGenericType(NominalTypeDecl *TheDecl, Type Parent, const ASTContext &C,
                     RecursiveTypeProperties properties)
    : TypeBase(TypeKind::UnboundGeneric,
               (!Parent || Parent->isCanonical())? &C : nullptr,
               properties),
      TheDecl(TheDecl), Parent(Parent) { }

public:
  static UnboundGenericType* get(NominalTypeDecl *TheDecl, Type Parent,
                                 const ASTContext &C);

  /// \brief Returns the declaration that declares this type.
  NominalTypeDecl *getDecl() const { return TheDecl; }

  /// \brief Returns the type of the parent of this type. This will
  /// be null for top-level types or local types, and for non-generic types
  /// will simply be the same as the declared type of the declaration context
  /// of TheDecl. For types nested within generic types, however, this will
  /// involve \c BoundGenericType nodes that provide context for the nested
  /// type, e.g., the bound type Dictionary<String, Int>.Inner would be
  /// represented as an UnboundGenericType with Dictionary<String, Int> as its
  /// parent type.
  Type getParent() const { return Parent; }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getDecl(), getParent());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, NominalTypeDecl *D,
                      Type Parent);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::UnboundGeneric;
  }
};
BEGIN_CAN_TYPE_WRAPPER(UnboundGenericType, Type)
  PROXY_CAN_TYPE_SIMPLE_GETTER(getParent)
END_CAN_TYPE_WRAPPER(UnboundGenericType, Type)

inline CanType getAsCanType(const Type &type) { return CanType(type); }
typedef ArrayRefView<Type,CanType,getAsCanType> CanTypeArrayRef;

/// BoundGenericType - An abstract class for applying a generic
/// nominal type to the given type arguments.
class BoundGenericType : public TypeBase, public llvm::FoldingSetNode {
  NominalTypeDecl *TheDecl;

  /// \brief The type of the parent, in which this type is nested.
  Type Parent;
  
  ArrayRef<Type> GenericArgs;
  

protected:
  BoundGenericType(TypeKind theKind, NominalTypeDecl *theDecl, Type parent,
                   ArrayRef<Type> genericArgs, const ASTContext *context,
                   RecursiveTypeProperties properties);

public:
  static BoundGenericType* get(NominalTypeDecl *TheDecl, Type Parent,
                               ArrayRef<Type> GenericArgs);

  /// \brief Returns the declaration that declares this type.
  NominalTypeDecl *getDecl() const { return TheDecl; }

  /// \brief Returns the type of the parent of this type. This will
  /// be null for top-level types or local types, and for non-generic types
  /// will simply be the same as the declared type of the declaration context
  /// of TheDecl. For types nested within generic types, however, this will
  /// involve \c BoundGenericType nodes that provide context for the nested
  /// type, e.g., the bound type Dictionary<String, Int>.Inner<Int> would be
  /// represented as a BoundGenericType with Dictionary<String, Int> as its
  /// parent type.
  Type getParent() const { return Parent; }

  ArrayRef<Type> getGenericArgs() const { return GenericArgs; }

  /// \brief Retrieve the set of substitutions used to produce this bound
  /// generic type from the underlying generic type.
  ///
  /// \param module The module in which we should compute the substitutions.
  /// FIXME: We currently don't account for this properly, so it can be null.
  ///
  /// \param resolver The resolver that handles lazy type checking, where
  /// required. This can be null for a fully-type-checked AST.
  ArrayRef<Substitution> getSubstitutions(Module *module,
                                          LazyResolver *resolver);

  void Profile(llvm::FoldingSetNodeID &ID) {
    RecursiveTypeProperties properties;
    Profile(ID, TheDecl, Parent, GenericArgs, properties);
  }
  static void Profile(llvm::FoldingSetNodeID &ID, NominalTypeDecl *TheDecl,
                      Type Parent, ArrayRef<Type> GenericArgs,
                      RecursiveTypeProperties &properties);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() >= TypeKind::First_BoundGenericType &&
           T->getKind() <= TypeKind::Last_BoundGenericType;
  }
};
BEGIN_CAN_TYPE_WRAPPER(BoundGenericType, Type)
  PROXY_CAN_TYPE_SIMPLE_GETTER(getParent)
  CanTypeArrayRef getGenericArgs() const {
    return CanTypeArrayRef(getPointer()->getGenericArgs());
  }
END_CAN_TYPE_WRAPPER(BoundGenericType, Type)


/// BoundGenericClassType - A subclass of BoundGenericType for the case
/// when the nominal type is a generic class type.
class BoundGenericClassType : public BoundGenericType {
private:
  BoundGenericClassType(ClassDecl *theDecl, Type parent,
                        ArrayRef<Type> genericArgs, const ASTContext *context,
                        RecursiveTypeProperties properties)
    : BoundGenericType(TypeKind::BoundGenericClass,
                       reinterpret_cast<NominalTypeDecl*>(theDecl), parent,
                       genericArgs, context, properties) {}
  friend class BoundGenericType;

public:
  static BoundGenericClassType* get(ClassDecl *theDecl, Type parent,
                                    ArrayRef<Type> genericArgs) {
    return cast<BoundGenericClassType>(
             BoundGenericType::get(reinterpret_cast<NominalTypeDecl*>(theDecl),
                                   parent, genericArgs));
  }

  /// \brief Returns the declaration that declares this type.
  ClassDecl *getDecl() const {
    return reinterpret_cast<ClassDecl*>(BoundGenericType::getDecl());
  }

  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::BoundGenericClass;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(BoundGenericClassType, BoundGenericType)

/// BoundGenericEnumType - A subclass of BoundGenericType for the case
/// when the nominal type is a generic enum type.
class BoundGenericEnumType : public BoundGenericType {
private:
  BoundGenericEnumType(EnumDecl *theDecl, Type parent,
                       ArrayRef<Type> genericArgs, const ASTContext *context,
                       RecursiveTypeProperties properties)
    : BoundGenericType(TypeKind::BoundGenericEnum,
                       reinterpret_cast<NominalTypeDecl*>(theDecl), parent,
                       genericArgs, context, properties) {}
  friend class BoundGenericType;

public:
  static BoundGenericEnumType* get(EnumDecl *theDecl, Type parent,
                                    ArrayRef<Type> genericArgs) {
    return cast<BoundGenericEnumType>(
             BoundGenericType::get(reinterpret_cast<NominalTypeDecl*>(theDecl),
                                   parent, genericArgs));
  }

  /// \brief Returns the declaration that declares this type.
  EnumDecl *getDecl() const {
    return reinterpret_cast<EnumDecl*>(BoundGenericType::getDecl());
  }

  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::BoundGenericEnum;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(BoundGenericEnumType, BoundGenericType)

/// BoundGenericStructType - A subclass of BoundGenericType for the case
/// when the nominal type is a generic struct type.
class BoundGenericStructType : public BoundGenericType {
private:
  BoundGenericStructType(StructDecl *theDecl, Type parent,
                         ArrayRef<Type> genericArgs, const ASTContext *context,
                         RecursiveTypeProperties properties)
    : BoundGenericType(TypeKind::BoundGenericStruct, 
                       reinterpret_cast<NominalTypeDecl*>(theDecl), parent,
                       genericArgs, context, properties) {}
  friend class BoundGenericType;

public:
  static BoundGenericStructType* get(StructDecl *theDecl, Type parent,
                                    ArrayRef<Type> genericArgs) {
    return cast<BoundGenericStructType>(
             BoundGenericType::get(reinterpret_cast<NominalTypeDecl*>(theDecl),
                                   parent, genericArgs));
  }

  /// \brief Returns the declaration that declares this type.
  StructDecl *getDecl() const {
    return reinterpret_cast<StructDecl*>(BoundGenericType::getDecl());
  }

  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::BoundGenericStruct;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(BoundGenericStructType, BoundGenericType)

/// NominalType - Represents a type with a name that is significant, such that
/// the name distinguishes it from other structurally-similar types that have
/// different names. Nominal types are always canonical.
class NominalType : public TypeBase {
  /// TheDecl - This is the TypeDecl which declares the given type. It
  /// specifies the name and other useful information about this type.
  NominalTypeDecl * const TheDecl;

  /// \brief The type of the parent, in which this type is nested.
  Type Parent;

protected:
  NominalType(TypeKind K, const ASTContext *C, NominalTypeDecl *TheDecl,
              Type Parent, RecursiveTypeProperties properties)
    : TypeBase(K, (!Parent || Parent->isCanonical())? C : nullptr,
               properties),
      TheDecl(TheDecl), Parent(Parent) { }

public:
  static NominalType *get(NominalTypeDecl *D, Type Parent, const ASTContext &C);

  /// \brief Returns the declaration that declares this type.
  NominalTypeDecl *getDecl() const { return TheDecl; }

  /// \brief Returns the type of the parent of this type. This will
  /// be null for top-level types or local types, and for non-generic types
  /// will simply be the same as the declared type of the declaration context
  /// of TheDecl. For types nested within generic types, however, this will
  /// involve \c BoundGenericType nodes that provide context for the nested
  /// type, e.g., the type Dictionary<String, Int>.ItemRange would be
  /// represented as a NominalType with Dictionary<String, Int> as its parent
  /// type.
  Type getParent() const { return Parent; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() >= TypeKind::First_NominalType &&
           T->getKind() <= TypeKind::Last_NominalType;
  }
};
BEGIN_CAN_TYPE_WRAPPER(NominalType, Type)
  PROXY_CAN_TYPE_SIMPLE_GETTER(getParent)
END_CAN_TYPE_WRAPPER(NominalType, Type)

/// EnumType - This represents the type declared by an EnumDecl.
class EnumType : public NominalType, public llvm::FoldingSetNode {
public:
  /// getDecl() - Returns the decl which declares this type.
  EnumDecl *getDecl() const {
    return reinterpret_cast<EnumDecl *>(NominalType::getDecl());
  }

  /// \brief Retrieve the type when we're referencing the given enum
  /// declaration in the parent type \c Parent.
  static EnumType *get(EnumDecl *D, Type Parent, const ASTContext &C);

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getDecl(), getParent());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, EnumDecl *D, Type Parent);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Enum;
  }

private:
  EnumType(EnumDecl *TheDecl, Type Parent, const ASTContext &Ctx,
            RecursiveTypeProperties properties);
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(EnumType, NominalType)

/// StructType - This represents the type declared by a StructDecl.
class StructType : public NominalType, public llvm::FoldingSetNode {  
public:
  /// getDecl() - Returns the decl which declares this type.
  StructDecl *getDecl() const {
    return reinterpret_cast<StructDecl *>(NominalType::getDecl());
  }

  /// \brief Retrieve the type when we're referencing the given struct
  /// declaration in the parent type \c Parent.
  static StructType *get(StructDecl *D, Type Parent, const ASTContext &C);

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getDecl(), getParent());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, StructDecl *D, Type Parent);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Struct;
  }
  
private:
  StructType(StructDecl *TheDecl, Type Parent, const ASTContext &Ctx,
             RecursiveTypeProperties properties);
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(StructType, NominalType)

/// ClassType - This represents the type declared by a ClassDecl.
class ClassType : public NominalType, public llvm::FoldingSetNode {  
public:
  /// getDecl() - Returns the decl which declares this type.
  ClassDecl *getDecl() const {
    return reinterpret_cast<ClassDecl *>(NominalType::getDecl());
  }

  /// \brief Retrieve the type when we're referencing the given class
  /// declaration in the parent type \c Parent.
  static ClassType *get(ClassDecl *D, Type Parent, const ASTContext &C);

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getDecl(), getParent());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, ClassDecl *D, Type Parent);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Class;
  }
  
private:
  ClassType(ClassDecl *TheDecl, Type Parent, const ASTContext &Ctx,
            RecursiveTypeProperties properties);
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(ClassType, NominalType)

/// Describes the representation of a metatype.
///
/// There are several potential representations for metatypes within
/// SIL, which are distinguished by the metatype representation. This
/// enumeration captures the different representations. Some
/// conversions between representations are possible: for example, one
/// can convert a thin representation to a thick one (but not
/// vice-versa), and different representations are required in
/// different places.
enum class MetatypeRepresentation : char {
  /// A thin metatype requires no runtime information, because the
  /// type itself provides no dynamic behavior.
  ///
  /// Struct and enum metatypes are thin, because dispatch to static
  /// struct and enum members is completely static.
  Thin,
  /// A thick metatype refers to a complete metatype representation
  /// that allows introspection and dynamic dispatch. 
  ///
  /// Thick metatypes are used for class and existential metatypes,
  /// which permit dynamic behavior.
  Thick,
  /// An Objective-C metatype refers to an Objective-C class object.
  ObjC
};

/// AnyMetatypeType - A common parent class of MetatypeType and
/// ExistentialMetatypeType.
class AnyMetatypeType : public TypeBase {
  Type InstanceType;
protected:
  AnyMetatypeType(TypeKind kind, const ASTContext *C,
                  RecursiveTypeProperties properties,
                  Type instanceType,
                  Optional<MetatypeRepresentation> repr);


public:
  Type getInstanceType() const { return InstanceType; }

  /// Does this metatype have a representation?
  ///
  /// Only SIL metatype types have a representation.
  bool hasRepresentation() const {
    return AnyMetatypeTypeBits.Representation > 0;
  }
  
  /// Retrieve the metatype representation.
  ///
  /// The metatype representation is a SIL-only property. Thin
  /// metatypes can be lowered away to empty types in IR, unless a
  /// metatype value is required at an abstraction level.
  MetatypeRepresentation getRepresentation() const {
    assert(AnyMetatypeTypeBits.Representation &&
           "metatype has no representation");
    return static_cast<MetatypeRepresentation>(
             AnyMetatypeTypeBits.Representation - 1);
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Metatype ||
           T->getKind() == TypeKind::ExistentialMetatype;
  }
};
BEGIN_CAN_TYPE_WRAPPER(AnyMetatypeType, Type)
  PROXY_CAN_TYPE_SIMPLE_GETTER(getInstanceType)
END_CAN_TYPE_WRAPPER(AnyMetatypeType, Type)

/// MetatypeType - This is the type given to a metatype value.  When a type is
/// declared, a 'metatype' value is injected into the value namespace to
/// resolve references to the type.  An example:
///
///  struct x { ... }  // declares type 'x' and metatype 'x'.
///  x.a()             // use of the metatype value since its a value context.
///
/// In general, this is spelled X.Type, unless X is an existential
/// type, in which case the ordinary metatype is spelled X.Protocol
/// and X.Type connotes the ExistentialMetatypeType.
class MetatypeType : public AnyMetatypeType {
  static MetatypeType *get(Type T, Optional<MetatypeRepresentation> Repr,
                           const ASTContext &C);

public:
  /// \brief Return the MetatypeType for the specified type declaration.
  ///
  /// This leaves the 'representation' property unavailable.
  static MetatypeType *get(Type T, const ASTContext &C) {
    return get(T, Nothing, C);
  }
  
  /// Return the MetatypeType for the specified type declaration with
  /// the given representation.
  ///
  /// Metatype representation is a SIL-only property. Thin metatypes
  /// can be lowered away to empty types in IR.
  static MetatypeType *get(Type T,
                           Optional<MetatypeRepresentation> repr = Nothing) {
    return get(T, repr, T->getASTContext());
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Metatype;
  }
  
private:
  MetatypeType(Type T, const ASTContext *C,
               RecursiveTypeProperties properties,
               Optional<MetatypeRepresentation> repr);
  friend class TypeDecl;
};
BEGIN_CAN_TYPE_WRAPPER(MetatypeType, AnyMetatypeType)
  static CanMetatypeType get(CanType type) {
    return CanMetatypeType(MetatypeType::get(type));
  }
  static CanMetatypeType get(CanType type, MetatypeRepresentation repr) {
    return CanMetatypeType(MetatypeType::get(type, repr));
  }
END_CAN_TYPE_WRAPPER(MetatypeType, AnyMetatypeType)

/// ExistentialMetatypeType - This is the type given to an existential
/// metatype value, i.e. the type of the dynamic type of an
/// existential value.  The instance type must be an existential type
/// of some sort.
///
/// Formally, this type is \exists t : T... . t.Type.  In contrast,
/// the MetatypeType for a ProtocolType is a singleton.
///
/// This is spelled X.Type, where X is an existential type.
///
/// The representation of an existential metatype cannot be thin.
class ExistentialMetatypeType : public AnyMetatypeType {
  static ExistentialMetatypeType *get(Type T,
                                      Optional<MetatypeRepresentation> Repr,
                                      const ASTContext &C);

public:
  /// Return the ExistentialMetatypeType for the specified type
  /// with the given representation.
  ///
  /// Metatype representation is a SIL-only property. Existential
  /// metatypes cannot be thin.
  static ExistentialMetatypeType *get(Type T,
                                      Optional<MetatypeRepresentation> repr
                                        = Nothing) {
    return get(T, repr, T->getASTContext());
  }

  /// Return the canonicalized list of protocols.
  void getAnyExistentialTypeProtocols(SmallVectorImpl<ProtocolDecl *> &protos) {
    getInstanceType()->getAnyExistentialTypeProtocols(protos);
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::ExistentialMetatype;
  }
  
private:
  ExistentialMetatypeType(Type T, const ASTContext *C,
                          RecursiveTypeProperties properties,
                          Optional<MetatypeRepresentation> repr);
  friend class TypeDecl;
};
BEGIN_CAN_TYPE_WRAPPER(ExistentialMetatypeType, AnyMetatypeType)
  static CanExistentialMetatypeType get(CanType type) {
    return CanExistentialMetatypeType(ExistentialMetatypeType::get(type));
  }
  static CanExistentialMetatypeType get(CanType type,
                                        MetatypeRepresentation repr) {
    return CanExistentialMetatypeType(ExistentialMetatypeType::get(type, repr));
  }
  void getAnyExistentialTypeProtocols(SmallVectorImpl<ProtocolDecl *> &protocols) {
    getInstanceType().getAnyExistentialTypeProtocols(protocols);
  }
END_CAN_TYPE_WRAPPER(ExistentialMetatypeType, AnyMetatypeType)
  
/// ModuleType - This is the type given to a module value, e.g. the "Builtin" in
/// "Builtin.int".  This is typically given to a ModuleExpr, but can also exist
/// on ParenExpr, for example.
class ModuleType : public TypeBase {
  Module *const TheModule;
  
public:
  /// get - Return the ModuleType for the specified module.
  static ModuleType *get(Module *M);

  Module *getModule() const { return TheModule; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return  T->getKind() == TypeKind::Module;
  }
  
private:
  ModuleType(Module *M, const ASTContext &Ctx)
    : TypeBase(TypeKind::Module, &Ctx, // Always canonical
               RecursiveTypeProperties()),
      TheModule(M) {
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(ModuleType, Type)
  
/// The type given to a dynamic \c Self return type.
///
/// Example:
/// \code
/// class X {
///   class func factory() -> Self { ... }
/// };
/// \endcode
///
/// In this example, \c Self is represented by a 
/// \c DynamicSelfType node whose self type is \c X.
class DynamicSelfType : public TypeBase {
  Type SelfType;

public:
  /// \brief Return the DynamicSelf for the specified self type.
  static DynamicSelfType *get(Type selfType, const ASTContext &ctx);

  /// Retrieve the (static) self type for this dynamic self type.
  Type getSelfType() const { return SelfType; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::DynamicSelf;
  }
  
private:
  DynamicSelfType(Type selfType, const ASTContext &ctx,
                  RecursiveTypeProperties properties)
    : TypeBase(TypeKind::DynamicSelf, selfType->isCanonical()? &ctx : 0, 
               properties),
      SelfType(selfType) { }

  friend class TypeDecl;
};
BEGIN_CAN_TYPE_WRAPPER(DynamicSelfType, Type)
  PROXY_CAN_TYPE_SIMPLE_GETTER(getSelfType)

  static CanDynamicSelfType get(CanType selfType, const ASTContext &ctx) {
    return CanDynamicSelfType(DynamicSelfType::get(selfType, ctx));
  }
END_CAN_TYPE_WRAPPER(DynamicSelfType, Type)

/// A high-level calling convention.
enum class AbstractCC : unsigned char {
  /// The calling convention used for calling a normal function.
  Freestanding = 0,

  /// The C freestanding calling convention.
  C,
  
  /// The ObjC method calling convention.
  ObjCMethod,

  /// The calling convention used for calling an instance method.
  Method,
  
  /// The calling convention used for calling opaque protocol witnesses.
  /// Note that methods of class-constrained protocols use the normal
  /// Method cc.
  WitnessMethod,
  
  Last_AbstractCC = WitnessMethod,
};
  
/// AnyFunctionType - A function type has a single input and result, but
/// these types may be tuples, for example:
///   "(int) -> int" or "(a : int, b : int) -> (int, int)".
/// Note that the parser requires that the input to a function type be a Tuple
/// or ParenType, but ParenType desugars to its element, so the input to a
/// function may be an arbitrary type.
///
/// There are two kinds of function types:  monomorphic (FunctionType) and
/// polymorphic (PolymorphicFunctionType). Both type families additionally can
/// be 'thin', indicating that a function value has no capture context and can be
/// represented at the binary level as a single function pointer.
class AnyFunctionType : public TypeBase {
  const Type Input;
  const Type Output;

public:
  /// \brief The representation form of a function.
  enum class Representation {
    // A "thick" function that carries a context pointer to reference captured
    // state. The default.
    Thick = 0,
    
    // A thick function that is represented as an Objective-C block.
    Block,
    
    // A "thin" function that needs no context.
    Thin,
  };
  
  /// \brief A class which abstracts out some details necessary for
  /// making a call.
  class ExtInfo {
    // Feel free to rearrange or add bits, but if you go over 7,
    // you'll need to adjust both the Bits field below and
    // BaseType::AnyFunctionTypeBits.

    //   |  CC  |representation|isAutoClosure|noReturn|
    //   |0 .. 3|    4 .. 5    |      6      |   7    |
    //
    enum : uint16_t { CallConvMask = 0xF };
    enum : uint16_t { RepresentationMask = 0x30, RepresentationShift = 4 };
    enum : uint16_t { AutoClosureMask = 0x40 };
    enum : uint16_t { NoReturnMask = 0x80 };

    uint16_t Bits;

    ExtInfo(unsigned Bits) : Bits(static_cast<uint16_t>(Bits)) {}

    friend class AnyFunctionType;
    friend class SILFunctionType;
    
  public:
    // Constructor with all defaults.
    ExtInfo() : Bits(0) {
      assert(getCC() == AbstractCC::Freestanding);
    }

    // Constructor for polymorphic type.
    ExtInfo(AbstractCC CC, Representation Rep, bool IsNoReturn) {
      Bits = ((unsigned) CC) |
             ((unsigned) Rep << RepresentationShift) |
             (IsNoReturn ? NoReturnMask : 0);
    }

    // Constructor with no defaults.
    ExtInfo(AbstractCC CC, Representation Rep, bool IsNoReturn,
            bool IsAutoClosure) : ExtInfo(CC, Rep, IsNoReturn){
      Bits = Bits |
             (IsAutoClosure ? AutoClosureMask : 0);
    }

    explicit ExtInfo(AbstractCC CC) : Bits(0) {
      Bits = (Bits & ~CallConvMask) | (unsigned) CC;
    }

    AbstractCC getCC() const { return AbstractCC(Bits & CallConvMask); }
    bool isNoReturn() const { return Bits & NoReturnMask; }
    bool isAutoClosure() const { return Bits & AutoClosureMask; }
    Representation getRepresentation() const {
      return Representation((Bits & RepresentationMask) >> RepresentationShift);
    }
    
    /// True if the function representation carries context.
    bool hasContext() const {
      switch (getRepresentation()) {
      case Representation::Thick:
      case Representation::Block:
        return true;
      case Representation::Thin:
        return false;
      }
    }
    
    // Note that we don't have setters. That is by design, use
    // the following with methods instead of mutating these objects.
    ExtInfo withCallingConv(AbstractCC CC) const {
      return ExtInfo((Bits & ~CallConvMask) | (unsigned) CC);
    }
    ExtInfo withRepresentation(Representation Rep) const {
      return ExtInfo((Bits & ~RepresentationMask)
                     | (unsigned)Rep << RepresentationShift);
    }
    ExtInfo withIsNoReturn(bool IsNoReturn) const {
      if (IsNoReturn)
        return ExtInfo(Bits | NoReturnMask);
      else
        return ExtInfo(Bits & ~NoReturnMask);
    }
    ExtInfo withIsAutoClosure(bool IsAutoClosure) const {
      if (IsAutoClosure)
        return ExtInfo(Bits | AutoClosureMask);
      else
        return ExtInfo(Bits & ~AutoClosureMask);
    }
    
    char getFuncAttrKey() const {
      return Bits;
    }

    bool operator==(ExtInfo Other) const {
      return Bits == Other.Bits;
    }
    bool operator!=(ExtInfo Other) const {
      return Bits != Other.Bits;
    }
  };

protected:
  AnyFunctionType(TypeKind Kind, const ASTContext *CanTypeContext,
                  Type Input, Type Output, RecursiveTypeProperties properties,
                  const ExtInfo &Info)
  : TypeBase(Kind, CanTypeContext, properties), Input(Input), Output(Output) {
    AnyFunctionTypeBits.ExtInfo = Info.Bits;
  }

public:

  Type getInput() const { return Input; }
  Type getResult() const { return Output; }

  ExtInfo getExtInfo() const {
    return ExtInfo(AnyFunctionTypeBits.ExtInfo);
  }

  /// \brief Returns the calling conventions of the function.
  AbstractCC getAbstractCC() const {
    return getExtInfo().getCC();
  }
  
  /// \brief Get the representation of the function type.
  Representation getRepresentation() const {
    return getExtInfo().getRepresentation();
  }
  
  bool isNoReturn() const {
    return getExtInfo().isNoReturn();
  }

  /// \brief True if this type allows an implicit conversion from a function
  /// argument expression of type T to a function of type () -> T.
  bool isAutoClosure() const {
    return getExtInfo().isAutoClosure();
  }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() >= TypeKind::First_AnyFunctionType &&
           T->getKind() <= TypeKind::Last_AnyFunctionType;
  }
};
BEGIN_CAN_TYPE_WRAPPER(AnyFunctionType, Type)
  typedef AnyFunctionType::ExtInfo ExtInfo;
  PROXY_CAN_TYPE_SIMPLE_GETTER(getInput)
  PROXY_CAN_TYPE_SIMPLE_GETTER(getResult)
END_CAN_TYPE_WRAPPER(AnyFunctionType, Type)

/// FunctionType - A monomorphic function type.
///
/// If the AutoClosure bit is set to true, then the input type is known to be ()
/// and a value of this function type is only assignable (in source code) from
/// the destination type of the function. Sema inserts an ImplicitClosure to
/// close over the value.  For example:
///   var x : @auto_closure () -> int = 4
class FunctionType : public AnyFunctionType {
public:
  /// 'Constructor' Factory Function
  static FunctionType *get(Type Input, Type Result) {
    return get(Input, Result, ExtInfo());
  }

  static FunctionType *get(Type Input, Type Result, const ExtInfo &Info);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Function;
  }
  
private:
  FunctionType(Type Input, Type Result,
               RecursiveTypeProperties properties,
               const ExtInfo &Info);
};
BEGIN_CAN_TYPE_WRAPPER(FunctionType, AnyFunctionType)
  static CanFunctionType get(CanType input, CanType result) {
    return CanFunctionType(FunctionType::get(input, result));
  }
  static CanFunctionType get(CanType input, CanType result,
                             const ExtInfo &info) {
    return CanFunctionType(FunctionType::get(input, result, info));
  }
END_CAN_TYPE_WRAPPER(FunctionType, AnyFunctionType)
  
/// PolymorphicFunctionType - A polymorphic function type.
class PolymorphicFunctionType : public AnyFunctionType {
  // TODO: storing a GenericParamList* here is really the wrong solution;
  // we should be able to store something readily canonicalizable.
  GenericParamList *Params;
public:
  /// 'Constructor' Factory Function
  static PolymorphicFunctionType *get(Type input, Type output,
                                      GenericParamList *params) {
    return get(input, output, params, ExtInfo());
  }

  static PolymorphicFunctionType *get(Type input, Type output,
                                      GenericParamList *params,
                                      const ExtInfo &Info);

  ArrayRef<GenericParam> getGenericParameters() const;
  ArrayRef<ArchetypeType *> getAllArchetypes() const;

  GenericParamList &getGenericParams() const { return *Params; }

  /// Substitute the given generic arguments into this polymorphic
  /// function type and return the resulting non-polymorphic type.
  FunctionType *substGenericArgs(Module *M, ArrayRef<Type> args);

  /// Substitute the given generic arguments into this polymorphic
  /// function type and return the resulting non-polymorphic type.
  ///
  /// The order of Substitutions must match the order of generic archetypes.
  FunctionType *substGenericArgs(Module *M, ArrayRef<Substitution> subs);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::PolymorphicFunction;
  }
  
private:
  PolymorphicFunctionType(Type input, Type output,
                          GenericParamList *params,
                          const ExtInfo &Info,
                          const ASTContext &C,
                          RecursiveTypeProperties properties);
};  
BEGIN_CAN_TYPE_WRAPPER(PolymorphicFunctionType, AnyFunctionType)
  static CanPolymorphicFunctionType get(CanType input, CanType result,
                                        GenericParamList *params,
                                        const ExtInfo &info) {
    return CanPolymorphicFunctionType(
             PolymorphicFunctionType::get(input, result, params, info));
  }
END_CAN_TYPE_WRAPPER(PolymorphicFunctionType, AnyFunctionType)

/// Describes a generic function type.
///
/// A generic function type describes a function that is polymorphic with
/// respect to some set of generic parameters and the requirements placed
/// on those parameters and dependent member types thereof. The input and
/// output types of the generic function can be expressed in terms of those
/// generic parameters.
///
/// FIXME: \c GenericFunctionType is meant as a replacement for
/// \c PolymorphicFunctionType.
class GenericFunctionType : public AnyFunctionType,
                            public llvm::FoldingSetNode
{
  GenericSignature *Signature;

  /// Construct a new generic function type.
  GenericFunctionType(GenericSignature *sig,
                      Type input,
                      Type result,
                      const ExtInfo &info,
                      const ASTContext *ctx,
                      RecursiveTypeProperties properties);
public:
  /// Create a new generic function type.
  static GenericFunctionType *get(GenericSignature *sig,
                                  Type input,
                                  Type result,
                                  const ExtInfo &info);

  /// Retrieve the generic signature of this function type.
  GenericSignature *getGenericSignature() const {
    return Signature;
  }
  
  /// Retrieve the generic parameters of this polymorphic function type.
  ArrayRef<GenericTypeParamType *> getGenericParams() const;

  /// Retrieve the requirements of this polymorphic function type.
  ArrayRef<Requirement> getRequirements() const;
                              
  /// Substitute all of the given generic arguments into this generic
  /// function type and return the resulting non-generic type.
  FunctionType *substGenericArgs(Module *M, ArrayRef<Type> args) const;

  /// Substitute the given generic arguments into this generic
  /// function type and return the resulting non-generic type.
  ///
  /// The order of Substitutions must match the order of generic parameters.
  FunctionType *substGenericArgs(Module *M, ArrayRef<Substitution> subs);

  /// Substitute the given generic arguments into this generic
  /// function type, possibly leaving some of the generic parameters
  /// unsubstituted, and return the resulting function type.
  AnyFunctionType *partialSubstGenericArgs(Module *M, ArrayRef<Type> args) const;
                              
  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getGenericSignature(), getInput(), getResult(),
            getExtInfo());
  }
  static void Profile(llvm::FoldingSetNodeID &ID,
                      GenericSignature *sig,
                      Type input,
                      Type result,
                      const ExtInfo &info);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::GenericFunction;
  }
};

BEGIN_CAN_TYPE_WRAPPER(GenericFunctionType, AnyFunctionType)
  static CanGenericFunctionType get(CanGenericSignature sig,
                                    CanType input, CanType result,
                                    const ExtInfo &info) {
    return CanGenericFunctionType(
              GenericFunctionType::get(sig, input, result, info));
  }
  
  CanGenericSignature getGenericSignature() const {
    return CanGenericSignature(getPointer()->getGenericSignature());
  }
  
  ArrayRef<CanTypeWrapper<GenericTypeParamType>> getGenericParams() const {
    return getGenericSignature().getGenericParams();
  }
END_CAN_TYPE_WRAPPER(GenericFunctionType, AnyFunctionType)

/// Conventions for passing arguments as parameters.
enum class ParameterConvention {
  /// This argument is passed indirectly, i.e. by directly passing
  /// the address of an object in memory.  The callee is responsible
  /// for destroying the object.  The callee may assume that the
  /// address does not alias any valid object.
  Indirect_In,

  /// This argument is passed indirectly, i.e. by directly passing
  /// the address of an object in memory.  The object is
  /// instantaneously valid on entry, and it must be instantaneously
  /// valid on exit.  The callee may assume that the address does
  /// not alias any valid object.
  Indirect_Inout,

  /// This argument is passed indirectly, i.e. by directly passing
  /// the address of an uninitialized object in memory.  The callee
  /// is responsible for leaving an initialized object at this
  /// address.  The callee may assume that the address does not
  /// alias any valid object.
  Indirect_Out,

  /// This argument is passed directly.  Its type is non-trivial,
  /// and the callee is responsible for destroying it.
  Direct_Owned,

  /// This argument is passed directly.  Its type may be trivial, or
  /// it may simply be that the callee is not responsible for
  /// destroying it.  Its validity is guaranteed only at the instant
  /// the call begins.
  Direct_Unowned,

  /// This argument is passed directly.  Its type is non-trivial, and
  /// the caller guarantees its validity for the entirety of the call.
  Direct_Guaranteed,
};
inline bool isIndirectParameter(ParameterConvention conv) {
  return conv <= ParameterConvention::Indirect_Out;
}
inline bool isConsumedParameter(ParameterConvention conv) {
  switch (conv) {
  case ParameterConvention::Indirect_In:
  case ParameterConvention::Direct_Owned:
    return true;

  case ParameterConvention::Indirect_Inout:
  case ParameterConvention::Indirect_Out:
  case ParameterConvention::Direct_Unowned:
  case ParameterConvention::Direct_Guaranteed:
    return false;
  }
  llvm_unreachable("bad convention kind");
}

/// A parameter type and the rules for passing it.
class SILParameterInfo {
  llvm::PointerIntPair<CanType, 3, ParameterConvention> TypeAndConvention;
public:
  SILParameterInfo() = default;
  SILParameterInfo(CanType type, ParameterConvention conv)
    : TypeAndConvention(type, conv) {
    assert(type->isLegalSILType() && "SILParameterInfo has illegal SIL type");
  }

  CanType getType() const {
    return TypeAndConvention.getPointer();
  }
  ParameterConvention getConvention() const {
    return TypeAndConvention.getInt();
  }
  bool isIndirect() const {
    return isIndirectParameter(getConvention());
  }
  bool isIndirectInOut() const {
    return getConvention() == ParameterConvention::Indirect_Inout;
  }
  bool isIndirectResult() const {
    return getConvention() == ParameterConvention::Indirect_Out;
  }

  /// True if this parameter is consumed by the callee, either
  /// indirectly or directly.
  bool isConsumed() const {
    return isConsumedParameter(getConvention());
  }

  SILType getSILType() const; // in SILType.h

  /// Transform this SILParameterInfo by applying the user-provided
  /// function to its type.
  template<typename F>
  SILParameterInfo transform(const F &fn) const {
    return SILParameterInfo(fn(getType())->getCanonicalType(), getConvention());
  }

  /// Replace references to substitutable types with new, concrete types and
  /// return the substituted result.
  ///
  /// The API is comparable to Type::subst.
  SILParameterInfo subst(Module *module, TypeSubstitutionMap &substitutions,
                         bool ignoreMissing, LazyResolver *resolver) const {
    Type type = getType().subst(module, substitutions, ignoreMissing, resolver);
    return SILParameterInfo(type->getCanonicalType(), getConvention());
  }

  void profile(llvm::FoldingSetNodeID &id) {
    id.AddPointer(TypeAndConvention.getOpaqueValue());
  }

  void dump() const;
  void print(llvm::raw_ostream &out,
             const PrintOptions &options = PrintOptions()) const;
  void print(ASTPrinter &Printer, const PrintOptions &Options) const;
  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &out,
                                       SILParameterInfo type) {
    type.print(out);
    return out;
  }

  bool operator==(SILParameterInfo rhs) const {
    return TypeAndConvention == rhs.TypeAndConvention;
  }
  bool operator!=(SILParameterInfo rhs) const {
    return !(*this == rhs);
  }
};

/// Conventions for returning values.  All return values at this
/// level are direct.
enum class ResultConvention {
  /// The caller is responsible for destroying this return value.
  /// Its type is non-trivial.
  Owned,

  /// The caller is not responsible for destroying this return value.
  /// Its type may be trivial, or it may simply be offered unsafely.
  /// It is valid at the instant of the return, but further operations
  /// may invalidate it.
  Unowned,

  /// This value has been (or may have been) returned autoreleased.
  /// The caller should make an effort to reclaim the autorelease.
  /// The type must be a class or class existential type, and this
  /// must be the only return value.
  Autoreleased,
};

/// A direct result type and the rules for returning it.
///
/// Indirect results require an implicit address parameter and are
/// therefore represented with a kind of SILParameterInfo.  For now, a
/// function with an indirect result will always have a SILResultInfo
/// with the empty tuple type '()'.
class SILResultInfo {
  llvm::PointerIntPair<CanType, 3, ResultConvention> TypeAndConvention;
public:
  SILResultInfo() = default;
  SILResultInfo(CanType type, ResultConvention conv)
    : TypeAndConvention(type, conv) {
    assert(type->isLegalSILType() && "SILResultInfo has illegal SIL type");
  }

  CanType getType() const {
    return TypeAndConvention.getPointer();
  }
  ResultConvention getConvention() const {
    return TypeAndConvention.getInt();
  }
  SILType getSILType() const; // in SILType.h

  /// Transform this SILResultInfo by applying the user-provided
  /// function to its type.
  SILResultInfo transform(const std::function<Type(Type)> &fn) const {
    return SILResultInfo(fn(getType())->getCanonicalType(), getConvention());
  }

  /// Replace references to substitutable types with new, concrete types and
  /// return the substituted result.
  ///
  /// The API is comparable to Type::subst.
  SILResultInfo subst(Module *module, TypeSubstitutionMap &substitutions,
                      bool ignoreMissing, LazyResolver *resolver) const {
    Type type = getType().subst(module, substitutions, ignoreMissing, resolver);
    return SILResultInfo(type->getCanonicalType(), getConvention());
  }

  void profile(llvm::FoldingSetNodeID &id) {
    id.AddPointer(TypeAndConvention.getOpaqueValue());
  }

  void dump() const;
  void print(llvm::raw_ostream &out,
             const PrintOptions &options = PrintOptions()) const;
  void print(ASTPrinter &Printer, const PrintOptions &Options) const;
  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &out,
                                       SILResultInfo type) {
    type.print(out);
    return out;
  }

  bool operator==(SILResultInfo rhs) const {
    return TypeAndConvention == rhs.TypeAndConvention;
  }
  bool operator!=(SILResultInfo rhs) const {
    return !(*this == rhs);
  }
};
  
class SILFunctionType;
typedef CanTypeWrapper<SILFunctionType> CanSILFunctionType;

// Some macros to aid the SILFunctionType transition.
#if 0
# define SIL_FUNCTION_TYPE_DEPRECATED __attribute__((deprecated))
# define SIL_FUNCTION_TYPE_IGNORE_DEPRECATED_BEGIN \
    _Pragma("clang diagnostic push") \
    _Pragma("clang diagnostic ignored \"-Wdeprecated\"")
# define SIL_FUNCTION_TYPE_IGNORE_DEPRECATED_END \
    _Pragma("clang diagnostic pop")
#else
# define SIL_FUNCTION_TYPE_DEPRECATED
# define SIL_FUNCTION_TYPE_IGNORE_DEPRECATED_BEGIN
# define SIL_FUNCTION_TYPE_IGNORE_DEPRECATED_END
#endif
  
/// SILFunctionType - The detailed type of a function value, suitable
/// for use by SIL.
///
/// This type is defined by the AST library because it must be capable
/// of appearing in secondary positions, e.g. within tuple and
/// function parameter and result types.
class SILFunctionType : public TypeBase, public llvm::FoldingSetNode {
public:
  typedef AnyFunctionType::ExtInfo ExtInfo;
  typedef AnyFunctionType::Representation Representation;
  
private:
  CanGenericSignature GenericSig;

  /// TODO: Permit an arbitrary number of results.
  SILResultInfo InterfaceResult;

  /// TODO: Retire in favor of InterfaceParameters.
  MutableArrayRef<SILParameterInfo> getMutableInterfaceParameters() {
    auto ptr = reinterpret_cast<SILParameterInfo*>(this + 1);
    unsigned n = SILFunctionTypeBits.NumParameters;
    return MutableArrayRef<SILParameterInfo>(ptr, n);
  }

  SILFunctionType(GenericSignature *genericSig,
                  ExtInfo ext,
                  ParameterConvention calleeConvention,
                  ArrayRef<SILParameterInfo> interfaceParams,
                  SILResultInfo interfaceResult,
                  const ASTContext &ctx,
                  RecursiveTypeProperties properties);

  static SILType getParameterSILType(const SILParameterInfo &param);// SILType.h

public:
  static CanSILFunctionType get(GenericSignature *genericSig,
                                ExtInfo ext,
                                ParameterConvention calleeConvention,
                                ArrayRef<SILParameterInfo> interfaceParams,
                                SILResultInfo interfaceResult,
                                const ASTContext &ctx);

  // in SILType.h
  SILType getSILInterfaceResult() const;
  SILType getSILInterfaceParameter(unsigned i) const;

  /// Return the convention under which the callee is passed, if this
  /// is a thick non-block callee.
  ParameterConvention getCalleeConvention() const {
    return ParameterConvention(SILFunctionTypeBits.CalleeConvention);
  }
  bool isCalleeConsumed() const {
    return getCalleeConvention() == ParameterConvention::Direct_Owned;
  }

  SILResultInfo getInterfaceResult() const {
    return InterfaceResult;
  }
  
  ArrayRef<SILParameterInfo> getInterfaceParameters() const {
    return const_cast<SILFunctionType*>(this)->getMutableInterfaceParameters();
  }

  bool hasIndirectResult() const {
    return !getInterfaceParameters().empty()
      && getInterfaceParameters()[0].isIndirectResult();
  }
  SILParameterInfo getIndirectInterfaceResult() const {
    assert(hasIndirectResult());
    return getInterfaceParameters()[0];
  }

  /// Returns the SILType of the semantic result of this function: the
  /// indirect result type, if there is one, otherwise the direct result.
  ///
  /// In SILType.h.
  SILType getSemanticInterfaceResultSILType() const;

  /// Get the parameters, ignoring any indirect-result parameter.
  ArrayRef<SILParameterInfo>
  getInterfaceParametersWithoutIndirectResult() const  {
    auto params = getInterfaceParameters();
    if (hasIndirectResult()) params = params.slice(1);
      return params;
  }

  using ParameterSILTypeArrayRef
    = ArrayRefView<SILParameterInfo, SILType, getParameterSILType>;  
  ParameterSILTypeArrayRef getInterfaceParameterSILTypes() const {
    return ParameterSILTypeArrayRef(getInterfaceParameters());
  }
  
  ParameterSILTypeArrayRef
  getInterfaceParameterSILTypesWithoutIndirectResult() const {
    return ParameterSILTypeArrayRef(getInterfaceParametersWithoutIndirectResult());
  }

  bool isPolymorphic() const { return GenericSig != nullptr; }
  GenericSignature *getGenericSignature() const { return GenericSig; }

  ExtInfo getExtInfo() const { return ExtInfo(SILFunctionTypeBits.ExtInfo); }

  /// \brief Returns the calling conventions of the function.
  AbstractCC getAbstractCC() const {
    return getExtInfo().getCC();
  }

  /// \brief Get the representation of the function type.
  Representation getRepresentation() const {
    return getExtInfo().getRepresentation();
  }
  
  bool isNoReturn() const {
    return getExtInfo().isNoReturn();
  }

  CanSILFunctionType substInterfaceGenericArgs(SILModule &silModule,
                                               Module *astModule,
                                               ArrayRef<Substitution> subs);

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getGenericSignature(), getExtInfo(), getCalleeConvention(),
            getInterfaceParameters(), getInterfaceResult());
  }
  static void Profile(llvm::FoldingSetNodeID &ID,
                      GenericSignature *genericSig,
                      ExtInfo info,
                      ParameterConvention calleeConvention,
                      ArrayRef<SILParameterInfo> params,
                      SILResultInfo result);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::SILFunction;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(SILFunctionType, Type)

/// ArrayType - An array type has a base type and either an unspecified or a
/// constant size.  For example "int[]" and "int[4]".  Array types cannot have
/// size = 0.
class ArrayType : public TypeBase {
  const Type Base;
  
  /// Size - When this is zero it indicates an unsized array like "int[]".
  uint64_t Size;
  
public:
  /// 'Constructor' Factory Function.
  static ArrayType *get(Type BaseType, uint64_t Size);

  Type getBaseType() const { return Base; }
  uint64_t getSize() const { return Size; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Array;
  }
  
private:
  ArrayType(Type Base, uint64_t Size, RecursiveTypeProperties properties);
};
BEGIN_CAN_TYPE_WRAPPER(ArrayType, Type)
  PROXY_CAN_TYPE_SIMPLE_GETTER(getBaseType)
END_CAN_TYPE_WRAPPER(ArrayType, Type)

/// A type with a special syntax that is always sugar for a library type.
///
/// The prime examples are arrays (T[] -> Array<T>) and
/// optionals (T? -> Optional<T>).
class SyntaxSugarType : public TypeBase {
  Type Base;
  llvm::PointerUnion<Type, const ASTContext *> ImplOrContext;

protected:
  // Syntax sugar types are never canonical.
  SyntaxSugarType(TypeKind K, const ASTContext &ctx, Type base,
                  RecursiveTypeProperties properties)
    : TypeBase(K, nullptr, properties), Base(base), ImplOrContext(&ctx) {}

public:
  Type getBaseType() const {
    return Base;
  }

  TypeBase *getSinglyDesugaredType();

  Type getImplementationType();

  static bool classof(const TypeBase *T) {
    return T->getKind() >= TypeKind::First_SyntaxSugarType &&
           T->getKind() <= TypeKind::Last_SyntaxSugarType;
  }
};
  
/// The type T[], which is always sugar for a library type.
class ArraySliceType : public SyntaxSugarType {
  ArraySliceType(const ASTContext &ctx, Type base,
                 RecursiveTypeProperties properties)
    : SyntaxSugarType(TypeKind::ArraySlice, ctx, base, properties) {}

public:
  /// Return a uniqued array slice type with the specified base type.
  static ArraySliceType *get(Type baseTy);

  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::ArraySlice;
  }
};

/// The type T?, which is always sugar for a library type.
class OptionalType : public SyntaxSugarType {
  OptionalType(const ASTContext &ctx,Type base,
               RecursiveTypeProperties properties)
    : SyntaxSugarType(TypeKind::Optional, ctx, base, properties) {}

public:
  /// Return a uniqued optional type with the specified base type.
  static OptionalType *get(Type baseTy);

  /// Build one of the optional type sugar kinds.
  ///
  /// It's a bit unnatural to have this on OptionalType, but we don't
  /// have an abstract common class, and polluting TypeBase with it
  /// would be unfortunate.  If we ever make an AnyOptionalType,
  /// we can move it there.
  ///
  /// \param kind - can't be OTK_None
  static Type get(OptionalTypeKind kind, Type baseTy);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Optional;
  }
};

/// The type @unchecked T?, which is always sugar for a library type.
class UncheckedOptionalType : public SyntaxSugarType {
  UncheckedOptionalType(const ASTContext &ctx, Type base,
                        RecursiveTypeProperties properties)
    : SyntaxSugarType(TypeKind::UncheckedOptional, ctx, base, properties) {}

public:
  /// Return a uniqued optional type with the specified base type.
  static UncheckedOptionalType *get(Type baseTy);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::UncheckedOptional;
  }
};

/// ProtocolType - A protocol type describes an abstract interface implemented
/// by another type.
class ProtocolType : public NominalType {
public:
  /// \brief Retrieve the type when we're referencing the given protocol.
  /// declaration.
  static ProtocolType *get(ProtocolDecl *D, const ASTContext &C);

  ProtocolDecl *getDecl() const {
    return reinterpret_cast<ProtocolDecl *>(NominalType::getDecl());
  }

  /// True if only classes may conform to the protocol.
  bool requiresClass() const;

  void getAnyExistentialTypeProtocols(SmallVectorImpl<ProtocolDecl *> &protos) {
    protos.push_back(getDecl());
  }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Protocol;
  }

  /// Canonicalizes the given set of protocols by eliminating any mentions
  /// of protocols that are already covered by inheritance due to other entries
  /// in the protocol list, then sorting them in some stable order.
  static void canonicalizeProtocols(SmallVectorImpl<ProtocolDecl *> &protocols);

private:
  friend class NominalTypeDecl;
  ProtocolType(ProtocolDecl *TheDecl, const ASTContext &Ctx);
};
BEGIN_CAN_TYPE_WRAPPER(ProtocolType, NominalType)
  void getAnyExistentialTypeProtocols(SmallVectorImpl<ProtocolDecl *> &protos) {
    getPointer()->getAnyExistentialTypeProtocols(protos);
  }
END_CAN_TYPE_WRAPPER(ProtocolType, NominalType)

/// ProtocolCompositionType - A type that composes some number of protocols
/// together to represent types that conform to all of the named protocols.
///
/// \code
/// protocol P { /* ... */ }
/// protocol Q { /* ... */ }
/// var x : protocol<P, Q>
/// \endcode
///
/// Here, the type of x is a composition of the protocols 'P' and 'Q'.
///
/// The canonical form of a protocol composition type is based on a sorted (by
/// module and name), minimized (based on redundancy due to protocol
/// inheritance) protocol list. If the sorted, minimized list is a single
/// protocol, then the canonical type is that protocol type. Otherwise, it is
/// a composition of the protocols in that list.
class ProtocolCompositionType : public TypeBase, public llvm::FoldingSetNode {
  ArrayRef<Type> Protocols;
  
public:
  /// \brief Retrieve an instance of a protocol composition type with the
  /// given set of protocols.
  static Type get(const ASTContext &C, ArrayRef<Type> Protocols);
  
  /// \brief Retrieve the set of protocols composed to create this type.
  ArrayRef<Type> getProtocols() const { return Protocols; }

  /// \brief Return the protocols of this type in canonical order.
  void getAnyExistentialTypeProtocols(SmallVectorImpl<ProtocolDecl *> &protos);

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, Protocols);
  }
  static void Profile(llvm::FoldingSetNodeID &ID, ArrayRef<Type> Protocols);

  /// True if one or more of the protocols is class.
  bool requiresClass() const;
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::ProtocolComposition;
  }
  
private:
  static ProtocolCompositionType *build(const ASTContext &C,
                                        ArrayRef<Type> Protocols);

  ProtocolCompositionType(const ASTContext *Ctx, ArrayRef<Type> Protocols)
    : TypeBase(TypeKind::ProtocolComposition, /*Context=*/Ctx,
               RecursiveTypeProperties()),
      Protocols(Protocols) { }
};
BEGIN_CAN_TYPE_WRAPPER(ProtocolCompositionType, Type)
  /// In the canonical representation, these are all ProtocolTypes.
  CanTypeArrayRef getProtocols() const {
    return CanTypeArrayRef(getPointer()->getProtocols());
  }
  void getAnyExistentialTypeProtocols(SmallVectorImpl<ProtocolDecl *> &protos) {
    for (CanType proto : getProtocols()) {
      cast<ProtocolType>(proto).getAnyExistentialTypeProtocols(protos);
    }
  }
END_CAN_TYPE_WRAPPER(ProtocolCompositionType, Type)

/// LValueType - An l-value is a handle to a physical object.  The
/// type of that object uniquely determines the type of an l-value
/// for it.
///
/// L-values are not fully first-class in Swift:
///
///  A type is said to "carry" an l-value if
///   - it is an l-value type or
///   - it is a tuple and at least one of its element types
///     carries an l-value.
///
/// The type of a function argument may carry an l-value.  This is done by
/// annotating the bound variable with InOutType.
///
/// The type of a return value, local variable, or field may not
/// carry an l-value.
///
/// When inferring a value type from an expression whose type
/// carries an l-value, the carried l-value types are converted
/// to their object type.
class LValueType : public TypeBase {
  Type ObjectTy;

  LValueType(Type objectTy, const ASTContext *canonicalContext,
             RecursiveTypeProperties properties)
    : TypeBase(TypeKind::LValue, canonicalContext, properties),
      ObjectTy(objectTy) {}

public:
  static LValueType *get(Type type);

  Type getObjectType() const { return ObjectTy; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *type) {
    return type->getKind() == TypeKind::LValue;
  }
};
BEGIN_CAN_TYPE_WRAPPER(LValueType, Type)
  PROXY_CAN_TYPE_SIMPLE_GETTER(getObjectType)
  static CanLValueType get(CanType type) {
    return CanLValueType(LValueType::get(type));
  }
END_CAN_TYPE_WRAPPER(LValueType, Type)
  
/// InOutType - An inout qualified type is an argument to a function passed
/// with an explicit "Address of" operator.  It is read in and then written back
/// to after the callee function is done.  This also models the receiver of
/// @mutable methods on value types.
///
class InOutType : public TypeBase {
  Type ObjectTy;
  
  InOutType(Type objectTy, const ASTContext *canonicalContext,
            RecursiveTypeProperties properties)
  : TypeBase(TypeKind::InOut, canonicalContext, properties),
    ObjectTy(objectTy) {}
  
public:
  static InOutType *get(Type type);
  
  Type getObjectType() const { return ObjectTy; }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *type) {
    return type->getKind() == TypeKind::InOut;
  }
};
BEGIN_CAN_TYPE_WRAPPER(InOutType, Type)
PROXY_CAN_TYPE_SIMPLE_GETTER(getObjectType)
static CanInOutType get(CanType type) {
  return CanInOutType(InOutType::get(type));
}
END_CAN_TYPE_WRAPPER(InOutType, Type)


/// SubstitutableType - A reference to a type that can be substituted, i.e.,
/// an archetype or a generic parameter.
class SubstitutableType : public TypeBase {
  ArrayRef<ProtocolDecl *> ConformsTo;
  Type Superclass;

protected:
  SubstitutableType(TypeKind K, const ASTContext *C,
                    RecursiveTypeProperties properties,
                    ArrayRef<ProtocolDecl *> ConformsTo,
                    Type Superclass)
    : TypeBase(K, C, properties),
      ConformsTo(ConformsTo), Superclass(Superclass) { }

public:
  /// \brief Retrieve the name of this type.
  Identifier getName() const;

  /// \brief Retrieve the parent of this type, or null if this is a
  /// primary type.
  SubstitutableType *getParent() const;

  /// \brief Retrieve the archetype corresponding to this substitutable type.
  ArchetypeType *getArchetype();

  // FIXME: Temporary hack.
  bool isPrimary() const;
  unsigned getPrimaryIndex() const;

  /// getConformsTo - Retrieve the set of protocols to which this substitutable
  /// type shall conform.
  ArrayRef<ProtocolDecl *> getConformsTo() const { return ConformsTo; }
  
  /// requiresClass - True if the type can only be substituted with class types.
  /// This is true if the type conforms to one or more class protocols or has
  /// a superclass constraint.
  bool requiresClass() const;

  /// \brief Retrieve the superclass of this type, if such a requirement exists.
  Type getSuperclass() const { return Superclass; }

  /// \brief Return true if the archetype has any requirements at all.
  bool hasRequirements() const {
    return !getConformsTo().empty() || getSuperclass();
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() >= TypeKind::First_SubstitutableType &&
           T->getKind() <= TypeKind::Last_SubstitutableType;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(SubstitutableType, Type)

/// An archetype is a type that represents a runtime type that is
/// known to conform to some set of requirements.
///
/// Archetypes are used to represent generic type parameters and their
/// associated types, as well as the runtime type stored within an
/// existential container.
class ArchetypeType : public SubstitutableType {
public:
  typedef llvm::PointerUnion<AssociatedTypeDecl *, ProtocolDecl *>
    AssocTypeOrProtocolType;
  
  /// A nested type. Either a dependent associated archetype, or a concrete
  /// type (which may be a bound archetype from an outer context).
  typedef llvm::PointerUnion<ArchetypeType*, Type> NestedType;
  
  static Type getNestedTypeValue(NestedType t) {
    if (auto ty = t.dyn_cast<Type>())
      return ty;
    return t.get<ArchetypeType*>();
  }
  
private:
  llvm::PointerUnion<ArchetypeType *, TypeBase *> ParentOrOpened;
  AssocTypeOrProtocolType AssocTypeOrProto;
  Identifier Name;
  unsigned IndexIfPrimaryOrExistentialID;
  ArrayRef<std::pair<Identifier, NestedType>> NestedTypes;
  
public:
  /// getNew - Create a new archetype with the given name.
  ///
  /// The ConformsTo array will be copied into the ASTContext by this routine.
  static ArchetypeType *getNew(const ASTContext &Ctx, ArchetypeType *Parent,
                               AssocTypeOrProtocolType AssocTypeOrProto,
                               Identifier Name, ArrayRef<Type> ConformsTo,
                               Type Superclass,
                               Optional<unsigned> Index = Optional<unsigned>());

  /// getNew - Create a new archetype with the given name.
  ///
  /// The ConformsTo array will be minimized then copied into the ASTContext
  /// by this routine.
  static ArchetypeType *getNew(const ASTContext &Ctx, ArchetypeType *Parent,
                               AssocTypeOrProtocolType AssocTypeOrProto,
                               Identifier Name,
                               SmallVectorImpl<ProtocolDecl *> &ConformsTo,
                               Type Superclass,
                               Optional<unsigned> Index = Optional<unsigned>());

  /// Create a new archetype that represents the opened type
  /// of an existential value.
  ///
  /// \param existential The existential type to open.
  ///
  /// \param knownID When non-empty, the known ID of the archetype. When empty,
  /// a fresh archetype with a unique ID will be opened.
  static ArchetypeType *getOpened(Type existential, 
                                  Optional<unsigned> knownID = Nothing);

  /// \brief Retrieve the name of this archetype.
  Identifier getName() const { return Name; }

  /// \brief Retrieve the fully-dotted name that should be used to display this
  /// archetype.
  std::string getFullName() const;

  /// \brief Retrieve the parent of this archetype, or null if this is a
  /// primary archetype.
  ArchetypeType *getParent() const { 
    return ParentOrOpened.dyn_cast<ArchetypeType *>(); 
  }

  /// Retrieve the opened existential type 
  Type getOpenedExistentialType() const {
    return ParentOrOpened.dyn_cast<TypeBase *>();
  }

  /// Retrieve the associated type to which this archetype (if it is a nested
  /// archetype) corresponds.
  ///
  /// This associated type will have the same name as the archetype and will
  /// be a member of one of the protocols to which the parent archetype
  /// conforms.
  AssociatedTypeDecl *getAssocType() const {
    return AssocTypeOrProto.dyn_cast<AssociatedTypeDecl *>();
  }

  /// Retrieve the protocol for which this archetype describes the 'Self'
  /// parameter.
  ProtocolDecl *getSelfProtocol() const {
    return AssocTypeOrProto.dyn_cast<ProtocolDecl *>();
  }
  
  /// True if this is the 'Self' parameter of a protocol or an associated type
  /// of 'Self'.
  bool isSelfDerived() {
    ArchetypeType *t = this;

    do {
      if (t->getSelfProtocol())
        return true;
    } while ((t = t->getParent()));

    return false;
  }

  /// Retrieve either the associated type or the protocol to which this
  /// associated type corresponds.
  AssocTypeOrProtocolType getAssocTypeOrProtocol() const {
    return AssocTypeOrProto;
  }

  /// \brief Retrieve the nested type with the given name.
  NestedType getNestedType(Identifier Name) const;
  
  Type getNestedTypeValue(Identifier Name) const {
    return getNestedTypeValue(getNestedType(Name));
  }
  
  /// \brief Check if the archetype contains a nested type with the given name.
  bool hasNestedType(Identifier Name) const;

  /// \brief Retrieve the nested types of this archetype.
  ArrayRef<std::pair<Identifier, NestedType>> getNestedTypes() const {
    return NestedTypes;
  }

  /// \brief Set the nested types to a copy of the given array of
  /// archetypes, which will first be sorted in place.
  void setNestedTypes(ASTContext &Ctx,
                      MutableArrayRef<std::pair<Identifier, NestedType>> Nested);

  /// isPrimary - Determine whether this is the archetype for a 'primary'
  /// archetype, e.g., 
  bool isPrimary() const { 
    return IndexIfPrimaryOrExistentialID > 0 && 
           getOpenedExistentialType().isNull();
  }

  // getPrimaryIndex - For a primary archetype, return the zero-based index.
  unsigned getPrimaryIndex() const {
    assert(isPrimary() && "Non-primary archetype does not have index");
    return IndexIfPrimaryOrExistentialID - 1;
  }

  /// Retrieve the ID number of this opened existential.
  unsigned getOpenedExistentialID() const {
    assert(getOpenedExistentialType() && "Not an opened existential archetype");
    return IndexIfPrimaryOrExistentialID;
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Archetype;
  }
  
  /// Convert an archetype to a dependent generic parameter type using the
  /// given mapping of primary archetypes to generic parameter types.
  Type getAsDependentType(
                    const llvm::DenseMap<ArchetypeType *, Type> &archetypeMap);
  
private:
  ArchetypeType(const ASTContext &Ctx, ArchetypeType *Parent,
                AssocTypeOrProtocolType AssocTypeOrProto,
                Identifier Name, ArrayRef<ProtocolDecl *> ConformsTo,
                Type Superclass, Optional<unsigned> Index)
    : SubstitutableType(TypeKind::Archetype, &Ctx,
                        RecursiveTypeProperties(),
                        ConformsTo, Superclass),
      ParentOrOpened(Parent), AssocTypeOrProto(AssocTypeOrProto), Name(Name),
      IndexIfPrimaryOrExistentialID(Index? *Index + 1 : 0) { }

  ArchetypeType(const ASTContext &Ctx, 
                Type Existential,
                unsigned ID,
                ArrayRef<ProtocolDecl *> ConformsTo,
                Type Superclass)
    : SubstitutableType(TypeKind::Archetype, &Ctx,
                        RecursiveTypeProperties(),
                        ConformsTo, Superclass),
      ParentOrOpened(Existential.getPointer()), 
      IndexIfPrimaryOrExistentialID(ID) { }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(ArchetypeType, SubstitutableType)

/// Abstract class used to describe the type of a generic type parameter
/// or associated type.
///
/// \sa AbstractTypeParamDecl
class AbstractTypeParamType : public SubstitutableType {
protected:
  AbstractTypeParamType(TypeKind kind, const ASTContext *ctx,
                        RecursiveTypeProperties properties)
    : SubstitutableType(kind, ctx, properties, { }, Type()) { }

public:
  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() >= TypeKind::First_AbstractTypeParamType &&
           T->getKind() <= TypeKind::Last_AbstractTypeParamType;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(AbstractTypeParamType, SubstitutableType)

/// Describes the type of a generic parameter.
///
/// \sa GenericTypeParamDecl
class GenericTypeParamType : public AbstractTypeParamType {
  /// The generic type parameter or depth/index.
  llvm::PointerUnion<GenericTypeParamDecl *, Fixnum<31>> ParamOrDepthIndex;

public:
  /// Retrieve a generic type parameter at the given depth and index.
  static GenericTypeParamType *get(unsigned depth, unsigned index,
                                   const ASTContext &ctx);

  /// Retrieve the declaration of the generic type parameter, or null if
  /// there is no such declaration.
  GenericTypeParamDecl *getDecl() const {
    return ParamOrDepthIndex.dyn_cast<GenericTypeParamDecl *>();
  }

  /// Get the name of the generic type parameter.
  Identifier getName() const;
  
  /// The depth of this generic type parameter, i.e., the number of outer
  /// levels of generic parameter lists that enclose this type parameter.
  ///
  /// \code
  /// struct X<T> {
  ///   func f<U>() { }
  /// }
  /// \endcode
  ///
  /// Here 'T' has depth 0 and 'U' has depth 1. Both have index 0.
  unsigned getDepth() const;

  /// The index of this generic type parameter within its generic parameter
  /// list.
  ///
  /// \code
  /// struct X<T, U> {
  ///   func f<V>() { }
  /// }
  /// \endcode
  ///
  /// Here 'T' and 'U' have indexes 0 and 1, respectively. 'V' has index 0.
  unsigned getIndex() const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::GenericTypeParam;
  }

private:
  friend class GenericTypeParamDecl;

  explicit GenericTypeParamType(GenericTypeParamDecl *param)
    : AbstractTypeParamType(TypeKind::GenericTypeParam, nullptr,
                            RecursiveTypeProperties::IsDependent),
      ParamOrDepthIndex(param) { }

  explicit GenericTypeParamType(unsigned depth,
                                unsigned index,
                                const ASTContext &ctx)
    : AbstractTypeParamType(TypeKind::GenericTypeParam, &ctx,
                            RecursiveTypeProperties::IsDependent),
      ParamOrDepthIndex(depth << 16 | index) { }
};
BEGIN_CAN_TYPE_WRAPPER(GenericTypeParamType, AbstractTypeParamType)
  static CanGenericTypeParamType get(unsigned depth, unsigned index,
                                     const ASTContext &C) {
    return CanGenericTypeParamType(GenericTypeParamType::get(depth, index, C));
  }
END_CAN_TYPE_WRAPPER(GenericTypeParamType, AbstractTypeParamType)

/// Describes the type of an associated type.
///
/// \sa AssociatedTypeDecl
class AssociatedTypeType : public AbstractTypeParamType {
  /// The generic type parameter.
  AssociatedTypeDecl *AssocType;

public:
  /// Retrieve the declaration of the associated type.
  AssociatedTypeDecl *getDecl() const { return AssocType; }

  /// Remove one level of top-level sugar from this type.
  TypeBase *getSinglyDesugaredType();

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::AssociatedType;
  }

private:
  friend class AssociatedTypeDecl;

  // These aren't classified as dependent for some reason.

  AssociatedTypeType(AssociatedTypeDecl *assocType)
    : AbstractTypeParamType(TypeKind::AssociatedType, nullptr,
                            RecursiveTypeProperties()),
      AssocType(assocType) { }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(AssociatedTypeType, AbstractTypeParamType)

/// SubstitutedType - A type that has been substituted for some other type,
/// which implies that the replacement type meets all of the requirements of
/// the original type.
class SubstitutedType : public TypeBase {
  // SubstitutedTypes are never canonical.
  explicit SubstitutedType(Type Original, Type Replacement,
                           RecursiveTypeProperties properties)
    : TypeBase(TypeKind::Substituted, nullptr, properties),
      Original(Original), Replacement(Replacement) {}
  
  Type Original;
  Type Replacement;
  
public:
  static SubstitutedType *get(Type Original, Type Replacement,
                              const ASTContext &C);
  
  /// \brief Retrieve the original type that is being replaced.
  Type getOriginal() const { return Original; }
  
  /// \brief Retrieve the replacement type.
  Type getReplacementType() const { return Replacement; }
  
  /// Remove one level of top-level sugar from this type.
  TypeBase *getSinglyDesugaredType();

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::Substituted;
  }
};

/// A type that refers to a member type of some type that is dependent on a
/// generic parameter.
class DependentMemberType : public TypeBase {
  Type Base;
  llvm::PointerUnion<Identifier, AssociatedTypeDecl *> NameOrAssocType;

  DependentMemberType(Type base, Identifier name, const ASTContext *ctx,
                      RecursiveTypeProperties properties)
    : TypeBase(TypeKind::DependentMember, ctx, properties),
      Base(base), NameOrAssocType(name) { }

  DependentMemberType(Type base, AssociatedTypeDecl *assocType,
                      const ASTContext *ctx,
                      RecursiveTypeProperties properties)
    : TypeBase(TypeKind::DependentMember, ctx, properties),
      Base(base), NameOrAssocType(assocType) { }

public:
  static DependentMemberType *get(Type base, Identifier name,
                                  const ASTContext &ctx);
  static DependentMemberType *get(Type base, AssociatedTypeDecl *assocType,
                                  const ASTContext &ctx);

  /// Retrieve the base type.
  Type getBase() const { return Base; }

  /// Retrieve the name of the member type.
  Identifier getName() const;

  /// Retrieve the associated type referenced as a member.
  ///
  /// The associated type will only be available after successful type checking.
  AssociatedTypeDecl *getAssocType() const {
    return NameOrAssocType.dyn_cast<AssociatedTypeDecl *>();
  }
  
  /// Substitute the base type, looking up our associated type in it if it is
  /// non-dependent. Returns null if the member could not be found in the new
  /// base.
  Type substBaseType(Module *M,
                     Type base,
                     LazyResolver *resolver = nullptr);

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::DependentMember;
  }
};
BEGIN_CAN_TYPE_WRAPPER(DependentMemberType, Type)
  static CanDependentMemberType get(CanType base, AssociatedTypeDecl *assocType,
                                    const ASTContext &C) {
    return CanDependentMemberType(DependentMemberType::get(base, assocType, C));
  }

  PROXY_CAN_TYPE_SIMPLE_GETTER(getBase)
END_CAN_TYPE_WRAPPER(DependentMemberType, Type)


/// \brief The storage type of a variable with non-strong reference
/// ownership semantics.
///
/// The referent type always satisfies allowsOwnership().
///
/// These types may appear in the AST only as the type of a variable;
/// getTypeOfReference strips this layer from the formal type of a
/// reference to the variable.  However, it is extremely useful to
/// represent this as a distinct type in SIL and IR-generation.
class ReferenceStorageType : public TypeBase {
protected:
  ReferenceStorageType(TypeKind kind, Type referent, const ASTContext *C,
                       RecursiveTypeProperties properties)
    : TypeBase(kind, C, properties), Referent(referent) {}

private:
  Type Referent;
public:
  static ReferenceStorageType *get(Type referent, Ownership ownership,
                                   const ASTContext &C);

  Type getReferentType() const { return Referent; }
  Ownership getOwnership() const {
    return (getKind() == TypeKind::WeakStorage ? Ownership::Weak
                                               : Ownership::Unowned);
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() >= TypeKind::First_ReferenceStorageType &&
           T->getKind() <= TypeKind::Last_ReferenceStorageType;
  }
};
BEGIN_CAN_TYPE_WRAPPER(ReferenceStorageType, Type)
  PROXY_CAN_TYPE_SIMPLE_GETTER(getReferentType)
END_CAN_TYPE_WRAPPER(ReferenceStorageType, Type)

/// \brief The storage type of a variable with [unowned] ownership semantics.
class UnownedStorageType : public ReferenceStorageType {
  friend class ReferenceStorageType;
  UnownedStorageType(Type referent, const ASTContext *C,
                     RecursiveTypeProperties properties)
    : ReferenceStorageType(TypeKind::UnownedStorage, referent, C, properties) {}

public:
  static UnownedStorageType *get(Type referent, const ASTContext &C) {
    return static_cast<UnownedStorageType*>(
                 ReferenceStorageType::get(referent, Ownership::Unowned, C));
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::UnownedStorage;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(UnownedStorageType, ReferenceStorageType)

/// \brief The storage type of a variable with [weak] ownership semantics.
class WeakStorageType : public ReferenceStorageType {
  friend class ReferenceStorageType;
  WeakStorageType(Type referent, const ASTContext *C,
                  RecursiveTypeProperties properties)
    : ReferenceStorageType(TypeKind::WeakStorage, referent, C, properties) {}

public:
  static WeakStorageType *get(Type referent, const ASTContext &C) {
    return static_cast<WeakStorageType*>(
                    ReferenceStorageType::get(referent, Ownership::Weak, C));
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::WeakStorage;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(WeakStorageType, ReferenceStorageType)

/// \brief A type variable used during type checking.
class TypeVariableType : public TypeBase {
  TypeVariableType(const ASTContext &C, unsigned ID)
    : TypeBase(TypeKind::TypeVariable, &C,
               RecursiveTypeProperties::HasTypeVariable) {
    TypeVariableTypeBits.ID = ID;
  }

  class Implementation;
  
public:
  /// \brief Create a new type variable whose implementation is constructed
  /// with the given arguments.
  template<typename ...Args>
  static TypeVariableType *getNew(const ASTContext &C, unsigned ID,
                                  Args &&...args);

  /// \brief Retrieve the implementation data corresponding to this type
  /// variable.
  ///
  /// The contents of the implementation data for this type are hidden in the
  /// details of the constraint solver used for type checking.
  Implementation &getImpl() {
    return *reinterpret_cast<Implementation *>(this + 1);
  }

  /// \brief Retrieve the implementation data corresponding to this type
  /// variable.
  ///
  /// The contents of the implementation data for this type are hidden in the
  /// details of the constraint solver used for type checking.
  const Implementation &getImpl() const {
    return *reinterpret_cast<const Implementation *>(this + 1);
  }

  /// \brief Access the implementation object for this type variable.
  Implementation *operator->() {
    return reinterpret_cast<Implementation *>(this + 1);
  }

  unsigned getID() const { return TypeVariableTypeBits.ID; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const TypeBase *T) {
    return T->getKind() == TypeKind::TypeVariable;
  }
};
DEFINE_EMPTY_CAN_TYPE_WRAPPER(TypeVariableType, Type)


inline bool TypeBase::isExistentialType() {
  return getCanonicalType().isExistentialType();
}

inline bool TypeBase::isAnyExistentialType() {
  return getCanonicalType().isAnyExistentialType();
}

inline bool CanType::isExistentialTypeImpl(CanType type) {
  return isa<ProtocolType>(type) || isa<ProtocolCompositionType>(type);
}

inline bool CanType::isAnyExistentialTypeImpl(CanType type) {
  return isExistentialTypeImpl(type) || isa<ExistentialMetatypeType>(type);
}

inline bool TypeBase::isClassExistentialType() {
  CanType T = getCanonicalType();
  if (auto pt = dyn_cast<ProtocolType>(T))
    return pt->requiresClass();
  if (auto pct = dyn_cast<ProtocolCompositionType>(T))
    return pct->requiresClass();
  return false;
}

inline ClassDecl *TypeBase::getClassOrBoundGenericClass() {
  return getCanonicalType().getClassOrBoundGenericClass();
}

inline ClassDecl *CanType::getClassOrBoundGenericClass() const {
  if (auto classTy = dyn_cast<ClassType>(*this))
    return classTy->getDecl();

  if (auto boundTy = dyn_cast<BoundGenericClassType>(*this))
    return boundTy->getDecl();

  return nullptr;
}

inline StructDecl *TypeBase::getStructOrBoundGenericStruct() {
  return getCanonicalType().getStructOrBoundGenericStruct();
}

inline StructDecl *CanType::getStructOrBoundGenericStruct() const {
  if (auto structTy = dyn_cast<StructType>(*this))
    return structTy->getDecl();

  if (auto boundTy = dyn_cast<BoundGenericStructType>(*this))
    return boundTy->getDecl();
  
  return nullptr;
}

inline EnumDecl *TypeBase::getEnumOrBoundGenericEnum() {
  return getCanonicalType().getEnumOrBoundGenericEnum();
}

inline EnumDecl *CanType::getEnumOrBoundGenericEnum() const {
  if (auto enumTy = dyn_cast<EnumType>(*this))
    return enumTy->getDecl();

  if (auto boundTy = dyn_cast<BoundGenericEnumType>(*this))
    return boundTy->getDecl();
  
  return nullptr;
}

inline NominalTypeDecl *TypeBase::getNominalOrBoundGenericNominal() {
  return getCanonicalType().getNominalOrBoundGenericNominal();
}

inline NominalTypeDecl *CanType::getNominalOrBoundGenericNominal() const {
  if (auto nomTy = dyn_cast<NominalType>(*this))
    return nomTy->getDecl();

  if (auto boundTy = dyn_cast<BoundGenericType>(*this))
    return boundTy->getDecl();
  
  return nullptr;
}

inline NominalTypeDecl *TypeBase::getAnyNominal() {
  return getCanonicalType().getAnyNominal();
}

inline NominalTypeDecl *CanType::getAnyNominal() const {
  if (auto nominalTy = dyn_cast<NominalType>(*this))
    return nominalTy->getDecl();

  if (auto boundTy = dyn_cast<BoundGenericType>(*this))
    return boundTy->getDecl();

  if (auto unboundTy = dyn_cast<UnboundGenericType>(*this))
    return unboundTy->getDecl();

  return nullptr;
}

inline bool TypeBase::isBuiltinIntegerType(unsigned n) {
  if (auto intTy = dyn_cast<BuiltinIntegerType>(getCanonicalType()))
    return intTy->getWidth().isFixedWidth()
      && intTy->getWidth().getFixedWidth() == n;
  return false;
}

/// getInOutObjectType - For an inout type, retrieves the underlying object
/// type.  Otherwise, returns the type itself.
inline Type TypeBase::getInOutObjectType() {
  if (auto iot = getAs<InOutType>())
    return iot->getObjectType();
  return this;
}

inline Type TypeBase::getRValueType() {
  if (auto lv = getAs<LValueType>())
    return lv->getObjectType();
  return this;
}
  
/// getLValueOrInOutObjectType - For an @lvalue or inout type, retrieves the
/// underlying object type. Otherwise, returns the type itself.
inline Type TypeBase::getLValueOrInOutObjectType() {
  if (auto iot = getAs<InOutType>())
    return iot->getObjectType();
  if (auto lv = getAs<LValueType>())
    return lv->getObjectType();
  return this;
}

/// For a ReferenceStorageType like @unowned, this returns the referent.
/// Otherwise, it returns the type itself.
inline Type TypeBase::getReferenceStorageReferent() {
  if (auto rst = getAs<ReferenceStorageType>())
    return rst->getReferentType();
  return this;
}

inline bool TypeBase::mayHaveSuperclass() {
  if (getClassOrBoundGenericClass())
    return true;

  if (auto archetype = getAs<ArchetypeType>())
    return (bool)archetype->requiresClass();

  return is<DynamicSelfType>();
}

inline TupleTypeElt::TupleTypeElt(Type ty,
                                  Identifier name,
                                  DefaultArgumentKind defArg,
                                  bool isVarArg)
  : Name(name), TyAndDefaultOrVarArg(ty.getPointer(),
                                     DefaultArgOrVarArg::None) {
  assert(!isVarArg || isa<ArraySliceType>(ty.getPointer()) ||
         (isa<BoundGenericType>(ty.getPointer()) &&
          ty->castTo<BoundGenericType>()->getGenericArgs().size() == 1));

  if (isVarArg) {
    assert(defArg == DefaultArgumentKind::None && "Defaulted vararg");
    TyAndDefaultOrVarArg.setInt(DefaultArgOrVarArg::VarArg);
  } else {
    switch (defArg) {
    case DefaultArgumentKind::None:
      break;

    case DefaultArgumentKind::Normal:
      TyAndDefaultOrVarArg.setInt(DefaultArgOrVarArg::DefaultArgument);
      break;

    case DefaultArgumentKind::Inherited:
      TyAndDefaultOrVarArg.setInt(DefaultArgOrVarArg::InheritedDefaultArgument);
      break;

    case DefaultArgumentKind::File:
      TyAndDefaultOrVarArg.setInt(DefaultArgOrVarArg::FileArgument);
      break;

    case DefaultArgumentKind::Line:
      TyAndDefaultOrVarArg.setInt(DefaultArgOrVarArg::LineArgument);
      break;

    case DefaultArgumentKind::Column:
      TyAndDefaultOrVarArg.setInt(DefaultArgOrVarArg::ColumnArgument);
      break;
        
    case DefaultArgumentKind::Function:
      TyAndDefaultOrVarArg.setInt(DefaultArgOrVarArg::FunctionArgument);
      break;
        
    }
  }
}

inline Type TupleTypeElt::getVarargBaseTy() const {
  TypeBase *T = getType().getPointer();
  if (ArraySliceType *AT = dyn_cast<ArraySliceType>(T))
    return AT->getBaseType();
  // It's the stdlib Array<T>.
  return cast<BoundGenericType>(T)->getGenericArgs()[0];
}

inline Identifier SubstitutableType::getName() const {
  if (auto Archetype = dyn_cast<ArchetypeType>(this))
    return Archetype->getName();
  if (auto GenericParam = dyn_cast<GenericTypeParamType>(this))
    return GenericParam->getName();
  if (auto DepMem = dyn_cast<DependentMemberType>(this))
    return DepMem->getName();

  llvm_unreachable("Not a substitutable type");
}

inline SubstitutableType *SubstitutableType::getParent() const {
  if (auto Archetype = dyn_cast<ArchetypeType>(this))
    return Archetype->getParent();

  return nullptr;
}

inline ArchetypeType *SubstitutableType::getArchetype() {
  if (auto Archetype = dyn_cast<ArchetypeType>(this))
    return Archetype;

  llvm_unreachable("Not a substitutable type");
}

inline bool SubstitutableType::isPrimary() const {
  if (auto Archetype = dyn_cast<ArchetypeType>(this))
    return Archetype->isPrimary();

  llvm_unreachable("Not a substitutable type");
}

inline unsigned SubstitutableType::getPrimaryIndex() const {
  if (auto Archetype = dyn_cast<ArchetypeType>(this))
    return Archetype->getPrimaryIndex();
  llvm_unreachable("Not a substitutable type");
}

inline CanType Type::getCanonicalTypeOrNull() const {
  return isNull() ? CanType() : getPointer()->getCanonicalType();
}

} // end namespace swift

namespace llvm {

// DenseMapInfo for BuiltinIntegerWidth.
template<>
struct DenseMapInfo<swift::BuiltinIntegerWidth> {
  using BuiltinIntegerWidth = swift::BuiltinIntegerWidth;
  
  static inline BuiltinIntegerWidth getEmptyKey() {
    return BuiltinIntegerWidth(BuiltinIntegerWidth::DenseMapEmpty);
  }
  
  static inline BuiltinIntegerWidth getTombstoneKey() {
    return BuiltinIntegerWidth(BuiltinIntegerWidth::DenseMapTombstone);
  }
  
  static unsigned getHashValue(BuiltinIntegerWidth w) {
    return DenseMapInfo<unsigned>::getHashValue(w.RawValue);
  }
  
  static bool isEqual(BuiltinIntegerWidth a, BuiltinIntegerWidth b) {
    return a == b;
  }
};


}
  
#endif
