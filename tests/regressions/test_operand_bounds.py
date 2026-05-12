"""
Test for bounds checking on Value.get_operand().

Previously, calling get_operand() with an out-of-range index on a value
with no operands (like a ConstantDataArray) would segfault. Now it raises
a clear error with the index and operand count.
"""

import llvm


def test_get_operand_out_of_range():
    """get_operand() on a value with 0 operands should raise, not segfault."""
    with llvm.create_context() as ctx:
        # ConstantDataArray created from const_string has 0 operands
        val = ctx.const_string("hello")
        assert val.num_operands == 0

        try:
            val.get_operand(0)
            assert False, "Expected an exception for out-of-range operand"
        except Exception as e:
            msg = str(e)
            assert "out of range" in msg, f"Unexpected error message: {msg}"
            assert "num_operands=0" in msg, f"Expected num_operands=0 in: {msg}"


if __name__ == "__main__":
    test_get_operand_out_of_range()
    print("test_get_operand_out_of_range: PASSED")
