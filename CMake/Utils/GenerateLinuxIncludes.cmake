# Generates the compatibility files required to use a Windows/MSVC installation from clang-cl running on Linux:
# Usage:
#   include(cmake/GenerateLinuxIncludes.cmake)
#   clang_msvc_generate(OUTPUT_DIR "${CMAKE_BINARY_DIR}/clang")

# We ignore a collection of dirs that have no use to Freelancer (we only build x86 Windows MSVC)
set(_CLANG_MSVC_IGNORED_DIRS
        "arm64"
        "x64"
        "testing"
        "\\.dll"
        "\\.idl"
        "\\.xsd"
        "uwp"
        "\\.xml"
        "licenses"
        "designtime"
        "windows.media"
        "app certification kit"
        "catalogs"
        "winrt"
        "onecore"
        "intrin\\.h"
)

set(LIBERTY_VFS_MSVC "/winsys/msvc")
set(LIBERTY_VFS_SDK "/winsys/sdk")

function(_clang_msvc_path_is_ignored PATH RESULT)
    string(TOLOWER "${PATH}" _lower)

    foreach (_ignored IN LISTS _CLANG_MSVC_IGNORED_DIRS)
        if (_lower MATCHES "${_ignored}")
            set(${RESULT} TRUE PARENT_SCOPE)
            return()
        endif ()
    endforeach ()

    set(${RESULT} FALSE PARENT_SCOPE)
endfunction()

function(_clang_msvc_find_version_dir PARENT RESULT_VERSION RESULT_PATH)
    if (NOT IS_DIRECTORY "${PARENT}")
        message(FATAL_ERROR "Version parent not found or not a directory: ${PARENT}")
    endif ()

    file(GLOB _dirs LIST_DIRECTORIES true "${PARENT}/*")

    set(_version_dirs "")
    foreach (_dir IN LISTS _dirs)
        if (IS_DIRECTORY "${_dir}")
            list(APPEND _version_dirs "${_dir}")
        endif ()
    endforeach ()

    if (NOT _version_dirs)
        message(FATAL_ERROR "No version directories found under: ${PARENT}")
    endif ()

    list(SORT _version_dirs)
    list(GET _version_dirs -1 _chosen)

    get_filename_component(_version "${_chosen}" NAME)

    set(${RESULT_VERSION} "${_version}" PARENT_SCOPE)
    set(${RESULT_PATH} "${_chosen}" PARENT_SCOPE)
endfunction()


function(_clang_msvc_detect_versions MSVC_WINE_ROOT)
    set(_msvc_root "${MSVC_WINE_ROOT}/vc/tools/msvc")
    set(_kits_root "${MSVC_WINE_ROOT}/kits/10")
    set(_sdk_include "${_kits_root}/include")

    _clang_msvc_find_version_dir(
            "${_msvc_root}"
            _msvc_version
            _msvc_version_dir
    )

    _clang_msvc_find_version_dir(
            "${_sdk_include}"
            _sdk_version
            _sdk_version_dir
    )

    set(LIBERTY_CLANG_MSVC_ROOT "${_msvc_root}" PARENT_SCOPE)
    set(LIBERTY_CLANG_MSVC_VERSION "${_msvc_version}" PARENT_SCOPE)

    set(LIBERTY_CLANG_MSVC_INCLUDE_DIRS
            "${_msvc_version_dir}/include"
            "${_msvc_version_dir}/atlmfc/include"
            PARENT_SCOPE
    )

    set(LIBERTY_CLANG_MSVC_SDK_ROOT "${_kits_root}" PARENT_SCOPE)
    set(LIBERTY_CLANG_MSVC_SDK_VERSION "${_sdk_version}" PARENT_SCOPE)

    set(LIBERTY_CLANG_MSVC_SDK_INCLUDE_DIRS
            "${_sdk_version_dir}/ucrt"
            "${_sdk_version_dir}/shared"
            "${_sdk_version_dir}/um"
            "${_sdk_version_dir}/winrt"
            "${_sdk_version_dir}/cppwinrt"
            PARENT_SCOPE
    )
endfunction()


function(_clang_msvc_collect_files BASE_DIR RESULT)
    file(GLOB_RECURSE _files
            LIST_DIRECTORIES false
            "${BASE_DIR}/*"
    )

    set(_result "")

    foreach (_file IN LISTS _files)
        _clang_msvc_path_is_ignored("${_file}" _ignored)

        if (_ignored)
            continue()
        endif ()

        # Python skips symlinks.
        if (IS_SYMLINK "${_file}")
            continue()
        endif ()

        file(RELATIVE_PATH _relative "${BASE_DIR}" "${_file}")
        file(TO_CMAKE_PATH "${_relative}" _relative)

        list(APPEND _result "${_relative}")
    endforeach ()

    set(${RESULT} "${_result}" PARENT_SCOPE)
endfunction()


function(_clang_msvc_yaml_escape INPUT RESULT)
    # Paths used here should generally not need escaping, but quote them
    # safely for YAML.
    string(REPLACE "\\" "\\\\" _value "${INPUT}")
    string(REPLACE "\"" "\\\"" _value "${_value}")
    set(${RESULT} "${_value}" PARENT_SCOPE)
endfunction()

function(_clang_msvc_vfs_indent INPUT INDENT RESULT)
    string(REPLACE "\n" "\n${INDENT}" _result "${INPUT}")
    set(${RESULT} "${_result}" PARENT_SCOPE)
endfunction()

function(_clang_msvc_vfs_directory REAL_DIR VFS_NAME LEVEL RESULT)
    _clang_msvc_collect_files("${REAL_DIR}" _files)

    # Find immediate children.
    file(GLOB _children LIST_DIRECTORIES true "${REAL_DIR}/*")
    list(SORT _children)

    set(_yaml "")

    # 2 Spaces per indent level
    math(EXPR _space_count "${LEVEL} * 2" OUTPUT_FORMAT DECIMAL)
    string(REPEAT " " ${_space_count} _indent_spacing)

    foreach (_child IN LISTS _children)
        get_filename_component(_name "${_child}" NAME)

        _clang_msvc_path_is_ignored("${_child}" _ignored)
        if (_ignored)
            continue()
        endif ()

        if (IS_DIRECTORY "${_child}")
            if (IS_SYMLINK "${_child}")
                continue()
            endif ()

            math(EXPR _next_level "${LEVEL} + 2" OUTPUT_FORMAT DECIMAL)
            _clang_msvc_vfs_directory(
                    "${_child}"
                    "${_name}"
                    ${_next_level}
                    _child_yaml
            )

            string(APPEND _yaml "${_indent_spacing}- type: directory\n")
            string(APPEND _yaml "${_indent_spacing}  name: \"${_name}\"\n")
            string(APPEND _yaml "${_indent_spacing}  contents:\n")

            # If an empty directory, put an empty array
            if (_child_yaml STREQUAL "")
                string(APPEND _yaml "${_indent_spacing}    []\n")
            else ()
                # Child YAML already uses 4-space indentation for contents.
                string(APPEND _yaml "${_child_yaml}")
            endif ()
        else ()
            if (IS_SYMLINK "${_child}")
                continue()
            endif ()

            _clang_msvc_yaml_escape("${_child}" _escaped)

            string(APPEND _yaml "${_indent_spacing}- type: file\n")
            string(APPEND _yaml "${_indent_spacing}  name: \"${_name}\"\n")
            string(APPEND _yaml "${_indent_spacing}  external-contents: \"${_escaped}\"\n")

            if (_name MATCHES "\\.lib$")
                string(REGEX REPLACE "\\.lib$" "" _without_lib "${_name}")

                string(APPEND _yaml "${_indent_spacing}- type: file\n")
                string(APPEND _yaml "${_indent_spacing}  name: \"${_without_lib}\"\n")
                string(APPEND _yaml "${_indent_spacing}  external-contents: \"${_escaped}\"\n")
            endif ()
        endif ()
    endforeach ()

    set(${RESULT} "${_yaml}" PARENT_SCOPE)
endfunction()


function(_clang_msvc_generate_vfs MSVC_ROOT SDK_ROOT OUTPUT)
    set(_yaml "version: 0\n")
    string(APPEND _yaml "case-sensitive: false\n")
    string(APPEND _yaml "roots:\n")
    string(APPEND _yaml "  - type: directory\n")
    string(APPEND _yaml "    name: \"${LIBERTY_VFS_MSVC}\"\n")
    string(APPEND _yaml "    contents:\n")

    _clang_msvc_vfs_directory(
            "${MSVC_ROOT}"
            "${LIBERTY_VFS_MSVC}"
            2
            _msvc_contents
    )

    string(APPEND _yaml "${_msvc_contents}")

    string(APPEND _yaml "  - type: directory\n")
    string(APPEND _yaml "    name: \"${LIBERTY_VFS_SDK}\"\n")
    string(APPEND _yaml "    contents:\n")

    _clang_msvc_vfs_directory(
            "${SDK_ROOT}"
            "${LIBERTY_VFS_SDK}"
            2
            _sdk_contents
    )

    string(APPEND _yaml "${_sdk_contents}")

    file(WRITE "${OUTPUT}" "${_yaml}")
endfunction()


function(_clang_msvc_get_include_dirs RESULT)
    set(_dirs
            "${LIBERTY_CLANG_MSVC_ROOT}/${LIBERTY_CLANG_MSVC_VERSION}/include"
            "${LIBERTY_CLANG_MSVC_ROOT}/${LIBERTY_CLANG_MSVC_VERSION}/atlmfc/include"

            "${LIBERTY_CLANG_MSVC_SDK_ROOT}/include/${LIBERTY_CLANG_MSVC_SDK_VERSION}/ucrt"
            "${LIBERTY_CLANG_MSVC_SDK_ROOT}/include/${LIBERTY_CLANG_MSVC_SDK_VERSION}/shared"
            "${LIBERTY_CLANG_MSVC_SDK_ROOT}/include/${LIBERTY_CLANG_MSVC_SDK_VERSION}/um"
            "${LIBERTY_CLANG_MSVC_SDK_ROOT}/include/${LIBERTY_CLANG_MSVC_SDK_VERSION}/winrt"
            "${LIBERTY_CLANG_MSVC_SDK_ROOT}/include/${LIBERTY_CLANG_MSVC_SDK_VERSION}/cppwinrt"
    )

    set(${RESULT} "${_dirs}" PARENT_SCOPE)
endfunction()


function(_clang_msvc_collect_include_files RESULT)
    _clang_msvc_get_include_dirs(_include_dirs)

    # Maps lower-case relative path -> actual path.
    set(_files "")

    foreach (_base IN LISTS _include_dirs)
        if (NOT IS_DIRECTORY "${_base}")
            continue()
        endif ()

        file(GLOB_RECURSE _base_files
                LIST_DIRECTORIES false
                "${_base}/*"
        )

        foreach (_file IN LISTS _base_files)
            if (IS_SYMLINK "${_file}")
                continue()
            endif ()

            _clang_msvc_path_is_ignored("${_file}" _ignored)
            if (_ignored)
                continue()
            endif ()

            file(RELATIVE_PATH _relative "${_base}" "${_file}")
            file(TO_CMAKE_PATH "${_relative}" _relative)

            string(TOLOWER "${_relative}" _lower)

            # First match wins, just like the Python dict.
            list(FIND _files "${_lower}" _existing)

            if (_existing EQUAL -1)
                list(APPEND _files "${_lower}")
                set("_CLANG_MSVC_FILE_${_lower}" "${_file}")
                set("_CLANG_MSVC_NAME_${_lower}" "${_relative}")
            endif ()
        endforeach ()
    endforeach ()

    set(_clang_msvc_include_keys "${_files}" PARENT_SCOPE)

    foreach (_key IN LISTS _files)
        set("_CLANG_MSVC_FILE_${_key}"
                "${_CLANG_MSVC_FILE_${_key}}"
                PARENT_SCOPE
        )

        set("_CLANG_MSVC_NAME_${_key}"
                "${_CLANG_MSVC_NAME_${_key}}"
                PARENT_SCOPE
        )
    endforeach ()

    set(${RESULT} "${_files}" PARENT_SCOPE)
endfunction()


function(_clang_msvc_create_link LINK TARGET)
    if (EXISTS "${LINK}" OR IS_SYMLINK "${LINK}")
        return()
    endif ()

    get_filename_component(_parent "${LINK}" DIRECTORY)
    file(MAKE_DIRECTORY "${_parent}")

    file(CREATE_LINK
            "${TARGET}"
            "${LINK}"
            SYMBOLIC
    )
endfunction()


function(_clang_msvc_generate_rc OUTPUT_DIR)
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")

    _clang_msvc_collect_include_files(_include_keys)

    # Keep the mapping available while scanning headers.
    foreach (_key IN LISTS _include_keys)
        set("_CLANG_MSVC_FILE_${_key}"
                "${_CLANG_MSVC_FILE_${_key}}"
        )
        set("_CLANG_MSVC_NAME_${_key}"
                "${_CLANG_MSVC_NAME_${_key}}"
        )
    endforeach ()

    set(_case_variants "")

    # Scan all headers for includes whose case differs from the canonical file name.
    foreach (_key IN LISTS _include_keys)
        set(_file "${_CLANG_MSVC_FILE_${_key}}")

        if (NOT EXISTS "${_file}")
            continue()
        endif ()

        file(READ "${_file}" _contents)

        # CMake doesn't support standard Regex, this should be equivalent to: 
        # ^\s*#\s*include\s*[<"](\S+)[>"]
        string(REGEX MATCHALL
                "[ \t]*#[ \t]*include[ \t]*[<\"]([^>\" \t\r\n]+)[>\"]"
                _matches
                "${_contents}"
        )

        foreach (_match IN LISTS _matches)
            string(REGEX REPLACE
                    "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\" \t\r\n]+)[>\"].*$"
                    "\\1"
                    _include
                    "${_match}"
            )

            string(REPLACE "\\" "/" _include "${_include}")
            string(TOLOWER "${_include}" _include_lower)

            list(FIND _include_keys "${_include_lower}" _found)

            if (NOT _found EQUAL -1)
                list(APPEND _case_variants "${_include}")
            endif ()
        endforeach ()
    endforeach ()

    list(REMOVE_DUPLICATES _case_variants)

    # Create canonical and lowercase links.
    foreach (_key IN LISTS _include_keys)
        set(_source "${_CLANG_MSVC_FILE_${_key}}")
        set(_name "${_CLANG_MSVC_NAME_${_key}}")

        set(_link "${OUTPUT_DIR}/${_name}")
        _clang_msvc_create_link("${_link}" "${_source}")

        string(TOLOWER "${_name}" _lower_name)

        if (NOT _lower_name STREQUAL "${_name}")
            _clang_msvc_create_link(
                    "${OUTPUT_DIR}/${_lower_name}"
                    "${_source}"
            )
        endif ()
    endforeach ()

    # Create links for include paths using different casing.
    foreach (_variant IN LISTS _case_variants)
        string(TOLOWER "${_variant}" _variant_lower)

        set(_found_source "")
        foreach (_key IN LISTS _include_keys)
            if (_key STREQUAL "${_variant_lower}")
                set(_found_source "${_CLANG_MSVC_FILE_${_key}}")
                set(_canonical "${_CLANG_MSVC_NAME_${_key}}")
                break()
            endif ()
        endforeach ()

        if (NOT _found_source)
            continue()
        endif ()

        if (_variant STREQUAL "${_canonical}")
            continue()
        endif ()

        _clang_msvc_create_link(
                "${OUTPUT_DIR}/${_variant}"
                "${_found_source}"
        )
    endforeach ()
endfunction()

function(set_msvc_root)
    cmake_parse_arguments(
            ARG
            ""
            "MSVC_ROOT"
            ""
            ${ARGN}
    )

    if (DEFINED LIBERTY_MSVC_ROOT)
        return()
    endif ()

    # Some versions of CMake will leave it undefined, while others default to an empty string, both are invalid tho.
    if (DEFINED ARG_MSVC_ROOT AND NOT ARG_MSVC_ROOT STREQUAL "")
        set(_msvc_root "${ARG_MSVC_ROOT}")
    elseif (EXISTS "/opt/msvc")
        set(_msvc_root "/opt/msvc")
    elseif (DEFINED ENV{MSVC})
        set(_msvc_root "$ENV{MSVC}")
    else ()
        message(FATAL_ERROR "Could not find MSVC installation, one of the following is required:\n\t
        - Install msvc-wine to /opt/msvc\n\t
        - Set the MSVC environment variable to point to an MSVC installation\n\t
        - Call set_msvc_root(MSVC_ROOT \"/path/to/msvc\")")
    endif ()

    # TODO: Should probably do some validation against what is actually installed at the specified dir
    if (NOT IS_DIRECTORY "${_msvc_root}")
        message(FATAL_ERROR "MSVC located at '${_msvc_root}' was not a directory")
    endif ()

    set(LIBERTY_MSVC_ROOT ${_msvc_root} PARENT_SCOPE)
endfunction()

function(clang_msvc_generate)
    cmake_parse_arguments(
            ARG
            ""
            "VFS_FILE;RC_DIR"
            ""
            ${ARGN}
    )

    if (NOT ARG_VFS_FILE OR NOT ARG_RC_DIR)
        message(FATAL_ERROR "clang_msvc_generate() requires VFS_FILE and RC_DIR to be specified")
    endif ()

    # If set_msvc_root hasn't been called already, this will force populate the variables or error
    set_msvc_root()

    # Detect which version of MSVC is installed and populate global variables for later reference
    _clang_msvc_detect_versions("${LIBERTY_MSVC_ROOT}")

    message(STATUS
            "Clang/MSVC compatibility:"
            " MSVC ${LIBERTY_CLANG_MSVC_VERSION},"
            " Windows SDK ${LIBERTY_CLANG_MSVC_SDK_VERSION}"
    )

    cmake_path(GET ARG_VFS_FILE PARENT_PATH _build_dir)
    # Should do nothing if already exists
    file(MAKE_DIRECTORY "${_build_dir}")

    if (NOT EXISTS ${ARG_VFS_FILE})
        _clang_msvc_generate_vfs(
                "${LIBERTY_CLANG_MSVC_ROOT}"
                "${LIBERTY_CLANG_MSVC_SDK_ROOT}"
                "${ARG_VFS_FILE}"
        )
        message(STATUS "Generated Clang VFS: ${ARG_VFS_FILE}")
    endif ()

    if (NOT EXISTS ${ARG_RC_DIR})
        _clang_msvc_generate_rc("${ARG_RC_DIR}")
        message(STATUS "Generated RC includes: ${ARG_RC_DIR}")
    endif ()

    message(STATUS "Clang msvc includes & VFS setup for usage")
    return(PROPAGATE
            LIBERTY_MSVC_ROOT
            LIBERTY_CLANG_MSVC_ROOT
            LIBERTY_CLANG_MSVC_SDK_ROOT
            LIBERTY_CLANG_MSVC_VERSION
            LIBERTY_CLANG_MSVC_SDK_VERSION
            LIBERTY_CLANG_MSVC_INCLUDE_DIRS
            LIBERTY_CLANG_MSVC_SDK_INCLUDE_DIRS

    )
endfunction()