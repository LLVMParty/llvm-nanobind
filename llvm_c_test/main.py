"""
Command dispatcher for llvm-c-test Python port.

Parses command line arguments and dispatches to appropriate command handler.
"""

import sys


def print_usage():
    """Print usage information."""
    print("llvm-c-test command", file=sys.stderr)
    print("", file=sys.stderr)
    print(" Commands:", file=sys.stderr)
    print("  * --module-dump", file=sys.stderr)
    print("    Read bitcode from stdin - print disassembly", file=sys.stderr)
    print("", file=sys.stderr)
    print("  * --lazy-module-dump", file=sys.stderr)
    print("    Lazily read bitcode from stdin - print disassembly", file=sys.stderr)
    print("", file=sys.stderr)
    print("  * --new-module-dump", file=sys.stderr)
    print("    Read bitcode from stdin - print disassembly", file=sys.stderr)
    print("", file=sys.stderr)
    print("  * --lazy-new-module-dump", file=sys.stderr)
    print("    Lazily read bitcode from stdin - print disassembly", file=sys.stderr)
    print("", file=sys.stderr)
    print("  * --module-list-functions", file=sys.stderr)
    print("    Read bitcode from stdin - list summary of functions", file=sys.stderr)
    print("", file=sys.stderr)
    print("  * --module-list-globals", file=sys.stderr)
    print("    Read bitcode from stdin - list summary of globals", file=sys.stderr)
    print("", file=sys.stderr)
    print("  * --targets-list", file=sys.stderr)
    print("    List available targets", file=sys.stderr)
    print("", file=sys.stderr)
    print("  * --calc", file=sys.stderr)
    print(
        "    Read lines of name, rpn from stdin - print generated module",
        file=sys.stderr,
    )
    print("", file=sys.stderr)


def main():
    """Main entry point - parse arguments and dispatch to command handler."""
    if len(sys.argv) != 2:
        print_usage()
        return 1

    command = sys.argv[1]

    # Phase 1 commands
    if command == "--targets-list":
        from .targets import targets_list

        return targets_list()
    elif command == "--calc":
        from .calc import calc

        return calc()
    elif command == "--module-dump":
        from .module_ops import module_dump

        return module_dump(lazy=False, new=False)
    elif command == "--lazy-module-dump":
        from .module_ops import module_dump

        return module_dump(lazy=True, new=False)
    elif command == "--new-module-dump":
        from .module_ops import module_dump

        return module_dump(lazy=False, new=True)
    elif command == "--lazy-new-module-dump":
        from .module_ops import module_dump

        return module_dump(lazy=True, new=True)
    elif command == "--module-list-functions":
        from .module_ops import module_list_functions

        return module_list_functions()
    elif command == "--module-list-globals":
        from .module_ops import module_list_globals

        return module_list_globals()
    # Phase 2 commands
    elif command == "--test-function-attributes":
        from .attributes import test_function_attributes

        return test_function_attributes()
    elif command == "--test-callsite-attributes":
        from .attributes import test_callsite_attributes

        return test_callsite_attributes()
    elif command == "--add-named-metadata-operand":
        from .metadata import add_named_metadata_operand

        return add_named_metadata_operand()
    elif command == "--set-metadata":
        from .metadata import set_metadata

        return set_metadata()
    elif command == "--replace-md-operand":
        from .metadata import replace_md_operand

        return replace_md_operand()
    elif command == "--is-a-value-as-metadata":
        from .metadata import is_a_value_as_metadata

        return is_a_value_as_metadata()
    else:
        print(f"Unknown command: {command}", file=sys.stderr)
        print_usage()
        return 1
