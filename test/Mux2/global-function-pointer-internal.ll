; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so --weak-name=libweak.so
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=STUB  %s
; RUN: opt -verify -S < %t/libweak.so  | FileCheck --check-prefix=WEAK  %s

@global = global void ()* @func
@global2 = internal global void ()* @func

declare void @exit(i32)

define internal void @func() {
  call void @exit(i32 0)
  unreachable
}

define i32 @main(i32, i8**) {
  %f = load void()*, void()** @global
  call void %f()
  %f2 = load void()*, void()** @global2
  call void %f2()
  ret i32 1
}

; MUXED-NOT: @global2
; MUXED: @global = extern_weak global void ()*
; MUXED: @global2 = internal global void ()* @__bcdb_private_func
; MUXED: define protected void @__bcdb_private_func()
; MUXED-NEXT: call void @__bcdb_id_{{.*}}()

; STUB-NOT: @global2
; STUB-NOT: @__bcdb_private_global2
; STUB-NOT: @__bcdb_private_global
; STUB: @global = global void ()* @__bcdb_private_func
; STUB: declare void @__bcdb_private_func()

; WEAK-NOT: @global
; WEAK-NOT: @__bcdb_private_global
