#!/opt/homebrew/bin/python3.14
try:
  import build.llvm as llvm
except ModuleNotFoundError as e:
  raise ModuleNotFoundError(f"{e}\n  You can compile the native module with the following commands:\n    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n    cmake --build build")

print(llvm.add(1, 3))
