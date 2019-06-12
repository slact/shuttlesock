#!/usr/bin/env zsh
MY_PATH="`dirname \"$0\"`"
MY_PATH="`( cd \"$MY_PATH\" && pwd )`"
_src_dir=${MY_PATH}/src
  
OPTS=()
compiler=""
build_dir="./build"
ANALYZE_FLAGS=( --use-cc=clang -maxloop 100 -enable-checker alpha.clone -enable-checker alpha.core -enable-checker alpha.deadcode -enable-checker alpha.security -enable-checker alpha.unix -enable-checker nullability )

export CLICOLOR_FORCE=1

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
      clang_analyze=1
      disable_ccache=1
      ;;
    verbose)
      verbose="-v"
      ;;
    coverage)
      compiler=gcc
      build_type=DebugCoverage
      ;;
    clang-coverage)
      compiler=clang
      build_type=DebugCoverage
      ;;
    gcc-coverage)
      compiler=gcc
      build_type=DebugCoverage
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


echo cmake $OPTS -B$build_dir

if [[ -n $clang_analyze ]]; then
  scan-build $ANALYZE_FLAGS --view cmake $OPTS -B$build_dir
  scan-build $ANALYZE_FLAGS --view cmake --build $build_dir $verbose
else
  cmake $OPTS -B$build_dir
  cmake --build $build_dir $verbose
fi
if [[ $build_type == "DebugCoverage" ]]; then
  cd $build_dir
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
  
fi
