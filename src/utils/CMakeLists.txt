add_library(utils STATIC
        logging/logging.cpp
        logging/logging.h
)


target_include_directories(utils PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern
)

target_link_libraries(utils PUBLIC
        spdlog::spdlog
        core
)