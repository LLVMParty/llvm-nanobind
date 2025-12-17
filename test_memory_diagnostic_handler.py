"""
Test for memory safety with diagnostic handler.

ROOT CAUSE:
When using the diagnostic handler API, there's a double-free crash when
disposing the memory buffer after loading a module. The crash occurs in
LLVMDisposeMemoryBuffer with error: "free_medium_botch" indicating the
same memory is being freed twice.

Stack trace shows:
- LLVMDisposeMemoryBuffer is called
- free_medium_botch error (double-free detection)
- Crash in memory buffer destructor

EXPECTED BEHAVIOR:
Should cleanly dispose of the memory buffer and module without crashing.

APIS INVOLVED:
- llvm.create_memory_buffer_with_stdin()
- llvm.context_set_diagnostic_handler(ctx)
- llvm.get_bitcode_module_2(membuf)
- llvm.diagnostic_was_called()

HYPOTHESIS:
The new bitcode API (get_bitcode_module_2) may be taking ownership of the
memory buffer differently than expected, leading to double-free when we
try to dispose it.
"""

import llvm
import subprocess
import sys
from pathlib import Path

# Get LLVM tools directory
LLVM_PREFIX = Path(__file__).parent / ".llvm-prefix"
if LLVM_PREFIX.exists():
    LLVM_BIN = Path(LLVM_PREFIX.read_text().strip()) / "bin"
else:
    LLVM_BIN = Path("/Users/admin/Projects/llvm-21/bin")  # Fallback


def test_diagnostic_handler_crash():
    """Minimal pure Python reproduction of diagnostic handler crash.

    Expected: Should cleanly load module and check diagnostic handler
    Actual: Crashes on cleanup with double-free in LLVMDisposeMemoryBuffer

    This test reproduces the crash that occurs in empty.ll test.
    """
    # Create empty bitcode directly (no temp files needed)
    # llvm-as reads from stdin and writes to stdout
    result = subprocess.run(
        [str(LLVM_BIN / "llvm-as"), "-"],
        input=b"",  # Empty module
        capture_output=True,
    )
    assert result.returncode == 0, f"llvm-as failed: {result.stderr.decode()}"

    bitcode_data = result.stdout

    # This is what diagnostic.py does
    ctx = llvm.global_context()

    # Set diagnostic handler
    llvm.context_set_diagnostic_handler(ctx)

    # Read from stdin simulation - use create_memory_buffer_with_stdin
    # For testing, we'll write to stdin in subprocess
    result = subprocess.run(
        [
            sys.executable,
            "-c",
            """
import llvm
import sys

# Simulate what happens in diagnostic.py test_diagnostic_handler()
ctx = llvm.global_context()
llvm.context_set_diagnostic_handler(ctx)

# Read stdin (we pass empty bitcode)
membuf = llvm.create_memory_buffer_with_stdin()

# Load module
try:
    mod = llvm.get_bitcode_module_2(membuf)
except Exception as e:
    pass

# Check diagnostic handler
if llvm.diagnostic_was_called():
    print("Diagnostic handler was called", file=sys.stderr)
else:
    print("Diagnostic handler was not called while loading module", file=sys.stderr)

# Cleanup happens here - THIS IS WHERE THE CRASH OCCURS
# When Python exits, the memory buffer destructor runs
# and calls LLVMDisposeMemoryBuffer, which double-frees
""",
        ],
        input=bitcode_data,
        capture_output=True,
    )

    print("Exit code:", result.returncode)
    print("Stdout:", result.stdout.decode())
    print("Stderr:", result.stderr.decode())

    # If we get here without crash, test passed
    # But it will crash with exit code 134 (SIGABRT)
    if result.returncode == 134:
        print("\n❌ CRASH DETECTED: Double-free in memory buffer disposal")
        print("This is the bug we need to fix.")
        return False
    else:
        print("\n✅ No crash - test passed!")
        return True


def test_diagnostic_handler_without_new_api():
    """Test diagnostic handler with old API (should work).

    This tests whether the issue is specific to the new API or
    a general diagnostic handler problem.
    """
    # Create empty bitcode directly
    result = subprocess.run(
        [str(LLVM_BIN / "llvm-as"), "-"], input=b"", capture_output=True
    )
    assert result.returncode == 0

    bitcode_data = result.stdout

    # Test with old API
    result = subprocess.run(
        [
            sys.executable,
            "-c",
            """
import llvm
import sys

ctx = llvm.global_context()
llvm.context_set_diagnostic_handler(ctx)

membuf = llvm.create_memory_buffer_with_stdin()

# Use old API instead of new
try:
    mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=False, new_api=False)
except Exception as e:
    print(f"Error: {e}", file=sys.stderr)

if llvm.diagnostic_was_called():
    print("Diagnostic handler was called", file=sys.stderr)
else:
    print("Diagnostic handler was not called", file=sys.stderr)
""",
        ],
        input=bitcode_data,
        capture_output=True,
    )

    print("\nOld API test:")
    print("Exit code:", result.returncode)
    print("Stderr:", result.stderr.decode())

    if result.returncode == 134:
        print("❌ Old API also crashes")
        return False
    else:
        print("✅ Old API works fine")
        return True


if __name__ == "__main__":
    print("=" * 60)
    print("Testing diagnostic handler memory safety")
    print("=" * 60)

    print("\n1. Testing with old API (baseline)...")
    old_api_works = test_diagnostic_handler_without_new_api()

    print("\n2. Testing with new API (the crashing case)...")
    new_api_works = test_diagnostic_handler_crash()

    print("\n" + "=" * 60)
    print("SUMMARY:")
    print(f"  Old API: {'✅ PASS' if old_api_works else '❌ CRASH'}")
    print(f"  New API: {'✅ PASS' if new_api_works else '❌ CRASH'}")
    print("=" * 60)

    if not new_api_works:
        print("\nThe new API (get_bitcode_module_2) has a memory management issue.")
        print("Hypothesis: The API may be taking ownership of the memory buffer")
        print("in a way that conflicts with our wrapper's ownership tracking.")
        sys.exit(1)
