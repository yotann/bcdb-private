#ifndef EXTRACT_BASIC_FUNCTIONS_H
#define EXTRACT_BASIC_FUNCTIONS_H

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>

#include "bcdb/BCDB.h"

namespace bcdb {

llvm::Error ExtractBasicFunctions(BCDB &bcdb, llvm::StringRef dest_path);
llvm::Error WriteFnEquivalenceInformation(BCDB &bcdb, llvm::StringRef AliveTvPath);
} // end namespace bcdb

#endif
