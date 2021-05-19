#include <cstdlib>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>

#include "bcdb/LLVMCompat.h"
#include "memodb/memodb.h"

using namespace llvm;

cl::OptionCategory MemoDBCategory("MemoDB options");

static cl::SubCommand GetCommand("get", "Get a value");
static cl::SubCommand ListHeadsCommand("list-heads", "List all heads");
static cl::SubCommand
    PutCommand("put", "Put a value, or find ID of an existing value");
static cl::SubCommand RefsToCommand("refs-to",
                                    "Find names that reference a value");
static cl::SubCommand SetCommand("set", "Set a head or a call result");

static cl::opt<std::string> UriOrEmpty(
    "uri", cl::Optional, cl::desc("URI of the database"),
    cl::init(std::string(StringRef::withNullAsEmpty(std::getenv("BCDB_URI")))),
    cl::cat(MemoDBCategory), cl::sub(*cl::AllSubCommands));

static StringRef GetUri() {
  if (UriOrEmpty.empty()) {
    report_fatal_error(
        "You must provide a database URI, such as sqlite:/tmp/example.bcdb, "
        "using the -uri option or the BCDB_URI environment variable.");
  }
  return UriOrEmpty;
}

// memodb_name options

static cl::opt<std::string> SourceURI(cl::Positional, cl::Required,
                                      cl::desc("<source URI>"),
                                      cl::value_desc("uri"),
                                      cl::cat(MemoDBCategory),
                                      cl::sub(GetCommand));

static cl::opt<std::string>
    TargetURI(cl::Positional, cl::Required, cl::desc("<target URI>"),
              cl::value_desc("uri"), cl::cat(MemoDBCategory),
              cl::sub(RefsToCommand), cl::sub(SetCommand));

static memodb_name GetNameFromURI(llvm::StringRef URI) {
  ParsedURI Parsed(URI);
  if (!Parsed.Authority.empty() || !Parsed.Query.empty() ||
      !Parsed.Fragment.empty())
    report_fatal_error("invalid name URI");
  if (Parsed.Scheme == "head")
    return memodb_head(Parsed.Path);
  else if (Parsed.Scheme == "id")
    return memodb_ref(Parsed.Path);
  else if (Parsed.Scheme == "call") {
    std::vector<memodb_ref> Args;
    if (Parsed.PathSegments.empty())
      report_fatal_error("invalid name URI");
    auto FuncName = Parsed.PathSegments.front();
    for (const auto &Arg : llvm::ArrayRef(Parsed.PathSegments).drop_front())
      Args.emplace_back(Arg);
    return memodb_call(FuncName, Args);
  } else
    report_fatal_error("invalid name URI");
}

// input options (XXX: must come after memodb_name options)

static cl::opt<std::string> InputURI(cl::Positional, cl::desc("<input URI>"),
                                     cl::init("-"), cl::value_desc("uri"),
                                     cl::cat(MemoDBCategory),
                                     cl::sub(PutCommand), cl::sub(SetCommand));

static memodb_ref ReadRef(memodb_db &Db, llvm::StringRef URI) {
  ExitOnError Err("value read: ");
  std::unique_ptr<MemoryBuffer> Buffer;
  if (URI == "-")
    Buffer = Err(errorOrToExpected(MemoryBuffer::getSTDIN()));
  else if (llvm::StringRef(URI).startswith("file:")) {
    ParsedURI Parsed(URI);
    if (!Parsed.Authority.empty() || !Parsed.Query.empty() ||
        !Parsed.Fragment.empty())
      report_fatal_error("invalid input URI");
    Buffer = Err(errorOrToExpected(MemoryBuffer::getFile(Parsed.Path)));
  } else {
    memodb_name Name = GetNameFromURI(URI);
    if (memodb_ref *Ref = std::get_if<memodb_ref>(&Name))
      return *Ref;
    else
      return Db.get(Name).as_ref();
  }
  memodb_value Value = memodb_value::load_cbor(
      {reinterpret_cast<const std::uint8_t *>(Buffer->getBufferStart()),
       Buffer->getBufferSize()});
  return Db.put(Value);
}

// output options

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"),
                           cl::sub(GetCommand));

static cl::opt<std::string> OutputFilename("o", cl::desc("<output file>"),
                                           cl::init("-"),
                                           cl::value_desc("filename"),
                                           cl::sub(GetCommand));

static void WriteValue(const memodb_value &Value) {
  ExitOnError Err("value write: ");
  std::error_code EC;
  auto OutputFile =
      std::make_unique<ToolOutputFile>(OutputFilename, EC, sys::fs::F_None);
  if (EC)
    Err(errorCodeToError(EC));
  if (Force || !CheckBitcodeOutputToConsole(OutputFile->os())) {
    std::vector<std::uint8_t> Buffer;
    Value.save_cbor(Buffer);
    OutputFile->os().write(reinterpret_cast<const char *>(Buffer.data()),
                           Buffer.size());
    OutputFile->keep();
  }
}

// memodb get

static int Get() {
  auto Name = GetNameFromURI(SourceURI);
  auto Db = memodb_db_open(GetUri());
  auto Value = Db->getOptional(Name);
  if (Value && !std::holds_alternative<memodb_ref>(Name))
    Value = Db->getOptional(Value->as_ref());
  if (Value)
    WriteValue(*Value);
  else
    llvm::errs() << "not found\n";
  return 0;
}

// memodb list-heads

static int ListHeads() {
  auto Db = memodb_db_open(GetUri());
  for (const memodb_head &Head : Db->list_heads())
    outs() << "head:" << Head.Name << "\n";
  return 0;
}

// memodb put

static int Put() {
  auto Db = memodb_db_open(GetUri());
  memodb_ref Ref = ReadRef(*Db, InputURI);
  outs() << "id:" << Ref << "\n";
  return 0;
}

// memodb refs-to

static int RefsTo() {
  auto Db = memodb_db_open(GetUri());
  memodb_ref Ref = ReadRef(*Db, TargetURI);
  for (const memodb_name &Name : Db->list_names_using(Ref)) {
    if (auto Head = std::get_if<memodb_head>(&Name))
      outs() << "head:" << Head->Name << "\n";
    else if (auto ParentRef = std::get_if<memodb_ref>(&Name))
      outs() << "id:" << *ParentRef << "\n";
    else if (auto Call = std::get_if<memodb_call>(&Name)) {
      outs() << "call:" << Call->Name;
      for (const auto &Arg : Call->Args)
        outs() << "/" << Arg;
      outs() << "\n";
    } else
      llvm_unreachable("impossible value for memodb_name");
  }
  return 0;
}

// memodb set

static int Set() {
  auto Db = memodb_db_open(GetUri());
  auto Name = GetNameFromURI(TargetURI);
  auto Value = ReadRef(*Db, InputURI);
  Db->set(Name, Value);
  return 0;
}

// main

int main(int argc, char **argv) {
  PrettyStackTraceProgram StackPrinter(argc, argv);
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  // Hide LLVM's options, since they're mostly irrelevant.
  bcdb::ReorganizeOptions([](cl::Option *O) {
    if (!bcdb::OptionHasCategory(*O, MemoDBCategory)) {
      O->setHiddenFlag(cl::Hidden);
      O->addSubCommand(*cl::AllSubCommands);
    }
  });

  cl::ParseCommandLineOptions(argc, argv, "MemoDB Tools");

  if (GetCommand) {
    return Get();
  } else if (ListHeadsCommand) {
    return ListHeads();
  } else if (PutCommand) {
    return Put();
  } else if (RefsToCommand) {
    return RefsTo();
  } else if (SetCommand) {
    return Set();
  } else {
    cl::PrintHelpMessage(false, true);
    return 0;
  }
}
