#include "bcdb/Outlining/SizeModel.h"

#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/CodeGen/AsmPrinter.h>
#include <llvm/CodeGen/MachineFunctionPass.h>
#include <llvm/CodeGen/MachineModuleInfo.h>
#include <llvm/CodeGen/Passes.h>
#include <llvm/CodeGen/TargetPassConfig.h>
#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/MCCodeEmitter.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/Debugify.h>
#include <memory>
#include <vector>

#include "bcdb/LLVMCompat.h"

using namespace bcdb;
using namespace llvm;

namespace {
// Prints a module with comments showing the size model results, for debugging.
class SizeModelWriter : public AssemblyAnnotationWriter {
  const SizeModelResults *size_model;

public:
  SizeModelWriter(const SizeModelResults *size_model)
      : size_model(size_model) {}

  void emitInstructionAnnot(const Instruction *I,
                            formatted_raw_ostream &os) override {
    auto it = size_model->instruction_sizes.find(I);
    if (it == size_model->instruction_sizes.end())
      return;
    os << formatv("; {0} bytes\n", it->second);
  }
};
} // end anonymous namespace

namespace {
// Track sizes of machine instructions.
//
// Normally, MCStreamer instances are used to write assembly files or object
// files. SizingStreamer doesn't write any files; it just tracks debug line
// numbers, and calculates the total size of all instructions associated with a
// given line number.
struct SizingStreamer : public MCStreamer {
  std::vector<unsigned> &sizes;
  MCCodeEmitter &mce;
  const MCSubtargetInfo &sti;
  unsigned current_line = 0;

  explicit SizingStreamer(std::vector<unsigned> &sizes, MCContext &context,
                          MCCodeEmitter &mce, const MCSubtargetInfo &sti)
      : MCStreamer(context), sizes(sizes), mce(mce), sti(sti) {}

  // Must implement (pure virtual function).
#if LLVM_VERSION_MAJOR >= 11
  bool emitSymbolAttribute(MCSymbol *, MCSymbolAttr) override {
#else
  bool EmitSymbolAttribute(MCSymbol *, MCSymbolAttr) override {
#endif
    return false; // not supported
  }

  // Must implement (pure virtual function).
#if LLVM_VERSION_MAJOR >= 11
  void emitCommonSymbol(MCSymbol *, uint64_t, unsigned) override {}
#else
  void EmitCommonSymbol(MCSymbol *, uint64_t, unsigned) override {}
#endif

  // Must implement (pure virtual function).
#if LLVM_VERSION_MAJOR >= 11
  void emitZerofill(MCSection *, MCSymbol *, uint64_t Size,
                    unsigned ByteAlignment, SMLoc Loc) override {}
#else
  void EmitZerofill(MCSection *, MCSymbol *, uint64_t Size,
                    unsigned ByteAlignment, SMLoc Loc) override {}
#endif

#if LLVM_VERSION_MAJOR >= 11
  void emitInstruction(const MCInst &inst,
                       const MCSubtargetInfo &sti) override {
    MCStreamer::emitInstruction(inst, sti);
#else
  void EmitInstruction(const MCInst &inst,
                       const MCSubtargetInfo &sti) override {
    MCStreamer::EmitInstruction(inst, sti);
#endif

    SmallVector<char, 256> buffer;
    raw_svector_ostream os(buffer);
    SmallVector<MCFixup, 4> fixups;
    mce.encodeInstruction(inst, os, fixups, sti);
    sizes[current_line] += os.str().size();
  }

#if LLVM_VERSION_MAJOR >= 11
  void emitDwarfLocDirective(unsigned file_no, unsigned line, unsigned column,
                             unsigned flags, unsigned isa,
                             unsigned discriminator,
                             StringRef filename) override {
    MCStreamer::emitDwarfLocDirective(file_no, line, column, flags, isa,
                                      discriminator, filename);
#else
  void EmitDwarfLocDirective(unsigned file_no, unsigned line, unsigned column,
                             unsigned flags, unsigned isa,
                             unsigned discriminator,
                             StringRef filename) override {
    MCStreamer::EmitDwarfLocDirective(file_no, line, column, flags, isa,
                                      discriminator, filename);
#endif
    current_line = line;
    if (current_line >= sizes.size())
      sizes.resize(current_line + 1);
  }

#if LLVM_VERSION_MAJOR >= 11
  void emitCVLocDirective(unsigned function_id, unsigned file_no, unsigned line,
                          unsigned column, bool prologue_end, bool is_stmt,
                          StringRef filename, SMLoc loc) override {
    MCStreamer::emitCVLocDirective(function_id, file_no, line, column,
                                   prologue_end, is_stmt, filename, loc);
#else
  void EmitCVLocDirective(unsigned function_id, unsigned file_no, unsigned line,
                          unsigned column, bool prologue_end, bool is_stmt,
                          StringRef filename, SMLoc loc) override {
    MCStreamer::EmitCVLocDirective(function_id, file_no, line, column,
                                   prologue_end, is_stmt, filename, loc);
#endif
    current_line = line;
    if (current_line >= sizes.size())
      sizes.resize(current_line + 1);
  }
};
} // end anonymous namespace

SizeModelResults::SizeModelResults(Module &m) : m(m) {
  // We need to run transformations on the module in order to compile it and
  // measure sizes, but we shouldn't modify the original module. So we make a
  // clone of it.
  auto cloned = CloneModule(m);
  DenseMap<Instruction *, Instruction *> cloned_to_orig;

  // Associate cloned instructions with original instructions.
  // We need to do this before making any changes to the cloned module.
  for (auto f_orig = m.begin(), f_cloned = cloned->begin();
       f_orig != m.end() && f_cloned != cloned->end(); ++f_orig, ++f_cloned) {
    if (f_orig->hasName() || f_cloned->hasName())
      assert(f_orig->getName() == f_cloned->getName());
    for (auto bb_orig = f_orig->begin(), bb_cloned = f_cloned->begin();
         bb_orig != f_orig->end() && bb_cloned != f_cloned->end();
         ++bb_orig, ++bb_cloned) {
      for (auto i_orig = bb_orig->begin(), i_cloned = bb_cloned->begin();
           i_orig != bb_orig->end() && i_cloned != bb_cloned->end();
           ++i_orig, ++i_cloned) {
        assert(i_orig->getOpcode() == i_cloned->getOpcode());
        cloned_to_orig[&*i_cloned] = &*i_orig;
      }
    }
  }

  // Debugify doesn't do anything if llvm.dbg.cu already exists.
  auto dbg = cloned->getNamedMetadata("llvm.dbg.cu");
  if (dbg)
    cloned->eraseNamedMetadata(dbg);

  // Create fake debug information, which assigns a different line number to
  // each IR instruction in the module. We use these line numbers to track
  // which machine instructions correspond to which IR instructions.
  std::unique_ptr<ModulePass> debugify(createDebugifyModulePass());
  debugify->runOnModule(*cloned);

  // Now we actually compile the module, using our custom MCStreamer
  // implementation that calculates instruction sizes without actually writing
  // a file. We have to do a lot of steps manually in order to use a custom
  // MCStreamer!

  // Based on llvm/tools/llc/llc.cpp:
  std::string error;
  const Target *target =
      TargetRegistry::lookupTarget(cloned->getTargetTriple(), error);
  if (!target) {
    report_fatal_error("Can't find target triple: " + error);
  }
  TargetOptions options;
  std::unique_ptr<TargetMachine> target_machine(
      target->createTargetMachine(cloned->getTargetTriple(), "", "", options,
                                  None, None, CodeGenOpt::Default));
  TargetLibraryInfoImpl tlii(Triple(cloned->getTargetTriple()));
  LLVMTargetMachine &llvmtm = static_cast<LLVMTargetMachine &>(*target_machine);
  legacy::PassManager pm;
  pm.add(new TargetLibraryInfoWrapperPass(tlii));

  // Based on LLVMTargetMachine::addPassesToEmitMC:
  auto mmiwp = new MachineModuleInfoWrapperPass(&llvmtm);
  MCContext *context = &mmiwp->getMMI().getContext();
  const MCSubtargetInfo &sti = *llvmtm.getMCSubtargetInfo();
  const MCRegisterInfo &mri = *llvmtm.getMCRegisterInfo();
  MCCodeEmitter *mce =
      target->createMCCodeEmitter(*llvmtm.getMCInstrInfo(), mri, *context);
  if (!mce) {
    report_fatal_error("Can't create machine code emitter");
  }

  // Based on llvm's addPassesToGenerateCode:
  TargetPassConfig *pass_config = llvmtm.createPassConfig(pm);
  pass_config->setDisableVerify(true);
  pm.add(pass_config);
  pm.add(mmiwp);
  if (pass_config->addISelPasses())
    report_fatal_error("addISelPasses failed");
  pass_config->addMachinePasses();
  pass_config->setInitialized();

  // TODO: Our custom MCStreamer should work for all targets, including x86.
  // But most other targets support TargetInstrInfo::getInstSizeInBytes(),
  // which we could use in a custom MachineFunctionPass without setting up
  // AsmPrinter. Would there be any advantages to doing that for non-x86
  // targets?

  // Actually set up our custom MCStreamer, and perform compilation!
  std::vector<unsigned> sizes;
  std::unique_ptr<MCStreamer> asm_streamer(
      new SizingStreamer(sizes, *context, *mce, sti));
  target->createNullTargetStreamer(*asm_streamer);
  FunctionPass *printer =
      target->createAsmPrinter(llvmtm, std::move(asm_streamer));
  if (!printer)
    report_fatal_error("createAsmPrinter failed");
  pm.add(printer);
  pm.add(createFreeMachineFunctionPass());
  pm.run(*cloned);

  // Now we actually take the per-line-number sizes calculated by
  // SizingStreamer, find the corresponding IR instructions in the cloned
  // module, and map them to the original instructions.
  //
  // TODO: Sometimes the obvious mapping isn't quite right.
  //
  // - When multiple IR instructions are combined into one machine instruction,
  //   the size is only assigned to one of the IR instructions and the others
  //   get a size of 0. It would be better to spread the size across all of
  //   them, if we can heuristically detect which instructions were combined.
  //
  // - Some machine instructions don't have any corresponding IR instruction.
  //   (They get a line number of 0.) This can happen with machine instructions
  //   that e.g. clear a register for future use. It would be better to
  //   heuristically find a good place to assign that size. (Maybe by tracking
  //   the next instruction that uses the output of the unassigned
  //   instruction?)
  //
  // - On wasm32 and riscv32, the size of the prologue instructions gets added
  //   to the size of the first instruction.
  //
  // - On wasm32, the size of the end_function instruction gets added to the
  //   size of the last instruction.
  for (auto &f : *cloned) {
    for (auto &bb : f) {
      for (auto &i : bb) {
        assert(i.getDebugLoc()); // should be guaranteed by debugify
        unsigned line = i.getDebugLoc().getLine();
        unsigned size = line < sizes.size() ? sizes[line] : 0;
        Instruction *i_orig = cloned_to_orig[&i];
        instruction_sizes[i_orig] = size;
      }
    }
  }
}

void SizeModelResults::print(raw_ostream &os) const {
  SizeModelWriter writer(this);
  m.print(os, &writer);
}

SizeModelWrapperPass::SizeModelWrapperPass() : ModulePass(ID) {}

bool SizeModelWrapperPass::runOnModule(Module &m) {
  size_model.emplace(m);
  return false;
}

void SizeModelWrapperPass::print(raw_ostream &os, const Module *M) const {
  size_model->print(os);
}

void SizeModelWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

void SizeModelWrapperPass::releaseMemory() { size_model.reset(); }

void SizeModelWrapperPass::verifyAnalysis() const {
  assert(false && "unimplemented");
}

char SizeModelWrapperPass::ID = 0;
static RegisterPass<SizeModelWrapperPass>
    X("size-model", "Size Model Analysis Pass", false, true);
