#!/usr/bin/env zsh
MY_PATH="`dirname \"$0\"`"
MY_PATH="`( cd \"$MY_PATH\" && pwd )`"
_src_dir=${MY_PATH}/src

ALL_OFF="\e[1;0m"
BLUE="${BOLD}\e[1;34m"
GREEN="${BOLD}\e[1;32m"
RED="${BOLD}\e[1;31m"
YELLOW="${BOLD}\e[1;33m"

OPTS=()
compiler=""
build_dir="./build"
ANALYZE_FLAGS=( --use-cc=clang -maxloop 100 -enable-checker alpha.clone -enable-checker alpha.core -enable-checker alpha.deadcode -enable-checker alpha.security -enable-checker alpha.unix -enable-checker nullability --view )
ANALYZE=()
export CLICOLOR_FORCE=1
build_type=Debug
for opt in $*; do
  case $opt in
    clang)
      compiler=clang;;
    clang-sanitize|sanitize|sanitize-memory)
      compiler=clang
      build_type=DebugMSan
      ;;
    sanitize-address)
      compuler=clang
      build_type=DebugASan
      ;;
    sanitize-threads|sanitize-thread)
      compuler=clang
      build_type=DebugTSan
      ;;
    no-ccache)
      disable_ccache=1
      ;;
    clean)
      clean=1
      ;;
    gcc)
      compiler=gcc;;
    gcc6)
      compiler=gcc-6;;
    gcc5)
      compiler=gcc-5;;
    O0)
      optimize_level=0;;
    O1)
      optimize_level=1;;
    O2)
      optimize_level=2;;
    O3)
      optimize_level=3;;
    Og)
      optimize_level=g;;
    clang-analyzer|analyzer|scan|analyze)
      clean=1
      clean_after=1
      clang_analyze=1
      disable_ccache=1
      ;;
    verbose|-v|v)
      verbose="-v"
      ;;
    coverage)
      compiler=gcc
      run_test=1
      build_type=DebugCoverage
      ;;
    clang-coverage)
      compiler=clang
      run_test=1
      build_type=DebugCoverage
      ;;
    gcc-coverage)
      compiler=gcc
      run_test=1
      build_type=DebugCoverage
      ;;
    release)
      build_type=Release
      ;;
    release-debug)
      build_type=RelWithDebInfo
      ;;
    test|runtest)
      run_test=1
      ;;
    *)
      OPTS+=( "$opt" )
      ;;
  esac
done

if [[ -n $clean ]]; then
  rm -Rf $build_dir
fi

if [[ -n $compiler ]]; then
  OPTS+=( "-DCMAKE_C_COMPILER=$compiler" )
fi
if [[ -n $build_type ]]; then
  OPTS+=( "-DCMAKE_BUILD_TYPE=$build_type" )
fi
if [[ -n $optimize_level ]]; then
  OPTS+=( "-DOPTIMIZE_LEVEL=$optimize_level" )
fi
if [[ -n $disable_ccache ]]; then
  OPTS+=( "-DDISABLE_CCACHE=1" )
fi

if [[ -n $clang_analyze ]]; then
  ANALYZE=("scan-build")
  ANALYZE+=($ANALYZE_FLAGS)
fi

TRAPINT() {
  if [[ -n $scan_view_pid ]]; then
    kill -s INT $scan_view_pid
  else
    exit 1
  fi
}

echo $BLUE $ANALYZE $YELLOW cmake $GREEN $OPTS $YELLOW -B$build_dir $ALL_OFF
$ANALYZE cmake $OPTS -B$build_dir
if ! [ $? -eq 0 ]; then;
  exit 1
fi
echo $BLUE $ANALYZE $YELLOW cmake $YELLOW --build $build_dir $verbose $ALL_OFF

if [[ -n $clang_analyze ]]; then
  $ANALYZE cmake --build $build_dir $verbose &
  scan_view_pid=$!
  wait $scan_view_pid
  scan_view_pid=""
else
  $ANALYZE cmake --build $build_dir $verbose
fi

if ! [ $? -eq 0 ]; then;
  exit 1
fi

if [[ -n $run_test ]]; then
  pushd $build_dir
  ./shuso_test
  popd $build_dir
fi

if [[ $build_type == "DebugCoverage" ]]; then
  pushd $build_dir
  ./shuso_test
  #ls CMakeFiles/shuttlesock.dir/src -alh
  #ls ../src -alh
  if [[ $compiler == "gcc" ]]; then
    mkdir coverage-report 2>/dev/null
    gcovr --root ../src -v --html-details -o coverage-report/index.html ./
  elif [[ $compiler == "clang" ]]; then
    llvm-profdata merge -sparse *.profraw -o .profdata
    llvm-cov show -format="html" -output-dir="coverage-report" -instr-profile=".profdata" "libshuttlesock.so" -object "shuso_test"
  fi

  xdg-open ./coverage-report/index.html
  popd $build_dir
fi

if [[ -n $clean_after ]]; then
  rm -Rf $build_dir
fi
