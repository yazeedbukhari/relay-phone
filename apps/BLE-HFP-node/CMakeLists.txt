cmake_minimum_required(VERSION 3.16)

set(DOTENV_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.env")
if(EXISTS "${DOTENV_FILE}")
    file(STRINGS "${DOTENV_FILE}" DOTENV_LINES)
    foreach(DOTENV_LINE ${DOTENV_LINES})
        if(DOTENV_LINE MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$")
            set(ENV{${CMAKE_MATCH_1}} "${CMAKE_MATCH_2}")
        endif()
    endforeach()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(relay-phone-transmitter)
