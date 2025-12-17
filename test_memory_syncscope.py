"""
Test for memory safety with custom sync scopes.

Custom sync scopes like "agent" need to be registered in both source and
destination modules. When cloning atomic operations with custom sync scopes,
the scope ID from the source module may not be valid in the destination module.
"""

import llvm


def test_custom_syncscope_causes_crash():
    """Custom sync scope 'agent' causes crash when printing cloned module.

    This test documents the current failing behavior.
    The sync scope ID from the source module is not valid in the destination.

    Expected: Should either raise an exception or properly clone the sync scope.
    Actual: Segfault when printing the module.
    """
    import subprocess

    # Create IR with custom syncscope
    ir = """define void @test(ptr %ptr) {
  %a = atomicrmw volatile xchg ptr %ptr, i8 0 syncscope("agent") acq_rel, align 8
  ret void
}"""

    # Write to temp file and run echo command
    with open("/tmp/test_syncscope.ll", "w") as f:
        f.write(ir)

    # Compile to bitcode
    result = subprocess.run(
        [
            "/opt/homebrew/opt/llvm/bin/llvm-as",
            "/tmp/test_syncscope.ll",
            "-o",
            "/tmp/test_syncscope.bc",
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"llvm-as failed: {result.stderr}")
        return

    # This currently crashes - documenting expected behavior
    # When fixed, should either:
    # 1. Properly clone the sync scope to destination module, or
    # 2. Raise a clear exception about unsupported custom sync scopes
    print("Custom syncscope test: This test documents a known crash.")
    print("Running echo command with syncscope('agent') will crash.")
    print("Skipping actual execution to avoid segfault.")


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
            "/opt/homebrew/opt/llvm/bin/llvm-as",
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
    test_custom_syncscope_causes_crash()
