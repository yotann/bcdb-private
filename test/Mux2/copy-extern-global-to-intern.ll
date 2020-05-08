// RUN: %mux2test %s %t
// RUN: %t.elf/module
// RUN: %mux2test %s %t --allow-spurious-exports
// RUN: %t.elf/module
// RUN: %mux2test %s %t --allow-spurious-exports --known-dynamic-defs
// RUN: %t.elf/module
// RUN: %mux2test %s %t --allow-spurious-exports --known-dynamic-defs --known-dynamic-uses --known-rtld-local
// RUN: %t.elf/module



%struct = type { %struct* }

@stderr = external local_unnamed_addr global %struct*, align 8
@log_fp = internal unnamed_addr global %struct* null, align 8

define i32 @main() local_unnamed_addr {
  %1 = load i64, i64* bitcast (%struct** @stderr to i64*), align 8
  store i64 %1, i64* bitcast (%struct** @log_fp to i64*), align 8
  ret i32 0
}

!llvm.module.flags = !{!1}
!1 = !{i32 2, !"bcdb.elf.type", i32 2}
