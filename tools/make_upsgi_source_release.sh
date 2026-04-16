#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <version>" >&2
    exit 1
fi

version="$1"
root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out_dir="$root_dir/dist"
release_dir_name="upsgi-$version"
stage_root="$out_dir/.release-stage-$version"
stage_dir="$stage_root/$release_dir_name"
archive_path="$out_dir/$release_dir_name.tar.gz"
checksum_path="$out_dir/$release_dir_name.sha256"

rm -rf "$stage_root"
mkdir -p "$stage_dir" "$out_dir"

tar \
    --exclude='*.o' \
    --exclude='./upsgi' \
    --exclude='libperl.so' \
    --exclude='upsgibuild.log' \
    --exclude='upsgibuild.lastcflags' \
    --exclude='upsgibuild.lastprofile' \
    --exclude='./dist' \
    --exclude='./tests/fork/artifacts' \
    --exclude='./docs/performance/EXECUTION_CHECKLIST.md' \
    -cf - -C "$root_dir" . | tar -xf - -C "$stage_dir"

mkdir -p "$stage_dir/dist"
tar -czf "$archive_path" -C "$stage_root" "$release_dir_name"
archive_name=$(basename "$archive_path")
(
    cd "$out_dir"
    sha256sum "$archive_name" > "$(basename "$checksum_path")"
)

rm -rf "$stage_root"

echo "created $archive_path"
echo "created $checksum_path"
