add_library(utils STATIC
        logging/logging.cpp
        logging/logging.h
)

target_link_libraries(utils PUBLIC
        spdlog::spdlog
)