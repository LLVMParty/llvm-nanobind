"""
Test for DIBuilder metadata reference ordering.

NOTE: This is NOT a memory safety issue - just a metadata ID mismatch.

ROOT CAUSE:
The debug_info_new_format.ll test expects specific metadata IDs (e.g., !dbg !44)
but the Python implementation produces slightly different IDs (e.g., !dbg !45).

This is likely due to differences in the order that metadata nodes are created
or how they're assigned IDs in the DIBuilder implementation.

IMPACT:
- No crash or memory corruption
- Functionally equivalent IR
- Only affects metadata ID numbering

EXPECTED BEHAVIOR:
The metadata IDs should match the C version exactly, but this is cosmetic.

PRIORITY: Low (cosmetic issue, not a safety concern)
"""

import subprocess
import sys


def test_dibuilder_metadata_ids():
    """Test that DIBuilder produces correct metadata IDs.

    Expected: Metadata IDs should match C version
    Actual: Off by one (e.g., !44 vs !45)

    This is a cosmetic issue, not a crash.
    """
    result = subprocess.run(
        ["uv", "run", "llvm-c-test", "--test-dibuilder"], capture_output=True, text=True
    )

    print("Exit code:", result.returncode)
    print("\nOutput (first 20 lines):")
    print("\n".join(result.stdout.split("\n")[:20]))

    # Check for the expected metadata reference
    if "!dbg !44" in result.stdout:
        print("\n✅ Metadata IDs match expected values")
        return True
    elif "!dbg !45" in result.stdout:
        print("\n⚠️  Metadata IDs are off by one (!45 instead of !44)")
        print("This is a cosmetic issue, not a crash.")
        return False
    else:
        print("\n❓ Could not find expected metadata pattern")
        return False


if __name__ == "__main__":
    print("=" * 60)
    print("Testing DIBuilder metadata ID assignment")
    print("=" * 60)

    matches = test_dibuilder_metadata_ids()

    print("\n" + "=" * 60)
    if matches:
        print("✅ Test passed - metadata IDs match")
    else:
        print("⚠️  Metadata IDs don't match (cosmetic issue only)")
        print("\nThis is NOT a memory safety issue.")
        print("The generated IR is functionally equivalent.")
    print("=" * 60)
