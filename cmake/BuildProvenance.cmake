# Invalidate eligibility before any production compilation, including partial
# target builds. Only completion of BOTH executables may restore the manifest.
set(_conf_files "CMakeLists.txt")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/CMakePresets.json")
    list(APPEND _conf_files "CMakePresets.json")
endif()
file(GLOB _cmake_modules RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake")
list(APPEND _conf_files ${_cmake_modules})
list(REMOVE_DUPLICATES _conf_files)
list(SORT _conf_files)

set(_conf_inputs "{}")
foreach(_conf_file IN LISTS _conf_files)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${_conf_file}" _h)
    string(JSON _conf_inputs SET "${_conf_inputs}" "${_conf_file}" "\"${_h}\"")
endforeach()

file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" _configure_cmakelists_hash)
string(UUID _configure_token NAMESPACE 00000000-0000-0000-0000-000000000000 TYPE SHA1)
file(WRITE "${CMAKE_BINARY_DIR}/provenance-configure.json"
    "{\"token\":\"${_configure_token}\",\"cmakelists\":\"${_configure_cmakelists_hash}\",\"configure_files\":${_conf_inputs},\"cache\":\"\"}")
set(_provenance_header "${CMAKE_BINARY_DIR}/provenance-inputs.h")
add_custom_target(provenance_start
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
        "-DSTART_BUILD=ON"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProvenanceInputs.cmake"
    BYPRODUCTS "${_provenance_header}"
    VERBATIM)
foreach(_target itu_platform itu_core main setup)
    add_dependencies(${_target} provenance_start)
    # Updating this generated header forces all production translation units to
    # rebuild even when an input's contents changed with its timestamp retained.
    # write-if-different preserves genuine no-op builds.
    if(MSVC)
        target_compile_options(${_target} PRIVATE "/FI${_provenance_header}")
    else()
        target_compile_options(${_target} PRIVATE -include "${_provenance_header}")
    endif()
endforeach()
foreach(_target main setup)
    # Only a successful link may attest executable bytes. A later no-op build
    # must not bless an executable replaced outside the build system.
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DBINARY_FILE=$<TARGET_FILE:${_target}>"
            "-DRECEIPT_FILE=${CMAKE_BINARY_DIR}/${_target}-$<CONFIG>.sha256"
            "-DINPUT_FILE=${CMAKE_BINARY_DIR}/provenance-start.json"
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
