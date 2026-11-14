#!/bin/sh
# Fetch the Dinghy starter source without carrying over its Git history.
set -eu

cd "$(dirname "$0")"
if [ -e dinghy ]; then
    echo 'dinghy/ already exists; leaving it untouched.' >&2
    exit 1
fi

tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM
git clone --depth 1 https://github.com/zvenson/dinghy.git "$tmp_dir/source"
test -f "$tmp_dir/source/firmware/src/dinghy.c"
test -x "$tmp_dir/source/build.sh"

mkdir "$tmp_dir/staged"
tar -C "$tmp_dir/source" --exclude=.git -cf - . | tar -C "$tmp_dir/staged" -xf -
git -C "$tmp_dir/source" rev-parse HEAD > "$tmp_dir/staged/.upstream-commit"
mv "$tmp_dir/staged" dinghy
echo 'Dinghy source copied into dinghy/. Start with the host test in README.md.'
