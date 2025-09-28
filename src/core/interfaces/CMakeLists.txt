add_library(math STATIC
    Math.cpp
    Math.h
)

target_compile_features(math PRIVATE cxx_std_17)
target_include_directories(math PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)