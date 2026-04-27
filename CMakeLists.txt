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
