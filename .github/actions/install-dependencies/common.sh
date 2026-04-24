# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set -e
set -E
set -x

trap 'exit_code=$?; echo "::error file=.github/actions/install-dependencies/common.sh,line=${LINENO},title=Dependency install failed::Command \"${BASH_COMMAND}\" exited with status ${exit_code}" 1>&2; exit "${exit_code}"' ERR

get_vcpkg_executable_path() {
	local env_dir="$1"

	if [[ -z "$env_dir" ]]; then
		echo "get_vcpkg_executable_path requires an environment directory" 1>&2
		return 1
	fi

	if [[ -f "$env_dir/vcpkg.exe" ]]; then
		echo "$env_dir/vcpkg.exe"
		return 0
	fi

	if [[ -f "$env_dir/vcpkg" ]]; then
		echo "$env_dir/vcpkg"
		return 0
	fi

	return 1
}

verify_required_env_variables_set() {
	if [[ -z "$MUMBLE_ENVIRONMENT_SOURCE" ]]; then
		echo "MUMBLE_ENVIRONMENT_SOURCE not set!" 1>&2
		exit 1
	fi

	if [[ -z "${MUMBLE_ENVIRONMENT_REPOSITORY:-}" ]]; then
		echo "MUMBLE_ENVIRONMENT_REPOSITORY not set!" 1>&2
		exit 1
	fi

	if [[ -z "${MUMBLE_ENVIRONMENT_COMMIT:-}" ]]; then
		echo "MUMBLE_ENVIRONMENT_COMMIT not set!" 1>&2
		exit 1
	fi

	if [[ -z "$MUMBLE_ENVIRONMENT_VERSION" ]]; then
		echo "MUMBLE_ENVIRONMENT_VERSION not set!" 1>&2
		exit 1
	fi

	if [[ -z "$MUMBLE_ENVIRONMENT_DIR" ]]; then
		echo "MUMBLE_ENVIRONMENT_DIR not set!" 1>&2
		exit 1
	fi
}

is_environment_ready() {
	local env_dir="$1"
	local vcpkg_executable

	vcpkg_executable="$( get_vcpkg_executable_path "$env_dir" 2> /dev/null )" || return 1

	[[ -f "$vcpkg_executable" ]] \
		&& [[ -f "$env_dir/scripts/buildsystems/vcpkg.cmake" ]] \
		&& [[ -d "$env_dir/installed/$MUMBLE_VCPKG_TRIPLET" ]]
}

remote_file_exists() {
	local url="$1"

	if [[ -z "$url" ]]; then
		echo "remote_file_exists requires a URL" 1>&2
		exit 1
	fi

	if command -v curl > /dev/null 2>&1; then
		curl -I -f -L "$url" > /dev/null
	elif command -v aria2c > /dev/null 2>&1; then
		aria2c --dry-run "$url" > /dev/null
	else
		return 1
	fi
}

download_file() {
	local url="$1"
	local output_file="$2"

	if [[ -z "$url" || -z "$output_file" ]]; then
		echo "download_file requires a URL and output file" 1>&2
		exit 1
	fi

	if [[ -s "$output_file" ]]; then
		echo "Reusing existing archive: $output_file"
		return
	fi

	if command -v aria2c > /dev/null 2>&1; then
		aria2c "$url" --out "$output_file"
	elif command -v curl > /dev/null 2>&1; then
		curl -L --fail --output "$output_file" "$url"
	else
		echo "Neither aria2c nor curl is available for downloading dependencies" 1>&2
		exit 1
	fi
}

git_clone_with_retry() {
	local repository_url="$1"
	local target_dir="$2"
	shift 2

	if [[ -z "$repository_url" || -z "$target_dir" ]]; then
		echo "git_clone_with_retry requires a repository URL and target directory" 1>&2
		exit 1
	fi

	local max_attempts="${GIT_CLONE_MAX_ATTEMPTS:-3}"
	local retry_delay_seconds="${GIT_CLONE_RETRY_DELAY_SECONDS:-15}"
	local attempt

	for (( attempt=1; attempt<=max_attempts; attempt++ )); do
		remove_tree_force "$target_dir"

		if git -c protocol.version=2 -c http.version=HTTP/1.1 clone "$@" "$repository_url" "$target_dir"; then
			return 0
		fi

		if (( attempt == max_attempts )); then
			echo "Failed to clone $repository_url after ${max_attempts} attempts" 1>&2
			return 1
		fi

		echo "Retrying clone of $repository_url in ${retry_delay_seconds}s (attempt ${attempt}/${max_attempts})" 1>&2
		sleep "$retry_delay_seconds"
	done
}

have_archive_extractor() {
	command -v 7z > /dev/null 2>&1 || command -v tar > /dev/null 2>&1
}

extract_with_progress() {
	local fromFile="$1"
	local targetDir="$2"

	if [ -z "$fromFile" ]; then
		echo "[ERROR]: Missing argument"
		exit 1
	fi

	if [ -z "$targetDir" ]; then
		targetDir="."
	fi

	echo "Extracting from \"$fromFile\" to \"$targetDir\""
	echo ""

	# Make targetDir an absolute path
	targetDir="$( realpath "$targetDir" )"

	# Use gtar and gwc if available (for MacOS compatibility)
	tar_exe="tar"
	if [[ -x "/c/Windows/System32/tar.exe" ]]; then
		tar_exe="/c/Windows/System32/tar.exe"
	elif [ -x "$(command -v tar.exe)" ]; then
		tar_exe="tar.exe"
	elif [ -x "$(command -v gtar)" ]; then
		tar_exe="gtar"
	fi
	wc_exe="wc"
	if [ -x "$(command -v gwc)" ]; then
		wc_exe="gwc"
	fi

	tmp_dir="__extract_root__"
	rm -rf "$tmp_dir"
	mkdir "$tmp_dir"

	if [[ "$fromFile" = *.7z || "$fromFile"  = *.zip ]]; then
		if command -v 7z > /dev/null 2>&1; then
			extract_cmd=( 7z x "$fromFile" -o"$tmp_dir" )

			summary="$( 7z l "$fromFile" | tail -n 1 )"
			fromSize="$( echo "$summary" | tr -s ' ' | cut -d ' ' -f 4 )"
			toSize="$( echo "$summary" | tr -s ' ' | cut -d ' ' -f 3 )"
		elif command -v tar > /dev/null 2>&1; then
			extract_cmd=( "$tar_exe" -xf "$fromFile" --directory "$tmp_dir" )

			fromSize="$( stat -c '%s' "$fromFile" 2> /dev/null || echo 0 )"
			toSize="$fromSize"
		else
			echo "Neither 7z nor tar is available for extracting $fromFile" 1>&2
			exit 1
		fi
	else
		# Get sizes in bytes
		fromSize=$(xz --robot --list "$fromFile" | tail -n -1 | cut -f 4)
		toSize=$(xz --robot --list "$fromFile" | tail -n -1 | cut -f 5)

		steps=100
		checkPointStep=$(expr "$toSize" / 1000 / "$steps" )

		extract_cmd=( "$tar_exe" -x --record-size=1K --checkpoint="$checkPointStep" --checkpoint-action="echo=%u / $toSize" --file "$fromFile" --directory "$tmp_dir" )
	fi

	# Convert sizes to KB
	local toSizeKB=$(expr "$toSize" / 1000)
	local fromSizeKB=$(expr "$fromSize" / 1000)

	echo "Compressed size:   $fromSizeKB KB"
	echo "Uncompressed size: $toSizeKB KB"


	echo ""

	"${extract_cmd[@]}"

	local num_files="$( ls -Al "$tmp_dir" | tail -n +2 | $wc_exe -l )"

	if [[ ! -d "$targetDir" ]]; then
		mkdir "$targetDir"
	fi

	if [[ "$num_files" = 1 && -d "$tmp_dir/$( ls "$tmp_dir" )" ]]; then
		# Skip top-level directory
		pushd "$(pwd)"
		cd "$tmp_dir"/*
		mv * "$targetDir"
		mv .* "$targetDir" 2> /dev/null || true
		popd
	else
		# Move all files
		mv "$tmp_dir"/* "$targetDir"
		mv "$tmp_dir"/.* "$targetDir" 2> /dev/null || true
	fi

	rm -rf "$tmp_dir"
}

make_build_env_available() {
	local env_file_extension="$1"

	if [[ -z "$env_file_extension" ]]; then
		echo "No file extension provided" 1>&2
		exit 1
	fi

	local env_dir="$MUMBLE_ENVIRONMENT_DIR"

	if is_environment_ready "$env_dir"; then
		echo "Environment is cached"
	else
		echo "Environment not cached -> preparing now"

		local env_archive="$MUMBLE_ENVIRONMENT_VERSION.$env_file_extension"
		local env_archive_url="$MUMBLE_ENVIRONMENT_SOURCE/$env_archive"

		if command -v curl > /dev/null 2>&1 && curl -I -f -L "$env_archive_url" > /dev/null 2>&1; then
			download_file "$env_archive_url" "$env_archive"

			echo "Extracting archive..."
			if [[ ! -d "$env_dir" ]]; then
				mkdir -p "$env_dir"
			fi

			extract_with_progress "$env_archive" "$env_dir"
		elif [[ "${MUMBLE_ALLOW_ENVIRONMENT_BOOTSTRAP:-}" = "ON" ]]; then
			echo "Environment archive is missing; falling back to local bootstrap."
			ensure_build_env_repo_checkout
			ensure_vcpkg_bootstrapped
			install_mumble_vcpkg_dependencies "$MUMBLE_VCPKG_TRIPLET"
		else
			echo "Environment archive is missing and local bootstrap is disabled: $env_archive_url" 1>&2
			exit 1
		fi

		if ! is_environment_ready "$env_dir"; then
			echo "Environment did not follow expected form" 1>&2
			ls -al "$env_dir"
			exit 1
		fi

		if [[ -f "$env_dir/installed/$MUMBLE_VCPKG_TRIPLET/tools/Ice/slice2cpp" ]]; then
			chmod +x "$env_dir/installed/$MUMBLE_VCPKG_TRIPLET/tools/Ice/slice2cpp"
		fi
	fi
}

environment_has_triplet() {
	local triplet="$1"

	if [[ -z "$triplet" ]]; then
		echo "environment_has_triplet requires a triplet" 1>&2
		exit 1
	fi

	local triplet_dir="$MUMBLE_ENVIRONMENT_DIR/installed/$triplet"
	[[ -d "$triplet_dir" && -n "$( ls -A "$triplet_dir" 2> /dev/null )" ]]
}

ensure_build_env_repo_checkout() {
	local env_dir="$MUMBLE_ENVIRONMENT_DIR"

	if [[ -d "$env_dir/ports" && -f "$env_dir/bootstrap-vcpkg.bat" ]]; then
		return
	fi

	local temp_dir="${env_dir}.repo"
	rm -rf "$temp_dir"
	mkdir -p "$env_dir"

	git_clone_with_retry "$MUMBLE_ENVIRONMENT_REPOSITORY" "$temp_dir" --filter=blob:none
	git -C "$temp_dir" checkout "$MUMBLE_ENVIRONMENT_COMMIT"
	rm -rf "$temp_dir/.git"

	shopt -s dotglob nullglob
	cp -a "$temp_dir"/* "$env_dir"/
	shopt -u dotglob nullglob

	rm -rf "$temp_dir"
}

ensure_vcpkg_bootstrapped() {
	local env_dir="$MUMBLE_ENVIRONMENT_DIR"

	if get_vcpkg_executable_path "$env_dir" > /dev/null 2>&1; then
		return
	fi

	if [[ "$OSTYPE" == msys* || "$OSTYPE" == cygwin* || -n "${WINDIR:-}" ]]; then
		if [[ ! -f "$env_dir/bootstrap-vcpkg.bat" ]]; then
			echo "bootstrap-vcpkg.bat not found in $env_dir" 1>&2
			exit 1
		fi

		local bootstrap_script_windows
		bootstrap_script_windows="$( cygpath -aw "$env_dir/bootstrap-vcpkg.bat" )"
		powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& \"$bootstrap_script_windows\" -disableMetrics"
	else
		if [[ ! -f "$env_dir/bootstrap-vcpkg.sh" ]]; then
			echo "bootstrap-vcpkg.sh not found in $env_dir" 1>&2
			exit 1
		fi

		chmod +x "$env_dir/bootstrap-vcpkg.sh"
		( cd "$env_dir" && ./bootstrap-vcpkg.sh -disableMetrics )
	fi

	if ! get_vcpkg_executable_path "$env_dir" > /dev/null 2>&1; then
		echo "vcpkg bootstrap did not produce a vcpkg executable in $env_dir" 1>&2
		exit 1
	fi
}

ensure_windows_mdnsresponder_port_patch() {
	local triplet="$1"

	if [[ "$triplet" != *windows* ]]; then
		return
	fi

	local port_dir="$MUMBLE_ENVIRONMENT_DIR/ports/mdnsresponder"
	local portfile="$port_dir/portfile.cmake"
	if [[ ! -f "$portfile" ]]; then
		return
	fi

	local script_dir
	script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

	local patch_source="$script_dir/patches/mdnsresponder-winres.patch"
	local patch_target="$port_dir/mumble-winres.patch"
	if [[ ! -f "$patch_source" ]]; then
		echo "Missing mdnsresponder patch payload: $patch_source" 1>&2
		exit 1
	fi

	cp "$patch_source" "$patch_target"

	if grep -q "mumble-winres.patch" "$portfile"; then
		return
	fi

	local temp_file="${portfile}.tmp"
	awk '
		{
			print
			if ($0 ~ /^[[:space:]]*HEAD_REF[[:space:]]+main[[:space:]]*$/) {
				print "    PATCHES"
				print "        mumble-winres.patch"
			}
		}
	' "$portfile" > "$temp_file"
	mv "$temp_file" "$portfile"
}

remove_tree_force() {
	local target="$1"

	if [[ -z "$target" || ! -e "$target" ]]; then
		return
	fi

	rm -rf "$target" 2> /dev/null || true
	if [[ ! -e "$target" ]]; then
		return
	fi

	if [[ "$target" == *:* || "$OSTYPE" == cygwin* || "$OSTYPE" == msys* ]]; then
		local windows_target
		windows_target="$( cygpath -aw "$target" )"
		if [[ -d "$target" ]]; then
			cmd.exe //d //c "if exist \"$windows_target\" rd /s /q \"$windows_target\"" || true
		else
			cmd.exe //d //c "if exist \"$windows_target\" del /f /q \"$windows_target\"" || true
		fi
	fi

	if [[ -e "$target" ]]; then
		echo "Failed to remove stale path: $target" 1>&2
		exit 1
	fi
}

get_vcpkg_buildtrees_root() {
	local triplet="${1:-}"

	if [[ "$triplet" == "x64-windows" ]]; then
		echo "$MUMBLE_ENVIRONMENT_DIR/bt"
	else
		echo "$MUMBLE_ENVIRONMENT_DIR/buildtrees"
	fi
}

qt_source_dir_missing_cmakelists() {
	local package_name="$1"
	local triplet="${2:-x64-windows}"
	local buildtree_dir="$( get_vcpkg_buildtrees_root "$triplet" )/$package_name"
	local source_root="$buildtree_dir/src"

	if [[ ! -d "$source_root" ]]; then
		return 1
	fi

	local clean_dir=""
	while IFS= read -r candidate; do
		clean_dir="$candidate"
		break
	done < <(
		find "$source_root" -maxdepth 1 -mindepth 1 -type d \( -name 'here-src-*.clean' -o -name 'here-src-*.clean.tmp' \) 2> /dev/null | sort
	)

	if [[ -z "$clean_dir" ]]; then
		return 1
	fi

	[[ ! -f "$clean_dir/CMakeLists.txt" ]]
}

clean_qt_package_state() {
	local package_name="$1"
	local triplet="$2"
	local buildtree_dir="$( get_vcpkg_buildtrees_root "$triplet" )/$package_name"
	local package_dir="$MUMBLE_ENVIRONMENT_DIR/packages/${package_name}_${triplet}"

	echo "Cleaning malformed Qt package state for $package_name ($triplet)"
	remove_tree_force "$buildtree_dir"
	remove_tree_force "$package_dir"
}

repair_malformed_qt_package_state() {
	local triplet="$1"
	local repaired=1

	for package_name in qtdeclarative qtwebchannel qtwebengine; do
		if qt_source_dir_missing_cmakelists "$package_name" "$triplet"; then
			clean_qt_package_state "$package_name" "$triplet"
			repaired=0
		fi
	done

	return "$repaired"
}

report_malformed_qt_package_state() {
	local triplet="$1"
	local found=1

	for package_name in qtdeclarative qtwebchannel qtwebengine; do
		if qt_source_dir_missing_cmakelists "$package_name" "$triplet"; then
			local buildtree_dir="$( get_vcpkg_buildtrees_root "$triplet" )/$package_name"
			local log_candidates=(
				"$buildtree_dir/error-logs-$triplet.txt"
				"$buildtree_dir/config-$triplet-out.log"
				"$buildtree_dir/stdout-$triplet.log"
			)
			local log_path=""
			for candidate in "${log_candidates[@]}"; do
				if [[ -f "$candidate" ]]; then
					log_path="$candidate"
					break
				fi
			done

			echo "Detected malformed Qt package source state for $package_name in $buildtree_dir/src (*.clean or *.clean.tmp missing CMakeLists.txt)." 1>&2
			if [[ -n "$log_path" ]]; then
				echo "Relevant log: $log_path" 1>&2
			fi
			found=0
		fi
	done

	return "$found"
}

report_qt_build_failure_logs() {
	local triplet="$1"
	local found=1
	local buildtrees_root="$( get_vcpkg_buildtrees_root "$triplet" )"

	for package_name in qtdeclarative qtwebchannel qtwebengine; do
		local package_root="$buildtrees_root/$package_name"
		local log_path=""
		local log_candidates=(
			"$package_root/error-logs-$triplet.txt"
			"$package_root/error-logs-$triplet-dbg.txt"
			"$package_root/config-$triplet-out.log"
			"$package_root/config-$triplet-dbg-out.log"
			"$package_root/install-$triplet-out.log"
			"$package_root/install-$triplet-dbg-out.log"
			"$package_root/stdout-$triplet.log"
			"$package_root/stdout-$triplet-dbg.log"
		)

		for candidate in "${log_candidates[@]}"; do
			if [[ -f "$candidate" ]]; then
				log_path="$candidate"
				break
			fi
		done

		if [[ -z "$log_path" || ! -f "$log_path" ]]; then
			continue
		fi

		found=0
		echo "::group::Qt dependency failure excerpt: $package_name"
		echo "Source log: $log_path"
		grep -E '(^FAILED:|: error:| fatal error | error C[0-9]+:| fatal error C[0-9]+:|LINK : fatal error LNK[0-9]+:|cl : Command line warning D9025|ninja: build stopped:)' "$log_path" | tail -n 40 || tail -n 80 "$log_path"
		echo "::endgroup::"
	done

	return "$found"
}

install_mumble_vcpkg_dependencies() {
	local triplet="$1"
	local dependency_file="$MUMBLE_ENVIRONMENT_DIR/mumble_dependencies.txt"
	local vcpkg_executable

	if [[ -z "$triplet" ]]; then
		echo "install_mumble_vcpkg_dependencies requires a triplet" 1>&2
		exit 1
	fi

	vcpkg_executable="$( get_vcpkg_executable_path "$MUMBLE_ENVIRONMENT_DIR" )" || {
		echo "Unable to find vcpkg executable under $MUMBLE_ENVIRONMENT_DIR" 1>&2
		exit 1
	}

	if [[ ! -f "$dependency_file" ]]; then
		echo "Missing dependency file: $dependency_file" 1>&2
		exit 1
	fi

	mapfile -t raw_dependencies < <( grep -vE '^[[:space:]]*(#|$)' "$dependency_file" )
	declare -A seen_dependencies=()
	dependencies=()

	for dependency in "${raw_dependencies[@]}"; do
		dependency="$( sed 's/[[:space:]]*#.*$//' <<< "$dependency" | xargs )"
		if [[ -z "$dependency" || -n "${seen_dependencies[$dependency]:-}" ]]; then
			continue
		fi

		dependencies+=( "$dependency" )
		seen_dependencies["$dependency"]=1
	done

	if [[ "$triplet" == *windows* && -z "${seen_dependencies[mdnsresponder]:-}" ]]; then
		dependencies+=( "mdnsresponder" )
		seen_dependencies["mdnsresponder"]=1
	fi

	for shared_dependency in qtwebchannel "qtwebengine[webengine,webchannel]"; do
		if [[ "$triplet" == "x64-windows" && -z "${seen_dependencies[$shared_dependency]:-}" ]]; then
			dependencies+=( "$shared_dependency" )
			seen_dependencies["$shared_dependency"]=1
		fi
	done

	ensure_windows_mdnsresponder_port_patch "$triplet"

	local retried_qt_state=0
	local -a vcpkg_install_args=( install --triplet "$triplet" )
	if [[ "$triplet" == "x64-windows" ]]; then
		local effective_vcpkg_max_concurrency="${MUMBLE_VCPKG_MAX_CONCURRENCY:-${VCPKG_MAX_CONCURRENCY:-}}"
		local effective_cmake_build_parallel_level="${MUMBLE_CMAKE_BUILD_PARALLEL_LEVEL:-${CMAKE_BUILD_PARALLEL_LEVEL:-}}"
		if [[ -z "$effective_vcpkg_max_concurrency" ]]; then
			effective_vcpkg_max_concurrency=2
		fi
		if [[ -z "$effective_cmake_build_parallel_level" ]]; then
			effective_cmake_build_parallel_level=2
		fi
		export VCPKG_MAX_CONCURRENCY="$effective_vcpkg_max_concurrency"
		export CMAKE_BUILD_PARALLEL_LEVEL="$effective_cmake_build_parallel_level"
		echo "Using shared Windows vcpkg bootstrap concurrency: VCPKG_MAX_CONCURRENCY=$VCPKG_MAX_CONCURRENCY, CMAKE_BUILD_PARALLEL_LEVEL=$CMAKE_BUILD_PARALLEL_LEVEL"

		local short_buildtrees_root="$( get_vcpkg_buildtrees_root "$triplet" )"
		mkdir -p "$short_buildtrees_root"
		vcpkg_install_args+=( --x-buildtrees-root="$short_buildtrees_root" )
	fi
	while true; do
		if [[ "$triplet" == "x64-windows" ]]; then
			repair_malformed_qt_package_state "$triplet" || true
		fi

		if "$vcpkg_executable" "${vcpkg_install_args[@]}" "${dependencies[@]}"; then
			return
		fi

		if [[ "$triplet" == "x64-windows" ]]; then
			report_qt_build_failure_logs "$triplet" || true
		fi

		if [[ "$triplet" == "x64-windows" && "$retried_qt_state" -eq 0 ]]; then
			if repair_malformed_qt_package_state "$triplet"; then
				echo "Detected malformed Qt package state after shared dependency failure; cleaned affected package state and retrying once."
				retried_qt_state=1
				continue
			fi
		fi

		if [[ "$triplet" == "x64-windows" ]]; then
			report_malformed_qt_package_state "$triplet" || true
		fi

		return 1
	done
}

configure_database_tables() {
	if [[ -x "$( which sudo )" ]]; then
		sudo_cmd="sudo"
	else
		sudo_cmd=""
	fi

	while [[ "$#" -gt 0 ]]; do
		case "$1" in
			"mysql")
				local sql_statements='CREATE DATABASE `mumble_test-db`;'
				sql_statements+="CREATE USER 'mumble_test-user'@'localhost' IDENTIFIED BY 'MumbleTestPassword';"
				sql_statements+="GRANT ALL PRIVILEGES ON \`mumble_test-db\`.* TO 'mumble_test-user'@'localhost';"

				if $sudo_cmd mysql --user=root -e "SELECT 1" 2> /dev/null; then
					# Passwordless
					mysql_cmd=( $sudo_cmd mysql --user=root )
				else
					mysql_cmd=( $sudo_cmd mysql --user=root --password="root" )
				fi

				echo "$sql_statements" | "${mysql_cmd[@]}"
				;;

			"postgresql")
				local sql_statements='CREATE DATABASE "mumble_test-db";'
				sql_statements+="CREATE USER \"mumble_test-user\" ENCRYPTED PASSWORD 'MumbleTestPassword';"
				sql_statements+='ALTER DATABASE "mumble_test-db" OWNER TO "mumble_test-user";'

				if [[ -n "$sudo_cmd" ]] && id -u postgres > /dev/null 1>&1; then
					# User postgres exists and we can use sudo to execute commands as that user
					psql_cmd=( "$sudo_cmd" -u postgres psql )
				else
					psql_cmd=( psql -d postgres )
				fi

				echo "$sql_statements" | "${psql_cmd[@]}"
				;;

			*)
				echo "Unsupported database '$1'" 1>&2
				exit 1
				;;
		esac

		shift
	done
}
