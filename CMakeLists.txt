cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    firmware/components
)

# Lua implementation switch:
#   vendored (default) - upstream Lua 5.4.8 vendored in firmware/components/lua/
#   registry           - UncleRus/esp-idf-lua from ESP-IDF component registry,
#                        wrapper compiled by firmware/components/lua_alt/
# Usage: idf.py -D LUA_SOURCE=registry build
set(LUA_SOURCE "vendored" CACHE STRING "Lua implementation: vendored or registry")

if(LUA_SOURCE STREQUAL "registry")
    set(EXCLUDE_COMPONENTS lua)
else()
    set(EXCLUDE_COMPONENTS lua_alt)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ble_bridge)
