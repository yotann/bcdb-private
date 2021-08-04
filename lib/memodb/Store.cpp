#include "memodb/Store.h"

#include "memodb_internal.h"

#include <llvm/Support/ConvertUTF.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/raw_os_ostream.h>
#include <sstream>

#include "memodb/Multibase.h"
#include "memodb/URI.h"

using namespace memodb;

bool Call::operator<(const Call &other) const {
  return Name != other.Name ? Name < other.Name : Args < other.Args;
}

bool Call::operator==(const Call &other) const {
  return Name == other.Name && Args == other.Args;
}

bool Call::operator!=(const Call &other) const { return !(*this == other); }

NodeRef::NodeRef(Store &store, const NodeRef &other)
    : store(store), cid(other.cid), node(other.node) {}

NodeRef::NodeRef(Store &store, const NodeOrCID &node_or_cid) : store(store) {
  const NodeOrCID::BaseType &base = node_or_cid;
  std::visit(Overloaded{
                 [&](const CID &cid) { this->cid = cid; },
                 [&](const Node &node) { this->node = node; },
             },
             base);
}

NodeRef::NodeRef(Store &store, const CID &cid) : store(store), cid(cid) {}

NodeRef::NodeRef(Store &store, const CID &cid, const Node &node)
    : store(store), cid(cid), node(node) {}

const Node &NodeRef::operator*() {
  if (!node)
    node = store.get(*cid);
  return *node;
}

const Node *NodeRef::operator->() { return &operator*(); }

const CID &NodeRef::getCID() {
  if (!cid)
    cid = store.put(*node);
  return *cid;
}

void NodeRef::freeNode() {
  getCID();
  node.reset();
}

std::unique_ptr<Store> Store::open(llvm::StringRef uri,
                                   bool create_if_missing) {
  if (uri.startswith("sqlite:")) {
    return memodb_sqlite_open(uri.substr(7), create_if_missing);
  } else if (uri.startswith("car:")) {
    return memodb_car_open(uri, create_if_missing);
  } else if (uri.startswith("rocksdb:")) {
    return memodb_rocksdb_open(uri, create_if_missing);
  } else if (uri.startswith("http:") || uri.startswith("https:")) {
    return memodb_http_open(uri, create_if_missing);
  } else {
    llvm::report_fatal_error(llvm::Twine("unsupported store URI ") + uri);
  }
}

std::ostream &memodb::operator<<(std::ostream &os, const Head &head) {
  URI uri;
  uri.path_segments = {"head", head.Name};
  uri.escape_slashes_in_segments = false;
  return os << uri.encode();
}

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os, const Head &head) {
  URI uri;
  uri.path_segments = {"head", head.Name};
  uri.escape_slashes_in_segments = false;
  return os << uri.encode();
}

std::ostream &memodb::operator<<(std::ostream &os, const Call &call) {
  std::string args;
  for (const CID &arg : call.Args)
    args += arg.asString(Multibase::base64url) + ",";
  args.pop_back();
  URI uri;
  uri.path_segments = {"call", call.Name, std::move(args)};
  return os << uri.encode();
}

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os, const Call &call) {
  std::string args;
  for (const CID &arg : call.Args)
    args += arg.asString(Multibase::base64url) + ",";
  args.pop_back();
  URI uri;
  uri.path_segments = {"call", call.Name, std::move(args)};
  return os << uri.encode();
}

std::optional<Name> Name::parse(llvm::StringRef uri_str) {
  auto uri = URI::parse(uri_str);
  if (!uri || !uri->scheme.empty() || !uri->host.empty() || uri->port != 0 ||
      uri->path_segments.empty() || uri->rootless ||
      !uri->query_params.empty() || !uri->fragment.empty())
    return std::nullopt;
  if (uri->path_segments[0] == "head" && uri->path_segments.size() >= 2) {
    Head result(uri->getPathString(1));
    if (result.Name.empty())
      return std::nullopt;
    return result;
  } else if (uri->path_segments[0] == "cid" && uri->path_segments.size() == 2) {
    return CID::parse(uri->path_segments[1]);
  } else if (uri->path_segments[0] == "call" &&
             uri->path_segments.size() == 3) {
    std::vector<CID> args;
    llvm::SmallVector<llvm::StringRef, 8> arg_strs;
    const auto &func_name = uri->path_segments[1];
    if (func_name.empty())
      return std::nullopt;
    llvm::StringRef(uri->path_segments[2]).split(arg_strs, ',');
    for (auto arg_str : arg_strs) {
      auto arg = CID::parse(arg_str);
      if (!arg)
        return std::nullopt;
      args.emplace_back(std::move(*arg));
    }
    return Call(func_name, args);
  } else {
    return std::nullopt;
  }
}

std::ostream &memodb::operator<<(std::ostream &os, const Name &name) {
  if (const CID *cid = std::get_if<CID>(&name)) {
    URI uri;
    uri.path_segments = {"cid", cid->asString(Multibase::base64url)};
    os << uri.encode();
  } else {
    name.visit([&](auto X) { os << X; });
  }
  return os;
}

llvm::raw_ostream &memodb::operator<<(llvm::raw_ostream &os, const Name &name) {
  if (const CID *cid = std::get_if<CID>(&name)) {
    URI uri;
    uri.path_segments = {"cid", cid->asString(Multibase::base64url)};
    os << uri.encode();
  } else {
    name.visit([&](auto X) { os << X; });
  }
  return os;
}

bool Store::has(const CID &CID) { return getOptional(CID).hasValue(); }

bool Store::has(const Name &Name) {
  if (const CID *cid = std::get_if<CID>(&Name))
    return has(*cid);
  return resolveOptional(Name).hasValue();
}

Node Store::get(const CID &CID) { return *getOptional(CID); }

CID Store::resolve(const Name &Name) { return *resolveOptional(Name); }

std::vector<Head> Store::list_heads() {
  std::vector<Head> Result;
  eachHead([&](const Head &Head) {
    Result.emplace_back(Head);
    return false;
  });
  return Result;
}

std::vector<Call> Store::list_calls(llvm::StringRef Func) {
  std::vector<Call> Result;
  eachCall(Func, [&](const Call &Call) {
    Result.emplace_back(Call);
    return false;
  });
  return Result;
}

std::vector<Path> Store::list_paths_to(const CID &ref) {
  using memodb::Node;
  auto listPathsWithin = [](const Node &Value,
                            const CID &Ref) -> std::vector<std::vector<Node>> {
    std::vector<std::vector<Node>> Result;
    std::vector<Node> CurPath;
    std::function<void(const Node &)> recurse = [&](const Node &Value) {
      if (Value.kind() == Kind::Link) {
        if (Value.as<CID>() == Ref)
          Result.push_back(CurPath);
      } else if (Value.kind() == Kind::List) {
        for (size_t i = 0; i < Value.size(); i++) {
          CurPath.push_back(i);
          recurse(Value[i]);
          CurPath.pop_back();
        }
      } else if (Value.kind() == Kind::Map) {
        for (const auto &item : Value.map_range()) {
          CurPath.emplace_back(utf8_string_arg, item.key());
          recurse(item.value());
          CurPath.pop_back();
        }
      }
    };
    recurse(Value);
    return Result;
  };

  std::vector<Path> Result;
  std::vector<Node> BackwardsPath;
  std::function<void(const CID &)> recurse = [&](const CID &Ref) {
    for (const auto &Parent : list_names_using(Ref)) {
      if (const CID *ParentRef = std::get_if<CID>(&Parent)) {
        const Node Node = get(*ParentRef);
        for (const auto &Subpath : listPathsWithin(Node, Ref)) {
          BackwardsPath.insert(BackwardsPath.end(), Subpath.rbegin(),
                               Subpath.rend());
          recurse(*ParentRef);
          BackwardsPath.erase(BackwardsPath.end() - Subpath.size(),
                              BackwardsPath.end());
        }
      } else {
        Result.emplace_back(Parent, std::vector<Node>(BackwardsPath.rbegin(),
                                                      BackwardsPath.rend()));
      }
    }
  };
  recurse(ref);
  return Result;
}
