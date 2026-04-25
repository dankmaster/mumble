#!/usr/bin/env bash

set -e
set -x

source "$( dirname "$0" )/common.sh"

verify_required_env_variables_set

shared_environment_has_webengine_webchannel_support() {
	local webengine_targets_file="$1"

	python - "$webengine_targets_file" <<'PY'
import re
import sys
from pathlib import Path

targets_file = Path(sys.argv[1])
text = targets_file.read_text(encoding="utf-8")

enabled_match = re.search(r'QT_ENABLED_PUBLIC_FEATURES\s+"([^"]*)"', text)
disabled_match = re.search(r'QT_DISABLED_PUBLIC_FEATURES\s+"([^"]*)"', text)

if enabled_match is None or disabled_match is None:
    raise SystemExit(1)

enabled_features = {feature for feature in enabled_match.group(1).split(";") if feature}
disabled_features = {feature for feature in disabled_match.group(1).split(";") if feature}

raise SystemExit(
    0
    if "webengine_webchannel" in enabled_features and "webengine_webchannel" not in disabled_features
    else 1
)
PY
}

shared_environment_has_webengine_runtime() {
	local triplet_dir="$MUMBLE_ENVIRONMENT_DIR/installed/x64-windows"
	local webengine_targets_file="$triplet_dir/share/Qt6WebEngineCore/Qt6WebEngineCoreTargets.cmake"

	[[ -f "$MUMBLE_ENVIRONMENT_DIR/vcpkg.exe" ]] \
		&& [[ -f "$MUMBLE_ENVIRONMENT_DIR/scripts/buildsystems/vcpkg.cmake" ]] \
		&& [[ -d "$triplet_dir/share/Qt6WebChannel" ]] \
		&& [[ -d "$triplet_dir/share/Qt6WebEngineWidgets" ]] \
		&& [[ -f "$webengine_targets_file" ]] \
		&& shared_environment_has_webengine_webchannel_support "$webengine_targets_file" \
		&& [[ -f "$triplet_dir/tools/Qt6/bin/windeployqt.exe" ]]
}

if have_archive_extractor; then
	archive_url="$MUMBLE_ENVIRONMENT_SOURCE/$MUMBLE_ENVIRONMENT_VERSION.7z"
	split_archive_url="$archive_url.001"
	if remote_file_exists "$archive_url" || remote_file_exists "$split_archive_url"; then
		make_build_env_available "7z"
	fi
fi

if ! shared_environment_has_webengine_runtime; then
	ensure_build_env_repo_checkout
	ensure_vcpkg_bootstrapped
	install_mumble_vcpkg_dependencies "x64-windows"
fi

if ! environment_has_triplet "x86-windows"; then
	ensure_build_env_repo_checkout
	ensure_vcpkg_bootstrapped
	"$MUMBLE_ENVIRONMENT_DIR/vcpkg.exe" install --triplet "x86-windows" boost
fi

rm -rf "${GITHUB_WORKSPACE}/3rdparty/asio"
download_file "https://dl.mumble.info/build/extra/asio_sdk.zip" "asio_sdk.zip"
extract_with_progress "asio_sdk.zip" "${GITHUB_WORKSPACE}/3rdparty/asio"

rm -rf "${GITHUB_WORKSPACE}/3rdparty/g15"
rm -rf "g15_sdk"
download_file "https://dl.mumble.info/build/extra/g15_sdk.zip" "g15_sdk.zip"
extract_with_progress "g15_sdk.zip" "g15_sdk"
mv "g15_sdk/LCDSDK" "${GITHUB_WORKSPACE}/3rdparty/g15"
rm -rf "g15_sdk"

rm -rf "C:/WixSharp"
download_file "https://github.com/oleg-shilo/wixsharp/releases/download/v1.19.0.0/WixSharp.1.19.0.0.7z" "WixSharp.7z"
extract_with_progress "WixSharp.7z" "C:/WixSharp"

if [[ ! -d "C:/vcvars-bash/.git" ]]; then
	rm -rf "C:/vcvars-bash"
	git_clone_with_retry "https://github.com/nathan818fr/vcvars-bash.git" "C:/vcvars-bash"
fi

if [[ "${MUMBLE_SKIP_DATABASE_SETUP:-}" = "ON" ]]; then
	echo "Skipping local database setup for Windows dependencies"
	exit 0
fi

echo -e "[mysqld]\nlog-bin-trust-function-creators = 1" >> "C:/Windows/my.ini"

mysqld --initialize-insecure --console

powershell -Command "Start-Process mysqld"

sleep 5

configure_database_tables "mysql"
