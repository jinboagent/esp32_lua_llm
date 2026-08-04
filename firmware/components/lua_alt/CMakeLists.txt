# lua_alt — alternative Lua implementation wrapper
#
# Compiles our wrapper (lua_port.c, script_mgmt.c) against the Lua library
# from the ESP-IDF component registry (UncleRus/esp-idf-lua) instead of the
# vendored upstream source in firmware/components/lua/.
#
# Active only when LUA_SOURCE=registry (see top-level CMakeLists.txt).
# In that mode the vendored `lua` component is excluded and the registry
# package takes the component name `lua`.

idf_component_register(
    SRCS
        "../lua/lua_port.c"
        "../lua/script_mgmt.c"
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/interfaces"
    REQUIRES lua storage
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter -Wno-missing-field-initializers)
