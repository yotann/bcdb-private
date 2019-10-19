#include "ExtractBasicFunctions.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ScopedPrinter.h>
#include <llvm/Support/TarWriter.h>
#include <llvm/Support/raw_ostream.h>

#include "bcdb/AlignBitcode.h"

using namespace bcdb;
using namespace llvm;

typedef DenseMap<Type *, unsigned> TypeNumberMap;

static void hashType(Type *T, TypeNumberMap &TNM, raw_ostream &os,
                     bool in_struct = false) {
  switch (T->getTypeID()) {
  case Type::VoidTyID:
    os << "v";
    break;

  case Type::HalfTyID:
    os << "h" << T->getPrimitiveSizeInBits();
    break;

  case Type::FloatTyID:
    os << "f" << T->getPrimitiveSizeInBits();
    break;

  case Type::DoubleTyID:
    os << "d" << T->getPrimitiveSizeInBits();
    break;

  case Type::FP128TyID:
    os << "g" << T->getPrimitiveSizeInBits();
    break;

  case Type::X86_FP80TyID:
  case Type::PPC_FP128TyID:
  case Type::X86_MMXTyID:
    os << "q" << T->getPrimitiveSizeInBits();
    break;

  case Type::IntegerTyID:
    switch (T->getPrimitiveSizeInBits()) {
    case 1:
      os << "b";
      break;
    case 8:
      os << "c";
      break;
    case 16:
      os << "s";
      break;
    case 32:
      os << "i";
      break;
    case 64:
      os << "l";
      break;
    case 128:
      os << "x";
      break;
    default:
      os << "n" << T->getPrimitiveSizeInBits();
      break;
    }
    break;

  case Type::LabelTyID:
    os << "Ql";
    break;

  case Type::MetadataTyID:
    os << "Qm";
    break;

  case Type::TokenTyID:
    os << "Qt";
    break;

  case Type::ArrayTyID:
    os << "A" << T->getArrayNumElements();
    hashType(T->getArrayElementType(), TNM, os);
    break;

  case Type::VectorTyID:
    os << "V" << T->getVectorNumElements();
    hashType(T->getVectorElementType(), TNM, os);
    break;

  case Type::StructTyID: {
    StructType *ST = cast<StructType>(T);
    if (ST->isOpaque()) {
      os << "O";
      break;
    }
    if (TNM.count(ST)) {
      os << "R" << TNM[ST];
      break;
    } else {
      unsigned num = TNM.size();
      TNM[ST] = num;
    }
    // TODO: isPacked, isLiteral
    if (!in_struct)
      os << "S";
    for (Type *ET : ST->elements())
      hashType(ET, TNM, os, /* in_struct */ true);
    if (!in_struct)
      os << "E";
    break;
  }

  case Type::FunctionTyID: {
    FunctionType *FT = cast<FunctionType>(T);
    os << "F";
    hashType(FT->getReturnType(), TNM, os);
    for (Type *ArgTy : FT->params())
      hashType(ArgTy, TNM, os);
    if (FT->isVarArg())
      os << "z";
    os << "E";
    break;
  }

  case Type::PointerTyID:
    if (T->getPointerElementType()->isStructTy() &&
        !T->getPointerAddressSpace()) {
      if (cast<StructType>(T->getPointerElementType())->isOpaque()) {
        os << "p";
        break;
      }
    }
    os << "P";
    if (T->getPointerAddressSpace())
      os << T->getPointerAddressSpace();
    hashType(T->getPointerElementType(), TNM, os);
    break;

  default:
    os << "?";
    break;
  }
}

static std::string hashFunctionType(Function &F) {
  std::string Str;
  raw_string_ostream os(Str);
  TypeNumberMap TNM;
  hashType(F.getFunctionType(), TNM, os);
  os.flush();
  if (Str.size() > 255)
    Str = to_string(hash_value(Str));
  return Str;
}

static std::string hashModuleGlobals(Module &M) {
  SmallVector<StringRef, 8> Names;
  for (GlobalVariable &GV : M.globals())
    if (GV.hasName())
      Names.push_back(GV.getName());
  for (Function &F : M.functions())
    if (F.hasName() && !F.isIntrinsic() &&
        F.getName() != "__gxx_personality_v0")
      Names.push_back(F.getName());
  sort(Names.begin(), Names.end());
  hash_code h = hash_combine_range(Names.begin(), Names.end());
  if (Names.empty())
    h = 0;
  return to_string(h);
}

Error bcdb::ExtractBasicFunctions(BCDB &bcdb, StringRef dest_path) {
  Expected<std::unique_ptr<TarWriter>> TarOrErr =
      TarWriter::create(dest_path, "functions");
  if (!TarOrErr)
    return TarOrErr.takeError();
  auto Tar = std::move(*TarOrErr);

  Expected<std::vector<std::string>> all_functions = bcdb.ListAllFunctions();
  if (!all_functions)
    return all_functions.takeError();

  for (auto &func_id : *all_functions) {
    auto MOrErr = bcdb.GetFunctionById(func_id);
    if (!MOrErr)
      return MOrErr.takeError();
    auto M = std::move(*MOrErr);

    for (Function &F : *M) {
      if (!F.isDeclaration()) {
        if (F.size() == 1) {
          auto Name = hashFunctionType(F) + "/" + hashModuleGlobals(*M);

          SmallVector<char, 0> Buffer;
          WriteAlignedModule(*M, Buffer);
          Tar->append(Name + "/" + func_id + ".bc",
                      StringRef(Buffer.data(), Buffer.size()));
        }
      }
    }
  }

  return Error::success();
}
