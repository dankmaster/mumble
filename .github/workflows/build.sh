#!/usr/bin/env bash

set -Eeuo pipefail
set -x

trap 'exit_code=$?; echo "::error file=.github/workflows/build.sh,line=${LINENO},title=CI build failed::Command \"${BASH_COMMAND}\" exited with status ${exit_code}"; exit "${exit_code}"' ERR

os=$1
build_type=$2
arch=$3
phase="${MUMBLE_CI_PHASE:-all}"

# Turn variables into lowercase
os="${os,,}"
# only consider name up to the hyphen
os=$(echo "$os" | sed 's/-.*//')
build_type="${build_type,,}"
arch="${arch,,}"


OS_SPECIFIC_CMAKE_OPTIONS=""

case "$os" in
	"ubuntu")
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-sqlite-tests=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-mysql-tests=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-postgresql-tests=ON"
		;;
	"windows")
		if ! [[ "$arch" = "x86_64" ]]; then
			echo "Unsupported architecture '$arch'"
			exit 1
		fi

		eval "$( "C:/vcvars-bash/vcvarsall.sh" x64 )"

		PATH="$PATH:/C/WixSharp"
		echo "PATH=$PATH" >> "$GITHUB_ENV"

		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -DCMAKE_C_COMPILER=cl"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -DCMAKE_CXX_COMPILER=cl"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-sqlite-tests=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-mysql-tests=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-postgresql-tests=OFF"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Dasio=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Dg15=ON"

		if [[ "${MUMBLE_ENABLE_WINDOWS_PACKAGING:-}" = "ON" ]]; then
			OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Dpackaging=ON"
		fi

		if [[ "${MUMBLE_SKIP_MSI_REBUILD:-}" = "ON" ]]; then
			OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Dskip-msi-rebuild=ON"
		fi

		if [[ -n "${MUMBLE_USE_ELEVATION:-}" ]]; then
			OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Delevation=ON"
		fi
		;;
	"macos")
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-sqlite-tests=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-mysql-tests=OFF"
		OS_SPECIFIC_CMAKE_OPTIONS="$OS_SPECIFIC_CMAKE_OPTIONS -Ddatabase-postgresql-tests=ON"
		OS_SPECIFIC_CMAKE_OPTIONS="-DCMAKE_OSX_ARCHITECTURES=$arch"
		;;
	*)
		echo "OS $os is not supported"
		exit 1
		;;
esac


buildDir="${GITHUB_WORKSPACE}/build"

mkdir -p "$buildDir"

cd "$buildDir"

case "$phase" in
	"all")
		run_configure="yes"
		run_build="yes"
		;;
	"configure")
		run_configure="yes"
		run_build="no"
		;;
	"build")
		run_configure="no"
		run_build="yes"
		;;
	*)
		echo "Unknown CI phase '$phase'"
		exit 1
		;;
esac

if [[ "$run_configure" == "yes" ]]; then
	echo "::notice title=CI phase::Running CMake configure"
	cmake -G Ninja \
		  -S "$GITHUB_WORKSPACE" \
		  -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
		  -DBUILD_NUMBER=$MUMBLE_BUILD_NUMBER \
		  $OS_SPECIFIC_CMAKE_OPTIONS \
		  $CMAKE_OPTIONS \
	      -DCMAKE_UNITY_BUILD=ON \
		  -Ddisplay-install-paths=ON \
		  $ADDITIONAL_CMAKE_OPTIONS \
		  $VCPKG_CMAKE_OPTIONS
fi

if [[ "$run_build" == "yes" ]]; then
	echo "::notice title=CI phase::Running CMake build"
	build_log="${RUNNER_TEMP:-/tmp}/mumble-build.log"
	rm -f "$build_log"

	set +e
	cmake --build . --config $BUILD_TYPE --verbose 2>&1 | tee "$build_log"
	build_status=${PIPESTATUS[0]}
	set -e

	if [[ "$build_status" -ne 0 ]]; then
		echo "::group::Build tool diagnostics"
		command -v cmake || true
		command -v ninja || true
		command -v cl || true
		command -v rc || true
		command -v mt || true
		command -v link || true
		command -v windeployqt || true
		command -v candle || true
		command -v light || true
		echo "::endgroup::"

		if [[ -f "$build_log" ]]; then
			echo "::group::Build log tail"
			tail -n 200 "$build_log" || true
			echo "::endgroup::"

			missing_command=$(grep -E 'command not found|is not recognized as an internal or external command|No such file or directory' "$build_log" | tail -n 1 || true)
			if [[ -n "$missing_command" ]]; then
				echo "::error file=.github/workflows/build.sh,title=Likely missing tool::${missing_command}"
			fi

			error_excerpt=$(grep -E '(^FAILED:|: error:| fatal error | error C[0-9]+:| fatal error C[0-9]+:|ninja: build stopped:)' "$build_log" | tail -n 20 || true)
			if [[ -n "$error_excerpt" ]]; then
				echo "::group::Build error excerpt"
				printf '%s\n' "$error_excerpt"
				echo "::endgroup::"

				if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
					{
						echo "### Build error excerpt"
						echo
						echo '```text'
						printf '%s\n' "$error_excerpt"
						echo '```'
					} >> "$GITHUB_STEP_SUMMARY"
				fi

				while IFS= read -r line; do
					[[ -z "$line" ]] && continue
					line=${line//'%'/'%25'}
					line=${line//$'\r'/'%0D'}
					line=${line//$'\n'/'%0A'}
					echo "::error file=.github/workflows/build.sh,title=Build log excerpt::${line}"
				done <<< "$error_excerpt"
			fi
		fi

		exit "$build_status"
	fi
fi
