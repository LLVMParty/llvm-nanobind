#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

int add(int a, int b) {
    return a + b;
}

// Reference: https://nanobind.readthedocs.io/en/latest/basics.html
NB_MODULE(llvm, m) {
    m.def("add", &add);
}
