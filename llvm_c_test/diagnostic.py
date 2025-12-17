"""
llvm-c-test diagnostic handler command implementations.

This module implements the --test-diagnostic-handler command which tests
the diagnostic handler functionality.
"""

import sys
import llvm


def test_diagnostic_handler():
    """Test diagnostic handler functionality.

    Sets up a diagnostic handler on the global context and attempts to
    load invalid bitcode to trigger it.

    TODO: This needs to be updated to use the new parsing API with LLVMParseError.
    The diagnostic handler is now integrated into the context automatically.
    """
    # TODO: Reimplement using ctx.parse_bitcode_from_bytes and catching LLVMParseError
    print("test-diagnostic-handler not yet implemented with new API", file=sys.stderr)
    return 0

    # Get global context
    global_ctx = llvm.global_context()

    # Set up diagnostic handler (now automatic)
    # llvm.context_set_diagnostic_handler(global_ctx)

    # Read stdin
    bitcode = sys.stdin.buffer.read()

    # Try to load bitcode - this may trigger the diagnostic handler
    try:
        with global_ctx.parse_bitcode_from_bytes(bitcode) as mod:
            # If we get here, loading succeeded
            pass
    except llvm.LLVMParseError as e:
        # Loading failed, diagnostics are in e.diagnostics
        pass

    # Check if diagnostic handler was called
    if False:  # llvm.diagnostic_was_called():
        print("Executing diagnostic handler", file=sys.stderr)

        # Get severity
        severity = llvm.get_diagnostic_severity()
        severity_name = "unknown"
        if severity == llvm.DiagnosticSeverity.Error:
            severity_name = "error"
        elif severity == llvm.DiagnosticSeverity.Warning:
            severity_name = "warning"
        elif severity == llvm.DiagnosticSeverity.Remark:
            severity_name = "remark"
        elif severity == llvm.DiagnosticSeverity.Note:
            severity_name = "note"

        print(f"Diagnostic severity is of type {severity_name}", file=sys.stderr)
        print("Diagnostic handler was called while loading module", file=sys.stderr)
    else:
        print("Diagnostic handler was not called while loading module", file=sys.stderr)

    return 0
