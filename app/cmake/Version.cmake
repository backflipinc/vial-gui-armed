# Same rule as util/set_version.py: a tagged commit is the tag (v2.0.0 -> 2.0.0),
# otherwise the last tag plus the commit (2.0.0+3.gabc1234).
function(vial_armed_git_version out_full out_numeric)
    find_package(Git QUIET)
    set(full "0.0.0")
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --long --match "v[0-9]*"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE describe OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE rc ERROR_QUIET)
        if(rc EQUAL 0 AND describe MATCHES "^v(.+)-([0-9]+)-g([0-9a-f]+)$")
            if(CMAKE_MATCH_2 STREQUAL "0")
                set(full "${CMAKE_MATCH_1}")
            else()
                set(full "${CMAKE_MATCH_1}+${CMAKE_MATCH_2}.g${CMAKE_MATCH_3}")
            endif()
        else()
            execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE sha OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE rc ERROR_QUIET)
            if(rc EQUAL 0)
                set(full "0.0.0+g${sha}")
            endif()
        endif()
    endif()
    string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" numeric "${full}")
    if(NOT numeric)
        set(numeric "0.0.0")
    endif()
    set(${out_full} "${full}" PARENT_SCOPE)
    set(${out_numeric} "${numeric}" PARENT_SCOPE)
endfunction()
