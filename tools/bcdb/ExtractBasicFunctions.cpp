#include "ExtractBasicFunctions.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TarWriter.h>

#include "bcdb/AlignBitcode.h"

using namespace bcdb;
using namespace llvm;

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
          SmallVector<char, 0> Buffer;
          WriteAlignedModule(*M, Buffer);
          Tar->append(func_id + ".bc", StringRef(Buffer.data(), Buffer.size()));
        }
      }
    }
  }

  return Error::success();
}
