# LLVM Obfuscation Tools

Standalone command-line tools for obfuscating LLVM bitcode.

## Tools

### ollvm_obf.py - Python port of `omill/tools/ollvm-obf`

A nanobind-based Python driver that implements the public
`omill/tools/ollvm-obf` pipeline:

- `--string-encrypt`
- `--code-clone`
- `--substitute`
- `--if-convert`
- `--loop-to-recursion`
- `--flatten`
- `--opaque-predicates`
- `--bogus-control-flow`
- `--bmi-mutate`
- `--const-unfold`
- `--schedule-instructions`
- `--outline-functions`
- `--arith-encode`
- `--stack-randomize`
- `--vectorize`
- `--reg-pressure`

Example:

```bash
uv run tools/obfuscation/ollvm_obf.py \
  --code-clone --substitute --if-convert --flatten \
  --opaque-predicates --bogus-control-flow --const-unfold \
  --schedule-instructions --verify-each input.ll -o output.ll
```

The CLI also supports `--vectorize-data`, `--vectorize-bitwise`,
`--vectorize-i64`, and `--vectorize-percent=N` for the vectorization stage.

Stored golden-master fixtures live under `tests/golden/ollvm_obf/`. The test
suite compares Python output against those checked-in public-pass `ollvm-obf`
reference files, so normal repo tests do **not** require a local `omill`
checkout or C++ `ollvm-obf` binary.

Canonical golden-master coverage includes selected parity cases for instruction
scheduling, if-conversion, BMI mutation, loop-to-recursion, and code cloning:

```bash
uv run pytest tests/regressions/test_ollvm_obf_golden_master.py -q
```

A second-tier semantic suite is also available for passes whose IR shape is
expected to differ while runtime behavior should remain equivalent (for example
substitution, string encryption, flattening, loop-to-recursion, BMI codegen,
vectorization, stack/register-liveness transforms, and a broad full-public-
pipeline mixed-module case):

```bash
uv run pytest tests/regressions/test_ollvm_obf_semantic_parity.py -q
```

To regenerate the checked-in golden files from an external C++ `ollvm-obf`
build, use:

```bash
uv run tools/obfuscation/generate_ollvm_obf_goldens.py \
  --ollvm-obf /path/to/ollvm-obf
```

### mba_sub - Mixed Boolean Arithmetic Substitution

Replaces arithmetic operations (`add`, `sub`, `mul`, `xor`, `or`) with 
equivalent but more complex expressions using boolean arithmetic identities.

```bash
mba_sub [options] <input.bc> <output.bc>

Options:
  --iterations N    Number of iterations (default: 1)
  --seed N          Random seed for reproducibility (default: random)
```

Example transformations:
- `x + y` → `~(x + (-x + (-x + ~y)))`
- `x - y` → `(x ^ -y) + 2*(x & -y)`
- `x ^ y` → `(~x & y) | (x & ~y)`
- `x * y` → `((x|y) * (x&y)) + ((x & ~y) * (y & ~x))`
- `x | y` → `~(~x & ~y)`

### bb_split - Basic Block Splitter

Splits large basic blocks into smaller ones to increase control flow 
complexity and make analysis harder.

```bash
bb_split [options] <input.bc> <output.bc>

Options:
  --iterations N    Number of iterations (default: 1)
  --min-size N      Minimum block size to consider for splitting (default: 10)
  --max-size N      Maximum block size after splitting (default: 20)
  --chance N        Percent chance to split eligible blocks (default: 40)
  --seed N          Random seed for reproducibility (default: random)
```

### indirect_branch - Simple Indirect Branch

Replaces direct branches with indirect branches through a block address array,
making control flow analysis harder.

```bash
indirect_branch [options] <input.bc> <output.bc>

Options:
  --iterations N    Number of iterations (default: 1)
  --chance N        Percent chance to replace branches (default: 50)
  --seed N          Random seed for reproducibility (default: random)
```

### control_flow_flatten - Control Flow Flattening

Flattens control flow by converting all basic blocks into a switch-based
dispatcher pattern. Each block gets a random state value, and control flow
is routed through a central dispatcher.

```bash
control_flow_flatten [options] <input.bc> <output.bc>

Options:
  --iterations N        Number of iterations (default: 1)
  --use-func-resolver N Percent chance to use function call for state check (default: 0)
  --use-global-state N  Percent chance to use global variables for state (default: 0)
  --use-opaque N        Percent chance to use opaque predicates (default: 0)
  --use-global-opaque N Percent chance to use globals in opaques (default: 0)
  --use-siphash N       Percent chance to use SipHash state transform (default: 0)
  --clone-siphash N     Percent chance to clone SipHash function (default: 0)
  --seed N              Random seed for reproducibility (default: random)
```

The SipHash option adds cryptographic hashing to the state comparison, making
it much harder to recover the original control flow through static analysis.

### indirect_branch_enc - Encrypted Indirect Branch

Advanced version of indirect_branch that uses XTEA encryption for branch
target addresses. Includes runtime XTEA decryption code.

```bash
indirect_branch_enc [options] <input.bc> <output.bc>

Options:
  --iterations N    Number of iterations (default: 1)
  --chance N        Percent chance to replace branches (default: 50)
  --seed N          Random seed for reproducibility (default: random)
```

### string_encrypt - String Encryption

Encrypts string constants using XOR cipher with SplitMix32 PRNG. Supports
two modes:
- **global**: Decryption happens at program startup via `.ctors`
- **stack**: Each string is decrypted on the stack at point of use

```bash
string_encrypt [options] <input.bc> <output.bc>

Options:
  --mode MODE       Encryption mode: 'global' or 'stack' (default: global)
  --skip-prefix P   Skip strings starting with this prefix
  --seed N          Random seed for reproducibility (default: random)
```

## Building

These tools are built automatically with the main project:

```bash
cmake -B build -G Ninja
cmake --build build --target mba_sub bb_split indirect_branch \
    control_flow_flatten indirect_branch_enc string_encrypt
```

## Testing

Run the test script to verify the tools produce expected output:

```bash
./tools/obfuscation/tests/run_tests.sh
```

Compare with the golden master output:

```bash
./tools/obfuscation/tests/run_tests.sh | diff - tools/obfuscation/tests/expected_output.txt
```

## Usage Example

Chain multiple obfuscation passes:

```bash
# Convert source to bitcode
clang -emit-llvm -c input.c -o input.bc

# Apply obfuscation passes (order matters!)
./build/string_encrypt --seed=42 input.bc step1.bc
./build/mba_sub --seed=42 step1.bc step2.bc
./build/bb_split --seed=42 step2.bc step3.bc
./build/control_flow_flatten --seed=42 step3.bc step4.bc
./build/indirect_branch --seed=42 step4.bc output.bc

# Compile to native code
clang output.bc -o output
```

## Reproducibility

All tools accept a `--seed` option for deterministic output. This is useful for:
- Golden master testing
- Debugging
- Reproducible builds

When `--seed` is not specified, a random seed is used.

## Pass Descriptions

| Pass | Complexity | Description |
|------|------------|-------------|
| `mba_sub` | Low | Replaces arithmetic with boolean algebra |
| `bb_split` | Low | Splits large basic blocks |
| `indirect_branch` | Medium | Simple indirect branches |
| `control_flow_flatten` | High | Switch-based dispatcher |
| `indirect_branch_enc` | High | XTEA-encrypted branches |
| `string_encrypt` | Medium | XOR-encrypted strings |
