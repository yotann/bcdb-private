# NOTE: when updating this file, run utils/cache-deps.sh to upload dependencies
# to Cachix so CI builds can use them.

let
  default_nixpkgs = (import <nixpkgs> {}).fetchFromGitHub {
    owner = "NixOS";
    repo = "nixpkgs-channels";
    rev = "cc6cf0a96a627e678ffc996a8f9d1416200d6c81";
    sha256 = "1srjikizp8ip4h42x7kr4qf00lxcp1l8zp6h0r1ddfdyw8gv9001";
  };
  debugLLVM = llvmPackages: (llvmPackages.llvm.override {
    debugVersion = true;
  }).overrideAttrs (o: {
    doCheck = false;
  });
in
{ nixpkgs ? default_nixpkgs }:

with import nixpkgs {};
rec {
  bcdb-llvm4 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_4;
  };
  bcdb-llvm5 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_5;
  };
  bcdb-llvm6 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_6;
  };
  bcdb-llvm7 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_7;
  };
  bcdb-llvm8 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_8;
  };
  bcdb-llvm9 = callPackage ./build.nix {
    llvm = debugLLVM llvmPackages_9;
  };
  bcdb-llvmAlive = callPackage ./build.nix {
    llvm = llvmForAlive;
  };

  bcdb-clang = callPackage ./build.nix {
    inherit (llvmPackages_7) stdenv;
    llvm = llvmPackages_7.llvm;
  };

  bcdb = bcdb-llvm7;

  llvmForAlive = (llvmPackages_9.llvm.override {
    debugVersion = true;
    enableSharedLibraries = true;
  }).overrideAttrs (o: {
    cmakeFlags = o.cmakeFlags ++ [
      "-DLLVM_ENABLE_RTTI=ON"
      "-DLLVM_ENABLE_EH=ON"
    ];
    doCheck = false;
    src = fetchFromGitHub {
      owner = "llvm";
      repo = "llvm-project";
      rev = "22d516261a98fd56ccce39b3031fdba8d64de696";
      sha256 = "1n63k7dq4rgcp754vjiyrpm41w3z4gdnf0ssa80q9i0rpc5fhfxq";
    };
    unpackPhase = null;
    sourceRoot = "source/llvm";
    postPatch = ''
      substitute '${./nix/llvm-outputs.patch}' ./llvm-outputs.patch --subst-var lib
      patch -p1 < ./llvm-outputs.patch
    '';
  });
}
