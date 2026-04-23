#!/usr/bin/env bash

set -Eeuo pipefail
set -x

max_attempts="${1:-3}"
retry_delay_seconds="${2:-15}"

if ! [[ "$max_attempts" =~ ^[0-9]+$ ]] || (( max_attempts < 1 )); then
	echo "max_attempts must be a positive integer (got '$max_attempts')" 1>&2
	exit 1
fi

if ! [[ "$retry_delay_seconds" =~ ^[0-9]+$ ]] || (( retry_delay_seconds < 0 )); then
	echo "retry_delay_seconds must be a non-negative integer (got '$retry_delay_seconds')" 1>&2
	exit 1
fi

git submodule sync --recursive

for (( attempt=1; attempt<=max_attempts; attempt++ )); do
	if git -c protocol.version=2 -c http.version=HTTP/1.1 submodule update --init --recursive --jobs 4; then
		git submodule status --recursive
		exit 0
	fi

	if (( attempt == max_attempts )); then
		echo "::error file=.github/workflows/update_submodules_with_retry.sh,title=Submodule fetch failed::Unable to initialize submodules after ${max_attempts} attempts."
		exit 1
	fi

	echo "::warning file=.github/workflows/update_submodules_with_retry.sh,title=Retrying submodule fetch::Attempt ${attempt}/${max_attempts} failed. Resetting partial submodule state and retrying in ${retry_delay_seconds}s."
	git submodule deinit --all --force || true

	modules_dir="$(git rev-parse --git-path modules)"
	if [[ -d "$modules_dir" ]]; then
		rm -rf "$modules_dir"
		mkdir -p "$modules_dir"
	fi

	sleep "$retry_delay_seconds"
	git submodule sync --recursive
done
