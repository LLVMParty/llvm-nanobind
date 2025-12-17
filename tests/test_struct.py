#!/usr/bin/env -S uv run
"""
Test: test_struct
Integration test: Struct manipulation with Point type

Python equivalent of tests/test_struct.cpp
Must produce identical output to the C++ version.
"""

import llvm


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_struct") as mod:
            i32 = ctx.int32_type()
            void_ty = ctx.void_type()
            ptr = ctx.pointer_type()

            # ==========================================
            # Define Point struct: { i32 x, i32 y }
            # ==========================================
            point_ty = ctx.named_struct_type("Point")
            point_ty.set_body([i32, i32], packed=False)

            with ctx.create_builder() as builder:
                # ==========================================
                # Function: void point_init(Point* p, i32 x, i32 y)
                # ==========================================
                init_ty = ctx.function_type(void_ty, [ptr, i32, i32])
                init_func = mod.add_function("point_init", init_ty)

                p = init_func.get_param(0)
                x = init_func.get_param(1)
                y = init_func.get_param(2)
                p.name = "p"
                x.name = "x"
                y.name = "y"

                init_entry = init_func.append_basic_block("entry", ctx)
                builder.position_at_end(init_entry)

                # Store x to p->x (field 0)
                x_ptr = builder.struct_gep(point_ty, p, 0, "x_ptr")
                builder.store(x, x_ptr)

                # Store y to p->y (field 1)
                y_ptr = builder.struct_gep(point_ty, p, 1, "y_ptr")
                builder.store(y, y_ptr)

                builder.ret_void()

                # ==========================================
                # Function: void point_add(Point* a, Point* b, Point* result)
                # ==========================================
                add_ty = ctx.function_type(void_ty, [ptr, ptr, ptr])
                add_func = mod.add_function("point_add", add_ty)

                a = add_func.get_param(0)
                b = add_func.get_param(1)
                result = add_func.get_param(2)
                a.name = "a"
                b.name = "b"
                result.name = "result"

                add_entry = add_func.append_basic_block("entry", ctx)
                builder.position_at_end(add_entry)

                # Load a->x and a->y
                a_x_ptr = builder.struct_gep(point_ty, a, 0, "a_x_ptr")
                a_y_ptr = builder.struct_gep(point_ty, a, 1, "a_y_ptr")
                a_x = builder.load(i32, a_x_ptr, "a_x")
                a_y = builder.load(i32, a_y_ptr, "a_y")

                # Load b->x and b->y
                b_x_ptr = builder.struct_gep(point_ty, b, 0, "b_x_ptr")
                b_y_ptr = builder.struct_gep(point_ty, b, 1, "b_y_ptr")
                b_x = builder.load(i32, b_x_ptr, "b_x")
                b_y = builder.load(i32, b_y_ptr, "b_y")

                # Compute sum
                sum_x = builder.add(a_x, b_x, "sum_x")
                sum_y = builder.add(a_y, b_y, "sum_y")

                # Store to result
                result_x_ptr = builder.struct_gep(point_ty, result, 0, "result_x_ptr")
                result_y_ptr = builder.struct_gep(point_ty, result, 1, "result_y_ptr")
                builder.store(sum_x, result_x_ptr)
                builder.store(sum_y, result_y_ptr)

                builder.ret_void()

                # ==========================================
                # Function: i32 point_manhattan_distance(Point* p)
                # Returns |x| + |y| (simplified: just x + y for demo)
                # ==========================================
                dist_ty = ctx.function_type(i32, [ptr])
                dist_func = mod.add_function("point_manhattan", dist_ty)

                dist_p = dist_func.get_param(0)
                dist_p.name = "p"

                dist_entry = dist_func.append_basic_block("entry", ctx)
                builder.position_at_end(dist_entry)

                px_ptr = builder.struct_gep(point_ty, dist_p, 0, "px_ptr")
                py_ptr = builder.struct_gep(point_ty, dist_p, 1, "py_ptr")
                px = builder.load(i32, px_ptr, "px")
                py = builder.load(i32, py_ptr, "py")
                dist = builder.add(px, py, "dist")
                builder.ret(dist)

                # ==========================================
                # Function: i32 test_points()
                # Creates two points, adds them, returns manhattan distance
                # ==========================================
                test_ty = ctx.function_type(i32, [])
                test_func = mod.add_function("test_points", test_ty)

                test_entry = test_func.append_basic_block("entry", ctx)
                builder.position_at_end(test_entry)

                # Allocate three Points
                p1 = builder.alloca(point_ty, "p1")
                p2 = builder.alloca(point_ty, "p2")
                p3 = builder.alloca(point_ty, "p3")

                # Initialize p1 = (3, 4)
                init_args1 = [p1, llvm.const_int(i32, 3), llvm.const_int(i32, 4)]
                builder.call(init_ty, init_func, init_args1, "")

                # Initialize p2 = (1, 2)
                init_args2 = [p2, llvm.const_int(i32, 1), llvm.const_int(i32, 2)]
                builder.call(init_ty, init_func, init_args2, "")

                # Add p1 + p2 -> p3
                add_args = [p1, p2, p3]
                builder.call(add_ty, add_func, add_args, "")

                # Get manhattan distance of p3
                dist_args = [p3]
                final_dist = builder.call(dist_ty, dist_func, dist_args, "final_dist")

                builder.ret(final_dist)

            # ==========================================
            # Global constant Point
            # ==========================================
            origin_vals = [llvm.const_int(i32, 0), llvm.const_int(i32, 0)]
            origin_const = llvm.const_named_struct(point_ty, origin_vals)
            origin_global = mod.add_global(point_ty, "origin")
            origin_global.set_initializer(origin_const)
            origin_global.set_constant(True)

            # Verify module
            if not mod.verify():
                print(f"; Verification failed: {mod.get_verification_error()}")
                return 1

            # Print diagnostic comments
            print("; Test: test_struct")
            print("; Integration test: Point struct manipulation")
            print(";")
            print("; Struct definition:")
            print(";   %Point = type { i32, i32 }  ; x, y fields")
            print(";")
            print("; Functions:")
            print(";   point_init(Point*, i32, i32) -> void")
            print(";   point_add(Point*, Point*, Point*) -> void")
            print(";   point_manhattan(Point*) -> i32")
            print(";   test_points() -> i32")
            print(";")
            print("; test_points creates:")
            print(";   p1 = (3, 4)")
            print(";   p2 = (1, 2)")
            print(";   p3 = p1 + p2 = (4, 6)")
            print(";   returns manhattan(p3) = 4 + 6 = 10")
            print(";")
            print("; Struct type info:")
            print(f";   name: {point_ty.struct_name}")
            print(f";   num elements: {point_ty.struct_element_count}")
            print(f";   is packed: {'yes' if point_ty.is_packed_struct else 'no'}")
            print(f";   is opaque: {'yes' if point_ty.is_opaque_struct else 'no'}")
            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    exit(main())
