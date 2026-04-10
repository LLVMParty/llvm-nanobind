from __future__ import annotations

from dataclasses import dataclass
import re


@dataclass(frozen=True, slots=True)
class GoldenCase:
    name: str
    args: tuple[str, ...]
    input_ir: str
    cleanup_pipeline: str | None = None


GOLDEN_CASES = [
    GoldenCase(
        name="schedule-instructions/simple-block",
        args=("--schedule-instructions", "--seed=7"),
        input_ir=r'''
        define i32 @f(i32 %x, i32 %y, i32 %z) {
        entry:
          %a = add i32 %x, %y
          %b = xor i32 %y, %z
          %c = or i32 %a, %b
          %d = add i32 %c, 1
          ret i32 %d
        }
        ''',
    ),
    GoldenCase(
        name="if-convert/diamond",
        args=("--if-convert", "--seed=7"),
        input_ir=r'''
        define i32 @diamond(i32 %x) {
        entry:
          %cmp = icmp sgt i32 %x, 0
          br i1 %cmp, label %then, label %else
        then:
          %a = add i32 %x, 11
          br label %merge
        else:
          %b = add i32 %x, 17
          br label %merge
        merge:
          %phi = phi i32 [ %a, %then ], [ %b, %else ]
          ret i32 %phi
        }
        ''',
        cleanup_pipeline="simplifycfg,adce,instnamer",
    ),
    GoldenCase(
        name="if-convert+schedule/diamond",
        args=("--if-convert", "--schedule-instructions", "--seed=7"),
        input_ir=r'''
        define i32 @diamond(i32 %x) {
        entry:
          %cmp = icmp sgt i32 %x, 0
          br i1 %cmp, label %then, label %else
        then:
          %a = add i32 %x, 11
          br label %merge
        else:
          %b = add i32 %x, 17
          br label %merge
        merge:
          %phi = phi i32 [ %a, %then ], [ %b, %else ]
          %r = add i32 %phi, 1
          ret i32 %r
        }
        ''',
        cleanup_pipeline="simplifycfg,adce,instnamer",
    ),
    GoldenCase(
        name="bmi-mutate/i32-and-mask-xor",
        args=("--bmi-mutate", "--seed=7"),
        input_ir=r'''
        define i32 @f(i32 %x, i32 %y) #0 {
        entry:
          %a = and i32 %x, 255
          %b = xor i32 %a, %y
          ret i32 %b
        }
        attributes #0 = { "target-features"="+bmi,+bmi2" }
        ''',
    ),
    GoldenCase(
        name="bmi-mutate/i64-and-mask-xor",
        args=("--bmi-mutate", "--seed=7"),
        input_ir=r'''
        define i64 @f(i64 %x, i64 %y) #0 {
        entry:
          %a = and i64 %x, 255
          %b = xor i64 %a, %y
          ret i64 %b
        }
        attributes #0 = { "target-features"="+bmi,+bmi2" }
        ''',
    ),
    GoldenCase(
        name="bmi-mutate/bextr-mask-window",
        args=("--bmi-mutate", "--seed=7"),
        input_ir=r'''
        define i32 @f(i32 %x) #0 {
        entry:
          %a = and i32 %x, 8160
          ret i32 %a
        }
        attributes #0 = { "target-features"="+bmi,+bmi2" }
        ''',
    ),
    GoldenCase(
        name="loop-to-recursion/simple-counter",
        args=("--loop-to-recursion", "--seed=7"),
        input_ir=r'''
        define i64 @sum_loop(i64 %n) {
        entry:
          br label %header
        header:
          %i = phi i64 [ 0, %entry ], [ %i.next, %header ]
          %sum = phi i64 [ 0, %entry ], [ %sum.next, %header ]
          %sum.next = add i64 %sum, %i
          %i.next = add i64 %i, 1
          %cmp = icmp slt i64 %i.next, %n
          br i1 %cmp, label %header, label %exit
        exit:
          %ret = phi i64 [ %sum.next, %header ]
          ret i64 %ret
        }
        ''',
    ),
    GoldenCase(
        name="loop-to-recursion/multi-exit-rejected",
        args=("--loop-to-recursion", "--seed=7"),
        input_ir=r'''
        define i32 @f(i32 %n) {
        entry:
          br label %header
        header:
          %i = phi i32 [ 0, %entry ], [ %next, %latch ]
          %cmp0 = icmp slt i32 %i, %n
          br i1 %cmp0, label %latch, label %exit
        latch:
          %next = add i32 %i, 1
          %cmp1 = icmp slt i32 %next, %n
          br i1 %cmp1, label %header, label %exit
        exit:
          %r = phi i32 [ %i, %header ], [ %next, %latch ]
          ret i32 %r
        }
        ''',
    ),
    GoldenCase(
        name="code-clone/internal-helper",
        args=("--code-clone", "--seed=123"),
        input_ir=r'''
        define internal i32 @helper(i32 %x, i32 %y) {
        entry:
          %0 = add i32 %x, %y
          %1 = xor i32 %0, 7
          %2 = or i32 %1, 3
          %3 = add i32 %2, 1
          %4 = and i32 %3, 255
          ret i32 %4
        }

        define i32 @main(i32 %n) {
        entry:
          %call = call i32 @helper(i32 %n, i32 5)
          ret i32 %call
        }
        ''',
    ),
    GoldenCase(
        name="code-clone+schedule/internal-helper",
        args=("--code-clone", "--schedule-instructions", "--seed=123"),
        input_ir=r'''
        define internal i32 @helper(i32 %x, i32 %y) {
        entry:
          %0 = add i32 %x, %y
          %1 = xor i32 %0, 7
          %2 = or i32 %1, 3
          %3 = add i32 %2, 1
          %4 = and i32 %3, 255
          ret i32 %4
        }

        define i32 @main(i32 %n) {
        entry:
          %call = call i32 @helper(i32 %n, i32 5)
          ret i32 %call
        }
        ''',
    ),
]


def case_slug(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "_", name)
