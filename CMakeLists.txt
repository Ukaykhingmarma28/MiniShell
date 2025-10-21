cmake_minimum_required(VERSION 3.12)
project(minishell LANGUAGES CXX)

# --- Options ---
option(ENABLE_SANITIZERS "Build with Address/UB sanitizers (Debug only)" ON)

# --- Target ---
add_executable(minishell
    main.cpp
)

# Require C++17
target_compile_features(minishell PRIVATE cxx_std_17)

# Warnings (GCC/Clang)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(minishell PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
elseif (MSVC)
  # Note: this code is POSIX-only; MSVC won't build without heavy porting.
  target_compile_options(minishell PRIVATE /W4 /permissive-)
endif()

# POSIX defines (safe no-ops on most platforms)
target_compile_definitions(minishell PRIVATE _POSIX_C_SOURCE=200809L)

# Sanitizers (only in Debug and on GCC/Clang)
if (ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(minishell PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(minishell PRIVATE    -fsanitize=address,undefined -fno-omit-frame-pointer)
  endif()
endif()

# Install rule (optional)
install(TARGETS minishell RUNTIME DESTINATION bin)
