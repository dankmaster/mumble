#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

usage() {
	cat <<'EOF'
Usage: scripts/run-local-ci.sh [os] [type] [arch] [phase]

Defaults:
  os    = ubuntu-24.04
  type  = shared
  arch  = x86_64
  phase = all

Examples:
  scripts/run-local-ci.sh
  scripts/run-local-ci.sh ubuntu-24.04 static x86_64 all
  INSTALL_DEPS=1 scripts/run-local-ci.sh ubuntu-24.04 shared x86_64 all

Environment overrides:
  BUILD_DIR            Build directory. Default: <repo>/build-local-<os>-<type>-<arch>
  BUILD_TYPE           CMake build type. Default: Release
  CMAKE_OPTIONS        Extra top-level CMake options. Default mirrors GitHub Actions.
  MUMBLE_BUILD_NUMBER  Build number passed to CMake. Default: 0
  INSTALL_DEPS         If set to 1, run the same install-dependencies action as CI.
  RUN_TESTS            If set to 0, skip ctest even when phase is all.
  JOBS                 Parallelism for cmake --build. Default: nproc or 4

Notes:
  - This script mirrors the Linux/macOS configure/build/test flow from .github/workflows/build.yml.
  - On Linux it will automatically use vendored local deps under .local-deps/ when present.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
	usage
	exit 0
fi

os="${1:-ubuntu-24.04}"
dep_type="${2:-shared}"
arch="${3:-x86_64}"
phase="${4:-all}"

os_normalized="${os,,}"
os_normalized="$(sed 's/-.*//' <<< "$os_normalized")"
dep_type="${dep_type,,}"
arch="${arch,,}"
phase="${phase,,}"

build_dir="${BUILD_DIR:-$repo_root/build-local-${os}-${dep_type}-${arch}}"
build_type="${BUILD_TYPE:-Release}"
install_deps="${INSTALL_DEPS:-0}"
run_tests="${RUN_TESTS:-1}"
jobs="${JOBS:-$(command -v nproc >/dev/null 2>&1 && nproc || echo 4)}"

export GITHUB_WORKSPACE="$repo_root"
export RUNNER_TEMP="${RUNNER_TEMP:-$repo_root/.tmp}"
mkdir -p "$RUNNER_TEMP"

github_env="$(mktemp)"
trap 'rm -f "$github_env"' EXIT
export GITHUB_ENV="$github_env"

export BUILD_TYPE="$build_type"
export MUMBLE_BUILD_NUMBER="${MUMBLE_BUILD_NUMBER:-0}"
export CMAKE_OPTIONS="${CMAKE_OPTIONS:--Dtests=ON -Dsymbols=ON -Ddisplay-install-paths=ON -Dtest-lto=OFF}"

audio_dep_root="$repo_root/.local-deps/audio/usr"
qt_svg_dep_root="$repo_root/.local-deps/qt6-svg/usr"

if [[ -d "$audio_dep_root" ]]; then
	export PKG_CONFIG_PATH="$audio_dep_root/lib/x86_64-linux-gnu/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
	export CMAKE_INCLUDE_PATH="$audio_dep_root/include:$audio_dep_root/include/x86_64-linux-gnu${CMAKE_INCLUDE_PATH:+:$CMAKE_INCLUDE_PATH}"
	export CMAKE_LIBRARY_PATH="$audio_dep_root/lib/x86_64-linux-gnu${CMAKE_LIBRARY_PATH:+:$CMAKE_LIBRARY_PATH}"
	export LD_LIBRARY_PATH="$audio_dep_root/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

if [[ -d "$qt_svg_dep_root" ]]; then
	export CMAKE_PREFIX_PATH="$qt_svg_dep_root${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
	export LD_LIBRARY_PATH="$qt_svg_dep_root/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

"$repo_root/.github/workflows/set_environment_variables.sh" "$os" "$dep_type" "$arch" "$repo_root"

while IFS= read -r line || [[ -n "$line" ]]; do
	[[ -z "$line" ]] && continue
	export "$line"
done < "$github_env"

if [[ "$install_deps" == "1" ]]; then
	"$repo_root/.github/actions/install-dependencies/main.sh" "$os" "$dep_type" "$arch"
fi

os_specific_cmake_options=""
case "$os_normalized" in
	ubuntu)
		os_specific_cmake_options+=" -Ddatabase-sqlite-tests=ON"
		os_specific_cmake_options+=" -Ddatabase-mysql-tests=ON"
		os_specific_cmake_options+=" -Ddatabase-postgresql-tests=ON"
		;;
	macos)
		os_specific_cmake_options+=" -Ddatabase-sqlite-tests=ON"
		os_specific_cmake_options+=" -Ddatabase-mysql-tests=OFF"
		os_specific_cmake_options+=" -Ddatabase-postgresql-tests=ON"
		os_specific_cmake_options+=" -DCMAKE_OSX_ARCHITECTURES=$arch"
		;;
	windows)
		echo "Local wrapper currently expects to run on a Linux/macOS shell host." >&2
		echo "Use .github/workflows/build.sh directly from a Windows runner for that matrix leg." >&2
		exit 1
		;;
	*)
		echo "Unsupported OS '$os'" >&2
		exit 1
		;;
esac

run_configure=0
run_build=0
run_ctest=0
case "$phase" in
	all)
		run_configure=1
		run_build=1
		if [[ "$run_tests" != "0" ]]; then
			run_ctest=1
		fi
		;;
	configure)
		run_configure=1
		;;
	build)
		run_build=1
		;;
	test)
		run_ctest=1
		;;
	*)
		echo "Unknown phase '$phase'" >&2
		exit 1
		;;
esac

mkdir -p "$build_dir"

if [[ "$run_configure" == "1" ]]; then
	echo "==> Configuring $os/$dep_type/$arch in $build_dir"
	cmake -G Ninja \
		-S "$repo_root" \
		-B "$build_dir" \
		-DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
		-DBUILD_NUMBER="$MUMBLE_BUILD_NUMBER" \
		$os_specific_cmake_options \
		$CMAKE_OPTIONS \
		-DCMAKE_UNITY_BUILD=ON \
		-Ddisplay-install-paths=ON \
		${ADDITIONAL_CMAKE_OPTIONS:-} \
		${VCPKG_CMAKE_OPTIONS:-}
fi

if [[ "$run_build" == "1" ]]; then
	echo "==> Building $os/$dep_type/$arch"
	cmake --build "$build_dir" --config "$BUILD_TYPE" --verbose -j"$jobs"
fi

if [[ "$run_ctest" == "1" ]]; then
	echo "==> Running tests"
	ctest --test-dir "$build_dir" --output-on-failure -C "$BUILD_TYPE"
fi
