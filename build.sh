#!/usr/bin/env bash
set -euo pipefail

# =============================================================================
# minishell build helper (cross-distro)
# =============================================================================
# Usage:
#   ./build.sh                     # Debug build (default), auto -j
#   ./build.sh -R                  # Release build
#   ./build.sh -t release          # same as -R
#   ./build.sh --no-readline       # build without readline
#   ./build.sh --no-sanitizers     # disable ASan/UBSan (Debug)
#   ./build.sh -j 8                # set jobs=8
#   ./build.sh --run               # run ./build/minishell after build
#   ./build.sh --clean             # rm -rf build
#   ./build.sh --no-install        # do NOT attempt to install dependencies
#
# Env overrides:
#   CXX=clang++ CC=clang ./build.sh -R
#
# What this script does:
#   1) Detect distro (Linux/macOS) and package manager
#   2) Ensure required tools are installed (or try to install them)
#   3) Configure and build with CMake/Ninja/Make
#   4) Optionally run the resulting binary
# =============================================================================

# -------- defaults --------
BUILD_TYPE="Debug"
WITH_READLINE=ON
ENABLE_SANITIZERS=ON
RUN_AFTER=false
CLEAN=false
NO_INSTALL=false
JOBS=""
GENERATOR=""
BUILD_DIR="build"

# -------- required deps (tools + dev libs) --------
# Tools we need: compiler, cmake, ninja (optional), pkg-config
REQUIRED_CMDS=( cmake pkg-config )
OPTIONAL_CMDS=( ninja )

# Dev packages vary per distro; we’ll map names at runtime.
# We’ll try to ensure: C++ toolchain, cmake, ninja, pkg-config, readline-dev, ncurses-dev

# -------- helpers --------
detect_jobs() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
  elif [[ "${OSTYPE:-}" == darwin* ]]; then
    sysctl -n hw.ncpu
  else
    echo 4
  fi
}

need_sudo() {
  [[ ${EUID:-$(id -u)} -ne 0 ]]
}

# Pretty logging
say()   { printf "\033[1;36m[build]\033[0m %s\n" "$*"; }
warn()  { printf "\033[1;33m[warn]\033[0m %s\n" "$*" >&2; }
err()   { printf "\033[1;31m[err]\033[0m  %s\n" "$*" >&2; }

# -------- parse args --------
while (( $# )); do
  case "$1" in
    -t|--type)          BUILD_TYPE="${2:-}"; shift ;;
    -R|--release)       BUILD_TYPE="Release" ;;
    -j)                 JOBS="${2:-}"; shift ;;
    --jobs=*)           JOBS="${1#*=}" ;;
    --run)              RUN_AFTER=true ;;
    --clean)            CLEAN=true ;;
    --no-readline)      WITH_READLINE=OFF ;;
    --no-sanitizers)    ENABLE_SANITIZERS=OFF ;;
    --no-install)       NO_INSTALL=true ;;
    -h|--help)
      sed -n '1,120p' "$0"; exit 0 ;;
    *)
      err "Unknown option: $1"; exit 1 ;;
  esac
  shift
done

# -------- detect platform / package manager --------
ID_LIKE=""; ID_NAME=""; PM=""
if [[ -f /etc/os-release ]]; then
  # shellcheck disable=SC1091
  . /etc/os-release
  ID_NAME="${ID:-}"
  ID_LIKE="${ID_LIKE:-}"
fi

is_cmd() { command -v "$1" >/dev/null 2>&1; }

if [[ "${OSTYPE:-}" == darwin* ]]; then
  PM="brew"
elif is_cmd pacman; then
  PM="pacman"
elif is_cmd apt-get; then
  PM="apt"
elif is_cmd dnf; then
  PM="dnf"
elif is_cmd zypper; then
  PM="zypper"
elif is_cmd apk; then
  PM="apk"
elif is_cmd xbps-install; then
  PM="xbps"
elif is_cmd emerge; then
  PM="emerge"
else
  PM=""
fi

# -------- choose generator (Ninja preferred) --------
if is_cmd ninja; then
  GENERATOR="-G Ninja"
fi

# -------- resolve jobs --------
if [[ -z "${JOBS}" ]]; then
  JOBS="$(detect_jobs)"
fi

# -------- optionally clean --------
if $CLEAN; then
  say "Removing ${BUILD_DIR}/"
  rm -rf "$BUILD_DIR"
fi

# -------- ensure build dir --------
if [[ ! -d "$BUILD_DIR" ]]; then
  say "Creating ${BUILD_DIR}/"
  mkdir -p "$BUILD_DIR"
fi

# -------- dependency installer --------
install_pkgs() {
  local pkgs=("$@")
  if [[ ${#pkgs[@]} -eq 0 ]]; then return 0; fi

  if $NO_INSTALL; then
    warn "Missing dependencies: ${pkgs[*]} (auto-install disabled --no-install)"
    return 0
  fi

  case "$PM" in
    apt)
      say "Installing deps via apt-get: ${pkgs[*]}"
      if need_sudo; then sudo apt-get update -y; sudo apt-get install -y "${pkgs[@]}"; else apt-get update -y && apt-get install -y "${pkgs[@]}"; fi
      ;;
    pacman)
      say "Installing deps via pacman: ${pkgs[*]}"
      if need_sudo; then sudo pacman -Sy --needed --noconfirm "${pkgs[@]}"; else pacman -Sy --needed --noconfirm "${pkgs[@]}"; fi
      ;;
    dnf)
      say "Installing deps via dnf: ${pkgs[*]}"
      if need_sudo; then sudo dnf -y install "${pkgs[@]}"; else dnf -y install "${pkgs[@]}"; fi
      ;;
    zypper)
      say "Installing deps via zypper: ${pkgs[*]}"
      if need_sudo; then sudo zypper -n install "${pkgs[@]}"; else zypper -n install "${pkgs[@]}"; fi
      ;;
    apk)
      say "Installing deps via apk: ${pkgs[*]}"
      if need_sudo; then sudo apk add --no-cache "${pkgs[@]}"; else apk add --no-cache "${pkgs[@]}"; fi
      ;;
    xbps)
      say "Installing deps via xbps-install: ${pkgs[*]}"
      if need_sudo; then sudo xbps-install -Sy "${pkgs[@]}"; else xbps-install -Sy "${pkgs[@]}"; fi
      ;;
    emerge)
      say "Installing deps via emerge: ${pkgs[*]}"
      if need_sudo; then sudo emerge -q "${pkgs[@]}"; else emerge -q "${pkgs[@]}"; fi
      ;;
    brew)
      if ! is_cmd brew; then
        err "Homebrew not found. Install from https://brew.sh/ and rerun."
        exit 1
      fi
      say "Installing deps via brew: ${pkgs[*]}"
      brew install "${pkgs[@]}"
      ;;
    *)
      warn "Unknown/unsupported package manager. Please install deps manually."
      return 1
      ;;
  esac
}

# -------- detect & install missing dependencies --------
MISSING_PKGS=()
MISSING_TOOLS=()

# Toolchain + cmake + pkg-config + ninja(optional)
# Map packages per distro
case "$PM" in
  apt)
    # Ubuntu/Debian
    TOOLCHAIN_PKGS=( build-essential )
    BASE_PKGS=( cmake pkg-config )
    NINJA_PKG=ninja-build
    READLINE_PKGS=( libreadline-dev libncurses-dev )
    ;;
  pacman)
    # Arch/Manjaro
    TOOLCHAIN_PKGS=( base-devel )
    BASE_PKGS=( cmake pkgconf )
    NINJA_PKG=ninja
    READLINE_PKGS=( readline ncurses )
    ;;
  dnf)
    # Fedora/RHEL
    TOOLCHAIN_PKGS=( @development-tools )
    BASE_PKGS=( cmake pkgconfig )
    NINJA_PKG=ninja-build
    READLINE_PKGS=( readline-devel ncurses-devel )
    ;;
  zypper)
    # openSUSE
    TOOLCHAIN_PKGS=( gcc-c++ make )
    BASE_PKGS=( cmake pkg-config )
    NINJA_PKG=ninja
    READLINE_PKGS=( readline-devel ncurses-devel )
    ;;
  apk)
    # Alpine
    TOOLCHAIN_PKGS=( build-base )
    BASE_PKGS=( cmake pkgconf )
    NINJA_PKG=ninja
    READLINE_PKGS=( readline-dev ncurses-dev bash )
    ;;
  xbps)
    # Void
    TOOLCHAIN_PKGS=( base-devel )
    BASE_PKGS=( cmake pkg-config )
    NINJA_PKG=ninja
    READLINE_PKGS=( readline-devel ncurses-devel )
    ;;
  emerge)
    # Gentoo (names approximate)
    TOOLCHAIN_PKGS=( sys-devel/gcc sys-devel/make )
    BASE_PKGS=( dev-util/cmake virtual/pkgconfig )
    NINJA_PKG=dev-util/ninja
    READLINE_PKGS=( sys-libs/readline sys-libs/ncurses )
    ;;
  brew)
    # macOS (Homebrew)
    TOOLCHAIN_PKGS=( ) # Xcode CLT provides compilers (xcode-select --install)
    BASE_PKGS=( cmake pkg-config )
    NINJA_PKG=ninja
    READLINE_PKGS=( readline )
    ;;
  *)
    TOOLCHAIN_PKGS=()
    BASE_PKGS=()
    NINJA_PKG=""
    READLINE_PKGS=()
    ;;
esac

# Check commands first
for c in "${REQUIRED_CMDS[@]}"; do
  if ! is_cmd "$c"; then MISSING_TOOLS+=("$c"); fi
done
# Optional but preferred
for c in "${OPTIONAL_CMDS[@]}"; do
  if ! is_cmd "$c"; then MISSING_TOOLS+=("$c"); fi
done

# Try pkg-config for readline if user wants readline
NEED_READLINE=false
if [[ "${WITH_READLINE}" == "ON" ]]; then
  if is_cmd pkg-config; then
    if ! pkg-config --exists readline; then
      NEED_READLINE=true
    fi
  else
    # pkg-config missing: we’ll install it and readline dev
    MISSING_TOOLS+=("pkg-config")
    NEED_READLINE=true
  fi
fi

# Build a package request list
if [[ -n "${PM}" ]]; then
  MISSING_PKGS+=( "${TOOLCHAIN_PKGS[@]}" )
  MISSING_PKGS+=( "${BASE_PKGS[@]}" )
  if [[ " ${MISSING_TOOLS[*]} " == *" ninja "* ]]; then
    [[ -n "${NINJA_PKG}" ]] && MISSING_PKGS+=( "${NINJA_PKG}" )
  fi
  if [[ "${WITH_READLINE}" == "ON" && "${NEED_READLINE}" == "true" ]]; then
    MISSING_PKGS+=( "${READLINE_PKGS[@]}" )
  fi
  # Install
  if ((${#MISSING_PKGS[@]})) || ((${#MISSING_TOOLS[@]})); then
    say "Ensuring dependencies…"
    install_pkgs "${MISSING_PKGS[@]}" || warn "Auto-install may have failed. Continuing…"
  fi
else
  warn "Could not detect a package manager. Please ensure you have: compiler, cmake, ninja, pkg-config, readline-dev, ncurses-dev."
fi

# Re-evaluate generator after possible installation of ninja
if [[ -z "${GENERATOR}" ]] && is_cmd ninja; then
  GENERATOR="-G Ninja"
fi

# macOS: help CMake find Homebrew readline (headers/libs not in default path)
if [[ "${PM}" == "brew" && "${WITH_READLINE}" == "ON" ]]; then
  if is_cmd brew && brew --prefix >/dev/null 2>&1; then
    BREW_PREFIX="$(brew --prefix)"
    if [[ -d "${BREW_PREFIX}/opt/readline" ]]; then
      export PKG_CONFIG_PATH="${BREW_PREFIX}/opt/readline/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
      export CPPFLAGS="-I${BREW_PREFIX}/opt/readline/include ${CPPFLAGS:-}"
      export LDFLAGS="-L${BREW_PREFIX}/opt/readline/lib ${LDFLAGS:-}"
    fi
  fi
fi

# -------- summary --------
echo "Config:"
echo "  Build Type        : ${BUILD_TYPE}"
echo "  Generator         : ${GENERATOR:-(default)}"
echo "  Readline          : ${WITH_READLINE}"
echo "  Sanitizers        : ${ENABLE_SANITIZERS}"
echo "  Parallel Jobs     : ${JOBS}"

# -------- configure --------
set -x
cmake -S . -B "$BUILD_DIR" \
  ${GENERATOR} \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DWITH_READLINE="${WITH_READLINE}" \
  -DENABLE_SANITIZERS="${ENABLE_SANITIZERS}"
set +x

# -------- build --------
set -x
cmake --build "$BUILD_DIR" -j "${JOBS}"
set +x

# -------- done --------
BIN="${BUILD_DIR}/minishell"
if [[ -x "$BIN" ]]; then
  say "Build complete: $BIN"
else
  err "Build finished but binary not found at $BIN"
fi

if $RUN_AFTER && [[ -x "$BIN" ]]; then
  say "Running ${BIN}"
  exec "$BIN"
fi
