#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
cd "$repo_dir"

version=${1:-$(./upsgi --version 2>/dev/null || true)}
if [[ -z "$version" ]]; then
    echo "usage: $0 <version>" >&2
    echo "or build ./upsgi first so the version can be inferred" >&2
    exit 2
fi

project_root_name="upsgi-$version"
out_dir="$repo_dir/dist"
mkdir -p "$out_dir"

src_tar="$out_dir/upsgi-$version-source.tar.gz"
sha_file="$out_dir/upsgi-$version-SHA256SUMS.txt"

tmpdir=$(mktemp -d)
cleanup() { rm -rf "$tmpdir"; }
trap cleanup EXIT

stage="$tmpdir/$project_root_name"
mkdir -p "$stage"

# Copy the working tree while excluding transient local artifacts.
tar \
    --exclude='./.git' \
    --exclude='./build' \
    --exclude='./dist/*.tar.gz' \
    --exclude='./dist/*SHA256SUMS.txt' \
    --exclude='./tests/fork/artifacts' \
    --exclude='./.local-lib' \
    --exclude='./__pycache__' \
    --exclude='./*/__pycache__' \
    --exclude='./uwsgibuild.*' \
    --exclude='./upsgi' \
    --exclude='./uwsgi' \
    --exclude='./*.o' \
    --exclude='./core/*.o' \
    --exclude='./plugins/*/*.o' \
    -cf - . | tar -xf - -C "$stage"

rm -f "$src_tar" "$sha_file"
tar -C "$tmpdir" -czf "$src_tar" "$project_root_name"
(
    cd "$out_dir"
    sha256sum "$(basename "$src_tar")" > "$(basename "$sha_file")"
)

echo "created: $src_tar"
echo "created: $sha_file"
