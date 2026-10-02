# Generate only after both production executables succeeded.
include("${SOURCE_DIR}/cmake/ProvenanceInputs.cmake")
file(READ "${BINARY_DIR}/provenance-start.json" _started_inputs)
if(NOT _started_inputs STREQUAL PROVENANCE_INPUTS)
    message(FATAL_ERROR "Build inputs changed during compilation; rebuild before packaging")
endif()
file(SHA256 "${MAIN_FILE}" MAIN_SHA256)
file(SHA256 "${SETUP_FILE}" SETUP_SHA256)
string(SHA256 _input_fingerprint "${PROVENANCE_INPUTS}")
foreach(_target main setup)
    file(READ "${BINARY_DIR}/${_target}-${BUILD_CONFIG}.sha256" _receipt)
    string(JSON _linked_hash GET "${_receipt}" binary)
    string(JSON _linked_inputs GET "${_receipt}" inputs)
    string(TOUPPER "${_target}" _upper_target)
    if(NOT _linked_hash STREQUAL "${${_upper_target}_SHA256}")
        message(FATAL_ERROR "${_target} differs from its successful link; rebuild the executable")
    endif()
    if(NOT _linked_inputs STREQUAL _input_fingerprint)
        message(FATAL_ERROR "${_target} was linked against different inputs; rebuild the executable")
    endif()
endforeach()
execute_process(
    COMMAND git log -1 --format=%H
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE REVISION_RESULT
    OUTPUT_VARIABLE ITU_GIT_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT REVISION_RESULT EQUAL 0 OR NOT ITU_GIT_REVISION MATCHES "^[0-9a-f]+$")
    set(ITU_GIT_REVISION "unknown")
endif()

set(ITU_IS_DIRTY FALSE)
execute_process(
    COMMAND git status --porcelain --untracked-files=normal
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE STATUS_RESULT
    OUTPUT_VARIABLE ITU_GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT STATUS_RESULT EQUAL 0 OR ITU_GIT_STATUS)
    set(ITU_IS_DIRTY TRUE)
endif()

if(ITU_IS_DIRTY)
    set(DIRTY_JSON "true")
else()
    set(DIRTY_JSON "false")
endif()

if(EXISTS "${DEPENDENCY_MANIFEST_FILE}")
    file(READ "${DEPENDENCY_MANIFEST_FILE}" ITU_DEPS_JSON)
    string(STRIP "${ITU_DEPS_JSON}" ITU_DEPS_JSON)
else()
    set(ITU_DEPS_JSON "{}")
endif()

set(NEW_MANIFEST_CONTENT "{\n  \"schema_version\": 2,\n  \"version\": \"${PROJECT_VERSION}\",\n  \"target\": \"${ITU_TARGET}\",\n  \"compiler\": {\"id\": \"${COMPILER_ID}\", \"version\": \"${COMPILER_VERSION}\"},\n  \"revision\": \"${ITU_GIT_REVISION}\",\n  \"dirty\": ${DIRTY_JSON},\n  \"inputs\": ${PROVENANCE_INPUTS},\n  \"binaries\": {\"main\": \"${MAIN_SHA256}\", \"setup\": \"${SETUP_SHA256}\"},\n  \"dependencies\": ${ITU_DEPS_JSON}\n}\n")

if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" EXISTING_MANIFEST_CONTENT)
    if(EXISTING_MANIFEST_CONTENT STREQUAL NEW_MANIFEST_CONTENT)
        return()
    endif()
endif()

get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT_FILE}" "${NEW_MANIFEST_CONTENT}")
