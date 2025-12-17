# Debugging Best Practices for llvm-nanobind

## Overview

This document outlines best practices for debugging hard crashes (segfaults) in the Python bindings. Our goal is to **protect users from footguns** by raising Python exceptions instead of crashing whenever possible.

## Core Principles

1. **Exception over Crash**: Use lifetime information and ownership tracking to validate references before calling LLVM-C APIs
2. **Isolate Crashes**: Create standalone test cases that reproduce the issue without external dependencies
3. **Document Thoroughly**: Every crash should be documented with root cause analysis and expected behavior

## When Encountering Hard Crashes

### Step 1: Isolate into a Standalone Test File

Create a `test_memory_*.py` file that reproduces the crash:

**Initial version (using subprocess):**
```python
"""
Brief description of the memory safety issue.

Detailed explanation of root cause and expected fix.
"""

import subprocess

def test_crash_via_subprocess():
    """Documents the crash by calling llvm-c-test."""
    # Use subprocess to call llvm-c-test and capture crash
    # This is safe but not ideal for CI/testing
    pass
```

**Refined version (pure Python - required):**
```python
"""
Brief description of the memory safety issue.

Detailed explanation of root cause and expected fix.
"""

import llvm

def test_crash_pure_python():
    """Minimal pure Python reproduction of the crash.
    
    This test should:
    - Use only the llvm module
    - Be completely standalone
    - Demonstrate the exact API calls that cause the crash
    - Include comments about expected vs actual behavior
    """
    ctx = llvm.Context()
    # ... minimal reproduction code ...
    # This will crash until fixed
    
def test_working_case():
    """Related case that works correctly.
    
    This provides a contrast to show what should work
    and helps validate the fix doesn't break working cases.
    """
    pass
```

### Step 2: Document the Crash

In the test file docstring and comments, include:

1. **What operation causes the crash**
   - Specific API calls
   - Sequence of operations
   - Any special conditions

2. **Root cause analysis**
   - Why does it crash?
   - Is it a lifetime issue?
   - Is it invalid data being passed to LLVM-C?
   - Is it a context/module ownership problem?

3. **Expected behavior**
   - What should happen instead?
   - Should it raise an exception?
   - Should it succeed with different behavior?

4. **Stack trace** (if available)
   - Include the segfault location
   - LLVM internal function that crashed
   - Python call stack

### Step 3: Create Both Positive and Negative Tests

Always include:
- **Negative test**: Documents the crash (may be skipped initially)
- **Positive test**: Related functionality that should work

Example:
```python
def test_custom_syncscope_crash():
    """Custom sync scopes cause crash - will be fixed."""
    # Documents the failing case
    pass

def test_standard_syncscope_works():
    """Standard sync scopes work correctly."""
    # Tests the working path
    assert result == expected
```

## Common Crash Categories

### 1. Lifetime/Ownership Issues

**Symptoms:**
- Segfault when accessing disposed objects
- Use-after-free errors
- Double-free in destructors

**Debug Strategy:**
- Check validity tokens before LLVM-C API calls
- Verify parent objects (Context, Module) are still alive
- Look for ownership transfer issues

**Example Fix:**
```python
# BAD: No validity check
def some_api_call(self):
    return LLVMSomeFunction(self._ptr)

# GOOD: Check validity first
def some_api_call(self):
    self.check_valid()  # Raises exception if invalid
    return LLVMSomeFunction(self._ptr)
```

### 2. Context-Specific Data

**Symptoms:**
- Crash when using values from different contexts
- Invalid IDs or references across module boundaries
- Segfault in LLVM print/dump functions

**Debug Strategy:**
- Check if data is being copied across contexts
- Look for context-specific IDs (sync scopes, metadata IDs)
- Verify all objects belong to the same context

**Example: Sync Scope ID Issue**
```python
# BAD: Sync scope ID is context-specific
src_id = src.get_atomic_sync_scope_id()
dst.set_atomic_sync_scope_id(src_id)  # Wrong context!

# GOOD: Map sync scope name across contexts
# (Implementation depends on available APIs)
```

### 3. Callback Lifetime Issues

**Symptoms:**
- Crash when callback is invoked
- Segfault in diagnostic handlers or other callbacks
- Memory corruption after function returns

**Debug Strategy:**
- Ensure callbacks keep Python objects alive
- Check if C++ is holding references past Python object lifetime
- Verify callback signature matches LLVM-C expectations

**Example Fix:**
```python
# Store callback to prevent garbage collection
self._callback_ref = callback
LLVMSetDiagnosticHandler(ctx, callback)
```

## Protecting Users from Footguns

### Validation Before LLVM-C Calls

Every wrapper method should validate inputs before calling LLVM-C APIs:

```python
def build_load(self, ty, ptr, name=""):
    """Build a load instruction.
    
    Raises:
        LLVMException: If builder, type, or pointer are invalid
    """
    # Check our own validity
    self.check_valid()
    
    # Check parameter validity
    if not isinstance(ty, Type):
        raise TypeError("ty must be a Type")
    ty.check_valid()
    
    if not isinstance(ptr, Value):
        raise TypeError("ptr must be a Value")
    ptr.check_valid()
    
    # Now safe to call C API
    result = LLVMBuildLoad2(self._ptr, ty._ptr, ptr._ptr, name)
    return Value._from_ptr(result)
```

### Exception Hierarchy

Use specific exception types to help users debug:

```python
class LLVMException(Exception):
    """Base exception for LLVM errors."""
    pass

class LLVMInvalidObjectException(LLVMException):
    """Object has been invalidated (parent disposed)."""
    pass

class LLVMContextMismatchException(LLVMException):
    """Objects from different contexts cannot be mixed."""
    pass

class LLVMTypeException(LLVMException):
    """Type-related error."""
    pass
```

### Ownership Documentation

Every class should document its ownership semantics:

```python
class Module:
    """LLVM Module.
    
    Ownership:
        - Owned by Context
        - Disposing the Context invalidates all Modules
        - Disposing a Module invalidates all Functions/Globals in it
        
    Lifetime:
        - Valid as long as parent Context is valid
        - Can be explicitly disposed with dispose()
        - Automatic disposal when Context is disposed
        
    Thread Safety:
        - Not thread-safe
        - Must be used from a single thread
    """
```

## Testing Strategy

### 1. Unit Tests for Each Crash

Each `test_memory_*.py` file should:
- Be runnable standalone: `uv run test_memory_xyz.py`
- Have clear pass/fail criteria
- Include comments about current status (known to crash, fixed, etc.)

### 2. Integration with CI

```python
import pytest

@pytest.mark.xfail(reason="Known crash - issue #123")
def test_custom_syncscope_crash():
    """This test is expected to fail until fixed."""
    # When fixed, remove @pytest.mark.xfail
    pass
```

### 3. Coverage

Run tests with coverage to ensure crash paths are tested:
```bash
uv run coverage run test_memory_syncscope.py
uv run coverage report
```

## Example: Complete Crash Investigation

### File: `test_memory_syncscope.py`

```python
"""
Test for memory safety with custom sync scopes.

ROOT CAUSE:
Custom sync scopes like "agent" need to be registered in both source and
destination contexts. When cloning atomic operations with custom sync scopes,
the scope ID from the source module is copied directly but this ID is only
valid in the source context. Using this ID in a different context causes a
segfault when LLVM tries to print/use the instruction.

EXPECTED FIX:
Instead of copying the sync scope ID directly, we need to:
1. Get the sync scope name from the source context
2. Register/lookup that name in the destination context
3. Use the new ID for the destination instruction

APIS NEEDED:
- Way to get sync scope name from ID (may need new LLVM-C binding)
- Context.get_sync_scope_id(name) - already exists
"""

import llvm

def test_custom_syncscope_crash():
    """Custom sync scope 'agent' causes crash when cloning.
    
    Expected: Should raise LLVMContextMismatchException or similar
    Actual: Segfaults in LLVMPrintModuleToString
    """
    # Create source module with custom syncscope
    src_ctx = llvm.Context()
    src_mod = llvm.Module.create_with_name("source", src_ctx)
    
    # ... create atomic instruction with syncscope("agent") ...
    
    # Create destination module
    dst_ctx = llvm.Context()
    dst_mod = llvm.Module.create_with_name("dest", dst_ctx)
    
    # Clone instruction (this should raise exception instead of crashing)
    # Currently crashes when trying to print/use the cloned instruction
    
def test_standard_syncscope_works():
    """Standard sync scopes work correctly across contexts."""
    # Similar test but with syncscope("singlethread")
    # This should work because singlethread has a reserved ID
    pass

if __name__ == "__main__":
    test_standard_syncscope_works()
    print("Standard syncscope test passed")
    
    # Uncomment when ready to test the crash
    # test_custom_syncscope_crash()
```

## Resources

- `devdocs/memory-model.md` - Memory model documentation
- `devdocs/fixing-tests/plan.md` - Current test fixing plan
- `AGENTS.md` - Agent guidelines (references this document)

## Summary Checklist

When debugging a crash:

- [ ] Create `test_memory_*.py` with pure Python reproduction
- [ ] Document root cause in test file docstring
- [ ] Include both failing and working test cases
- [ ] Add validation checks to prevent crash (if possible)
- [ ] Raise appropriate Python exception instead of crashing
- [ ] Update relevant documentation
- [ ] Add test to CI with appropriate markers
- [ ] Verify fix doesn't break existing functionality
