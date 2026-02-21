# This is an INTERFACE target for LLVM, usage:
#   target_link_libraries(${PROJECT_NAME} <PRIVATE|PUBLIC|INTERFACE> LLVM-Wrapper)
# The include directories and compile definitions will be properly handled.

if(LLVM-Wrapper_FOUND OR TARGET LLVM-Wrapper)
    return()
endif()

set(CMAKE_FOLDER_LLVM "${CMAKE_FOLDER}")
if(CMAKE_FOLDER)
    set(CMAKE_FOLDER "${CMAKE_FOLDER}/LLVM")
else()
    set(CMAKE_FOLDER "LLVM")
endif()

function(derive_llvm_prefix)
    find_package(LLVM QUIET)
    if(NOT LLVM_FOUND)
        if(CMAKE_HOST_SYSTEM_NAME MATCHES "Darwin")
            execute_process(
                COMMAND brew --prefix llvm
                RESULT_VARIABLE BREW_LLVM
                OUTPUT_VARIABLE BREW_LLVM_PREFIX
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(BREW_LLVM EQUAL 0 AND EXISTS "${BREW_LLVM_PREFIX}")
                message(STATUS "Found LLVM keg installed by Homebrew at ${BREW_LLVM_PREFIX}")
                set(LLVM_INSTALL_PREFIX "${BREW_LLVM_PREFIX}")
            else()
                message(FATAL_ERROR "LLVM could not be found, use -DCMAKE_PREFIX_PATH=/path/to/llvm")
            endif()
        else()
            message(FATAL_ERROR "LLVM could not be found, use -DCMAKE_PREFIX_PATH=/path/to/llvm")
        endif()
    endif()
    message(STATUS "Created .llvm-prefix: ${LLVM_INSTALL_PREFIX}")
    file(WRITE "${PROJECT_SOURCE_DIR}/.llvm-prefix" "${LLVM_INSTALL_PREFIX}\n")
    list(PREPEND CMAKE_PREFIX_PATH "${LLVM_INSTALL_PREFIX}" PARENT_SCOPE)
endfunction()

# Compile commands for a macos debug build:
# cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -S llvm -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_ENABLE_RTTI=ON -DLLVM_PARALLEL_LINK_JOBS=1 -DLLVM_LINK_LLVM_DYLIB=ON -DLLVM_BUILD_LLVM_C_DYLIB=ON -DLLVM_INSTALL_UTILS=ON
# cmake --build build
# cmake --install build --prefix ~/Projects/llvm-21
if(NOT LLVM_DIR)
    if(EXISTS "${PROJECT_SOURCE_DIR}/.llvm-prefix")
        file(READ "${PROJECT_SOURCE_DIR}/.llvm-prefix" LLVM_PREFIX_PATH)
        string(STRIP "${LLVM_PREFIX_PATH}" LLVM_PREFIX_PATH)
        if(LLVM_PREFIX_PATH MATCHES "^~")
            string(REPLACE "~" "$ENV{HOME}" LLVM_PREFIX_PATH "${LLVM_PREFIX_PATH}")
        endif()
        if(EXISTS "${LLVM_PREFIX_PATH}")
            list(PREPEND CMAKE_PREFIX_PATH "${LLVM_PREFIX_PATH}")
            message(STATUS "Found LLVM prefix path from .llvm-prefix: ${LLVM_PREFIX_PATH}")
        else()
            message(WARNING "LLVM prefix path from .llvm-prefix does not exist: ${LLVM_PREFIX_PATH}")
            derive_llvm_prefix()
        endif()
    else()
        derive_llvm_prefix()
    endif()
endif()

# Extract the arguments passed to find_package
# Documentation: https://cmake.org/cmake/help/latest/manual/cmake-developer.7.html#find-modules
list(APPEND FIND_ARGS "${LLVM-Wrapper_FIND_VERSION}")
if(LLVM-Wrapper_FIND_QUIETLY)
    list(APPEND FIND_ARGS "QUIET")
endif()
if(LLVM-Wrapper_FIND_REQUIRED)
    list(APPEND FIND_ARGS "REQUIRED")
endif()

# Workaround: prebuilt LLVM 21 on Windows references LibXml2::LibXml2
# in LLVMWindowsManifest, but LibXml2 may not be installed. Create a
# dummy imported target so cmake doesn't error during configuration.
if(WIN32 AND NOT TARGET LibXml2::LibXml2)
    add_library(LibXml2::LibXml2 INTERFACE IMPORTED)
endif()

# Find LLVM
find_package(LLVM ${FIND_ARGS})
unset(FIND_ARGS)

if(NOT LLVM-Wrapper_FIND_QUIETLY)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
endif()

# Split the definitions properly (https://weliveindetail.github.io/blog/post/2017/07/17/notes-setup.html)
separate_arguments(LLVM_DEFINITIONS)

# https://github.com/JonathanSalwan/Triton/issues/1082#issuecomment-1030826696
if(LLVM_LINK_LLVM_DYLIB)
    set(LLVM-Wrapper_LIBS LLVM)
elseif(LLVM-Wrapper_FIND_COMPONENTS)
    if(NOT LLVM-Wrapper_FIND_QUIETLY)
        message(STATUS "LLVM components: ${LLVM-Wrapper_FIND_COMPONENTS}")
    endif()
    llvm_map_components_to_libnames(LLVM-Wrapper_LIBS ${LLVM-Wrapper_FIND_COMPONENTS})
else()
    set(LLVM-Wrapper_LIBS ${LLVM_AVAILABLE_LIBS})
endif()

# Some diagnostics (https://stackoverflow.com/a/17666004/1806760)
if(NOT LLVM-Wrapper_FIND_QUIETLY)
    message(STATUS "LLVM libraries: ${LLVM-Wrapper_LIBS}")
    message(STATUS "LLVM includes: ${LLVM_INCLUDE_DIRS}")
    message(STATUS "LLVM definitions: ${LLVM_DEFINITIONS}")
    message(STATUS "LLVM tools: ${LLVM_TOOLS_BINARY_DIR}")
endif()

add_library(LLVM-Wrapper INTERFACE IMPORTED)
target_include_directories(LLVM-Wrapper SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
target_link_libraries(LLVM-Wrapper INTERFACE ${LLVM-Wrapper_LIBS})
target_compile_definitions(LLVM-Wrapper INTERFACE ${LLVM_DEFINITIONS})

# Set the appropriate minimum C++ standard
if(LLVM_VERSION VERSION_GREATER_EQUAL "16.0.0")
    # https://releases.llvm.org/16.0.0/docs/CodingStandards.html#c-standard-versions
    target_compile_features(LLVM-Wrapper INTERFACE cxx_std_17)
elseif(LLVM_VERSION VERSION_GREATER_EQUAL "10.0.0")
    # https://releases.llvm.org/10.0.0/docs/CodingStandards.html#c-standard-versions
    target_compile_features(LLVM-Wrapper INTERFACE cxx_std_14)
else()
    # https://releases.llvm.org/9.0.0/docs/CodingStandards.html#c-standard-versions
    target_compile_features(LLVM-Wrapper INTERFACE cxx_std_11)
endif()

if(WIN32)
    target_compile_definitions(LLVM-Wrapper INTERFACE NOMINMAX)

    # This target has an unnecessary diaguids.lib embedded in the installation
    if(TARGET LLVMDebugInfoPDB)
        get_target_property(LLVMDebugInfoPDB_LIBS LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
        foreach(LLVMDebugInfoPDB_LIB ${LLVMDebugInfoPDB_LIBS})
            if(LLVMDebugInfoPDB_LIB MATCHES "diaguids.lib")
                list(REMOVE_ITEM LLVMDebugInfoPDB_LIBS "${LLVMDebugInfoPDB_LIB}")
                break()
            endif()
        endforeach()
        set_target_properties(LLVMDebugInfoPDB PROPERTIES
            INTERFACE_LINK_LIBRARIES "${LLVMDebugInfoPDB_LIBS}"
        )
        unset(LLVMDebugInfoPDB_LIBS)
    endif()
endif()

add_library(LLVM-C-Wrapper INTERFACE)
target_include_directories(LLVM-C-Wrapper SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
if(TARGET LLVM-C)
    target_link_libraries(LLVM-C-Wrapper INTERFACE LLVM-C)
elseif(LLVM_LINK_LLVM_DYLIB)
    # apt.llvm.org packages don't build libLLVM-C, but all C API symbols
    # are available in the monolithic libLLVM.so shared library.
    message(STATUS "LLVM-C target not found, falling back to LLVM (LLVM_LINK_LLVM_DYLIB=ON)")
    target_link_libraries(LLVM-C-Wrapper INTERFACE LLVM)
else()
    message(FATAL_ERROR "LLVM-C target not found. Make sure LLVM is built with the C bindings.")
endif()

set(CMAKE_FOLDER "${CMAKE_FOLDER_LLVM}")
unset(CMAKE_FOLDER_LLVM)

set(LLVM-Wrapper_FOUND ON)
