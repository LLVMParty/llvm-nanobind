#!/opt/homebrew/bin/python3.14
try:
  from build.llvm import global_context, create_context, LLVMContextManager, LLVMModuleManager
except ModuleNotFoundError as e:
  raise ModuleNotFoundError(f"{e}\n  You can compile the native module with the following commands:\n    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n    cmake --build build")

global_ctx = global_context()
with global_ctx.create_module("test_module") as module:
  print(module.data_layout)

with create_context() as context:
  with context.create_module("test_module") as module:
    print(module.data_layout)
