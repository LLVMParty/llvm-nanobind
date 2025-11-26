function(nanobind_add_typed_module tgt)
    nanobind_add_module(${tgt} STABLE_ABI ${ARGN})
    nanobind_add_stub(
        ${tgt}_stub
        MODULE ${tgt}
        OUTPUT ${tgt}.pyi
        PYTHON_PATH $<TARGET_FILE_DIR:${tgt}>
        MARKER_FILE py.typed
        DEPENDS ${tgt}
    )
endfunction()