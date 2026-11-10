cmake_minimum_required(VERSION 3.22)
project(emucorex_cpu_topology_tests LANGUAGES CXX)

# Standalone tests of the production placement policy; no emulator VM or app install needed.
set(PCSX2_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../pcsx2")
add_executable(iop_topology_tests
    topology_tests.cpp
    "${PCSX2_ROOT}/pcsx2/IopThread/IopPlacement.cpp"
)
target_compile_features(iop_topology_tests PRIVATE cxx_std_20)
target_include_directories(iop_topology_tests PRIVATE
    "${PCSX2_ROOT}" "${PCSX2_ROOT}/pcsx2" "${PCSX2_ROOT}/3rdparty/cpuinfo/include"
)
target_compile_options(iop_topology_tests PRIVATE -Wall -Wextra -Werror)
enable_testing()
add_test(NAME iop_topology COMMAND iop_topology_tests)
