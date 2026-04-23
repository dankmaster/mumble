#!/usr/bin/env bash

set -e
set -x

source "$( dirname "$0" )/common.sh"

verify_required_env_variables_set

if ! have_archive_extractor; then
	echo "A local archive extractor is required for the Windows dependency bootstrap (7z or tar)." 1>&2
	exit 1
fi

make_build_env_available "7z"

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

# Configure database tables for Mumble tests
echo -e "[mysqld]\nlog-bin-trust-function-creators = 1" >> "C:/Windows/my.ini"

mysqld --initialize-insecure --console

powershell -Command "Start-Process mysqld"

# Give the MySQL daemon some time to start up
sleep 5

configure_database_tables "mysql"
