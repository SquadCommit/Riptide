cmake_minimum_required(VERSION 3.16)
project(tsunami_lab CXX C)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------- build options (replicate SConstruct variables) ----------
set(BUILD_MODE "release" CACHE STRING "release | debug | release+san | debug+san")
set(OPT_LEVEL  "o2"      CACHE STRING "o2 | o3 | ofast")
set(ARCH       "none"    CACHE STRING "none | native | icelake-server")
option(ENABLE_OMP "Enable OpenMP"          OFF)
option(ENABLE_VIZ "Build viz target"       ON)
option(ENABLE_INLINE "Allow inlining"      ON)
option(ENABLE_LTO "Enable link-time / interprocedural optimization" ON)

# ---------- common flags ----------
add_compile_options(-Wall -Wextra -Wpedantic)
# -Wno-keyword-macro is Clang-only; silence the GCC unknown-option warning
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wno-keyword-macro)
endif()

if(BUILD_MODE MATCHES "debug")
  add_compile_options(-g -O0)
else()
  if(OPT_LEVEL STREQUAL "ofast")
    add_compile_options(-Ofast)
  elseif(OPT_LEVEL STREQUAL "o3")
    add_compile_options(-O3)
  else()
    add_compile_options(-O2)
  endif()
endif()

if(NOT ENABLE_INLINE)
  add_compile_options(-g -fno-inline)
endif()

if(ARCH STREQUAL "none")
elseif(ARCH STREQUAL "native")
  # Nix silently strips "-march=native" as impure; pass an explicit target
  # (e.g. -DARCH=znver4) instead.
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    # AArch64 clang has no -march=native; the CPU is selected via -mcpu.
    add_compile_options(-mcpu=native)
  else()
    add_compile_options(-march=native)
  endif()
elseif(ARCH MATCHES "^apple-")
  add_compile_options(-mcpu=${ARCH})
else()
  add_compile_options(-march=${ARCH})
endif()

if(BUILD_MODE MATCHES "san")
  add_compile_options(-g
    -fsanitize=address -fsanitize=undefined
    -fsanitize=float-divide-by-zero -fsanitize=bounds
    -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address -fsanitize=undefined)
else()
  add_compile_options(-Werror)
endif()

if(ENABLE_LTO AND NOT BUILD_MODE MATCHES "debug" AND NOT BUILD_MODE MATCHES "san")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err LANGUAGES C CXX)
  if(_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(STATUS "LTO requested but not supported by toolchain (${_ipo_err}), skipping")
  endif()
endif()

if(ENABLE_OMP)
  find_package(OpenMP REQUIRED)
else()
  add_compile_options(-Wno-unknown-pragmas)
endif()

# ---------- threads (SolverThread background loop) ----------
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ---------- NetCDF ----------
find_program(NC_CONFIG nc-config REQUIRED)
execute_process(COMMAND ${NC_CONFIG} --prefix
  OUTPUT_VARIABLE NETCDF_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
set(NETCDF_INCLUDE "${NETCDF_PREFIX}/include")
set(NETCDF_LIBDIR  "${NETCDF_PREFIX}/lib")

# ---------- submodules ----------
add_subdirectory(submodules/pugixml  EXCLUDE_FROM_ALL)
# Suppress global -Werror for pugixml so unknown/irrelevant warnings don't break the build
target_compile_options(pugixml-static PRIVATE -w)
add_subdirectory(submodules/Catch2   EXCLUDE_FROM_ALL)

# ---------- solver library ----------
add_library(solver_lib STATIC
  src/solvers/roe/Roe.cpp
  src/util/OmpDefaults.cpp
  src/patches/WavePropagation1d/WavePropagation1d.cpp
  src/patches/WavePropagation2d/WavePropagation2d.cpp
  src/setups/dambreak/DamBreak1d.cpp
  src/setups/dambreak/CircularDamBreak2d.cpp
  src/setups/shockshock/ShockShock1d.cpp
  src/setups/rarerare/RareRare1d.cpp
  src/setups/subcritical1d/SubCritical1d.cpp
  src/setups/supercritical1d/SuperCritical1d.cpp
  src/setups/tsunamievent1d/TsunamiEvent1d.cpp
  src/setups/artificialtsunami2d/ArtificialTsunami2d.cpp
  src/setups/tsunamievent2d/TsunamiEvent2d.cpp
  src/setups/checkpoint/CheckPoint.cpp
  src/displacement/GaussianDisplacement.cpp
  src/displacement/OkadaDisplacement.cpp
  src/visualization/SolverThread.cpp
  src/io/netcdf/NetCDF.cpp
  src/io/csv/Csv.cpp
  src/io/stations/Stations.cpp
  src/io/Slab2Reader.cpp
)
target_include_directories(solver_lib PUBLIC src ${NETCDF_INCLUDE})
target_link_directories(solver_lib PUBLIC ${NETCDF_LIBDIR})
target_link_libraries(solver_lib PUBLIC netcdf pugixml Threads::Threads)
if(ENABLE_OMP)
  target_link_libraries(solver_lib PUBLIC OpenMP::OpenMP_CXX)
endif()

# ---------- batch executable ----------
add_executable(tsunami_lab src/main.cpp)
target_link_libraries(tsunami_lab PRIVATE solver_lib)

# ---------- tests ----------
add_executable(tests
  src/tests.cpp
  src/solvers/roe/Roe.test.cpp
  src/solvers/fwave/FWave.test.cpp
  src/patches/WavePropagation1d/WavePropagation1d.test.cpp
  src/patches/WavePropagation2d/WavePropagation2d.test.cpp
  src/io/csv/Csv.test.cpp
  src/setups/dambreak/DamBreak1d.test.cpp
  src/setups/dambreak/CircularDamBreak2d.test.cpp
  src/setups/shockshock/ShockShock1d.test.cpp
  src/setups/rarerare/RareRare1d.test.cpp
  src/setups/subcritical1d/SubCritical1d.test.cpp
  src/setups/supercritical1d/SuperCritical1d.test.cpp
  src/setups/tsunamievent1d/TsunamiEvent1d.test.cpp
  src/setups/artificialtsunami2d/ArtificialTsunami2d.test.cpp
  src/setups/tsunamievent2d/TsunamiEvent2d.test.cpp
  src/setups/tsunamievent2d/TsunamiEvent2dCompare.test.cpp
  src/setups/checkpoint/CheckPoint.test.cpp
  src/displacement/GaussianDisplacement.test.cpp
  src/displacement/OkadaDisplacement.test.cpp
  src/visualization/SolverThread.test.cpp
  src/io/stations/Stations.test.cpp
  src/io/netcdf/NetCDF.test.cpp
)
target_include_directories(tests PRIVATE submodules/Catch2/single_include)
target_link_libraries(tests PRIVATE solver_lib)

# ---------- visualization ----------
if(ENABLE_VIZ)
  find_package(OpenGL REQUIRED)
  find_package(glfw3  REQUIRED)
  find_package(glm    REQUIRED)

  # GLAD — committed generated files, no submodule needed
  add_library(glad STATIC thirdparty/glad/src/glad.c)
  target_include_directories(glad PUBLIC thirdparty/glad/include)
  # -w: suppress pedantic pointer-cast warnings in generated C code
  # -O1: required by glibc _FORTIFY_SOURCE (active even in debug builds)
  target_compile_options(glad PRIVATE -w -O1)

  # ImGui — submodule, no own CMakeLists, so we build it manually
  add_library(imgui STATIC
    submodules/imgui/imgui.cpp
    submodules/imgui/imgui_draw.cpp
    submodules/imgui/imgui_widgets.cpp
    submodules/imgui/imgui_tables.cpp
    submodules/imgui/backends/imgui_impl_glfw.cpp
    submodules/imgui/backends/imgui_impl_opengl3.cpp
  )
  target_include_directories(imgui PUBLIC
    submodules/imgui
    submodules/imgui/backends
    thirdparty/glad/include
  )
  target_link_libraries(imgui PUBLIC glfw)
  target_compile_options(imgui PRIVATE -w -O1)

  # viz executable
  add_executable(tsunami_lab_viz
    src/main_viz.cpp
    src/util/CpuAffinity.cpp
    src/visualization/Window.cpp
    src/visualization/GlobeView.cpp
    src/visualization/Gebco.cpp
    src/visualization/RegionView.cpp
  )
  target_include_directories(tsunami_lab_viz PRIVATE src thirdparty/glad/include)
  target_compile_definitions(tsunami_lab_viz PRIVATE
    SHADER_DIR="${CMAKE_SOURCE_DIR}/src/visualization/shaders")
  target_link_libraries(tsunami_lab_viz PRIVATE solver_lib glad imgui glfw OpenGL::GL glm::glm)

  if(APPLE)
    target_link_libraries(tsunami_lab_viz PRIVATE
      "-framework Cocoa"
      "-framework IOKit"
      "-framework CoreVideo"
    )
  endif()
endif()
