cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sendspin-xiao)

# sendspin-cpp's CMakeLists puts esphome__micro-flac / micro-opus into
# REQUIRES inside `if(CONFIG_SENDSPIN_ENABLE_PLAYER)`. ESP-IDF's early
# dependency-expansion pass evaluates that conditional with CONFIG_ unset on a
# fresh build, so the libs get dropped from sendspin-cpp's REQUIRES even when
# the flag is later set "y" — its sources then fail to find
# <micro_flac/flac_decoder.h>. Force-link them here.
idf_component_get_property(SENDSPIN_LIB sendspin__sendspin-cpp COMPONENT_LIB)
target_link_libraries(${SENDSPIN_LIB} PUBLIC
    idf::esphome__micro-flac
    idf::esphome__micro-opus
)

# GCC 14.2 (esp-idf 5.5.1 toolchain) trips -Wmaybe-uninitialized in
# sendspin-cpp (ShadowSlot<T>) and the bundled opus silk decoder
# (NLSF2A.c cos_LSF_QA). All false positives — and we can't patch the
# managed components. Silence the -Werror for the affected deps.
foreach(LIB
        ${SENDSPIN_LIB}
        idf::esphome__micro-flac
        idf::esphome__micro-opus)
    target_compile_options(${LIB} PRIVATE -Wno-error=maybe-uninitialized)
endforeach()
