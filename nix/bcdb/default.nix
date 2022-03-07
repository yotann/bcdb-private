{ stdenv, lib, nix-gitignore, clang, cmake, libsodium, llvm, python3, sqlite, boost175,
rocksdb ? null, nng ? null,
sanitize ? false }:

let
  gitFilter = patterns: root: with nix-gitignore;
      gitignoreFilterPure (_: _: true) (withGitignoreFile patterns root) root;

  # In order to determine REVISION_DESCRIPTION_FINAL the normal way, we would
  # have to copy all of .git/ into the Nix store, which is very slow. Instead,
  # we can determine the current revision using Nix.
  resolveRef = ref:
    let path = ../../.git + ("/" + symref);
    in if builtins.pathExists path
       then lib.fileContents path
       else let lines = lib.splitString "\n" (builtins.readFile ../../.git/packed-refs);
            in lib.findFirst (lib.hasSuffix " ${symref}") "unknown" lines;
  symref = lib.removePrefix "ref: " (lib.fileContents ../../.git/HEAD);
  revision = if lib.hasInfix "/" symref
             then resolveRef symref
             else symref;
  revision-short = if builtins.pathExists ../../.git
                   then builtins.substring 0 7 revision
                   else "unknown";

in stdenv.mkDerivation {
  name = "bcdb";
  version = "0.1.0-${revision-short}";

  src = builtins.path {
    path = ../..;
    name = "bcdb-source";
    filter = gitFilter [''
      .*
      *.md
      *.nix
      /docs/
      /experiments/
      /flake.lock
      /nix/
    ''] ../..;
  };

  nativeBuildInputs = [ clang cmake python3 ];
  buildInputs = [ boost175 libsodium llvm nng rocksdb sqlite ];

  preConfigure = ''
    patchShebangs third_party/lit/lit.py
  '';
  cmakeBuildType = "RelWithDebInfo";
  doCheck = true;
  dontStrip = true;

  enableParallelBuilding = true;

  cmakeFlags = [
    "-DREVISION_DESCRIPTION=g${revision-short}-NIX"
  ] ++ lib.optional sanitize "-DCMAKE_BUILD_TYPE=SANITIZE";
  preCheck = lib.optionalString sanitize
    "export LSAN_OPTIONS=suppressions=$src/lsan.supp";
}
