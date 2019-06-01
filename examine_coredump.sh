#!/bin/zsh
target=$1
program_name=shuso_test
core_dir="./coredump"
if [ -z $target ]; then
  target=$(realpath build/$program_name)
  dump=$(realpath $core_dir/last.core)
else
  dump=$(realpath $core_dir/$target.core)
fi

mkdir $core_dir 2>/dev/null

echo "saving coredump for $target at $dump"

sudo coredumpctl dump $target > $dump
pushd build
kdbg $program_name "$dump" 2>/dev/null
popd
# rm "$dump" #keep it around for now
