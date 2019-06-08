#!/bin/zsh
VALGRIND_OPT=( "--tool=memcheck" "--track-origins=yes" "--read-var-info=yes" )

VG_MEMCHECK_OPT=( "--leak-check=full" "--show-leak-kinds=all" "--leak-check-heuristics=all" "--keep-stacktraces=alloc-and-free" "--suppressions=${DEVDIR}/vg.supp" )

#expensive definedness checks (newish option)
VG_MEMCHECK_OPT+=( "--expensive-definedness-checks=yes")

#long stack traces
VG_MEMCHECK_OPT+=("--num-callers=20")

#generate suppresions
#VG_MEMCHECK_OPT+=("--gen-suppressions=all")

#track files
#VG_MEMCHECK_OPT+=("--track-fds=yes")

TEST=build/shuso_test
TEST_OPT=()

DEBUGGER_NAME="kdbg"
DEBUGGER_CMD="dbus-run-session kdbg -p %s $TEST"

for opt in $*; do
  case $opt in
    leak|leakcheck|valgrind|memcheck)
      valgrind=1
      VALGRIND_OPT+=($VG_MEMCHECK_OPT);;
    debug-memcheck)
      valgrind=1
      VALGRIND_OPT+=($VG_MEMCHECK_OPT)
      VALGRIND_OPT+=( "--vgdb=yes" "--vgdb-error=1" )
      #ATTACH_DDD=1
      ;;
    sanitize-undefined)
      FSANITIZE_UNDEFINED=1
      ;;
    callgrind|profile)
      VALGRIND_OPT=( "--tool=callgrind" "--collect-jumps=yes"  "--collect-systime=yes" "--branch-sim=yes" "--cache-sim=yes" "--simulate-hwpref=yes" "--simulate-wb=yes" "--callgrind-out-file=callgrind-shuttlesock-%p.out")
      valgrind=1;;
    helgrind)
      VALGRIND_OPT=( "--tool=helgrind" "--free-is-write=yes")
      valgrind=1
      ;;
    cachegrind)
      VALGRIND_OPT=( "--tool=cachegrind" )
      valgrind=1;;
    verbose)
      TEST_OPT+=("--verbose")
      ;;
  esac
done

export ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer
export ASAN_OPTIONS=symbolize=1

debugger_pids=()

TRAPINT() {
  if [[ $debugger == 1 ]]; then
    sudo kill $debugger_pids
  fi
}

attach_debugger() {
  master_pid=`cat /tmp/shuttlesock-test-master.pid`
  while [[ -z $child_pids ]]; do
    if [[ -z $child_text_match ]]; then
      child_pids=`pgrep -P $master_pid`
    else
      child_pids=`pgrep -P $master_pid -f $child_text_match`
    fi
    sleep 0.1
  done
  while read -r line; do
    echo "attaching $1 to $line"
    sudo $(printf $2 $line) &
    debugger_pids+="$!"
  done <<< $child_pids
  echo "$1 at $debugger_pids"
}

if [[ ! -f $TEST ]]; then
  echo "$TEST not found"
  exit 1
fi

if [[ $debugger == 1 ]]; then
  $SUDO $TEST $TEST_OPT
  if ! [ $? -eq 0 ]; then; 
    echo "failed to start shuso_test"; 
    exit 1
  fi
  sleep 0.2
  attach_debugger "$DEBUGGER_NAME" "$DEBUGGER_CMD"
  wait $debugger_pids
  kill $master_pid
elif [[ $debug_master == 1 ]]; then
  pushd $SRCDIR
  sudo kdbg -a "$TEST_OPT" "$TEST"
  popd
elif [[ $valgrind == 1 ]]; then
  echo $SUDO valgrind $VALGRIND_OPT $TEST $TEST_OPT
  $SUDO valgrind $VALGRIND_OPT $TEST $TEST_OPT
else
  echo $SUDO $TEST $TEST_OPT
  $SUDO $TEST $TEST_OPT &
  wait $!
fi