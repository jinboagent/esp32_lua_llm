# Unity Unit Test Template

> Copy this template to `tests/harness/test_<module>_on_target.c` and
> replace all `<placeholder>` tokens.

## File Template

```c
/*
 * test_<module>_on_target.c — Unit tests for <module>
 *
 * Build: host-side with Unity + cmake (no ESP-IDF required).
 * See "Build Instructions" at the bottom.
 */

#include "unity.h"
#include "<module>_if.h"          /* Module under test              */

/* ------------------------------------------------------------------ */
/*  Mocks / stubs for dependencies                                    */
/* ------------------------------------------------------------------ */

/*
 * Provide minimal stubs for any functions the module calls from
 * dependent subsystems.  Keep stubs stateless where possible.
 *
 * Example:
 *   int scan_pipeline_start(void) { return 0; }
 *   int scan_pipeline_stop(void)  { return 0; }
 */

/* ------------------------------------------------------------------ */
/*  Test fixtures                                                     */
/* ------------------------------------------------------------------ */

static char response_buf[256];

void setUp(void)
{
    /* Called before every test.  Reset module state. */
    <module>_init();
    memset(response_buf, 0, sizeof(response_buf));
}

void tearDown(void)
{
    /* Called after every test.  Free resources if any (none in
     * zero-alloc designs, but keep the hook for symmetry). */
}

/* ------------------------------------------------------------------ */
/*  Test functions                                                    */
/*                                                                    */
/*  Naming convention:  test_<module>_<scenario>                      */
/* ------------------------------------------------------------------ */

/* --- Happy path --------------------------------------------------- */

void test_<module>_init_returns_zero(void)
{
    int rc = <module>_init();
    TEST_ASSERT_EQUAL_INT(0, rc);
}

void test_<module>_valid_command_returns_ok(void)
{
    /* Arrange — already done in setUp if needed */

    /* Act */
    int rc = <module>_<action>("VALID INPUT", response_buf,
                                sizeof(response_buf));

    /* Assert */
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING(
        "{\"status\":\"ok\",\"cmd\":\"<expected>\"}",
        response_buf);
}

/* --- Error paths -------------------------------------------------- */

void test_<module>_null_cmd_returns_error(void)
{
    int rc = <module>_<action>(NULL, response_buf, sizeof(response_buf));
    TEST_ASSERT_EQUAL_INT(-902, rc);   /* or appropriate error code */
}

void test_<module>_null_response_returns_error(void)
{
    int rc = <module>_<action>("CMD", NULL, 0);
    TEST_ASSERT_EQUAL_INT(-902, rc);
}

void test_<module>_invalid_command_returns_error(void)
{
    int rc = <module>_<action>("GARBAGE", response_buf,
                                sizeof(response_buf));
    TEST_ASSERT_EQUAL_INT(-901, rc);
    /* Optionally check the error JSON content */
    TEST_ASSERT_NOT_NULL(strstr(response_buf, "\"status\":\"error\""));
}

/* --- Boundary / edge cases ---------------------------------------- */

void test_<module>_buffer_too_small_returns_error(void)
{
    char tiny[8];
    int rc = <module>_<action>("VALID INPUT", tiny, sizeof(tiny));
    /* Expect truncation error or graceful handling */
    TEST_ASSERT(rc < 0 || strlen(tiny) < sizeof(tiny));
}

/* ------------------------------------------------------------------ */
/*  Test runner                                                       */
/* ------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    /* Happy path */
    RUN_TEST(test_<module>_init_returns_zero);
    RUN_TEST(test_<module>_valid_command_returns_ok);

    /* Error paths */
    RUN_TEST(test_<module>_null_cmd_returns_error);
    RUN_TEST(test_<module>_null_response_returns_error);
    RUN_TEST(test_<module>_invalid_command_returns_error);

    /* Boundary */
    RUN_TEST(test_<module>_buffer_too_small_returns_error);

    return UNITY_END();
}
```

## Naming Convention

| Element              | Pattern                                    | Example                                |
|----------------------|--------------------------------------------|----------------------------------------|
| Test function        | `test_<module>_<scenario>`                 | `test_cli_scan_start_in_idle`          |
| Test file            | `test_<module>_on_target.c`                | `test_cli_on_target.c`                 |
| Mock/stub function   | `<dependency>_<function>` (same signature) | `scan_pipeline_start`                  |

## Assertions Cheat Sheet

```c
TEST_ASSERT_EQUAL_INT(expected, actual);
TEST_ASSERT_EQUAL_UINT32(expected, actual);
TEST_ASSERT_EQUAL_STRING(expected, actual);
TEST_ASSERT_NOT_NULL(pointer);
TEST_ASSERT_NULL(pointer);
TEST_ASSERT_TRUE(condition);
TEST_ASSERT_FALSE(condition);
TEST_ASSERT_EQUAL_HEX8(expected, actual);
TEST_ASSERT_INT_WITHIN(delta, expected, actual);
```

## Build Instructions (Host-Side with cmake)

### Directory Layout

```
tests/harness/
├── CMakeLists.txt
├── unity/                  # Unity source (unity.c, unity.h, unity_internals.h)
├── mocks/                  # Mock/stub .c files for dependencies
│   └── mock_scan_pipeline.c
├── src/                    # Module source copied/linked for host build
│   └── cli_commands.c
└── test_cli_on_target.c    # This test file
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(ble_bridge_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Werror -Wno-unused-parameter)

# Unity
add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC unity)

# Module under test (compiled for host — no ESP-IDF headers)
add_library(module_under_test STATIC src/<module>.c)
target_include_directories(module_under_test PUBLIC
    ${CMAKE_SOURCE_DIR}/../../interfaces
    ${CMAKE_SOURCE_DIR}/../../firmware/components/<module>
)

# Mocks
add_library(mocks STATIC mocks/mock_<dep>.c)
target_include_directories(mocks PUBLIC
    ${CMAKE_SOURCE_DIR}/../../interfaces
)

# Test executable
add_executable(test_<module>_on_target test_<module>_on_target.c)
target_link_libraries(test_<module>_on_target
    PRIVATE module_under_test mocks unity
)
target_include_directories(test_<module>_on_target PRIVATE
    ${CMAKE_SOURCE_DIR}/../../interfaces
    ${CMAKE_SOURCE_DIR}/../../firmware/components/<module>
)
```

### Build & Run

```bash
cd tests/harness
cmake -B build -S .
cmake --build build
./build/test_<module>_on_target
```

### Notes

- The module `.c` file is compiled directly on the host. Any ESP-IDF
  specific calls (GPIO, BLE, FreeRTOS) must be stubbed out in the `mocks/`
  directory or guarded with `#ifdef ESP_PLATFORM` in the source.
- Unity is vendored — download `unity.c`, `unity.h`, `unity_internals.h`
  from [ThrowTheSwitch/Unity](https://github.com/ThrowTheSwitch/Unity)
  and place them in `tests/harness/unity/`.
- No `malloc`/`free` in test code or module code — all buffers are
  stack-allocated or `static`.
