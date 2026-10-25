cmake_minimum_required(VERSION 3.14)
project(MatrixLab CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Dependencies (downloaded on first configure; needs Git and internet) ---
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib GIT_REPOSITORY https://github.com/raysan5/raylib.git GIT_TAG 5.5 GIT_SHALLOW TRUE)
FetchContent_Declare(raygui GIT_REPOSITORY https://github.com/raysan5/raygui.git GIT_TAG 4.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib raygui)  # raygui is a single header, so it is only downloaded

# --- vectors + calculations: no graphics, so the tests link only this ---
find_package(Threads REQUIRED)
add_library(calc src/calculations/ops.cpp)
target_include_directories(calc PUBLIC src)
target_link_libraries(calc PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(calc PRIVATE /W4)
else()
    target_compile_options(calc PRIVATE -Wall -Wextra)
endif()

# --- the desktop app ---
add_executable(matrixlab src/app/main.cpp src/graphics/ui_panel.cpp src/graphics/scene3d.cpp)
target_include_directories(matrixlab PRIVATE ${raygui_SOURCE_DIR}/src)
target_link_libraries(matrixlab PRIVATE calc raylib)

# --- tests ---
enable_testing()
add_executable(test_ops test/test_ops.cpp)
target_link_libraries(test_ops PRIVATE calc)
add_test(NAME ops COMMAND test_ops)