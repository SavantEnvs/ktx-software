cmake_minimum_required(VERSION 3.22)

# Builds libktx's read-only library and the Mayhem fuzz harness / functional oracle on top of it, as a
# project of its own (like upstream's tests/use-ktx). Driven by mayhem/build.sh.
#   MAYHEM_FUZZ=ON   libFuzzer coverage instrumentation on libktx; builds ktx_read_fuzzer + its -standalone
#   MAYHEM_FUZZ=OFF  plain build (normal flags); builds ktx_oracle only
project(ktx-mayhem LANGUAGES C CXX)

option(MAYHEM_FUZZ "Build the fuzz harness" OFF)
set(MAYHEM_LSAN_OFF "" CACHE FILEPATH "TU defining __lsan_is_turned_off() (leak detection off), linked into every fuzz binary")
set(MAYHEM_STANDALONE_MAIN "" CACHE FILEPATH "LLVM run-once driver (C source) for the -standalone reproducer")

set(BUILD_SHARED_LIBS OFF)
set(LIBKTX_VERSION_READ_ONLY ON CACHE BOOL "" FORCE)
set(LIBKTX_FEATURE_VK_UPLOAD OFF CACHE BOOL "" FORCE)
set(LIBKTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "" FORCE)
if(MAYHEM_FUZZ)
    add_compile_options(-fsanitize=fuzzer-no-link)
endif()
add_subdirectory(../lib ktx)

set(harness_src ${CMAKE_CURRENT_SOURCE_DIR}/harnesses/ktx_read_fuzzer.cc)

if(MAYHEM_FUZZ)
    add_executable(ktx_read_fuzzer ${harness_src} ${MAYHEM_LSAN_OFF})
    target_compile_features(ktx_read_fuzzer PRIVATE cxx_std_17)
    target_link_libraries(ktx_read_fuzzer PRIVATE ktx_read)
    target_link_options(ktx_read_fuzzer PRIVATE -fsanitize=fuzzer)

    add_executable(ktx_read_fuzzer_standalone ${harness_src} ${MAYHEM_LSAN_OFF} ${MAYHEM_STANDALONE_MAIN})
    target_compile_features(ktx_read_fuzzer_standalone PRIVATE cxx_std_17)
    target_link_libraries(ktx_read_fuzzer_standalone PRIVATE ktx_read)
else()
    add_executable(ktx_oracle harnesses/ktx_oracle.cc)
    target_compile_features(ktx_oracle PRIVATE cxx_std_17)
    target_link_libraries(ktx_oracle PRIVATE ktx_read)
endif()
