# Building fetch

## Prerequisites

- CMake 3.25+
- C++20 compiler (GCC 11+, Clang 13+, MSVC 2022+)
- [vcpkg](https://github.com/microsoft/vcpkg)
- OpenSSL 3.0+ (installed via vcpkg)

---

## 1. Clone & Bootstrap vcpkg

Clone vcpkg to a standard global location so it can be shared across projects:

```bash
# Linux / macOS
git clone https://github.com/microsoft/vcpkg.git ~/.vcpkg
~/.vcpkg/bootstrap-vcpkg.sh
```

```powershell
# Windows (PowerShell)
git clone https://github.com/microsoft/vcpkg.git "$env:USERPROFILE\.vcpkg"
& "$env:USERPROFILE\.vcpkg\bootstrap-vcpkg.bat"
```

Then export `VCPKG_ROOT` so CMake can find it automatically. Add the
appropriate line to your shell's startup file to make it permanent:

```bash
# Linux / macOS — add to ~/.bashrc or ~/.zshrc
export VCPKG_ROOT="$HOME/.vcpkg"
export PATH="$VCPKG_ROOT:$PATH"
```

```powershell
# Windows — run once in an admin PowerShell to set it permanently
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "$env:USERPROFILE\.vcpkg", "User")
[System.Environment]::SetEnvironmentVariable("Path", "$env:USERPROFILE\.vcpkg;$env:Path", "User")
```

> **Note:** Restart your terminal (or re-source your shell config) after
> setting these for the changes to take effect.

---

## 2. Install Dependencies

```bash
vcpkg install openssl
```

Or let CMake handle it automatically via `vcpkg.json` manifest mode — just
configure and it will install dependencies for you.

---

## 3. Configure

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
```

> On Windows (PowerShell):
> ```powershell
> cmake -B build `
>   -DCMAKE_BUILD_TYPE=Release `
>   -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
> ```

If `VCPKG_ROOT` is set in your environment, the toolchain file is picked up
automatically and `-DCMAKE_TOOLCHAIN_FILE` can be omitted.

---

## 4. Build

```bash
cmake --build build
```

For a specific config on multi-config generators (Visual Studio, Xcode):

```bash
cmake --build build --config Release
```

---

## 5. Run Tests

```bash
# via CTest
ctest --test-dir build --output-on-failure

# or directly
./build/fetch_tests          # Linux / macOS
.\build\Release\fetch_tests  # Windows
```

To skip building the test suite entirely:

```bash
cmake -B build -DFETCH_BUILD_TESTS=OFF
```

---

## 6. Install

```bash
cmake --install build --prefix /usr/local
```

---

## 7. Consuming fetch in Another Project

After installing, or when using as a subdirectory:

```cmake
find_package(fetch REQUIRED)

target_link_libraries(myapp PRIVATE fetch::fetch)
```

Or as a subdirectory without installing:

```cmake
add_subdirectory(third_party/fetch)
target_link_libraries(myapp PRIVATE fetch::fetch)
```

---

## Build Options

| Option                | Default   | Description                                       |
|-----------------------|-----------|---------------------------------------------------|
| `FETCH_BUILD_TESTS`   | `ON`      | Build the test suite                              |
| `CMAKE_BUILD_TYPE`    | `Release` | Debug / Release / RelWithDebInfo / MinSizeRel     |

---

## Tested Platforms

| Platform       | Compiler         | Status |
|----------------|------------------|--------|
| Ubuntu 22.04+  | GCC 12, Clang 15 | ✅     |
| macOS 13+      | Apple Clang 15   | ✅     |
| Windows 11     | MSVC 2022        | ✅     |