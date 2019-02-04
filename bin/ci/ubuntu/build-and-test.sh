#!/bin/bash -u
# We use set -e and bash with -u to bail on first non zero exit code of any
# processes launched or upon any unbound variable.
# We use set -x to print commands before running them to help with
# debugging.
set -ex
__dirname=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
echo "using CC: $CC"
echo "using TARGET: $TARGET"

# Ensure APP defaults to rippled if it's not set.
: ${APP:=rippled}
echo "using APP: $APP"

JOBS=${NUM_PROCESSORS:-2}
JOBS=$((JOBS+1))

if [[ ${BUILD:-scons} == "cmake" ]]; then
  echo "cmake building ${APP}"
  CMAKE_EXTRA_ARGS=" -DCMAKE_VERBOSE_MAKEFILE=ON"
  CMAKE_TARGET=$CC.$TARGET
  BUILDARGS=" -j${JOBS}"
  if [[ ${VERBOSE_BUILD:-} == true ]]; then
    # TODO: if we use a different generator, this
    # option to build verbose would need to change:
    BUILDARGS+=" verbose=1"
  fi
  if [[ ${CI:-} == true ]]; then
    CMAKE_TARGET=$CMAKE_TARGET.ci
  fi
  if [[ ${USE_CCACHE:-} == true ]]; then
    echo "using ccache with basedir [${CCACHE_BASEDIR:-}]"
    CMAKE_EXTRA_ARGS+=" -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
  fi
  if [ -d "build/${CMAKE_TARGET}" ]; then
    rm -rf "build/${CMAKE_TARGET}"
  fi
  mkdir -p "build/${CMAKE_TARGET}"
  pushd "build/${CMAKE_TARGET}"
  cmake ../.. -Dtarget=$CMAKE_TARGET ${CMAKE_EXTRA_ARGS}
  cmake --build . -- $BUILDARGS
  if [[ ${BUILD_BOTH:-} == true ]]; then
    if [[ ${TARGET} == *.unity ]]; then
      cmake --build . --target rippled_classic -- $BUILDARGS
    else
      cmake --build . --target rippled_unity -- $BUILDARGS
    fi
  fi
  popd
  export APP_PATH="$PWD/build/${CMAKE_TARGET}/${APP}"
  echo "using APP_PATH: $APP_PATH"
else
  export APP_PATH="$PWD/build/$CC.$TARGET/${APP}"
  echo "using APP_PATH: $APP_PATH"
  # Make sure vcxproj is up to date
  scons vcxproj
  git diff --exit-code
  # $CC will be either `clang` or `gcc`
  # http://docs.travis-ci.com/user/migrating-from-legacy/?utm_source=legacy-notice&utm_medium=banner&utm_campaign=legacy-upgrade
  #   indicates that 2 cores are available to containers.
  scons -j${JOBS} $CC.$TARGET
fi
# We can be sure we're using the build/$CC.$TARGET variant
# (-f so never err)
rm -f build/${APP}

# See what we've actually built
ldd $APP_PATH

function join_by { local IFS="$1"; shift; echo "$*"; }

# This is a list of manual tests
# in rippled that we want to run
declare -a manual_tests=(
  "beast.chrono.abstract_clock"
  "beast.unit_test.print"
  "ripple.NodeStore.Timing"
  "ripple.app.Flow_manual"
  "ripple.app.NoRippleCheckLimits"
  "ripple.app.PayStrandAllPairs"
  "ripple.consensus.ByzantineFailureSim"
  "ripple.consensus.DistributedValidators"
  "ripple.consensus.ScaleFreeSim"
  "ripple.ripple_data.digest"
  "ripple.tx.CrossingLimits"
  "ripple.tx.FindOversizeCross"
  "ripple.tx.Offer_manual"
  "ripple.tx.OversizeMeta"
  "ripple.tx.PlumpBook"
)

if [[ ${APP} == "rippled" ]]; then
  if [[ ${MANUAL_TESTS:-} == true ]]; then
    APP_ARGS+="--unittest=$(join_by , "${manual_tests[@]}")"
  else
    APP_ARGS+="--unittest --quiet --unittest-log"
  fi
  # Only report on src/ripple files
  export LCOV_FILES="*/src/ripple/*"
  # Nothing to explicitly exclude
  export LCOV_EXCLUDE_FILES="LCOV_NO_EXCLUDE"
else
  : ${APP_ARGS:=}
  : ${LCOV_FILES:="*/src/*"}
  # Don't exclude anything
  : ${LCOV_EXCLUDE_FILES:="LCOV_NO_EXCLUDE"}
fi

if [[ $TARGET == "coverage" ]]; then
  export PATH=$PATH:$LCOV_ROOT/usr/bin

  # Create baseline coverage data file
  lcov --no-external -c -i -d . -o baseline.info | grep -v "ignoring data for external file"
fi

if [[ ${SKIP_TESTS:-} == true ]]; then
  echo "skipping tests for ${TARGET}"
  exit
fi

if [[ $TARGET == debug* ]]; then
  # Execute unit tests under gdb, printing a call stack
  # if we get a crash.
  $GDB_ROOT/bin/gdb -return-child-result -quiet -batch \
                    -ex "set env MALLOC_CHECK_=3" \
                    -ex "set print thread-events off" \
                    -ex run \
                    -ex "thread apply all backtrace full" \
                    -ex "quit" \
                    --args $APP_PATH $APP_ARGS
else
  $APP_PATH $APP_ARGS
fi

if [[ $TARGET == "coverage" ]]; then
  # Create test coverage data file
  lcov --no-external -c -d . -o tests.info | grep -v "ignoring data for external file"

  # Combine baseline and test coverage data
  lcov -a baseline.info -a tests.info -o lcov-all.info

  # Included files
  lcov -e "lcov-all.info" "${LCOV_FILES}" -o lcov.pre.info

  # Excluded files
  lcov --remove lcov.pre.info "${LCOV_EXCLUDE_FILES}" -o lcov.info

  # Push the results (lcov.info) to codecov
  codecov -X gcov # don't even try and look for .gcov files ;)

  find . -name "*.gcda" | xargs rm -f
fi


