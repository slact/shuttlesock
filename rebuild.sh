#!/usr/bin/env zsh
MY_PATH="`dirname \"$0\"`"
MY_PATH="`( cd \"$MY_PATH\" && pwd )`"
_src_dir=${MY_PATH}/src

ALL_OFF="\e[1;0m"
BLUE="\e[1;34m"
GREEN="\e[1;32m"
RED="\e[1;31m"
YELLOW="\e[1;33m"

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
      compiler=clang
      build_type=DebugASan
      ;;
    sanitize-threads|sanitize-thread)
      compiler=clang
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
    valgrind)
      valgrind=1
      ;;
    stalloc_track_space)
      stalloc_track_space=1
      ;;
    sanitize)
      sanitize=1
      ;;
    verbose|-v|v)
      verbose_build=1
      ;;
    coverage)
      compiler=clang
      run_test=1
      build_type=DebugCoverage
      ;;
    clang-coverage)
      compiler=clang
      run_test=1
      build_type=DebugCoverage
      ;;
    no-display-coverage)
      no_display_coverage=1
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
    no-eventfd)
      disable_eventfd=1
      ;;
    -*)
      OPTS+=( "$opt" )
      ;;
    *)
      echo "${RED}>> invalid option ${ALL_OFF}$opt${ALL_OFF}" 1>&2
      invalid_options=1
      ;;
  esac
done

if [[ -n $invalid_options ]]; then
  exit 1
fi

if [[ -n $clean ]]; then
  echo "${YELLOW}>> rm -Rf $build_dir${ALL_OFF}"
  rm -Rf $build_dir
fi

OPTS+=( "-DCMAKE_BUILD_TYPE=$build_type" )
if [[ -n $compiler ]]; then
  OPTS+=( "-DCMAKE_C_COMPILER=$compiler" )
fi
if [[ -n $optimize_level ]]; then
  OPTS+=( "-DOPTIMIZE_LEVEL=$optimize_level" )
fi

OPTS+=( "-DSHUTTLESOCK_STALLOC_TRACK_SPACE=${stalloc_track_space}" )
OPTS+=( "-DSHUTTLESOCK_VALGRIND=${valgrind}" )
OPTS+=( "-DSHUTTLESOCK_SANITIZE=${sanitize}" )
OPTS+=( "-DDISABLE_CCACHE=${disable_ccache}" )

if [[ -n $disable_eventfd ]]; then
  OPTS+=( "-DSHUTTLESOCK_USE_EVENTFD=NO" )
fi

if [[ -n $clang_analyze ]]; then
  ANALYZE=("scan-build")
  ANALYZE+=($ANALYZE_FLAGS)
  ANALYZE+=("")
fi

TRAPINT() {
  if [[ -n $scan_view_pid ]]; then
    kill -s TERM $scan_view_pid
  else
    exit 1
  fi
}

cmake_help=$(cmake --help)
if [[ "$cmake_help" == *" -B "* ]]; then
  #relatively modern cmake
  print -n "\n${YELLOW}>> ${BLUE}${ANALYZE}${YELLOW}cmake${ALL_OFF}"
  for opt in $OPTS ; do
    print -n " ${GREEN}\\\\\n $opt${ALL_OFF}"
  done
  print -n " ${GREEN}\\\\\n ${YELLOW}-B$build_dir $ALL_OFF\n\n"
  $ANALYZE cmake $OPTS -B$build_dir
else
  #shitty old cmake
  if [[ ! -d "$build_dir" ]]; then
    echo "${YELLOW}>> mkdir $build_dir${ALL_OFF}"
    mkdir "$build_dir"
  fi
  echo "${YELLOW}>> cd $build_dir${ALL_OFF}"
  cd $build_dir
  print -n "${YELLOW}>> ${BLUE}${ANALYZE}${YELLOW}cmake${ALL_OFF}"
  for opt in $OPTS ; do
    print -n " ${GREEN}\\\\\n $opt${ALL_OFF}"
  done
  print -n " ${GREEN}\\\\\n ${YELLOW}../ $ALL_OFF\n\n"
  $ANALYZE cmake $OPTS ../
  echo "${YELLOW}>> cd ../${ALL_OFF}"
  cd ../
fi

if [[ -n $verbose_build ]]; then
  cmake_build_help=$(cmake --build 2>&1)
  if [[ "$cmake_build_help" == *" --verbose "* ]]; then
    verbose_build_flag="--verbose"
  else
    direct_makefile_build=1
    verbose_build_flag="VERBOSE=1"
  fi
fi

if ! [ $? -eq 0 ]; then;
  exit 1
fi


if [[ -n $direct_makefile_build ]]; then
  echo "\n$YELLOW>> cd $build_dir"
  cd ./build
  MAKE_COMMAND=( make ${verbose_build_flag} )
else
  echo ""
  MAKE_COMMAND=( cmake --build $build_dir $verbose_build_flag )
fi
echo "$YELLOW>> ${BLUE}${ANALYZE}${YELLOW}${MAKE_COMMAND}${ALL_OFF}\n"

if [[ -n $clang_analyze ]]; then
  $ANALYZE $MAKE_COMMAND &
  scan_view_pid=$!
  wait $scan_view_pid
  scan_view_pid=""
else
  $ANALYZE $MAKE_COMMAND
fi
if [[ -n $direct_makefile_build ]]; then
  echo "\n$YELLOW>> cd .."
  cd ..
fi

if ! [ $? -eq 0 ]; then;
  exit 1
fi

if [[ -n $run_test ]]; then
  pushd $build_dir
  echo "\n${GREEN}>> Running tests...${ALL_OFF}"
  ./shuso_test
  if ! [ $? -eq 0 ]; then;
    echo "\n${RED}>> tests failed.${ALL_OFF}"
    exit 1
  fi
  popd
fi

if [[ $build_type == "DebugCoverage" ]]; then
  pushd $build_dir
  #ls CMakeFiles/shuttlesock.dir/src -alh
  #ls ../src -alh
  if [[ -z $no_display_coverage ]]; then
    print -n  "\n${GREEN}>> Preparing coverage results...${ALL_OFF}"
    if [[ $compiler == "gcc" ]]; then
      mkdir coverage-report 2>/dev/null
      gcovr --root ../src --html-details -o coverage-report/index.html ./
    elif [[ $compiler == "clang" ]]; then
      llvm-profdata merge -sparse *.profraw -o .profdata
      llvm-cov show -format="html" -output-dir="coverage-report" -instr-profile=".profdata"  -ignore-filename-regex="test/.*" -ignore-filename-regex="lib/.*" "libshuttlesock.so" -object "shuso_test"
    fi
    print -n  "${GREEN}done.${ALL_OFF}\n\n"
    xdg-open ./coverage-report/index.html
  else
    echo "\n${GREEN}>> Coverage generated.${ALL_OFF}\n"
  fi
  popd $build_dir
fi

if [[ -n $clean_after ]]; then
  rm -Rf $build_dir
fi
