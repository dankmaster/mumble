#!/usr/bin/env bash

set -e
set -x

os=$1
build_type=$2
arch=$3
workspace=$4


if [[ "$os" == "" || "$build_type" == "" || "$arch" == "" || "$workspace" == "" ]]; then
	echo "Invalid parameters"
	exit 1
fi

# Turn variables into lowercase
os="${os,,}"
# only consider name up to the hyphen
os=$(echo "$os" | sed 's/-.*//')
build_type="${build_type,,}"
arch="${arch,,}"


MUMBLE_ENVIRONMENT_DIR="${MUMBLE_ENVIRONMENT_DIR_OVERRIDE:-$workspace/build_env}"
MUMBLE_ENVIRONMENT_SOURCE="${MUMBLE_ENVIRONMENT_SOURCE_OVERRIDE:-https://github.com/mumble-voip/vcpkg/releases/download/2026-02}"
MUMBLE_ENVIRONMENT_REPOSITORY="${MUMBLE_ENVIRONMENT_REPOSITORY_OVERRIDE:-https://github.com/mumble-voip/vcpkg.git}"
MUMBLE_ENVIRONMENT_COMMIT="${MUMBLE_ENVIRONMENT_COMMIT_OVERRIDE:-b1fe4a4257}"
MUMBLE_ENVIRONMENT_VERSION=""
ADDITIONAL_CMAKE_OPTIONS=""
VCPKG_CMAKE_OPTIONS=""
MUMBLE_BUNDLED_SPDLOG="${MUMBLE_BUNDLED_SPDLOG_OVERRIDE:-OFF}"
MUMBLE_BUNDLED_CLI11="${MUMBLE_BUNDLED_CLI11_OVERRIDE:-OFF}"

if [[ "$build_type" == "static" ]]; then
	ADDITIONAL_CMAKE_OPTIONS="$ADDITIONAL_CMAKE_OPTIONS -Dstatic=ON"
elif [[ "$build_type" == "shared" ]]; then
	ADDITIONAL_CMAKE_OPTIONS="$ADDITIONAL_CMAKE_OPTIONS -Dstatic=OFF"
else
	echo "Unknown build type '$build_type'"
	exit 1
fi

ADDITIONAL_CMAKE_OPTIONS="$ADDITIONAL_CMAKE_OPTIONS -Dbundled-spdlog=$MUMBLE_BUNDLED_SPDLOG -Dbundled-cli11=$MUMBLE_BUNDLED_CLI11"

VCPKG_TARGET_TRIPLET=""
if [[ "$arch" == "x86_64" ]]; then
	VCPKG_TARGET_TRIPLET="x64"
else
	echo "Unknown architecture '$arch'"
	exit 1
fi


case "$os" in
	"ubuntu")
		VCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET-linux"
		;;
	"windows")
		if [[ "$build_type" == "static" ]]; then
			VCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET-windows-static-md"
		elif [[ "$build_type" == "shared" ]]; then
			VCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET-windows"
			ADDITIONAL_CMAKE_OPTIONS="$ADDITIONAL_CMAKE_OPTIONS -Dmodern-layout-webengine=ON"
		fi
		;;
	"macos")
		VCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET-osx"
		;;
	*)
		echo "OS $os is not supported"
		exit 1
		;;
esac

MUMBLE_ENVIRONMENT_VERSION="mumble_env.${VCPKG_TARGET_TRIPLET}.${MUMBLE_ENVIRONMENT_COMMIT}"

if [[ "$os" == "windows" ]]; then
	VCPKG_CMAKE_OPTIONS="-DCMAKE_TOOLCHAIN_FILE='$MUMBLE_ENVIRONMENT_DIR/scripts/buildsystems/vcpkg.cmake'"
	VCPKG_CMAKE_OPTIONS="$VCPKG_CMAKE_OPTIONS -DVCPKG_TARGET_TRIPLET='$VCPKG_TARGET_TRIPLET'"
	VCPKG_CMAKE_OPTIONS="$VCPKG_CMAKE_OPTIONS -DIce_HOME='$MUMBLE_ENVIRONMENT_DIR/installed/$VCPKG_TARGET_TRIPLET'"
fi

# set environment variables in a way that GitHub Actions understands and preserves
echo "MUMBLE_ENVIRONMENT_SOURCE=$MUMBLE_ENVIRONMENT_SOURCE" >> "$GITHUB_ENV"
echo "MUMBLE_ENVIRONMENT_REPOSITORY=$MUMBLE_ENVIRONMENT_REPOSITORY" >> "$GITHUB_ENV"
echo "MUMBLE_ENVIRONMENT_COMMIT=$MUMBLE_ENVIRONMENT_COMMIT" >> "$GITHUB_ENV"
echo "MUMBLE_ENVIRONMENT_DIR=$MUMBLE_ENVIRONMENT_DIR" >> "$GITHUB_ENV"
echo "MUMBLE_ENVIRONMENT_VERSION=$MUMBLE_ENVIRONMENT_VERSION" >> "$GITHUB_ENV"
echo "ADDITIONAL_CMAKE_OPTIONS=$ADDITIONAL_CMAKE_OPTIONS" >> "$GITHUB_ENV"
echo "VCPKG_CMAKE_OPTIONS=$VCPKG_CMAKE_OPTIONS" >> "$GITHUB_ENV"
echo "MUMBLE_VCPKG_TRIPLET=$VCPKG_TARGET_TRIPLET" >> "$GITHUB_ENV"
echo "QT_DEBUG_PLUGINS=1" >> "$GITHUB_ENV"

if [[ "$os" = "ubuntu" ]]; then
	# Setting this is necessary in order to be able to run tests on the CLI
	echo "QT_QPA_PLATFORM=offscreen" >> "$GITHUB_ENV"
fi
