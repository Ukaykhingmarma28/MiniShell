cmake_minimum_required(VERSION 3.12)
project(minishell LANGUAGES CXX)

# --- Options ---
option(ENABLE_SANITIZERS "Build with Address/UB sanitizers (Debug only)" ON)
option(ENABLE_LTO        "Enable Link-Time Optimization for Release builds"   OFF)
option(WITH_READLINE     "Enable GNU Readline (history/shortcuts) if present" ON)

# Default build type if not set
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Target ---
add_executable(minishell
    main.cpp
)

# Headers are alongside sources
target_include_directories(minishell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Require C++17
target_compile_features(minishell PRIVATE cxx_std_17)

# Warnings (GCC/Clang/MSVC)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(minishell PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
elseif (MSVC)
  # Note: this code is POSIX-only; MSVC won't build without heavy porting.
  target_compile_options(minishell PRIVATE /W4 /permissive-)
endif()

# POSIX feature macros (safe on most platforms)
target_compile_definitions(minishell PRIVATE _POSIX_C_SOURCE=200809L)

# ---- Optional: GNU Readline (history, Ctrl-R, arrows) ----
# Tries to find and link readline. On macOS/Homebrew and many Linux distros,
# readline needs curses/ncurses as well.
if (WITH_READLINE)
  find_path(READLINE_INCLUDE_DIR readline/readline.h)
  find_library(READLINE_LIBRARY NAMES readline)

  # Curses (for termcap symbols), optional but common
  find_package(Curses)

  if (READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
    message(STATUS "Readline found: ${READLINE_LIBRARY}")
    target_include_directories(minishell PRIVATE ${READLINE_INCLUDE_DIR})
    target_link_libraries(minishell PRIVATE ${READLINE_LIBRARY})
    if (CURSES_FOUND)
      target_link_libraries(minishell PRIVATE ${CURSES_LIBRARIES})
    else()
      # Some platforms need tinfo explicitly if Curses not found
      find_library(TINFO_LIBRARY NAMES tinfo)
      if (TINFO_LIBRARY)
        target_link_libraries(minishell PRIVATE ${TINFO_LIBRARY})
      endif()
    endif()
    target_compile_definitions(minishell PRIVATE MINISHELL_HAVE_READLINE=1)
  else()
    message(STATUS "Readline not found; building without line editing/history.")
  endif()
endif()

# ---- Sanitizers (Debug + GCC/Clang) ----
if (ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(minishell PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(minishell    PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  endif()
endif()

# ---- LTO (Release) ----
if (ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
  if (ipo_ok)
    set_property(TARGET minishell PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(STATUS "IPO/LTO not supported: ${ipo_msg}")
  endif()
endif()

# Install rule (optional)
install(TARGETS minishell RUNTIME DESTINATION bin)
