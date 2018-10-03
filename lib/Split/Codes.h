#ifndef BCDB_SPLIT_CODES_H
#define BCDB_SPLIT_CODES_H

namespace bcdb {
namespace codes {

// Same values used by LLVM for bitcode.
enum LinkageTypeCodes {
  LINKAGE_TYPE_EXTERNAL = 0,
  LINKAGE_TYPE_APPENDING = 2,
  LINKAGE_TYPE_INTERNAL = 3,
  LINKAGE_TYPE_EXTERNAL_WEAK = 7,
  LINKAGE_TYPE_COMMON = 8,
  LINKAGE_TYPE_PRIVATE = 9,
  LINKAGE_TYPE_AVAILABLE_EXTERNALLY = 12,
  LINKAGE_TYPE_WEAK_ANY = 16,
  LINKAGE_TYPE_WEAK_ODR = 17,
  LINKAGE_TYPE_LINK_ONCE_ANY = 18,
  LINKAGE_TYPE_LINK_ONCE_ODR = 19
};

} // End codes namespace
} // End bcdb namespace

#endif
