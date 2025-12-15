/**
 * Test: test_struct
 * Integration test: Struct manipulation with Point type
 *
 * This test creates a named struct type and demonstrates:
 * - Named struct creation and body setting
 * - Struct GEP for field access
 * - Load/store of struct fields
 * - A complete point_add function
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_struct", ctx);

    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(ctx);
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx, 0);

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

    // ==========================================
    // Define Point struct: { i32 x, i32 y }
    // ==========================================
    LLVMTypeRef point_ty = LLVMStructCreateNamed(ctx, "Point");
    LLVMTypeRef point_fields[] = {i32, i32};
    LLVMStructSetBody(point_ty, point_fields, 2, 0);

    // ==========================================
    // Function: void point_init(Point* p, i32 x, i32 y)
    // ==========================================
    LLVMTypeRef init_params[] = {ptr, i32, i32};
    LLVMTypeRef init_ty = LLVMFunctionType(void_ty, init_params, 3, 0);
    LLVMValueRef init_func = LLVMAddFunction(mod, "point_init", init_ty);

    LLVMValueRef p = LLVMGetParam(init_func, 0);
    LLVMValueRef x = LLVMGetParam(init_func, 1);
    LLVMValueRef y = LLVMGetParam(init_func, 2);
    LLVMSetValueName2(p, "p", 1);
    LLVMSetValueName2(x, "x", 1);
    LLVMSetValueName2(y, "y", 1);

    LLVMBasicBlockRef init_entry = LLVMAppendBasicBlockInContext(ctx, init_func, "entry");
    LLVMPositionBuilderAtEnd(builder, init_entry);

    // Store x to p->x (field 0)
    LLVMValueRef x_ptr = LLVMBuildStructGEP2(builder, point_ty, p, 0, "x_ptr");
    LLVMBuildStore(builder, x, x_ptr);

    // Store y to p->y (field 1)
    LLVMValueRef y_ptr = LLVMBuildStructGEP2(builder, point_ty, p, 1, "y_ptr");
    LLVMBuildStore(builder, y, y_ptr);

    LLVMBuildRetVoid(builder);

    // ==========================================
    // Function: void point_add(Point* a, Point* b, Point* result)
    // ==========================================
    LLVMTypeRef add_params[] = {ptr, ptr, ptr};
    LLVMTypeRef add_ty = LLVMFunctionType(void_ty, add_params, 3, 0);
    LLVMValueRef add_func = LLVMAddFunction(mod, "point_add", add_ty);

    LLVMValueRef a = LLVMGetParam(add_func, 0);
    LLVMValueRef b = LLVMGetParam(add_func, 1);
    LLVMValueRef result = LLVMGetParam(add_func, 2);
    LLVMSetValueName2(a, "a", 1);
    LLVMSetValueName2(b, "b", 1);
    LLVMSetValueName2(result, "result", 6);

    LLVMBasicBlockRef add_entry = LLVMAppendBasicBlockInContext(ctx, add_func, "entry");
    LLVMPositionBuilderAtEnd(builder, add_entry);

    // Load a->x and a->y
    LLVMValueRef a_x_ptr = LLVMBuildStructGEP2(builder, point_ty, a, 0, "a_x_ptr");
    LLVMValueRef a_y_ptr = LLVMBuildStructGEP2(builder, point_ty, a, 1, "a_y_ptr");
    LLVMValueRef a_x = LLVMBuildLoad2(builder, i32, a_x_ptr, "a_x");
    LLVMValueRef a_y = LLVMBuildLoad2(builder, i32, a_y_ptr, "a_y");

    // Load b->x and b->y
    LLVMValueRef b_x_ptr = LLVMBuildStructGEP2(builder, point_ty, b, 0, "b_x_ptr");
    LLVMValueRef b_y_ptr = LLVMBuildStructGEP2(builder, point_ty, b, 1, "b_y_ptr");
    LLVMValueRef b_x = LLVMBuildLoad2(builder, i32, b_x_ptr, "b_x");
    LLVMValueRef b_y = LLVMBuildLoad2(builder, i32, b_y_ptr, "b_y");

    // Compute sum
    LLVMValueRef sum_x = LLVMBuildAdd(builder, a_x, b_x, "sum_x");
    LLVMValueRef sum_y = LLVMBuildAdd(builder, a_y, b_y, "sum_y");

    // Store to result
    LLVMValueRef result_x_ptr = LLVMBuildStructGEP2(builder, point_ty, result, 0, "result_x_ptr");
    LLVMValueRef result_y_ptr = LLVMBuildStructGEP2(builder, point_ty, result, 1, "result_y_ptr");
    LLVMBuildStore(builder, sum_x, result_x_ptr);
    LLVMBuildStore(builder, sum_y, result_y_ptr);

    LLVMBuildRetVoid(builder);

    // ==========================================
    // Function: i32 point_manhattan_distance(Point* p)
    // Returns |x| + |y| (simplified: just x + y for demo)
    // ==========================================
    LLVMTypeRef dist_params[] = {ptr};
    LLVMTypeRef dist_ty = LLVMFunctionType(i32, dist_params, 1, 0);
    LLVMValueRef dist_func = LLVMAddFunction(mod, "point_manhattan", dist_ty);

    LLVMValueRef dist_p = LLVMGetParam(dist_func, 0);
    LLVMSetValueName2(dist_p, "p", 1);

    LLVMBasicBlockRef dist_entry = LLVMAppendBasicBlockInContext(ctx, dist_func, "entry");
    LLVMPositionBuilderAtEnd(builder, dist_entry);

    LLVMValueRef px_ptr = LLVMBuildStructGEP2(builder, point_ty, dist_p, 0, "px_ptr");
    LLVMValueRef py_ptr = LLVMBuildStructGEP2(builder, point_ty, dist_p, 1, "py_ptr");
    LLVMValueRef px = LLVMBuildLoad2(builder, i32, px_ptr, "px");
    LLVMValueRef py = LLVMBuildLoad2(builder, i32, py_ptr, "py");
    LLVMValueRef dist = LLVMBuildAdd(builder, px, py, "dist");
    LLVMBuildRet(builder, dist);

    // ==========================================
    // Function: i32 test_points()
    // Creates two points, adds them, returns manhattan distance
    // ==========================================
    LLVMTypeRef test_ty = LLVMFunctionType(i32, nullptr, 0, 0);
    LLVMValueRef test_func = LLVMAddFunction(mod, "test_points", test_ty);

    LLVMBasicBlockRef test_entry = LLVMAppendBasicBlockInContext(ctx, test_func, "entry");
    LLVMPositionBuilderAtEnd(builder, test_entry);

    // Allocate three Points
    LLVMValueRef p1 = LLVMBuildAlloca(builder, point_ty, "p1");
    LLVMValueRef p2 = LLVMBuildAlloca(builder, point_ty, "p2");
    LLVMValueRef p3 = LLVMBuildAlloca(builder, point_ty, "p3");

    // Initialize p1 = (3, 4)
    LLVMValueRef init_args1[] = {p1, LLVMConstInt(i32, 3, 0), LLVMConstInt(i32, 4, 0)};
    LLVMBuildCall2(builder, init_ty, init_func, init_args1, 3, "");

    // Initialize p2 = (1, 2)
    LLVMValueRef init_args2[] = {p2, LLVMConstInt(i32, 1, 0), LLVMConstInt(i32, 2, 0)};
    LLVMBuildCall2(builder, init_ty, init_func, init_args2, 3, "");

    // Add p1 + p2 -> p3
    LLVMValueRef add_args[] = {p1, p2, p3};
    LLVMBuildCall2(builder, add_ty, add_func, add_args, 3, "");

    // Get manhattan distance of p3
    LLVMValueRef dist_args[] = {p3};
    LLVMValueRef final_dist = LLVMBuildCall2(builder, dist_ty, dist_func, dist_args, 1, "final_dist");

    LLVMBuildRet(builder, final_dist);

    // ==========================================
    // Global constant Point
    // ==========================================
    LLVMValueRef origin_vals[] = {LLVMConstInt(i32, 0, 0), LLVMConstInt(i32, 0, 0)};
    LLVMValueRef origin_const = LLVMConstNamedStruct(point_ty, origin_vals, 2);
    LLVMValueRef origin_global = LLVMAddGlobal(mod, point_ty, "origin");
    LLVMSetInitializer(origin_global, origin_const);
    LLVMSetGlobalConstant(origin_global, 1);

    LLVMDisposeBuilder(builder);

    // Verify module
    char *error = nullptr;
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, "; Verification failed: %s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeModule(mod);
        LLVMContextDispose(ctx);
        return 1;
    }
    LLVMDisposeMessage(error);

    // Print diagnostic comments
    printf("; Test: test_struct\n");
    printf("; Integration test: Point struct manipulation\n");
    printf(";\n");
    printf("; Struct definition:\n");
    printf(";   %%Point = type { i32, i32 }  ; x, y fields\n");
    printf(";\n");
    printf("; Functions:\n");
    printf(";   point_init(Point*, i32, i32) -> void\n");
    printf(";   point_add(Point*, Point*, Point*) -> void\n");
    printf(";   point_manhattan(Point*) -> i32\n");
    printf(";   test_points() -> i32\n");
    printf(";\n");
    printf("; test_points creates:\n");
    printf(";   p1 = (3, 4)\n");
    printf(";   p2 = (1, 2)\n");
    printf(";   p3 = p1 + p2 = (4, 6)\n");
    printf(";   returns manhattan(p3) = 4 + 6 = 10\n");
    printf(";\n");
    printf("; Struct type info:\n");
    printf(";   name: %s\n", LLVMGetStructName(point_ty));
    printf(";   num elements: %u\n", LLVMCountStructElementTypes(point_ty));
    printf(";   is packed: %s\n", LLVMIsPackedStruct(point_ty) ? "yes" : "no");
    printf(";   is opaque: %s\n", LLVMIsOpaqueStruct(point_ty) ? "yes" : "no");
    printf("\n");

    // Print module IR
    char *ir = LLVMPrintModuleToString(mod);
    printf("%s", ir);
    LLVMDisposeMessage(ir);

    // Cleanup
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);

    return 0;
}
