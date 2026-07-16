# Build Environment Notes

This document records how the ESP-IDF build environment was set up during development,
what tools were found, what issues were encountered, and concerns for future sessions.

---

## 1. ESP-IDF Installation

### Location
```
C:\Espressif\
├── idf_cmd_init.bat              ← Environment setup script (run this first)
├── frameworks\
│   ├── esp-idf-v5.1/             ← DEFAULT version (used by idf_cmd_init.bat)
│   ├── esp-idf-v5.3.1/           ← Newer version available
│   └── esp-idf-v5.5/             ← Newest version available
└── tools\
    ├── tools\cmake\3.24.0\       ← ESP-IDF bundled cmake
    ├── tools\cmake\3.30.2\       ← Also available
    ├── tools\ninja\1.10.2\       ← Build system
    ├── tools\xtensa-esp32s3-elf\ ← Cross-compiler for ESP32-S3
    ├── tools\xtensa-esp-elf\     ← Generic Xtensa toolchain
    ├── tools\riscv32-esp-elf\    ← RISC-V toolchain (ESP32-C3 etc.)
    └── python_env\               ← Python virtualenv for IDF tools
```

### How to activate the build environment
```bash
# Option A: Default (uses ESP-IDF v5.1)
C:\Espressif\idf_cmd_init.bat

# Option B: Use a specific version
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.3.1
C:\Espressif\idf_cmd_init.bat
```

### ⚠️ Concern: Default version is v5.1 (old)
The `idf_cmd_init.bat` defaults to `esp-idf-v5.1` which is the oldest installed version.
Our project targets ESP-IDF v5.x — v5.1 works but is missing newer features and bug fixes.
**Recommendation:** Decide which version to standardize on (v5.3.1 suggested) and
always set `IDF_PATH` before running `idf_cmd_init.bat`.

### ⚠️ Concern: Multiple ESP-IDF versions installed
Three versions coexist (v5.1, v5.3.1, v5.5). Each has its own Python virtualenv,
tool versions, and component APIs. Code that builds on one version may not build on another.
**Recommendation:** Pick ONE version for this project and stick with it.
Document the chosen version in sdkconfig.defaults or a project README.

---

## 2. Native C Compiler (for host-side testing)

### Problem
The ESP-IDF installation only includes **cross-compilers** (xtensa-esp32s3-elf-gcc, etc.)
that produce code for the ESP32 chip — NOT for running on Windows.
We needed a native gcc to compile and run unit tests on the PC.

### Solution: MSYS2 + MinGW-w64
```
C:\msys64\                        ← MSYS2 installation
├── mingw64\bin\gcc.exe           ← Native x86_64 gcc (installed via pacman)
├── mingw64\bin\cmake.exe         ← Native cmake
├── mingw64\bin\mingw32-make.exe  ← Native make
└── usr\bin\bash.exe              ← MSYS2 bash shell
```

### How it was installed
```bash
C:\msys64\usr\bin\bash.exe -lc "pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make"
```
This installed gcc 14.2.0, cmake 3.31.5, and make 4.4.1 (~666MB total).

### How to use for host-side tests
```bash
# Must add MinGW to PATH before running cmake/build
set PATH=C:\msys64\mingw64\bin;%PATH%

# Then build and run tests
cmake -G "MinGW Makefiles" -B tests/host/build -S tests/host
cmake --build tests/host/build
tests/host/build/test_runner.exe
```

### ⚠️ Concern: MinGW not in system PATH
The MinGW gcc is NOT in the system PATH by default. Every new terminal session
needs `set PATH=C:\msys64\mingw64\bin;%PATH%` before cmake/gcc commands work.
**Recommendation:** Add `C:\msys64\mingw64\bin` to the system PATH environment
variable permanently, or create a batch script that sets it up.

### ⚠️ Concern: Two separate toolchains
We now have TWO gcc toolchains on this machine:
1. `C:\msys64\mingw64\bin\gcc.exe` — native x86_64, for host tests
2. `C:\Espressif\tools\...\xtensa-esp32s3-elf-gcc.exe` — cross-compiler, for ESP32

These must NOT be mixed. Host tests use MinGW, ESP32 builds use ESP-IDF's toolchain.
The CMakeLists.txt files are separate (tests/host/ vs project root), so this should
be safe as long as the right environment is activated.

---

## 3. Build Commands Summary

### Host-side unit tests (PC, no hardware needed)
```bash
set PATH=C:\msys64\mingw64\bin;%PATH%
cmake -G "MinGW Makefiles" -B tests/host/build -S tests/host
cmake --build tests/host/build
tests\host\build\test_runner.exe
```

### ESP32-S3 firmware build (cross-compile)
```bash
C:\Espressif\idf_cmd_init.bat
cd E:\agent\esp32_lua_llm
idf.py set-target esp32s3
idf.py build
```

### Flash to ESP32-S3-DevKitC-1
```bash
idf.py -p COM4 flash
```

### Monitor serial output
```bash
idf.py -p COM4 monitor
```

### Full rebuild from scratch
```bash
rmdir /s /q build
idf.py set-target esp32s3
idf.py build
```

---

## 4. Issues Encountered

### 4.1 Git "dubious ownership" error
```
fatal: detected dubious ownership in repository at 'E:/agent/esp32_lua_llm'
'E:/agent/esp32_lua_llm' is owned by: BUILTIN/Administrators
but the current user is: DESKTOP-6DEMLU7/jinbo
```
**Cause:** The project directory was created by an admin account, but we're running as
a regular user. Git refuses to operate on directories owned by a different user.
**Fix:** Run `git config --global --add safe.directory E:/agent/esp32_lua_llm`
**Impact:** Only affects git version info in the build (cosmetic). Build succeeds anyway.

### 4.2 CMake target name error
```
CMake Error: Cannot specify include directories for target "ble_sniffer.elf"
which is not built by this project.
```
**Cause:** Used `target_include_directories(ble_sniffer.elf ...)` in main/CMakeLists.txt.
ESP-IDF components must use `INCLUDE_DIRS` in `idf_component_register()` instead.
**Fix:** Changed to `idf_component_register(INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/interfaces")`

### 4.3 No native compiler found initially
The ESP-IDF installation only has cross-compilers. Had to install MinGW via MSYS2
pacman to get a native gcc for host-side unit testing.

### 4.4 Build directory cleanup
`idf.py fullclean` sometimes refuses to delete the build directory if it's in a bad state.
Manual `rmdir /s /q build` is needed as a fallback.

---

## 5. Recommendations for Future Sessions

1. **Always activate the right environment first:**
   - For ESP32 builds: `C:\Espressif\idf_cmd_init.bat` (check which IDF version)
   - For host tests: `set PATH=C:\msys64\mingw64\bin;%PATH%`

2. **Standardize on one ESP-IDF version** — recommend v5.3.1 (not the default v5.1)

3. **Add MinGW to system PATH** permanently to avoid forgetting it each session

4. **Fix git ownership** once: `git config --global --add safe.directory E:/agent/esp32_lua_llm`

5. **Never mix toolchains** — MinGW gcc for host tests, xtensa-esp32s3-elf-gcc for firmware

6. **If build fails mysteriously**, delete the build/ directory and rebuild from scratch

---

## 6. Disk Usage

| Component | Size |
|-----------|------|
| ESP-IDF v5.1 | ~2 GB |
| ESP-IDF v5.3.1 | ~2 GB |
| ESP-IDF v5.5 | ~2 GB |
| ESP-IDF tools (all toolchains) | ~10 GB |
| MSYS2 + MinGW | ~1.5 GB |
| Project build/ directory | ~500 MB |
| **Total** | **~18 GB** |

Consider removing unused ESP-IDF versions to save disk space:
```bash
python C:\Espressif\frameworks\esp-idf-v5.1\tools\idf_tools.py uninstall
```
