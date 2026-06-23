cmake_minimum_required(VERSION 3.16)

project(utility_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UTILITY_BENCH_WITH_OPENFHE "Build OpenFHE CKKS benchmarks" OFF)

add_executable(utility_bench
    src/main.cpp
)

add_executable(fedavg_bench
    src/fedavg_bench.cpp
)

add_executable(poly_ml_bench
    src/poly_ml_bench.cpp
)

add_executable(dense_layer_bench
    src/dense_layer_bench.cpp
)

add_executable(function_eval_bench
    src/function_eval_bench.cpp
)

add_executable(linear_score_bench
    src/linear_score_bench.cpp
)

add_executable(sum_amount_opt_bench
    src_optimized/sum_amount/sum_amount_opt_bench.cpp
)

add_executable(rolling_avg_opt_bench
    src_optimized/rolling_avg/rolling_avg_opt_bench.cpp
)

add_executable(poly_score_opt_bench
    src_optimized/poly_score/poly_score_opt_bench.cpp
)

add_executable(weighted_sum_opt_bench
    src_optimized/weighted_sum/weighted_sum_opt_bench.cpp
)

add_executable(lookup_opt_bench
    src_optimized/lookup/lookup_opt_bench.cpp
)

target_compile_options(utility_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

target_compile_options(fedavg_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

target_compile_options(poly_ml_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

target_compile_options(dense_layer_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

target_compile_options(function_eval_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

target_compile_options(linear_score_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

target_compile_options(sum_amount_opt_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

target_compile_options(rolling_avg_opt_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

target_compile_options(poly_score_opt_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

target_compile_options(weighted_sum_opt_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

target_compile_options(lookup_opt_bench PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

target_include_directories(sum_amount_opt_bench PRIVATE
    src
)

target_include_directories(rolling_avg_opt_bench PRIVATE
    src
)

target_include_directories(poly_score_opt_bench PRIVATE
    src
)

target_include_directories(weighted_sum_opt_bench PRIVATE
    src
)

target_include_directories(lookup_opt_bench PRIVATE
    src
)

if(UTILITY_BENCH_WITH_OPENFHE)
    find_package(OpenFHE CONFIG REQUIRED)

    separate_arguments(OPENFHE_CXX_FLAGS_LIST NATIVE_COMMAND "${OpenFHE_CXX_FLAGS}")
    separate_arguments(OPENFHE_EXE_LINKER_FLAGS_LIST NATIVE_COMMAND "${OpenFHE_EXE_LINKER_FLAGS}")

    target_compile_definitions(utility_bench PRIVATE UTILITY_BENCH_WITH_OPENFHE=1)
    target_compile_definitions(fedavg_bench PRIVATE UTILITY_BENCH_WITH_OPENFHE=1)
    target_compile_definitions(poly_ml_bench PRIVATE UTILITY_BENCH_WITH_OPENFHE=1)
    target_compile_definitions(dense_layer_bench PRIVATE UTILITY_BENCH_WITH_OPENFHE=1)
    target_compile_definitions(function_eval_bench PRIVATE UTILITY_BENCH_WITH_OPENFHE=1)
    target_compile_definitions(linear_score_bench PRIVATE UTILITY_BENCH_WITH_OPENFHE=1)
    target_compile_definitions(sum_amount_opt_bench PRIVATE UTILITY_BENCH_WITH_OPENFHE=1)
    target_compile_definitions(rolling_avg_opt_bench PRIVATE UTILITY_BENCH_WITH_OPENFHE=1)
    target_compile_definitions(poly_score_opt_bench PRIVATE UTILITY_BENCH_WITH_OPENFHE=1)
    target_compile_definitions(weighted_sum_opt_bench PRIVATE UTILITY_BENCH_WITH_OPENFHE=1)
    target_compile_definitions(lookup_opt_bench PRIVATE UTILITY_BENCH_WITH_OPENFHE=1)

    target_compile_options(utility_bench PRIVATE ${OPENFHE_CXX_FLAGS_LIST})
    target_compile_options(fedavg_bench PRIVATE ${OPENFHE_CXX_FLAGS_LIST})
    target_compile_options(poly_ml_bench PRIVATE ${OPENFHE_CXX_FLAGS_LIST})
    target_compile_options(dense_layer_bench PRIVATE ${OPENFHE_CXX_FLAGS_LIST})
    target_compile_options(function_eval_bench PRIVATE ${OPENFHE_CXX_FLAGS_LIST})
    target_compile_options(linear_score_bench PRIVATE ${OPENFHE_CXX_FLAGS_LIST})
    target_compile_options(sum_amount_opt_bench PRIVATE ${OPENFHE_CXX_FLAGS_LIST})
    target_compile_options(rolling_avg_opt_bench PRIVATE ${OPENFHE_CXX_FLAGS_LIST})
    target_compile_options(poly_score_opt_bench PRIVATE ${OPENFHE_CXX_FLAGS_LIST})
    target_compile_options(weighted_sum_opt_bench PRIVATE ${OPENFHE_CXX_FLAGS_LIST})
    target_compile_options(lookup_opt_bench PRIVATE ${OPENFHE_CXX_FLAGS_LIST})
    # OpenFHE headers intentionally use compiler extensions such as __int128.
    # Mark them as SYSTEM so our local warning policy does not turn third-party
    # header warnings into build failures.
    target_include_directories(utility_bench SYSTEM PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_include_directories(fedavg_bench SYSTEM PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_include_directories(poly_ml_bench SYSTEM PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_include_directories(dense_layer_bench SYSTEM PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_include_directories(function_eval_bench SYSTEM PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_include_directories(linear_score_bench SYSTEM PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_include_directories(sum_amount_opt_bench SYSTEM PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_include_directories(rolling_avg_opt_bench SYSTEM PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_include_directories(poly_score_opt_bench SYSTEM PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_include_directories(weighted_sum_opt_bench SYSTEM PRIVATE
        ${OPENMP_INCLUDES}
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/third-party/include
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/binfhe
    )
    target_include_directories(lookup_opt_bench SYSTEM PRIVATE
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
    target_link_directories(fedavg_bench PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_directories(poly_ml_bench PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_directories(dense_layer_bench PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_directories(function_eval_bench PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_directories(linear_score_bench PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_directories(sum_amount_opt_bench PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_directories(rolling_avg_opt_bench PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_directories(poly_score_opt_bench PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_directories(weighted_sum_opt_bench PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )
    target_link_directories(lookup_opt_bench PRIVATE
        ${OpenFHE_LIBDIR}
        ${OPENMP_LIBRARIES}
    )

    if(UNIX AND NOT APPLE)
        target_link_options(utility_bench PRIVATE -Wl,--no-as-needed)
        target_link_options(fedavg_bench PRIVATE -Wl,--no-as-needed)
        target_link_options(poly_ml_bench PRIVATE -Wl,--no-as-needed)
        target_link_options(dense_layer_bench PRIVATE -Wl,--no-as-needed)
        target_link_options(function_eval_bench PRIVATE -Wl,--no-as-needed)
        target_link_options(linear_score_bench PRIVATE -Wl,--no-as-needed)
        target_link_options(sum_amount_opt_bench PRIVATE -Wl,--no-as-needed)
        target_link_options(rolling_avg_opt_bench PRIVATE -Wl,--no-as-needed)
        target_link_options(poly_score_opt_bench PRIVATE -Wl,--no-as-needed)
        target_link_options(weighted_sum_opt_bench PRIVATE -Wl,--no-as-needed)
        target_link_options(lookup_opt_bench PRIVATE -Wl,--no-as-needed)
    endif()

    target_link_options(utility_bench PRIVATE ${OPENFHE_EXE_LINKER_FLAGS_LIST})
    target_link_options(fedavg_bench PRIVATE ${OPENFHE_EXE_LINKER_FLAGS_LIST})
    target_link_options(poly_ml_bench PRIVATE ${OPENFHE_EXE_LINKER_FLAGS_LIST})
    target_link_options(dense_layer_bench PRIVATE ${OPENFHE_EXE_LINKER_FLAGS_LIST})
    target_link_options(function_eval_bench PRIVATE ${OPENFHE_EXE_LINKER_FLAGS_LIST})
    target_link_options(linear_score_bench PRIVATE ${OPENFHE_EXE_LINKER_FLAGS_LIST})
    target_link_options(sum_amount_opt_bench PRIVATE ${OPENFHE_EXE_LINKER_FLAGS_LIST})
    target_link_options(rolling_avg_opt_bench PRIVATE ${OPENFHE_EXE_LINKER_FLAGS_LIST})
    target_link_options(poly_score_opt_bench PRIVATE ${OPENFHE_EXE_LINKER_FLAGS_LIST})
    target_link_options(weighted_sum_opt_bench PRIVATE ${OPENFHE_EXE_LINKER_FLAGS_LIST})
    target_link_options(lookup_opt_bench PRIVATE ${OPENFHE_EXE_LINKER_FLAGS_LIST})
    target_link_libraries(utility_bench PRIVATE ${OpenFHE_SHARED_LIBRARIES})
    target_link_libraries(fedavg_bench PRIVATE ${OpenFHE_SHARED_LIBRARIES})
    target_link_libraries(poly_ml_bench PRIVATE ${OpenFHE_SHARED_LIBRARIES})
    target_link_libraries(dense_layer_bench PRIVATE ${OpenFHE_SHARED_LIBRARIES})
    target_link_libraries(function_eval_bench PRIVATE ${OpenFHE_SHARED_LIBRARIES})
    target_link_libraries(linear_score_bench PRIVATE ${OpenFHE_SHARED_LIBRARIES})
    target_link_libraries(sum_amount_opt_bench PRIVATE ${OpenFHE_SHARED_LIBRARIES})
    target_link_libraries(rolling_avg_opt_bench PRIVATE ${OpenFHE_SHARED_LIBRARIES})
    target_link_libraries(poly_score_opt_bench PRIVATE ${OpenFHE_SHARED_LIBRARIES})
    target_link_libraries(weighted_sum_opt_bench PRIVATE ${OpenFHE_SHARED_LIBRARIES})
    target_link_libraries(lookup_opt_bench PRIVATE ${OpenFHE_SHARED_LIBRARIES})
endif()
