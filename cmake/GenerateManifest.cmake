# Generate build-manifest.json during build with fresh provenance
execute_process(
    COMMAND git log -1 --format=%H
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE ITU_GIT_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT ITU_GIT_REVISION MATCHES "^[0-9a-f]+$")
    set(ITU_GIT_REVISION "unknown")
endif()

set(ITU_IS_DIRTY FALSE)
execute_process(
    COMMAND git status --porcelain --untracked-files=no
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE ITU_GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(ITU_GIT_STATUS)
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

set(NEW_MANIFEST_CONTENT "{\n  \"schema_version\": 1,\n  \"version\": \"${PROJECT_VERSION}\",\n  \"target\": \"${ITU_TARGET}\",\n  \"compiler\": {\"id\": \"${COMPILER_ID}\", \"version\": \"${COMPILER_VERSION}\"},\n  \"revision\": \"${ITU_GIT_REVISION}\",\n  \"dirty\": ${DIRTY_JSON},\n  \"dependencies\": ${ITU_DEPS_JSON}\n}\n")

if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" EXISTING_MANIFEST_CONTENT)
    if(EXISTING_MANIFEST_CONTENT STREQUAL NEW_MANIFEST_CONTENT)
        return()
    endif()
endif()

get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT_FILE}" "${NEW_MANIFEST_CONTENT}")
