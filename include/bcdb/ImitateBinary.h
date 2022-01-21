#ifndef BCDB_IMITATEBINARY_H
#define BCDB_IMITATEBINARY_H

#include <memory>
#include <string>
#include <vector>

namespace llvm {
class LLVMContext;
class Module;
namespace object {
class Binary;
} // end namespace object
} // end namespace llvm

namespace bcdb {

std::unique_ptr<llvm::Module>
ExtractModuleFromBinary(llvm::LLVMContext &Context, llvm::object::Binary &B);

bool AnnotateModuleWithBinary(llvm::Module &M, llvm::object::Binary &B);

std::vector<std::string> ImitateClangArgs(llvm::Module &M);
std::vector<std::string> ImitateLLCArgs(llvm::Module &M);

} // end namespace bcdb

#endif // BCDB_IMITATEBINARY_H
