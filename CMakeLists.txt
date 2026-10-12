cmake_minimum_required(VERSION 3.16)

project(systemc_tlm_at_learning LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(tlm_at_four_phase
    src/main.cpp
)

if(WIN32 AND MINGW)
    get_filename_component(_mingw_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_mingw_prefix "${_mingw_bin_dir}" DIRECTORY)
    set(_systemc_include_dir "${_mingw_prefix}/include")
    file(GLOB _systemc_runtime_dlls "${_mingw_prefix}/bin/libsystemc-*.dll")
    list(LENGTH _systemc_runtime_dlls _systemc_runtime_dll_count)
endif()

if(SYSTEMC_INCLUDE_DIR AND SYSTEMC_LIBRARY)
    target_include_directories(tlm_at_four_phase PRIVATE ${SYSTEMC_INCLUDE_DIR})
    target_link_libraries(tlm_at_four_phase PRIVATE ${SYSTEMC_LIBRARY})
    if(WIN32 AND SYSTEMC_LIBRARY MATCHES "\\.dll\\.a$")
        set_property(TARGET tlm_at_four_phase PROPERTY CXX_STANDARD 20)
        target_compile_definitions(tlm_at_four_phase PRIVATE TLM_AT_SYSTEMC_DLL)
    endif()
elseif(WIN32 AND MINGW AND EXISTS "${_systemc_include_dir}/systemc"
    AND _systemc_runtime_dll_count EQUAL 1)
    find_program(SYSTEMC_GENDEF_EXECUTABLE NAMES gendef)
    find_program(SYSTEMC_DLLTOOL_EXECUTABLE NAMES dlltool)

    if(NOT SYSTEMC_GENDEF_EXECUTABLE OR NOT SYSTEMC_DLLTOOL_EXECUTABLE)
        message(FATAL_ERROR
            "MSYS2 SystemC was found, but gendef and dlltool are required to "
            "generate its missing DLL import library. Add the UCRT64 bin directory "
            "to PATH and install the MSYS2 UCRT64 toolchain package."
        )
    endif()

    list(GET _systemc_runtime_dlls 0 _systemc_runtime_dll)
    get_filename_component(_systemc_runtime_name "${_systemc_runtime_dll}" NAME)
    set(_systemc_definition_file "${CMAKE_CURRENT_BINARY_DIR}/systemc.def")
    set(_systemc_import_library "${CMAKE_CURRENT_BINARY_DIR}/${_systemc_runtime_name}.a")

    if(NOT EXISTS "${_systemc_import_library}"
        OR "${_systemc_runtime_dll}" IS_NEWER_THAN "${_systemc_import_library}")
        execute_process(
            COMMAND "${SYSTEMC_GENDEF_EXECUTABLE}" - "${_systemc_runtime_dll}"
            OUTPUT_FILE "${_systemc_definition_file}"
            RESULT_VARIABLE _gendef_result
            ERROR_VARIABLE _gendef_error
        )
        if(NOT _gendef_result EQUAL 0)
            message(FATAL_ERROR "gendef failed to inspect SystemC: ${_gendef_error}")
        endif()

        execute_process(
            COMMAND "${SYSTEMC_DLLTOOL_EXECUTABLE}"
                --dllname "${_systemc_runtime_name}"
                --input-def "${_systemc_definition_file}"
                --output-lib "${_systemc_import_library}"
            RESULT_VARIABLE _dlltool_result
            ERROR_VARIABLE _dlltool_error
        )
        if(NOT _dlltool_result EQUAL 0)
            message(FATAL_ERROR
                "dlltool failed to create the SystemC import library: ${_dlltool_error}"
            )
        endif()
    endif()

    target_include_directories(tlm_at_four_phase PRIVATE "${_systemc_include_dir}")
    target_link_libraries(tlm_at_four_phase PRIVATE "${_systemc_import_library}")
    set_property(TARGET tlm_at_four_phase PROPERTY CXX_STANDARD 20)
    target_compile_definitions(tlm_at_four_phase PRIVATE TLM_AT_SYSTEMC_DLL)
else()
    find_package(SystemCLanguage CONFIG QUIET)
    find_package(SystemC CONFIG QUIET)

    if(TARGET SystemC::systemc)
        target_link_libraries(tlm_at_four_phase PRIVATE SystemC::systemc)
    elseif(TARGET SystemC::SystemC)
        target_link_libraries(tlm_at_four_phase PRIVATE SystemC::SystemC)
    elseif(TARGET SystemC::systemc_shared)
        target_link_libraries(tlm_at_four_phase PRIVATE SystemC::systemc_shared)
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SYSTEMC QUIET IMPORTED_TARGET systemc)
        endif()

        if(TARGET PkgConfig::SYSTEMC)
            target_link_libraries(tlm_at_four_phase PRIVATE PkgConfig::SYSTEMC)
        else()
            find_path(SYSTEMC_INCLUDE_DIR NAMES systemc)
            find_library(SYSTEMC_LIBRARY NAMES systemc)

            if(NOT SYSTEMC_INCLUDE_DIR OR NOT SYSTEMC_LIBRARY)
                message(FATAL_ERROR
                    "SystemC was not found. Install SystemC/TLM-2.0 and configure "
                    "with -DSystemCLanguage_DIR=<SystemC package directory>, "
                    "PKG_CONFIG_PATH, or SYSTEMC_INCLUDE_DIR and SYSTEMC_LIBRARY."
                )
            endif()

            target_include_directories(tlm_at_four_phase PRIVATE ${SYSTEMC_INCLUDE_DIR})
            target_link_libraries(tlm_at_four_phase PRIVATE ${SYSTEMC_LIBRARY})
        endif()
    endif()
endif()

if(MSVC)
    target_compile_options(tlm_at_four_phase PRIVATE /W4)
else()
    target_compile_options(tlm_at_four_phase PRIVATE -Wall -Wextra -Wpedantic)
endif()
