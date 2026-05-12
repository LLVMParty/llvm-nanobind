"""
Regression tests for raw-bytes constant support.

Historically, const_string()/const_data_array() only accepted str and routed
through UTF-8 encoding, which expands bytes > 127. These tests ensure bytes
inputs preserve exact byte length.
"""

import llvm


def test_const_string_accepts_bytes_without_utf8_expansion():
    with llvm.create_context() as ctx:
        with ctx.create_module("bytes_const_string") as mod:
            i8 = ctx.types.i8

            raw = bytes([0xFF, 0x80, 0x42, 0x00, 0x7F])

            # bytes path: exact raw length
            const_raw = ctx.const_string(raw, dont_null_terminate=True)
            assert const_raw.type.array_length == len(raw)

            const_raw_nt = ctx.const_string(raw, dont_null_terminate=False)
            assert const_raw_nt.type.array_length == len(raw) + 1

            # Context method should behave identically.
            const_ctx = ctx.const_string(raw, dont_null_terminate=True)
            assert const_ctx.type.array_length == len(raw)

            # str path still follows UTF-8 behavior for non-ASCII code points.
            legacy = raw.decode("latin-1")
            const_legacy = ctx.const_string(legacy, dont_null_terminate=True)
            assert const_legacy.type.array_length > len(raw)

            # Materialize constants in globals and verify module validity.
            g_raw = mod.add_global(i8.array(len(raw)), "g_raw")
            g_raw.initializer = const_raw

            g_raw_nt = mod.add_global(i8.array(len(raw) + 1), "g_raw_nt")
            g_raw_nt.initializer = const_raw_nt

            assert mod.verify(), mod.get_verification_error()


def test_const_data_array_accepts_bytes_without_utf8_expansion():
    with llvm.create_context() as ctx:
        with ctx.create_module("bytes_const_data_array") as mod:
            i8 = ctx.types.i8
            raw = bytes([0xFF, 0x80, 0x42, 0x00, 0x7F])

            arr_global = i8.array_const(raw)
            assert arr_global.type.array_length == len(raw)

            # Repeated Type helper call.
            arr_method = i8.array_const(raw)
            assert arr_method.type.array_length == len(raw)

            g1 = mod.add_global(i8.array(len(raw)), "g1")
            g1.initializer = arr_global

            g2 = mod.add_global(i8.array(len(raw)), "g2")
            g2.initializer = arr_method

            assert mod.verify(), mod.get_verification_error()


if __name__ == "__main__":
    test_const_string_accepts_bytes_without_utf8_expansion()
    print("test_const_string_accepts_bytes_without_utf8_expansion: PASSED")

    test_const_data_array_accepts_bytes_without_utf8_expansion()
    print("test_const_data_array_accepts_bytes_without_utf8_expansion: PASSED")
