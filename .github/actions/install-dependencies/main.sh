#!/usr/bin/env bash

os="$1"
dep_type="$2"
arch="$3"

if [[ "$os" == "" || "$dep_type" == "" || "$arch" == "" ]]; then
	echo "Invalid/Missing parameters"
	exit 1
fi

# Turn variables into lowercase
os="${os,,}"
# only consider name up to the hyphen
os=$(sed 's/-.*//' <<< "$os")
# transform to lowercase
dep_type="${dep_type,,}"
arch="${arch,,}"

echo "Installing dependencies for $os ($dep_type) - $arch"

script_dir=$(dirname "$0")
script_name="install_${os}_${dep_type}_${arch}.sh"
script_path="$script_dir/$script_name"

if [[ ! -f "$script_path" ]]; then
	echo "No dependency installer is defined for $os ($dep_type) - $arch" 1>&2
	exit 1
fi

# Execute respective script
"$script_path"
