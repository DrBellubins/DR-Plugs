#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <source_folder> <destination_parent_or_folder>"
  exit 1
fi

src="$1"
dest="$2"

if [[ ! -d "$src" ]]; then
  echo "Source folder does not exist: $src"
  exit 1
fi

# If destination exists, remove it first
if [[ -e "$dest" ]]; then
  rm -rf -- "$dest"
fi

# Move source to destination
mv -- "$src" "$dest"