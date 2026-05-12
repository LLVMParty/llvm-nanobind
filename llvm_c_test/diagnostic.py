"""
llvm-c-test diagnostic handler command implementations.

This module implements the --test-diagnostic-handler command which tests
the diagnostic handler functionality.
"""

import sys
import llvm


def test_diagnostic_handler():
    """Test diagnostic handler functionality.

    The Python binding automatically installs a diagnostic handler that collects
    diagnostics. We use ctx.diagnostics to check if any were triggered during
    bitcode parsing.
    """
    # Get global context (has automatic diagnostic handler installed)
    ctx = llvm.global_context()

    # Clear any previous diagnostics
    ctx.clear_diagnostics()

    # Read stdin
    bitcode = sys.stdin.buffer.read()

    # Track if handler was called via diagnostics list
    handler_called = False
    diagnostics = []

    # Try to load bitcode - this may trigger the diagnostic handler
    try:
        with ctx.parse_bitcode_from_bytes(bitcode) as mod:
            # If we get here, loading succeeded
            pass
    except llvm.LLVMParseError:
        # Loading failed - diagnostic handler was called
        # Get diagnostics from context (they were collected before exception)
        pass

    # Check context diagnostics
    diagnostics = ctx.diagnostics
    handler_called = len(diagnostics) > 0

    # Check if diagnostic handler was called
    if handler_called and len(diagnostics) > 0:
        print("Executing diagnostic handler", file=sys.stderr)

        # Get severity from first diagnostic
        severity = diagnostics[0].severity
        print(f"Diagnostic severity is of type {severity}", file=sys.stderr)
        print("Diagnostic handler was called while loading module", file=sys.stderr)
    else:
        print("Diagnostic handler was not called while loading module", file=sys.stderr)

    return 0
