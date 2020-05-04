; RUN: bcdb init -uri sqlite:%t.bcdb
; RUN: bcdb add -uri sqlite:%t.bcdb %s -name prog
; RUN: bcdb mux2 -uri sqlite:%t.bcdb prog -o %t --muxed-name=libmuxed.so --weak-name=libweak.so --allow-spurious-exports
; RUN: opt -verify -S < %t/libmuxed.so | FileCheck --check-prefix=MUXED %s
; RUN: opt -verify -S < %t/prog        | FileCheck --check-prefix=PROG  %s
; RUN: opt -verify -S < %t/libweak.so  | FileCheck --check-prefix=WEAK  %s

@alias1 = weak_odr alias void (), void ()* @target1
@alias2 = weak_odr alias void (), void ()* @target2

define weak_odr void @target1() {
  call void @alias1()
  ret void
}

define weak_odr void @target2() {
  ret void
}

; MUXED: define protected void @__bcdb_id_1()
; MUXED-NEXT: call void @alias1()
; MUXED: declare extern_weak void @alias1()
; MUXED: define protected void @__bcdb_id_2()
; MUXED-NEXT: ret void

; PROG: @alias1 = alias void (), void ()* @target1
; PROG: @alias2 = alias void (), void ()* @target2
; PROG: declare void @__bcdb_id_1()
; PROG: define void @target1()
; PROG-NEXT: call void @__bcdb_id_1()
; PROG: declare void @__bcdb_id_2()
; PROG: define void @target2()
; PROG-NEXT: call void @__bcdb_id_2()

; WEAK: define weak void @alias1()
; WEAK-NOT: @target1
; WEAK-NOT: @alias2
; WEAK-NOT: @target2
