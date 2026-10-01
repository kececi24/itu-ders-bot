# Invalidate eligibility before any production compilation, including partial
# target builds. Only completion of BOTH executables may restore the manifest.
add_custom_target(provenance_start
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
        "-DSTART_BUILD=ON"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProvenanceInputs.cmake"
    VERBATIM)
foreach(_target itu_platform itu_core main setup)
    add_dependencies(${_target} provenance_start)
endforeach()
foreach(_target main setup)
    # Only a successful link may attest executable bytes. A later no-op build
    # must not bless an executable replaced outside the build system.
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DBINARY_FILE=$<TARGET_FILE:${_target}>"
            "-DRECEIPT_FILE=${CMAKE_BINARY_DIR}/${_target}-$<CONFIG>.sha256"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BinaryReceipt.cmake"
        VERBATIM)
endforeach()

# A machine-readable package input generated only after a complete build.
add_custom_target(build_manifest ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
        "-DOUTPUT_FILE=${CMAKE_BINARY_DIR}/bin/build-manifest.json"
        "-DPROJECT_VERSION=${PROJECT_VERSION}"
        "-DITU_TARGET=${ITU_TARGET}"
        "-DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
        "-DCOMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}"
        "-DDEPENDENCY_MANIFEST_FILE=${CMAKE_BINARY_DIR}/dependency-manifest.json"
        "-DMAIN_FILE=$<TARGET_FILE:main>"
        "-DSETUP_FILE=$<TARGET_FILE:setup>"
        "-DBUILD_CONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateManifest.cmake"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/bin/build-manifest.json"
    COMMENT "Generating build manifest with current build provenance"
    VERBATIM
)
add_dependencies(build_manifest main setup)
