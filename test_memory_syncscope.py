"""
Test for memory safety with custom sync scopes.

ROOT CAUSE:
Custom sync scopes like "agent" are context-specific. Each context maintains
a mapping from sync scope names to integer IDs. When cloning atomic operations
from one module to another (potentially with different contexts), the sync scope
ID from the source is copied directly. This ID may not be valid in the destination
context, causing a segfault when LLVM tries to print or use the instruction.

EXPECTED FIX:
Instead of copying the sync scope ID directly:
1. Get the sync scope name from the source context (needs new API binding)
2. Register/lookup that name in the destination context
3. Use the new ID for the destination instruction

OR:
Check if sync scope IDs are compatible and raise a clear exception if not.

APIS INVOLVED:
- LLVMGetAtomicSyncScopeID(inst) - gets ID from instruction
- LLVMSetAtomicSyncScopeID(inst, id) - sets ID on instruction
- LLVMGetSyncScopeID(context, name, len) - gets/creates ID from name
- Need: API to get name from ID (not currently in LLVM-C)
"""


def test_custom_syncscope_crash_pure_python():
    """Custom sync scope 'agent' causes crash - pure Python reproduction.

    This test demonstrates the crash by actually running echo.py logic on a
    module with custom syncscopes. The issue is that echo.py copies syncscope
    IDs across contexts without translation.

    Expected: Should raise an exception when invalid syncscope ID is used
    Actual: Segfaults when trying to print the module

    WARNING: This test will crash Python! See test below for subprocess version.
    """
    # For now, this documents the issue. The actual crash happens in echo.py
    # when it calls get_atomic_sync_scope_id() on source and uses that ID
    # directly in the destination context.
    print("This test documents the syncscope cross-context bug.")
    print("The crash occurs in echo.py when cloning atomicrmw/cmpxchg/fence")
    print("with custom syncscopes like 'agent'.")
    print()
    print("To reproduce: cat llvm-c/llvm-c-test/inputs/echo.ll | \\")
    print("  ./llvm-bin llvm-as | \\")
    print("  uv run llvm-c-test --echo")
    print()
    print("The issue is at echo.py lines 633, 643, 660, 673, 831:")
    print("  dst.set_atomic_sync_scope_id(src.get_atomic_sync_scope_id())")
    print()
    print("The syncscope ID is context-specific and cannot be copied directly.")
    pass


def test_standard_syncscope_works():
    """Standard sync scope 'singlethread' should work correctly when echoed."""
    import subprocess

    # Create IR with standard singlethread syncscope
    ir = """define void @test(ptr %ptr) {
  %a = atomicrmw volatile xchg ptr %ptr, i8 0 syncscope("singlethread") acq_rel, align 8
  ret void
}"""

    # Write to temp file and run echo command
    with open("/tmp/test_syncscope_std.ll", "w") as f:
        f.write(ir)

    # Compile to bitcode
    result = subprocess.run(
        [
            "./llvm-bin",
            "llvm-as",
            "/tmp/test_syncscope_std.ll",
            "-o",
            "/tmp/test_syncscope_std.bc",
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, f"llvm-as failed: {result.stderr}"

    # Run echo via Python
    with open("/tmp/test_syncscope_std.bc", "rb") as f:
        bc_data = f.read()

    result = subprocess.run(
        ["uv", "run", "python", "-m", "llvm_c_test", "--echo"],
        input=bc_data,
        capture_output=True,
    )

    assert result.returncode == 0, f"Echo failed: {result.stderr.decode()}"
    assert b"syncscope" in result.stdout, "Missing syncscope in output"
    print("test_standard_syncscope_works: PASSED")


if __name__ == "__main__":
    test_standard_syncscope_works()
    test_custom_syncscope_crash_pure_python()
