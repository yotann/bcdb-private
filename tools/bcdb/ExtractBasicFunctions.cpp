#include "ExtractBasicFunctions.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TarWriter.h>

#include "bcdb/AlignBitcode.h"

using namespace bcdb;
using namespace llvm;

Error bcdb::ExtractBasicFunctions(BCDB &bcdb, StringRef dest_path) {
  Expected<std::unique_ptr<TarWriter>> tar_writer = TarWriter::create(dest_path, "basic_functions");
  if(!tar_writer) return tar_writer.takeError();

  std::vector<std::string> basic_functions;
  Expected<std::vector<std::string>> all_functions = bcdb.ListAllFunctions();
  if(!all_functions) return all_functions.takeError();
  for (auto &func_id : *all_functions){
    auto M = bcdb.GetFunctionById(func_id);
    if (!M) return M.takeError();
    for (Function &F : **M) {
      //skip declarations
      if (!F.isDeclaration()){
        if(F.size() == 1){
          SmallVector<char, 0> Buffer;
          WriteAlignedModule(**M, Buffer);
          basic_functions.push_back(func_id);
          //append file to archive
          std::string basic_func(Buffer.begin(), Buffer.end());
          (*tar_writer)->append(func_id + ".bc", basic_func);
        }
      }
    }
  }

  return Error::success();
}
