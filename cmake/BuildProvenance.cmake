# Bind generated build rules to their configure-time inputs. Generate both
# snapshots only after configuration succeeds; CMake writes its final cache
# before evaluating file(GENERATE), including on the very first configure.
set(_conf_files "CMakeLists.txt")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/CMakePresets.json")
    list(APPEND _conf_files "CMakePresets.json")
endif()
file(GLOB_RECURSE _cmake_inputs LIST_DIRECTORIES false RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*")
foreach(_file IN LISTS _cmake_inputs)
    if(NOT _file MATCHES "(^|/)__pycache__/" AND NOT _file MATCHES "\\.pyc$"
            AND NOT _file MATCHES "(^|/)\\.DS_Store$")
        list(APPEND _conf_files "${_file}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _conf_files)
list(SORT _conf_files)

set(_conf_inputs "{\"source\":{},\"external\":{}}")
foreach(_conf_file IN LISTS _conf_files)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${_conf_file}" _h)
    string(JSON _conf_inputs SET "${_conf_inputs}" source "${_conf_file}" "\"${_h}\"")
endforeach()
foreach(_conf_file IN LISTS ITU_CONFIGURE_INPUT_FILES)
    # Keep the logical path so retargeting a dependency symlink is detectable.
    cmake_path(ABSOLUTE_PATH _conf_file BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
    file(SHA256 "${_conf_file}" _h)
    string(JSON _conf_inputs SET "${_conf_inputs}" external "${_conf_file}" "\"${_h}\"")
endforeach()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/provenance-configure.json" CONTENT "${_conf_inputs}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/provenance-configured-cache.txt"
    INPUT "${CMAKE_BINARY_DIR}/CMakeCache.txt")

# Invalidate eligibility before any production compilation, including partial
# target builds. Only completion of BOTH executables may restore the manifest.
set(_provenance_header "${CMAKE_BINARY_DIR}/provenance-inputs.h")
add_custom_target(provenance_start
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
        "-DSTART_BUILD=ON"
        # CMP0112 NEW: these components do not introduce a dependency on the
        # executables, which themselves depend on provenance_start.
        "-DMAIN_FILE=$<TARGET_FILE_DIR:main>/$<TARGET_FILE_NAME:main>"
        "-DSETUP_FILE=$<TARGET_FILE_DIR:setup>/$<TARGET_FILE_NAME:setup>"
        "-DBUILD_CONFIG=$<CONFIG>"
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
    # A legitimate relink can change bytes without changing input contents
    # (for example a touched source or removed object). Remove the old output
    # immediately before linking rather than allowing POST_BUILD to bless any
    # changed output under a preserve admission.
    add_custom_command(TARGET ${_target} PRE_LINK
        COMMAND "${CMAKE_COMMAND}"
            "-DBINARY_FILE=$<TARGET_FILE:${_target}>"
            "-DRECEIPT_FILE=${CMAKE_BINARY_DIR}/${_target}-$<CONFIG>.sha256"
            "-DINPUT_FILE=${CMAKE_BINARY_DIR}/provenance-start.json"
            "-DADMISSION_FILE=${CMAKE_BINARY_DIR}/${_target}-$<CONFIG>.admission.json"
            "-DPREPARE_LINK=ON"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BinaryReceipt.cmake"
        VERBATIM)
    # Generators may execute POST_BUILD without linking. The start admission
    # either preserves an existing receipt or first removes an untrusted output
    # so that a new executable must be produced before it can be attested.
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DBINARY_FILE=$<TARGET_FILE:${_target}>"
            "-DRECEIPT_FILE=${CMAKE_BINARY_DIR}/${_target}-$<CONFIG>.sha256"
            "-DINPUT_FILE=${CMAKE_BINARY_DIR}/provenance-start.json"
            "-DADMISSION_FILE=${CMAKE_BINARY_DIR}/${_target}-$<CONFIG>.admission.json"
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
