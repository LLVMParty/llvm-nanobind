#!/opt/homebrew/bin/python3.14
try:
  from build.llvm import global_context, create_context, ContextManager, ModuleManager
except ModuleNotFoundError as e:
  raise ModuleNotFoundError(f"{e}\n  You can compile the native module with the following commands:\n    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n    cmake --build build")

global_ctx = global_context()
with global_ctx.create_module("global_module") as module:
  print(f"{module.target_triple=}")
  print(f"{module.name=}")
  print(f"{module.data_layout=}")

with create_context() as context:
  with context.create_module("local_module") as module:
    print(f"{module.target_triple=}")
    print(f"{module.name=}")
    print(f"{module.data_layout=}")
