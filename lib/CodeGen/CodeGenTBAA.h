//===--- CodeGenTBAA.h - TBAA information for LLVM CodeGen ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the code that manages TBAA information and defines the TBAA policy
// for the optimizer to use.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CODEGENTBAA_H
#define LLVM_CLANG_LIB_CODEGEN_CODEGENTBAA_H

#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"

namespace clang {
  class ASTContext;
  class CodeGenOptions;
  class LangOptions;
  class MangleContext;
  class QualType;
  class Type;

namespace CodeGen {
class CGRecordLayout;

// TBAAAccessKind - A kind of TBAA memory access descriptor.
enum class TBAAAccessKind : unsigned {
  Ordinary,
  MayAlias,
};

// TBAAAccessInfo - Describes a memory access in terms of TBAA.
struct TBAAAccessInfo {
  TBAAAccessInfo(TBAAAccessKind Kind, llvm::MDNode *BaseType,
                 llvm::MDNode *AccessType, uint64_t Offset)
    : Kind(Kind), BaseType(BaseType), AccessType(AccessType), Offset(Offset)
  {}

  TBAAAccessInfo(llvm::MDNode *BaseType, llvm::MDNode *AccessType,
                 uint64_t Offset)
    : TBAAAccessInfo(TBAAAccessKind::Ordinary, BaseType, AccessType, Offset)
  {}

  explicit TBAAAccessInfo(llvm::MDNode *AccessType)
    : TBAAAccessInfo(/* BaseType= */ nullptr, AccessType, /* Offset= */ 0)
  {}

  TBAAAccessInfo()
    : TBAAAccessInfo(/* AccessType= */ nullptr)
  {}

  static TBAAAccessInfo getMayAliasInfo() {
    return TBAAAccessInfo(TBAAAccessKind::MayAlias, /* BaseType= */ nullptr,
                          /* AccessType= */ nullptr, /* Offset= */ 0);
  }

  bool isMayAlias() const { return Kind == TBAAAccessKind::MayAlias; }

  bool operator==(const TBAAAccessInfo &Other) const {
    return Kind == Other.Kind &&
           BaseType == Other.BaseType &&
           AccessType == Other.AccessType &&
           Offset == Other.Offset;
  }

  bool operator!=(const TBAAAccessInfo &Other) const {
    return !(*this == Other);
  }

  explicit operator bool() const {
    return *this != TBAAAccessInfo();
  }

  /// Kind - The kind of the access descriptor.
  TBAAAccessKind Kind;

  /// BaseType - The base/leading access type. May be null if this access
  /// descriptor represents an access that is not considered to be an access
  /// to an aggregate or union member.
  llvm::MDNode *BaseType;

  /// AccessType - The final access type. May be null if there is no TBAA
  /// information available about this access.
  llvm::MDNode *AccessType;

  /// Offset - The byte offset of the final access within the base one. Must be
  /// zero if the base access type is not specified.
  uint64_t Offset;
};

/// CodeGenTBAA - This class organizes the cross-module state that is used
/// while lowering AST types to LLVM types.
class CodeGenTBAA {
  ASTContext &Context;
  const CodeGenOptions &CodeGenOpts;
  const LangOptions &Features;
  MangleContext &MContext;

  // MDHelper - Helper for creating metadata.
  llvm::MDBuilder MDHelper;

  /// MetadataCache - This maps clang::Types to scalar llvm::MDNodes describing
  /// them.
  llvm::DenseMap<const Type *, llvm::MDNode *> MetadataCache;
  /// This maps clang::Types to a base access type in the type DAG.
  llvm::DenseMap<const Type *, llvm::MDNode *> BaseTypeMetadataCache;
  /// This maps TBAA access descriptors to tag nodes.
  llvm::DenseMap<TBAAAccessInfo, llvm::MDNode *> AccessTagMetadataCache;

  /// StructMetadataCache - This maps clang::Types to llvm::MDNodes describing
  /// them for struct assignments.
  llvm::DenseMap<const Type *, llvm::MDNode *> StructMetadataCache;

  llvm::MDNode *Root;
  llvm::MDNode *Char;

  /// getRoot - This is the mdnode for the root of the metadata type graph
  /// for this translation unit.
  llvm::MDNode *getRoot();

  /// getChar - This is the mdnode for "char", which is special, and any types
  /// considered to be equivalent to it.
  llvm::MDNode *getChar();

  /// CollectFields - Collect information about the fields of a type for
  /// !tbaa.struct metadata formation. Return false for an unsupported type.
  bool CollectFields(uint64_t BaseOffset,
                     QualType Ty,
                     SmallVectorImpl<llvm::MDBuilder::TBAAStructField> &Fields,
                     bool MayAlias);

  /// A wrapper function to create a scalar type. For struct-path aware TBAA,
  /// the scalar type has the same format as the struct type: name, offset,
  /// pointer to another node in the type DAG.
  llvm::MDNode *createTBAAScalarType(StringRef Name, llvm::MDNode *Parent);

public:
  CodeGenTBAA(ASTContext &Ctx, llvm::LLVMContext &VMContext,
              const CodeGenOptions &CGO,
              const LangOptions &Features,
              MangleContext &MContext);
  ~CodeGenTBAA();

  /// getTypeInfo - Get metadata used to describe accesses to objects of the
  /// given type.
  llvm::MDNode *getTypeInfo(QualType QTy);

  /// getVTablePtrAccessInfo - Get the TBAA information that describes an
  /// access to a virtual table pointer.
  TBAAAccessInfo getVTablePtrAccessInfo();

  /// getTBAAStructInfo - Get the TBAAStruct MDNode to be used for a memcpy of
  /// the given type.
  llvm::MDNode *getTBAAStructInfo(QualType QTy);

  /// getBaseTypeInfo - Get metadata that describes the given base access type.
  /// Return null if the type is not suitable for use in TBAA access tags.
  llvm::MDNode *getBaseTypeInfo(QualType QTy);

  /// getAccessTagInfo - Get TBAA tag for a given memory access.
  llvm::MDNode *getAccessTagInfo(TBAAAccessInfo Info);

  /// mergeTBAAInfoForCast - Get merged TBAA information for the purpose of
  /// type casts.
  TBAAAccessInfo mergeTBAAInfoForCast(TBAAAccessInfo SourceInfo,
                                      TBAAAccessInfo TargetInfo);

  /// mergeTBAAInfoForConditionalOperator - Get merged TBAA information for the
  /// purpose of conditional operator.
  TBAAAccessInfo mergeTBAAInfoForConditionalOperator(TBAAAccessInfo InfoA,
                                                     TBAAAccessInfo InfoB);
};

}  // end namespace CodeGen
}  // end namespace clang

namespace llvm {

template<> struct DenseMapInfo<clang::CodeGen::TBAAAccessInfo> {
  static clang::CodeGen::TBAAAccessInfo getEmptyKey() {
    unsigned UnsignedKey = DenseMapInfo<unsigned>::getEmptyKey();
    return clang::CodeGen::TBAAAccessInfo(
      static_cast<clang::CodeGen::TBAAAccessKind>(UnsignedKey),
      DenseMapInfo<MDNode *>::getEmptyKey(),
      DenseMapInfo<MDNode *>::getEmptyKey(),
      DenseMapInfo<uint64_t>::getEmptyKey());
  }

  static clang::CodeGen::TBAAAccessInfo getTombstoneKey() {
    unsigned UnsignedKey = DenseMapInfo<unsigned>::getTombstoneKey();
    return clang::CodeGen::TBAAAccessInfo(
      static_cast<clang::CodeGen::TBAAAccessKind>(UnsignedKey),
      DenseMapInfo<MDNode *>::getTombstoneKey(),
      DenseMapInfo<MDNode *>::getTombstoneKey(),
      DenseMapInfo<uint64_t>::getTombstoneKey());
  }

  static unsigned getHashValue(const clang::CodeGen::TBAAAccessInfo &Val) {
    auto KindValue = static_cast<unsigned>(Val.Kind);
    return DenseMapInfo<unsigned>::getHashValue(KindValue) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.BaseType) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.AccessType) ^
           DenseMapInfo<uint64_t>::getHashValue(Val.Offset);
  }

  static bool isEqual(const clang::CodeGen::TBAAAccessInfo &LHS,
                      const clang::CodeGen::TBAAAccessInfo &RHS) {
    return LHS == RHS;
  }
};

}  // end namespace llvm

#endif
