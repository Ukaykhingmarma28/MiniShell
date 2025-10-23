#!/usr/bin/env bash
set -euo pipefail

# ---------------------------
# minishell build helper
# ---------------------------
# Usage:
#   ./build.sh                     # Debug build (default), auto -j
#   ./build.sh -R                  # Release build
#   ./build.sh -t release          # same as -R
#   ./build.sh --no-readline       # build without readline
#   ./build.sh --no-sanitizers     # disable ASan/UBSan (Debug)
#   ./build.sh -j 8                # set jobs=8
#   ./build.sh --run               # run ./build/minishell after build
#   ./build.sh --clean             # rm -rf build
#
# Env overrides:
#   CXX=clang++ ./build.sh -R
#   CC=clang ./build.sh -R

# -------- defaults --------
BUILD_TYPE="Debug"
WITH_READLINE=ON
ENABLE_SANITIZERS=ON
RUN_AFTER=false
CLEAN=false
JOBS=""
GENERATOR=""
BUILD_DIR="build"

# Detect CPU count for default -j
detect_jobs() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
  elif [[ "$OSTYPE" == darwin* ]]; then
    sysctl -n hw.ncpu
  else
    echo 4
  fi
}

# Pick Ninja if available
if command -v ninja >/dev/null 2>&1; then
  GENERATOR="-G Ninja"
fi

# -------- parse args --------
while (( $# )); do
  case "$1" in
    -t|--type)
      BUILD_TYPE="${2:-}"; shift ;;
    -R|--release)
      BUILD_TYPE="Release" ;;
    -j)
      JOBS="${2:-}"; shift ;;
    --jobs=*)
      JOBS="${1#*=}" ;;
    --run)
      RUN_AFTER=true ;;
    --clean)
      CLEAN=true ;;
    --no-readline)
      WITH_READLINE=OFF ;;
    --no-sanitizers)
      ENABLE_SANITIZERS=OFF ;;
    -h|--help)
      sed -n '1,80p' "$0"; exit 0 ;;
    *)
      echo "Unknown option: $1" >&2; exit 1 ;;
  esac
  shift
done

# -------- actions --------
if $CLEAN; then
  echo "Removing ${BUILD_DIR}/"
  rm -rf "$BUILD_DIR"
fi

# Create build dir if missing
if [[ ! -d "$BUILD_DIR" ]]; then
  echo "Creating ${BUILD_DIR}/"
  mkdir -p "$BUILD_DIR"
fi

# Resolve jobs
if [[ -z "${JOBS}" ]]; then
  JOBS="$(detect_jobs)"
fi

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
  echo "Build complete: $BIN"
else
  echo "Build finished but binary not found at $BIN" >&2
fi

if $RUN_AFTER && [[ -x "$BIN" ]]; then
  echo "Running ${BIN}"
  exec "$BIN"
fi
