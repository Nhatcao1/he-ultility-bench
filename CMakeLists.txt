cmake_minimum_required(VERSION 3.16)

project(utility_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UTILITY_BENCH_WITH_OPENFHE "Build OpenFHE CKKS benchmarks" OFF)

add_executable(utility_bench
    src/main.cpp
)

target_compile_options(utility_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

if(UTILITY_BENCH_WITH_OPENFHE)
    find_package(OpenFHE CONFIG REQUIRED)

    separate_arguments(OPENFHE_CXX_FLAGS_LIST NATIVE_COMMAND "${OpenFHE_CXX_FLAGS}")
    separate_arguments(OPENFHE_EXE_LINKER_FLAGS_LIST NATIVE_COMMAND "${OpenFHE_EXE_LINKER_FLAGS}")

    target_compile_definitions(utility_bench PRIVATE UTILITY_BENCH_WITH_OPENFHE=1)

    target_compile_options(utility_bench PRIVATE ${OPENFHE_CXX_FLAGS_LIST})
    target_include_directories(utility_bench PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_link_directories(utility_bench PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )

    if(UNIX AND NOT APPLE)
        target_link_options(utility_bench PRIVATE -Wl,--no-as-needed)
    endif()

    target_link_options(utility_bench PRIVATE ${OPENFHE_EXE_LINKER_FLAGS_LIST})
    target_link_libraries(utility_bench PRIVATE ${OpenFHE_SHARED_LIBRARIES})
endif()
