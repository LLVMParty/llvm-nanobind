"""
Test for memory safety with lazy module loading.

ROOT CAUSE:
When using lazy=True with parse_bitcode_in_context, the module crashes on
disposal. The crash occurs in the Module's destructor trying to free a
MemoryBuffer via unique_ptr::reset().

Stack trace shows:
- Module destructor (LLVMDisposeModule)
- MemoryBuffer unique_ptr reset
- Segfault or double-free

EXPECTED BEHAVIOR:
Lazy-loaded modules should cleanly dispose without crashing.

APIS INVOLVED:
- llvm.parse_bitcode_in_context(ctx, membuf, lazy=True, new_api=False)
- Module disposal

HYPOTHESIS:
With lazy=True, LLVM takes ownership of the memory buffer and embeds it
in the Module. When we dispose our MemoryBuffer wrapper, LLVM's Module
still holds a reference. When the Module is later disposed, it tries to
free the already-freed buffer.

The C version handles this by NOT disposing the memory buffer when lazy=True:
  if (!Lazy)
    LLVMDisposeMemoryBuffer(MB);

Our Python bindings may not have this conditional logic.
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


def test_lazy_module_crash():
    """Minimal pure Python reproduction of lazy module crash.

    Expected: Should load module lazily and dispose cleanly
    Actual: Crashes on module disposal

    This reproduces the functions.ll --lazy-module-dump crash.
    """
    # Create a simple bitcode directly
    ir = """define i32 @test() {
  ret i32 42
}"""

    bitcode_result = subprocess.run(
        [str(LLVM_BIN / "llvm-as"), "-"], input=ir.encode(), capture_output=True
    )
    assert bitcode_result.returncode == 0

    bitcode_data = bitcode_result.stdout

    result = subprocess.run(
        [
            sys.executable,
            "-c",
            """
import llvm
import sys

membuf = llvm.create_memory_buffer_with_stdin()
ctx = llvm.global_context()

# Load with lazy=True and new_api=True
try:
    mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=True, new_api=True)
    print("Lazy module (new API) loaded successfully", file=sys.stderr)
    print(str(mod), file=sys.stderr)
except Exception as e:
    print(f"Error: {e}", file=sys.stderr)
    sys.exit(1)
""",
        ],
        input=bitcode_data,
        capture_output=True,
    )
    assert bitcode_result.returncode == 0

    bitcode_data = bitcode_result.stdout

    result = subprocess.run(
        [
            sys.executable,
            "-c",
            """
import llvm
import sys

membuf = llvm.create_memory_buffer_with_stdin()
ctx = llvm.global_context()

# Load with lazy=False (should work)
try:
    mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=False, new_api=False)
    print("Non-lazy module loaded successfully", file=sys.stderr)
    print(str(mod), file=sys.stderr)
except Exception as e:
    print(f"Error: {e}", file=sys.stderr)
    sys.exit(1)
""",
        ],
        input=bitcode_data,
        capture_output=True,
    )
    assert bitcode_result.returncode == 0, (
        f"llvm-as failed: {bitcode_result.stderr.decode()}"
    )

    bitcode_data = bitcode_result.stdout

    # Test lazy loading
    result = subprocess.run(
        [
            sys.executable,
            "-c",
            """
import llvm
import sys

# Read bitcode
membuf = llvm.create_memory_buffer_with_stdin()
ctx = llvm.global_context()

# Load with lazy=True (THIS CAUSES THE CRASH)
try:
    mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=True, new_api=False)
    print("Lazy module loaded successfully", file=sys.stderr)
    
    # Try to print it
    print(str(mod), file=sys.stderr)
except Exception as e:
    print(f"Error: {e}", file=sys.stderr)
    sys.exit(1)

# Cleanup happens here - THIS IS WHERE THE CRASH OCCURS
# When Python exits, Module destructor runs and tries to free
# the MemoryBuffer that we already freed or that it owns
""",
        ],
        input=bitcode_data,
        capture_output=True,
    )
    assert result.returncode == 0, f"llvm-as failed: {result.stderr.decode()}"

    # Test lazy loading
    result = subprocess.run(
        [
            sys.executable,
            "-c",
            """
import llvm
import sys

# Read bitcode
membuf = llvm.create_memory_buffer_with_stdin()
ctx = llvm.global_context()

# Load with lazy=True (THIS CAUSES THE CRASH)
try:
    mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=True, new_api=False)
    print("Lazy module loaded successfully", file=sys.stderr)
    
    # Try to print it
    print(str(mod), file=sys.stderr)
except Exception as e:
    print(f"Error: {e}", file=sys.stderr)
    sys.exit(1)

# Cleanup happens here - THIS IS WHERE THE CRASH OCCURS
# When Python exits, Module destructor runs and tries to free
# the MemoryBuffer that we already freed or that it owns
""",
        ],
        input=open("/tmp/lazy_test.bc", "rb").read(),
        capture_output=True,
    )

    print("Exit code:", result.returncode)
    print("Stderr:", result.stderr.decode())

    # Check for crash (SIGSEGV=-11, SIGABRT=-6 or 134)
    if result.returncode < 0 or result.returncode == 134:
        print("\n❌ CRASH DETECTED: Memory corruption in lazy module disposal")
        print("Exit code:", result.returncode)
        return False
    else:
        print("\n✅ No crash - lazy loading works!")
        return True


def test_non_lazy_module():
    """Test non-lazy module loading (baseline - should work).

    This verifies that the issue is specific to lazy loading.
    """
    ir = """define i32 @test() {
  ret i32 42
}"""

    result = subprocess.run(
        [
            "/Users/admin/Projects/llvm-21/bin/llvm-as",
            "-",
            "-o",
            "/tmp/non_lazy_test.bc",
        ],
        input=ir.encode(),
        capture_output=True,
    )
    assert result.returncode == 0

    result = subprocess.run(
        [
            sys.executable,
            "-c",
            """
import llvm
import sys

membuf = llvm.create_memory_buffer_with_stdin()
ctx = llvm.global_context()

# Load with lazy=False (should work)
try:
    mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=False, new_api=False)
    print("Non-lazy module loaded successfully", file=sys.stderr)
    print(str(mod), file=sys.stderr)
except Exception as e:
    print(f"Error: {e}", file=sys.stderr)
    sys.exit(1)
""",
        ],
        input=open("/tmp/non_lazy_test.bc", "rb").read(),
        capture_output=True,
    )

    print("\nNon-lazy test:")
    print("Exit code:", result.returncode)

    if result.returncode < 0 or result.returncode == 134:
        print("❌ Non-lazy also crashes (unexpected!)")
        return False
    else:
        print("✅ Non-lazy works fine")
        return True


def test_lazy_with_new_api():
    """Test lazy loading with new API.

    Checks if the issue is specific to old API or affects new API too.
    """
    ir = """define i32 @test() {
  ret i32 42
}"""

    result = subprocess.run(
        [
            "/Users/admin/Projects/llvm-21/bin/llvm-as",
            "-",
            "-o",
            "/tmp/lazy_new_test.bc",
        ],
        input=ir.encode(),
        capture_output=True,
    )
    assert result.returncode == 0

    result = subprocess.run(
        [
            sys.executable,
            "-c",
            """
import llvm
import sys

membuf = llvm.create_memory_buffer_with_stdin()
ctx = llvm.global_context()

# Load with lazy=True and new_api=True
try:
    mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=True, new_api=True)
    print("Lazy module (new API) loaded successfully", file=sys.stderr)
    print(str(mod), file=sys.stderr)
except Exception as e:
    print(f"Error: {e}", file=sys.stderr)
    sys.exit(1)
""",
        ],
        input=open("/tmp/lazy_new_test.bc", "rb").read(),
        capture_output=True,
    )

    print("\nLazy with new API test:")
    print("Exit code:", result.returncode)

    if result.returncode < 0 or result.returncode == 134:
        print("❌ New API also crashes")
        return False
    else:
        print("✅ New API works")
        return True


if __name__ == "__main__":
    print("=" * 60)
    print("Testing lazy module loading memory safety")
    print("=" * 60)

    print("\n1. Testing non-lazy loading (baseline)...")
    non_lazy_works = test_non_lazy_module()

    print("\n2. Testing lazy loading with old API...")
    lazy_works = test_lazy_module_crash()

    print("\n3. Testing lazy loading with new API...")
    lazy_new_works = test_lazy_with_new_api()

    print("\n" + "=" * 60)
    print("SUMMARY:")
    print(f"  Non-lazy:     {'✅ PASS' if non_lazy_works else '❌ CRASH'}")
    print(f"  Lazy (old):   {'✅ PASS' if lazy_works else '❌ CRASH'}")
    print(f"  Lazy (new):   {'✅ PASS' if lazy_new_works else '❌ CRASH'}")
    print("=" * 60)

    if not lazy_works or not lazy_new_works:
        print("\nLazy module loading has a memory management issue.")
        print("The LLVM-C API documentation states:")
        print("  'If Lazy is true, the module is loaded lazily, and the memory")
        print("   buffer is NOT disposed when the module is disposed.'")
        print("\nOur wrapper may be disposing the buffer unconditionally.")
        sys.exit(1)
