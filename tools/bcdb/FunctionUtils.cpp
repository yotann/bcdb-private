#include "FunctionUtils.h"

#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <algorithm>
#include <unordered_map>

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/ScopedPrinter.h>
#include <llvm/Support/TarWriter.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/CodeExtractor.h>

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
    llvm_unreachable("unsupported type");
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
  std::sort(Names.begin(), Names.end());
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

  int i = 0;
  for (auto &func_id : *all_functions) {
    // Prevent memory leaks.
    if (i++ % 1024 == 0)
      bcdb.ResetContext();

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
        } /* else {
          std::vector<BasicBlock*> BBs;
          for (BasicBlock &BB : F)
            BBs.push_back(&BB);
          for (BasicBlock *BB : BBs) {
            CodeExtractor CE({BB}, nullptr);
            CE.extractCodeRegion();
          }
        } */
      }
    }
  }

  return Error::success();
}

static std::string getTmpFile(int pid, std::string suffix) {
  return "/tmp/bcdb-alive_" + std::to_string(pid) + "_" + suffix;
}

static void wait_for_one(unsigned &nchildren, BCDB &bcdb) {
  int wstatus;
  auto childpid = wait(&wstatus);
  if (childpid == -1 || !WIFEXITED(wstatus)) {
    fprintf(stderr, "Child %d didn't terminate normally\n", childpid);
    nchildren--;
    return;
  }

  std::string filename1 = getTmpFile(childpid, "in1");
  std::string filename2 = getTmpFile(childpid, "in2");
  if (remove(filename1.c_str()) == -1) {
    fprintf(stderr, "Error removing file1 for pid %d\n", childpid);
    perror("remove() error\n");
  }

  if (remove(filename2.c_str()) == -1) {
    fprintf(stderr, "Error removing file2 for pid %d\n", childpid);
    perror("remove() error\n");
  }

  // check the output file for results
  std::string filename_out = getTmpFile(childpid, "out");
  std::ifstream fout(filename_out);
  if (fout.is_open()) {
    std::stringstream buffer;
    buffer << fout.rdbuf();

    // find function ids:
    auto fn1 = buffer.str().find("fid1=");
    auto fn2 = buffer.str().find("fid2=");
    if (fn1 != std::string::npos && fn2 != std::string::npos) {
      int func_id1 = std::stoi(buffer.str().substr(fn1 + strlen("fid1=")));
      int func_id2 = std::stoi(buffer.str().substr(fn2 + strlen("fid2=")));

      bool t_correct = buffer.str().find("Transformation seems to be correct!")
                                                           != std::string::npos;
      bool rt_correct = buffer.str().find("Reverse transformation seems to be correct!")
                                                           != std::string::npos;
      if (t_correct || rt_correct) {
        if (t_correct) {
          bcdb.SetEquivalence(std::to_string(func_id1), std::to_string(func_id2));
        }
        if (rt_correct) {
          bcdb.SetEquivalence(std::to_string(func_id2), std::to_string(func_id1));
        }
      }
    }
    else {
      fprintf(stderr,
              "Output file for pid %d doesn't contain function ids\n",
              childpid);
    }

    if (remove(filename_out.c_str()) == -1) {
      fprintf(stderr, "Error removing fileout for pid %d\n", childpid);
      perror("Error removing\n");
    }
  }

  nchildren--;
}

Error bcdb::WriteFnEquivalenceInformation(BCDB &bcdb, StringRef AliveTvPath) {
  Expected<std::vector<std::string>> all_functions = bcdb.ListAllFunctions();
  if (!all_functions)
    return all_functions.takeError();

  auto total = all_functions.get().size();
  unsigned  progressCounter = 0;
  std::unordered_map<std::string, std::vector<std::string>> funcid_buckets;
  unsigned i = 0;
  for (auto &func_id : *all_functions) {
    if (i % (total / 10) == 0) {
      fprintf(stdout, "%d0%% function ids processed...\n", progressCounter++);
    }

    // Prevent memory leaks.
    if (i++ % 1024 == 0)
      bcdb.ResetContext();

    auto MOrErr = bcdb.GetFunctionById(func_id);
    if (!MOrErr)
      return MOrErr.takeError();
    auto M = std::move(*MOrErr);

    for (Function &F : *M) {
      if (!F.isDeclaration()) {
        if (F.size() == 1) {
          auto Name = hashFunctionType(F) + "/" + hashModuleGlobals(*M);

          if (funcid_buckets.find(Name) != funcid_buckets.end()) {
            funcid_buckets[Name].push_back(func_id);
          } else {
            funcid_buckets[Name] = {func_id};
          }
        }
      }
    }
  }
  fprintf(stdout, "Function id processing done\n");

  unsigned maxThreads = std::thread::hardware_concurrency();
  unsigned nchildren = 0;
  for (auto& element : funcid_buckets) {
    auto& values = element.second;
    unsigned memLeakCounter = 0;
    for (size_t i = 0; i < values.size(); i++) {
      for (size_t j = i + 1; j < values.size(); j++) {
        // prevent mem leak
        if (memLeakCounter++ % 512 == 0)
          bcdb.ResetContext();

        auto MOrErr = bcdb.GetFunctionById(values[i]);
        if (!MOrErr)
          return MOrErr.takeError();
        auto M1 = std::move(*MOrErr);
        SmallVector<char, 0> Buffer1;
        WriteAlignedModule(*M1, Buffer1);

        MOrErr = bcdb.GetFunctionById(values[j]);
        if (!MOrErr)
          return MOrErr.takeError();
        auto M2 = std::move(*MOrErr);
        SmallVector<char, 0> Buffer2;
        WriteAlignedModule(*M2, Buffer2);

        if (nchildren > maxThreads)
          wait_for_one(nchildren, bcdb);

        if (fork() == 0) {
          // preparing input files for alive-tv
          std::string temp_in1 = getTmpFile(getpid(), "in1");
          std::string temp_in2 = getTmpFile(getpid(), "in2");
          int fd1 = open(temp_in1.c_str(), O_CREAT | O_RDWR | O_EXCL, S_IRUSR);
          if (fd1 == -1) {
            printf("Input1 file %s open failed!\n", temp_in1.c_str());
            perror("Input1 file open failed\n");
            std::exit(-1);
          }
          int fd2 = open(temp_in2.c_str(), O_CREAT | O_RDWR | O_EXCL, S_IRUSR);
          if (fd2 == -1) {
            printf("Input2 file %s open failed!\n", temp_in2.c_str());
            perror("Input2 file open failed!\n");
            std::exit(-1);
          }
          auto fd1_nwrite = write(fd1, Buffer1.data(), Buffer1.size());
          if (fd1_nwrite == -1) {
            perror("Error writing to input1\n");
            std::exit(-1);
          }
          if (close(fd1) == -1) {
            perror("Error closing input1\n");
            std::exit(-1);
          }
          auto fd2_nwrite = write(fd2, Buffer2.data(), Buffer2.size());
          if (fd2_nwrite == -1) {
            perror("Error writing to input2\n");
            std::exit(-1);
          }
          if (close(fd2) == -1) {
            perror("Error closing input2\n");
            std::exit(-1);
          }

          std::string temp_out = getTmpFile(getpid(), "out");
          int fd_out = open(temp_out.c_str(), O_CREAT | O_RDWR | O_EXCL, S_IRUSR);
          if (fd_out == -1) {
            printf("Output file %s open failed!\n", temp_out.c_str());
            perror("Error opening output file\n");
            std::exit(-1);
          }
          int fout_nwrite = dprintf(fd_out, "fid1=%s\nfid2=%s\n",
                                    values[i].data(), values[j].data());
          if (fout_nwrite == -1) {
            perror("Error writing to output file\n");
            std::exit(-1);
          }

          // redirect all std output of child to /dev/null
          int devNull = -1;
          if ((devNull = open("/dev/null", O_WRONLY)) != -1) {
            dup2(devNull, 1);
          } else {
            perror("Cannot open /dev/null\n");
          }

          // redirect all errors to file
          dup2(fd_out, 2);
          execl(AliveTvPath.str().c_str(), "alive-tv",
                  "--bidirectional", temp_in1.c_str(), temp_in2.c_str(), NULL);
          perror("Exec failed!");
          std::exit(-1);
        }

        // this is parent:
        nchildren++;
      }
    }
  }

  while (nchildren > 0)
    wait_for_one(nchildren, bcdb);

  return Error::success();
}