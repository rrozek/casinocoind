
cmake_minimum_required (VERSION 3.9.0)
set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake ${CMAKE_MODULE_PATH})
include (CheckCXXCompilerFlag)
if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.11)
  include (FetchContent)
endif ()
include (ExternalProject)
include (CMakeFuncs) # must come *after* ExternalProject b/c it overrides one function in EP
include (ProcessorCount)
if (target)
  message (WARNING
    "The target option is deprecated and will be removed in a future release")
  parse_target()
endif ()
project (casinocoind)

#[===================================================================[
   convenience variables and sanity checks
#]===================================================================]

ProcessorCount(num_procs)
get_property (is_multiconfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (is_multiconfig STREQUAL "NOTFOUND")
  if (${CMAKE_GENERATOR} STREQUAL "Xcode" OR ${CMAKE_GENERATOR} MATCHES "^Visual Studio")
    set (is_multiconfig TRUE)
  endif ()
endif ()

set (CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
if (NOT is_multiconfig)
  if (NOT CMAKE_BUILD_TYPE)
    message (STATUS "Build type not specified - defaulting to Release")
    set (CMAKE_BUILD_TYPE Release CACHE STRING "build type" FORCE)
  elseif (NOT (CMAKE_BUILD_TYPE STREQUAL Debug OR CMAKE_BUILD_TYPE STREQUAL Release))
    # for simplicity, these are the only two config types we care about. Limiting
    # the build types simplifies dealing with external project builds especially
    message (FATAL_ERROR " *** Only Debug or Release build types are currently supported ***")
  endif ()
endif ()

if (${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_SOURCE_DIR})
  set (is_root_project ON)
else ()
  set (is_root_project OFF)
endif ()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES ".*Clang") # both Clang and AppleClang
  set (is_clang TRUE)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set (is_gcc TRUE)
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5.1)
    message (FATAL_ERROR "This project requires GCC 5.1 or later")
  endif ()
endif ()
if (CMAKE_GENERATOR STREQUAL "Xcode")
  set (is_xcode TRUE)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set (is_linux TRUE)
else ()
  set (is_linux FALSE)
endif ()

# check for in-source build and fail
if ("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message (FATAL_ERROR "Builds (in-source) are not allowed in "
    "${CMAKE_CURRENT_SOURCE_DIR}. Please remove CMakeCache.txt and the CMakeFiles "
    "directory from ${CMAKE_CURRENT_SOURCE_DIR} and try building in a separate directory.")
endif ()

if ("${CMAKE_GENERATOR}" MATCHES "Visual Studio" AND
    NOT ("${CMAKE_GENERATOR}" MATCHES .*Win64.*))
  message (FATAL_ERROR
    "Visual Studio 32-bit build is not supported. Use -G\"${CMAKE_GENERATOR} Win64\"")
endif ()

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message (FATAL_ERROR "casinocoind requires a 64 bit target architecture.\n"
    "The most likely cause of this warning is trying to build casinocoind with a 32-bit OS.")
endif ()

if (APPLE AND NOT HOMEBREW)
  find_program (HOMEBREW brew)
endif ()

#[===================================================================[
   read version from source
#]===================================================================]

file (STRINGS src/casinocoin/protocol/impl/BuildInfo.cpp BUILD_INFO)
foreach (line_ ${BUILD_INFO})
  if (line_ MATCHES "versionString[ ]*=[ ]*\"(.+)\"")
    set (casinocoind_version ${CMAKE_MATCH_1})
  endif ()
endforeach ()
if (casinocoind_version)
  message (STATUS "casinocoind version: ${casinocoind_version}")
else ()
  message (FATAL_ERROR "unable to determine casinocoind version")
endif ()

#[===================================================================[
   declare user options/settings
#]===================================================================]

option (assert "Enables asserts, even in release builds" OFF)
option (unity "Creates a build based on unity sources. This is the default" ON)
if (is_gcc OR is_clang)
  option (coverage "Generates coverage info." OFF)
  option (profile "Add profiling flags" OFF)
  set (coverage_test "" CACHE STRING
    "On gcc & clang, the specific unit test(s) to run for coverage. Default is all tests.")
  if (coverage_test AND NOT coverage)
    set (coverage ON CACHE BOOL "gcc/clang only" FORCE)
  endif ()
  option (coverage_core_only
    "Include only src/casinocoin files when generating coverage report. \
    Set to OFF to include all sources in coverage report."
    ON)
else ()
  set (profile OFF CACHE BOOL "gcc/clang only" FORCE)
  set (coverage OFF CACHE BOOL "gcc/clang only" FORCE)
endif ()
if (is_linux)
  option (BUILD_SHARED_LIBS "build shared casinocoin libraries" OFF)
  option (static "link protobuf, openssl, libc++, and boost statically" ON)
  option (perf "Enables flags that assist with perf recording" OFF)
  option (use_gold "enables detection of gold (binutils) linker" ON)
else ()
  # we are not ready to allow shared-libs on windows because it would require
  # export declarations. On macos it's more feasible, but static openssl
  # produces odd linker errors, thus we disable shared lib builds for now.
  set (BUILD_SHARED_LIBS OFF CACHE BOOL "build shared casinocoin libraries - OFF for win/macos" FORCE)
  set (static ON CACHE BOOL "static link, linux only. ON for WIN/macos" FORCE)
  set (perf OFF CACHE BOOL "perf flags, linux only" FORCE)
  set (use_gold OFF CACHE BOOL "gold linker, linux only" FORCE)
endif ()
option (jemalloc "Enables jemalloc for heap profiling" OFF)
option (werr "treat warnings as errors" OFF)
option (local_protobuf
  "Force use of a local build of protobuf instead of system version." OFF)

# this one is a string and therefore can't be an option
set (san "" CACHE STRING "On gcc & clang, add sanitizer instrumentation")
set_property (CACHE san PROPERTY STRINGS ";undefined;memory;address;thread")
if (san)
  string (TOLOWER ${san} san)
  set (SAN_FLAG "-fsanitize=${san}")
  set (SAN_LIB "")
  if (is_gcc)
    if (san STREQUAL "address")
      set (SAN_LIB "asan")
    elseif (san STREQUAL "thread")
      set (SAN_LIB "tsan")
    elseif (san STREQUAL "memory")
      set (SAN_LIB "msan")
    elseif (san STREQUAL "undefined")
      set (SAN_LIB "ubsan")
    endif ()
  endif ()
  set (_saved_CRL ${CMAKE_REQUIRED_LIBRARIES})
  set (CMAKE_REQUIRED_LIBRARIES "${SAN_FLAG};${SAN_LIB}")
  check_cxx_compiler_flag (${SAN_FLAG} COMPILER_SUPPORTS_SAN)
  set (CMAKE_REQUIRED_LIBRARIES ${_saved_CRL})
  if (NOT COMPILER_SUPPORTS_SAN)
    message (FATAL_ERROR "${san} sanitizer does not seem to be supported by your compiler")
  endif ()
endif ()

# the remaining options are obscure and rarely used
option (beast_no_unit_test_inline
  "Prevents unit test definitions from being inserted into global table"
  OFF)
# NOTE - THIS OPTION CURRENTLY DOES NOT COMPILE :
# TODO: fix or remove
option (verify_nodeobject_keys
  "This verifies that the hash of node objects matches the payload. \
  This check is expensive - use with caution."
  OFF)
option (single_io_service_thread
  "Restricts the number of threads calling io_service::run to one. \
  This can be useful when debugging."
  OFF)
option (boost_show_deprecated
  "Allow boost to fail on deprecated usage. Only useful if you're trying\
  to find deprecated calls."
  OFF)
if (WIN32)
  option (beast_disable_autolink "Disables autolinking of system libraries on WIN32" OFF)
else ()
  set (beast_disable_autolink OFF CACHE BOOL "WIN32 only" FORCE)
endif ()
if (coverage)
  message (STATUS "coverage build requested - forcing Debug build")
  set (CMAKE_BUILD_TYPE Debug CACHE STRING "build type" FORCE)
endif ()

#[===================================================================[
   setup project-wide compiler settings
#]===================================================================]

#[=========================================================[
   TODO some/most of these common settings belong in a
   toolchain file, especially the ABI-impacting ones
#]=========================================================]
add_library (common INTERFACE)
add_library (casinocoin::common ALIAS common)
# add a single global dependency on this interface lib
link_libraries (casinocoin::common)
set_target_properties (common
  PROPERTIES INTERFACE_POSITION_INDEPENDENT_CODE ON)
target_compile_features (common INTERFACE cxx_std_14)
target_compile_definitions (common
  INTERFACE
    $<$<CONFIG:Debug>:DEBUG _DEBUG>
    $<$<AND:$<BOOL:profile>,$<NOT:$<BOOL:assert>>>:NDEBUG>)
    # ^^^^ NOTE: CMAKE release builds already have NDEBUG
    # defined, so no need to add it explicitly except for
    # this special case of (profile ON) and (assert OFF)
    # -- presumably this is because we don't want profile
    # builds asserting unless asserts were specifically
    # requested

if (MSVC)
  # remove existing exception flag since we set it to -EHa
  string (REGEX REPLACE "[-/]EH[a-z]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  # also remove dynamic runtime
  foreach (var_
      CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
      CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE)
    string (REGEX REPLACE "[-/]MD[d]*" " " ${var_} "${${var_}}")
  endforeach ()

  target_compile_options (common
    INTERFACE
      -bigobj            # Increase object file max size
      -fp:precise        # Floating point behavior
      -Gd                # __cdecl calling convention
      -Gm-               # Minimal rebuild: disabled
      -Gy-               # Function level linking: disabled
      -MP                # Multiprocessor compilation
      -openmp-           # pragma omp: disabled
      -errorReport:none  # No error reporting to Internet
      -nologo            # Suppress login banner
      -wd4018            # Disable signed/unsigned comparison warnings
      -wd4244            # Disable float to int possible loss of data warnings
      -wd4267            # Disable size_t to T possible loss of data warnings
      -wd4800            # Disable C4800(int to bool performance)
      -wd4503            # Decorated name length exceeded, name was truncated
      $<$<COMPILE_LANGUAGE:CXX>:
        -EHa
        -GR
      >
      $<$<CONFIG:Release>:-Ox>
      $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:
        -GS
        -Zc:forScope
      >
      # static runtime
      $<$<CONFIG:Debug>:-MTd>
      $<$<NOT:$<CONFIG:Debug>>:-MT>
      $<$<BOOL:${werr}>:-WX>
      )
  target_compile_definitions (common
    INTERFACE
      _WIN32_WINNT=0x6000
      _SCL_SECURE_NO_WARNINGS
      _CRT_SECURE_NO_WARNINGS
      WIN32_CONSOLE
      NOMINMAX
      # TODO: Resolve these warnings, don't just silence them
      _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
      $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:_CRTDBG_MAP_ALLOC>)
  target_link_libraries (common
    INTERFACE
      -errorreport:none
      -machine:X64)
else ()
  # HACK : because these need to come first, before any warning demotion
  string (APPEND CMAKE_CXX_FLAGS " -Wall -Wdeprecated")
  # not MSVC
  target_compile_options (common
    INTERFACE
      $<$<BOOL:${werr}>:-Werror>
      $<$<COMPILE_LANGUAGE:CXX>:
        -frtti
        -Wnon-virtual-dtor
      >
      -Wno-sign-compare
      -Wno-char-subscripts
      -Wno-format
      -Wno-unused-local-typedefs
      $<$<BOOL:${is_gcc}>:
        -Wno-unused-but-set-variable
        -Wno-deprecated
      >
      $<$<NOT:$<CONFIG:Debug>>:-fno-strict-aliasing>
      # tweak gcc optimization for debug
      $<$<AND:$<BOOL:is_gcc>,$<CONFIG:Debug>>:-O0>
      # Add debug symbols to release config
      $<$<CONFIG:Release>:-g>)
  target_link_libraries (common
    INTERFACE
      -rdynamic
      $<$<AND:$<BOOL:${static}>,$<NOT:$<BOOL:${APPLE}>>>:-static-libstdc++>)
endif ()

if (use_gold AND is_gcc)
  # use gold linker if available
  execute_process (
    COMMAND ${CMAKE_CXX_COMPILER} -fuse-ld=gold -Wl,--version
    ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
    #[=========================================================[
       NOTE: THE gold linker inserts -rpath as DT_RUNPATH by
       default intead of DT_RPATH, so you might have slightly
       unexpected runtime ld behavior if you were expecting
       DT_RPATH.  Specify --disable-new-dtags to gold if you do
       not want the default DT_RUNPATH behavior. This rpath
       treatment as well as static/dynamic selection means that
       gold does not currently have ideal default behavior when
       we are using jemalloc. Thus for simplicity we don't use
       it when jemalloc is requested. An alternative to
       disabling would be to figure out all the settings
       required to make gold play nicely with jemalloc.
    #]=========================================================]
  if (("${LD_VERSION}" MATCHES "GNU gold") AND (NOT jemalloc))
    target_link_libraries (common
      INTERFACE
        -fuse-ld=gold
        -Wl,--no-as-needed
        #[=========================================================[
           see https://bugs.launchpad.net/ubuntu/+source/eglibc/+bug/1253638/comments/5
           DT_RUNPATH does not work great for transitive
           dependencies (of which boost has a few) - so just
           switch to DT_RPATH if doing dynamic linking with gold
        #]=========================================================]
        $<$<NOT:$<BOOL:${static}>>:-Wl,--disable-new-dtags>)
  endif ()
  unset (LD_VERSION)
endif ()

if (is_clang)
  # use lld linker if available
  execute_process (
    COMMAND ${CMAKE_CXX_COMPILER} -fuse-ld=lld -Wl,--version
    ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
  if ("${LD_VERSION}" MATCHES "LLD")
    target_link_libraries (common INTERFACE -fuse-ld=lld)
  endif ()
  unset (LD_VERSION)
endif()

if (assert)
  foreach (var_ CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE)
    STRING (REGEX REPLACE "[-/]DNDEBUG" "" ${var_} "${${var_}}")
  endforeach ()
endif ()

#[===================================================================[
   casinocoind compile options/settings via an interface library
#]===================================================================]

add_library (opts INTERFACE)
add_library (casinocoin::opts ALIAS opts)
target_compile_definitions (opts
  INTERFACE
    BOOST_ASIO_HAS_STD_ARRAY
    BOOST_ASIO_DISABLE_HANDLER_TYPE_REQUIREMENTS
    $<$<BOOL:${boost_show_deprecated}>:
      BOOST_ASIO_NO_DEPRECATED
      BOOST_FILESYSTEM_NO_DEPRECATED
    >
    $<$<NOT:$<BOOL:${boost_show_deprecated}>>:
      BOOST_COROUTINES_NO_DEPRECATION_WARNING
      BOOST_BEAST_ALLOW_DEPRECATED
      BOOST_FILESYSTEM_DEPRECATED
    >
    $<$<BOOL:${beast_no_unit_test_inline}>:BEAST_NO_UNIT_TEST_INLINE=1>
    $<$<BOOL:${beast_disable_autolink}>:BEAST_DONT_AUTOLINK_TO_WIN32_LIBRARIES=1>
    $<$<BOOL:${single_io_service_thread}>:casinocoin_SINGLE_IO_SERVICE_THREAD=1>
    # doesn't currently compile ? :
    $<$<BOOL:${verify_nodeobject_keys}>:casinocoin_VERIFY_NODEOBJECT_KEYS=1>)
target_compile_options (opts
  INTERFACE
    $<$<AND:$<BOOL:${is_gcc}>,$<COMPILE_LANGUAGE:CXX>>:-Wsuggest-override>
    $<$<BOOL:${perf}>:-fno-omit-frame-pointer>
    $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${coverage}>>:-fprofile-arcs -ftest-coverage>
    $<$<AND:$<BOOL:${is_clang}>,$<BOOL:${coverage}>>:-fprofile-instr-generate -fcoverage-mapping>
    $<$<BOOL:${profile}>:-pg>
    $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${profile}>>:-p>)

target_link_libraries (opts
  INTERFACE
    $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${coverage}>>:-fprofile-arcs -ftest-coverage>
    $<$<AND:$<BOOL:${is_clang}>,$<BOOL:${coverage}>>:-fprofile-instr-generate -fcoverage-mapping>
    $<$<BOOL:${profile}>:-pg>
    $<$<AND:$<BOOL:${is_gcc}>,$<BOOL:${profile}>>:-p>)

if (jemalloc)
  find_package (jemalloc REQUIRED)
  target_compile_definitions (opts INTERFACE PROFILE_JEMALLOC)
  target_include_directories (opts SYSTEM INTERFACE ${JEMALLOC_INCLUDE_DIRS})
  target_link_libraries (opts INTERFACE ${JEMALLOC_LIBRARIES})
  get_filename_component (JEMALLOC_LIB_PATH ${JEMALLOC_LIBRARIES} DIRECTORY)
  ## TODO see if we can use the BUILD_RPATH target property (is it transitive?)
  set (CMAKE_BUILD_RPATH ${CMAKE_BUILD_RPATH} ${JEMALLOC_LIB_PATH})
endif ()

if (san)
  target_compile_options (opts
    INTERFACE
      ${SAN_FLAG}
      -fno-omit-frame-pointer)
  target_compile_definitions (opts
    INTERFACE
      $<$<STREQUAL:${san},address>:SANITIZER=ASAN>
      $<$<STREQUAL:${san},thread>:SANITIZER=TSAN>
      $<$<STREQUAL:${san},memory>:SANITIZER=MSAN>
      $<$<STREQUAL:${san},undefined>:SANITIZER=UBSAN>)
  target_link_libraries (opts INTERFACE ${SAN_FLAG} ${SAN_LIB})
endif ()

#[===================================================================[
   casinocoind transitive library deps via an interface library
#]===================================================================]

add_library (casinocoin_syslibs INTERFACE)
add_library (casinocoin::syslibs ALIAS casinocoin_syslibs)
target_link_libraries (casinocoin_syslibs
  INTERFACE
    $<$<BOOL:${MSVC}>:
      legacy_stdio_definitions.lib
      Shlwapi
      kernel32
      user32
      gdi32
      winspool
      comdlg32
      advapi32
      shell32
      ole32
      oleaut32
      uuid
      odbc32
      odbccp32
      crypt32
    >
    $<$<NOT:$<BOOL:${MSVC}>>:dl>
    $<$<NOT:$<OR:$<BOOL:${MSVC}>,$<BOOL:${APPLE}>>>:rt>)

if (NOT MSVC)
  set (THREADS_PREFER_PTHREAD_FLAG ON)
  find_package (Threads)
  target_link_libraries (casinocoin_syslibs INTERFACE Threads::Threads)
endif ()

add_library (casinocoin_libs INTERFACE)
add_library (casinocoin::libs ALIAS casinocoin_libs)
target_link_libraries (casinocoin_libs INTERFACE casinocoin::syslibs)

#[===================================================================[
   NIH prefix path..this is where we will download
   and build any ExternalProjects, and they will hopefully
   survive across build directory deletion (manual cleans)
#]===================================================================]

string (REGEX REPLACE "[ \\/%]+" "_" gen_for_path ${CMAKE_GENERATOR})
string (TOLOWER ${gen_for_path} gen_for_path)
# HACK: trying to shorten paths for windows CI (which hits 260 MAXPATH easily)
# @see:  https://issues.jenkins-ci.org/browse/JENKINS-38706?focusedCommentId=339847
string (REPLACE "visual_studio" "vs" gen_for_path ${gen_for_path})
if (NOT DEFINED NIH_CACHE_ROOT)
  if (DEFINED ENV{NIH_CACHE_ROOT})
    set (NIH_CACHE_ROOT $ENV{NIH_CACHE_ROOT})
  else ()
    set (NIH_CACHE_ROOT ${CMAKE_SOURCE_DIR})
  endif ()
endif ()
set (nih_cache_path
  "${NIH_CACHE_ROOT}/.nih_c/${gen_for_path}/${CMAKE_CXX_COMPILER_ID}_${CMAKE_CXX_COMPILER_VERSION}")
if (NOT is_multiconfig)
  set (nih_cache_path "${nih_cache_path}/${CMAKE_BUILD_TYPE}")
endif ()
file(TO_CMAKE_PATH "${nih_cache_path}" nih_cache_path)
message (STATUS "NIH-EP cache path: ${nih_cache_path}")
## two convenience variables:
set (ep_lib_prefix ${CMAKE_STATIC_LIBRARY_PREFIX})
set (ep_lib_suffix ${CMAKE_STATIC_LIBRARY_SUFFIX})

# this is a setting for FetchContent and needs to be
# a cache variable
# https://cmake.org/cmake/help/latest/module/FetchContent.html#populating-the-content
set (FETCHCONTENT_BASE_DIR ${nih_cache_path} CACHE STRING "" FORCE)

#[===================================================================[
   NIH dep: boost
#]===================================================================]

if ((NOT DEFINED BOOST_ROOT) AND (DEFINED ENV{BOOST_ROOT}))
  set (BOOST_ROOT $ENV{BOOST_ROOT})
endif ()
file (TO_CMAKE_PATH "${BOOST_ROOT}" BOOST_ROOT)
if (WIN32 OR CYGWIN)
  # Workaround for MSVC having two boost versions - x86 and x64 on same PC in stage folders
  if (DEFINED BOOST_ROOT)
    if (IS_DIRECTORY ${BOOST_ROOT}/stage64/lib)
      set (BOOST_LIBRARYDIR ${BOOST_ROOT}/stage64/lib)
    else ()
      set (BOOST_LIBRARYDIR ${BOOST_ROOT}/stage/lib)
    endif ()
  endif ()
endif ()
message (STATUS "BOOST_ROOT: ${BOOST_ROOT}")
message (STATUS "BOOST_LIBRARYDIR: ${BOOST_LIBRARYDIR}")

# uncomment the following as needed to debug FindBoost issues:
#set (Boost_DEBUG ON)

#[=========================================================[
   boost dynamic libraries don't trivially support @rpath
   linking right now (cmake's default), so just force
   static linking for macos, or if requested on linux by flag
#]=========================================================]
if (static)
  set (Boost_USE_STATIC_LIBS ON)
endif ()
set (Boost_USE_MULTITHREADED ON)
if (static AND NOT APPLE)
  set (Boost_USE_STATIC_RUNTIME ON)
else ()
  set (Boost_USE_STATIC_RUNTIME OFF)
endif ()
find_package (Boost 1.67 REQUIRED
  COMPONENTS
    chrono
    context
    coroutine
    date_time
    filesystem
    program_options
    regex
    serialization
    system
    thread)

add_library (casinocoin_boost INTERFACE)
add_library (casinocoin::boost ALIAS casinocoin_boost)
if (is_xcode)
  target_include_directories (casinocoin_boost BEFORE INTERFACE ${Boost_INCLUDE_DIRS})
  target_compile_options (casinocoin_boost INTERFACE --system-header-prefix="boost/")
else ()
  target_include_directories (casinocoin_boost SYSTEM INTERFACE ${Boost_INCLUDE_DIRS})
endif()

target_link_libraries (casinocoin_boost
  INTERFACE
    Boost::boost
    Boost::chrono
    Boost::coroutine
    Boost::date_time
    Boost::filesystem
    Boost::program_options
    Boost::regex
    Boost::serialization
    Boost::system
    Boost::thread)

#[===================================================================[
   NIH dep: openssl
#]===================================================================]

#[===============================================[
  OPENSSL_ROOT_DIR is the only variable that
  FindOpenSSL honors for locating, so convert any
  OPENSSL_ROOT vars to this
#]===============================================]
if (NOT DEFINED OPENSSL_ROOT_DIR)
  if (DEFINED ENV{OPENSSL_ROOT})
    set (OPENSSL_ROOT_DIR $ENV{OPENSSL_ROOT})
  elseif (HOMEBREW)
    execute_process (COMMAND ${HOMEBREW} --prefix openssl
      OUTPUT_VARIABLE OPENSSL_ROOT_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif ()
  file (TO_CMAKE_PATH "${OPENSSL_ROOT_DIR}" OPENSSL_ROOT_DIR)
endif ()

if (static)
  set (OPENSSL_USE_STATIC_LIBS ON)
endif ()
set (OPENSSL_MSVC_STATIC_RT ON)
set (_ssl_min_ver 1.0.2)
# HACK for travis
if ("$ENV{CI}" STREQUAL "true" AND "$ENV{TRAVIS}" STREQUAL "true")
  set (_ssl_min_ver 1.0.1)
endif ()
find_package (OpenSSL ${_ssl_min_ver} REQUIRED)
target_link_libraries (casinocoin_libs
  INTERFACE
    OpenSSL::SSL
    OpenSSL::Crypto)
# disable SSLv2...this can also be done when building/configuring OpenSSL
set_target_properties(OpenSSL::SSL PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS OPENSSL_NO_SSL2)
#[=========================================================[
   https://gitlab.kitware.com/cmake/cmake/issues/16885
   depending on how openssl is built, it might depend
   on zlib. In fact, the openssl find package should
   figure this out for us, but it does not currently...
   so let's add zlib ourselves to the lib list
   TODO: investigate linking to static zlib for static
   build option
#]=========================================================]
find_package (ZLIB)
set (has_zlib FALSE)
if (TARGET ZLIB::ZLIB)
  set_target_properties(OpenSSL::Crypto PROPERTIES
    INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
  set (has_zlib TRUE)
endif ()

#[===================================================================[
   NIH dep: secp256k1
#]===================================================================]

add_library (secp256k1 STATIC
  src/secp256k1/src/secp256k1.c)
target_compile_definitions (secp256k1
  PRIVATE
    USE_NUM_NONE
    USE_FIELD_10X26
    USE_FIELD_INV_BUILTIN
    USE_SCALAR_8X32
    USE_SCALAR_INV_BUILTIN)
target_include_directories (secp256k1
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/secp256k1)
target_compile_options (secp256k1
  PRIVATE
    $<$<BOOL:${MSVC}>:-wd4319>
    $<$<NOT:$<BOOL:${MSVC}>>:
      -Wno-deprecated-declarations
      -Wno-unused-function
    >
    $<$<BOOL:${is_gcc}>:-Wno-nonnull-compare>)
add_library (NIH::secp256k1 ALIAS secp256k1)
target_link_libraries (casinocoin_libs INTERFACE NIH::secp256k1)
#[===========================[
   headers installation
#]===========================]
install (
  FILES
    src/secp256k1/include/secp256k1.h
  DESTINATION include/secp256k1/include)

#[===================================================================[
   NIH dep: ed25519-donna
#]===================================================================]

add_library (ed25519-donna STATIC
  src/ed25519-donna/ed25519.c)
target_include_directories (ed25519-donna
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ed25519-donna)
#[=========================================================[
   NOTE for macos:
   https://github.com/floodyberry/ed25519-donna/issues/29
   our source for ed25519-donna-portable.h has been
   patched to workaround this.
#]=========================================================]
target_link_libraries (ed25519-donna PUBLIC OpenSSL::SSL)
add_library (NIH::ed25519-donna ALIAS ed25519-donna)
target_link_libraries (casinocoin_libs INTERFACE NIH::ed25519-donna)
#[===========================[
   headers installation
#]===========================]
install (
  FILES
    src/ed25519-donna/ed25519.h
  DESTINATION include/ed25519-donna)

#[===================================================================[
   NIH dep: lz4
#]===================================================================]

ExternalProject_Add (lz4
  PREFIX ${nih_cache_path}
  GIT_REPOSITORY https://github.com/lz4/lz4.git
  GIT_TAG v1.8.2
  SOURCE_SUBDIR contrib/cmake_unofficial
  CMAKE_ARGS
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    -DBUILD_STATIC_LIBS=ON
    -DBUILD_SHARED_LIBS=OFF
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_C_FLAGS=-GR -Gd -fp:precise -FS -MP"
      "-DCMAKE_C_FLAGS_DEBUG=-MTd"
      "-DCMAKE_C_FLAGS_RELEASE=-MT"
    >
  LOG_BUILD ON
  LOG_CONFIGURE ON
  BUILD_COMMAND
    ${CMAKE_COMMAND}
    --build .
    --config $<CONFIG>
    --target lz4_static
    $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel$<$<BOOL:${is_xcode}>: ${num_procs}>>
    $<$<BOOL:${is_multiconfig}>:
      COMMAND
        ${CMAKE_COMMAND} -E copy
        <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}lz4$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
        <BINARY_DIR>
      >
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS
    <BINARY_DIR>/${ep_lib_prefix}lz4${ep_lib_suffix}
    <BINARY_DIR>/${ep_lib_prefix}lz4_d${ep_lib_suffix}
)
ExternalProject_Get_Property (lz4 BINARY_DIR)
ExternalProject_Get_Property (lz4 SOURCE_DIR)
ExternalProject_Get_Property (lz4 STAMP_DIR)
if (CMAKE_VERBOSE_MAKEFILE)
  print_ep_logs (lz4)
endif ()
add_library (lz4_lib STATIC IMPORTED GLOBAL)
file (MAKE_DIRECTORY ${SOURCE_DIR}/lz4)
set_target_properties (lz4_lib PROPERTIES
  IMPORTED_LOCATION_DEBUG
    ${BINARY_DIR}/${ep_lib_prefix}lz4_d${ep_lib_suffix}
  IMPORTED_LOCATION_RELEASE
    ${BINARY_DIR}/${ep_lib_prefix}lz4${ep_lib_suffix}
  INTERFACE_INCLUDE_DIRECTORIES
    ${SOURCE_DIR}/lib)
add_dependencies (lz4_lib lz4)
target_link_libraries (casinocoin_libs INTERFACE lz4_lib)
exclude_if_included (lz4)
exclude_if_included (lz4_lib)

#[===================================================================[
   NIH dep: libarchive
#]===================================================================]

set (lib_post "")
if (MSVC)
  set (lib_post "_static")
endif ()
ExternalProject_Add (libarchive
  PREFIX ${nih_cache_path}
  GIT_REPOSITORY https://github.com/libarchive/libarchive.git
  GIT_TAG v3.3.3
  CMAKE_ARGS
    # passing the compiler seems to be needed for windows CI, sadly
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    -DENABLE_LZ4=ON
    -ULZ4_*
    -DLZ4_INCLUDE_DIR=$<TARGET_PROPERTY:lz4_lib,INTERFACE_INCLUDE_DIRECTORIES>
    # because we are building a static lib, this lz4 library doesn't
    # actually matter since you can't generally link static libs to other static
    # libs. The include files are needed, but the library itself is not (until
    # we link our application, at which point we use the lz4 we built above).
    # nonetheless, we need to provide a library to libarchive else it will
    # NOT include lz4 support when configuring
    -DLZ4_LIBRARY=$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:lz4_lib,IMPORTED_LOCATION_DEBUG>,$<TARGET_PROPERTY:lz4_lib,IMPORTED_LOCATION_RELEASE>>
    -DENABLE_WERROR=OFF
    -DENABLE_TAR=OFF
    -DENABLE_TAR_SHARED=OFF
    -DENABLE_INSTALL=ON
    -DENABLE_NETTLE=OFF
    -DENABLE_OPENSSL=OFF
    -DENABLE_LZO=OFF
    -DENABLE_LZMA=OFF
    -DENABLE_ZLIB=OFF
    -DENABLE_BZip2=OFF
    -DENABLE_LIBXML2=OFF
    -DENABLE_EXPAT=OFF
    -DENABLE_PCREPOSIX=OFF
    -DENABLE_LibGCC=OFF
    -DENABLE_CNG=OFF
    -DENABLE_CPIO=OFF
    -DENABLE_CPIO_SHARED=OFF
    -DENABLE_CAT=OFF
    -DENABLE_CAT_SHARED=OFF
    -DENABLE_XATTR=OFF
    -DENABLE_ACL=OFF
    -DENABLE_ICONV=OFF
    -DENABLE_TEST=OFF
    -DENABLE_COVERAGE=OFF
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_C_FLAGS=-GR -Gd -fp:precise -FS -MP"
      "-DCMAKE_C_FLAGS_DEBUG=-MTd"
      "-DCMAKE_C_FLAGS_RELEASE=-MT"
    >
  LOG_BUILD ON
  LOG_CONFIGURE ON
  BUILD_COMMAND
    ${CMAKE_COMMAND}
    --build .
    --config $<CONFIG>
    --target archive_static
    $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel$<$<BOOL:${is_xcode}>: ${num_procs}>>
    $<$<BOOL:${is_multiconfig}>:
      COMMAND
        ${CMAKE_COMMAND} -E copy
          <BINARY_DIR>/libarchive/$<CONFIG>/${ep_lib_prefix}archive${lib_post}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>/libarchive
      >
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  DEPENDS lz4
  BUILD_BYPRODUCTS
    <BINARY_DIR>/libarchive/${ep_lib_prefix}archive${lib_post}${ep_lib_suffix}
    <BINARY_DIR>/libarchive/${ep_lib_prefix}archive${lib_post}_d${ep_lib_suffix}
)
ExternalProject_Get_Property (libarchive BINARY_DIR)
ExternalProject_Get_Property (libarchive SOURCE_DIR)
ExternalProject_Get_Property (libarchive STAMP_DIR)
if (CMAKE_VERBOSE_MAKEFILE)
  print_ep_logs (libarchive)
endif ()
add_library (archive_lib STATIC IMPORTED GLOBAL)
file (MAKE_DIRECTORY ${SOURCE_DIR}/libarchive)
set_target_properties (archive_lib PROPERTIES
  IMPORTED_LOCATION_DEBUG
    ${BINARY_DIR}/libarchive/${ep_lib_prefix}archive${lib_post}_d${ep_lib_suffix}
  IMPORTED_LOCATION_RELEASE
    ${BINARY_DIR}/libarchive/${ep_lib_prefix}archive${lib_post}${ep_lib_suffix}
  INTERFACE_INCLUDE_DIRECTORIES
    ${SOURCE_DIR}/libarchive
  INTERFACE_COMPILE_DEFINITIONS
    LIBARCHIVE_STATIC)
add_dependencies (archive_lib libarchive)
target_link_libraries (archive_lib INTERFACE lz4_lib)
target_link_libraries (casinocoin_libs INTERFACE archive_lib)
exclude_if_included (libarchive)
exclude_if_included (archive_lib)

#[===================================================================[
   NIH dep: sqlite
#]===================================================================]

ExternalProject_Add (sqlite3
  PREFIX ${nih_cache_path}
  # sqlite doesn't use git, but it provides versioned tarballs
  URL https://www.sqlite.org/2018/sqlite-amalgamation-3260000.zip
  # ^^^ version is apparent in the URL:  3260000 => 3.26.0
  URL_HASH SHA256=de5dcab133aa339a4cf9e97c40aa6062570086d6085d8f9ad7bc6ddf8a52096e
  # we wrote a very simple CMake file to build sqlite
  # so that's what we copy here so that we can build with
  # CMake. sqlite doesn't generally provided a build system
  # for the single amalgamation source file.
  PATCH_COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/Builds/CMake/CMake_sqlite3.txt
    <SOURCE_DIR>/CMakeLists.txt
  CMAKE_ARGS
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_C_FLAGS=-GR -Gd -fp:precise -FS -MP"
      "-DCMAKE_C_FLAGS_DEBUG=-MTd"
      "-DCMAKE_C_FLAGS_RELEASE=-MT"
    >
  LOG_BUILD ON
  LOG_CONFIGURE ON
  BUILD_COMMAND
    ${CMAKE_COMMAND}
    --build .
    --config $<CONFIG>
    $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel$<$<BOOL:${is_xcode}>: ${num_procs}>>
    $<$<BOOL:${is_multiconfig}>:
      COMMAND
        ${CMAKE_COMMAND} -E copy
          <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}sqlite3$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>
      >
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS
    <BINARY_DIR>/${ep_lib_prefix}sqlite3${ep_lib_suffix}
    <BINARY_DIR>/${ep_lib_prefix}sqlite3_d${ep_lib_suffix}
)
ExternalProject_Get_Property (sqlite3 BINARY_DIR)
ExternalProject_Get_Property (sqlite3 SOURCE_DIR)
ExternalProject_Get_Property (sqlite3 STAMP_DIR)
if (CMAKE_VERBOSE_MAKEFILE)
  print_ep_logs (sqlite3)
endif ()
add_library (sqlite STATIC IMPORTED GLOBAL)
set_target_properties (sqlite PROPERTIES
  IMPORTED_LOCATION_DEBUG
    ${BINARY_DIR}/${ep_lib_prefix}sqlite3_d${ep_lib_suffix}
  IMPORTED_LOCATION_RELEASE
    ${BINARY_DIR}/${ep_lib_prefix}sqlite3${ep_lib_suffix}
  INTERFACE_INCLUDE_DIRECTORIES
    ${SOURCE_DIR})
add_dependencies (sqlite sqlite3)
target_link_libraries (sqlite INTERFACE $<$<NOT:$<BOOL:${MSVC}>>:dl>)
target_link_libraries (casinocoin_libs INTERFACE sqlite)
exclude_if_included (sqlite3)
exclude_if_included (sqlite)
set(sqlite_BINARY_DIR ${BINARY_DIR})

#[===================================================================[
   NIH dep: soci
#]===================================================================]

set (soci_lib_pre ${ep_lib_prefix})
set (soci_lib_post "")
if (WIN32)
  # for some reason soci on windows still prepends lib (non-standard)
  set (soci_lib_pre lib)
  # this version in the name might change if/when we change versions of soci
  set (soci_lib_post "_4_0")
endif ()
ExternalProject_Add (soci
  PREFIX ${nih_cache_path}
  GIT_REPOSITORY https://github.com/SOCI/soci.git
  GIT_TAG 3a1f602b3021b925d38828e3ff95f9e7f8887ff7
  # We had an issue with soci integer range checking for boost::optional
  # and needed to remove the exception that SOCI throws in this case.
  # This is *probably* a bug in SOCI, but has never been investigated more
  # nor reported to the maintainers.
  # This cmake script comments out the lines in question.
  # This patch process is likely fragile and should be reviewed carefully
  # whenever we update the GIT_TAG above.
  PATCH_COMMAND
    ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/Builds/CMake/soci_patch.cmake
  CMAKE_ARGS
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/sqlite3
    -DCMAKE_INCLUDE_PATH=$<TARGET_PROPERTY:sqlite,INTERFACE_INCLUDE_DIRECTORIES>
    -DCMAKE_LIBRARY_PATH=${sqlite_BINARY_DIR}
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    -DSOCI_CXX_C11=ON
    -DSOCI_STATIC=ON
    -DSOCI_LIBDIR=lib
    -DSOCI_SHARED=OFF
    -DSOCI_TESTS=OFF
    -DBOOST_ROOT=${Boost_DIR}
    -DWITH_BOOST=ON
    -DSOCI_DB2=OFF
    -DSOCI_FIREBIRD=OFF
    -DSOCI_MYSQL=OFF
    -DSOCI_ODBC=OFF
    -DSOCI_ORACLE=OFF
    -DSOCI_POSTGRESQL=OFF
    -DSOCI_SQLITE3=ON
    -DSQLITE3_INCLUDE_DIR=$<TARGET_PROPERTY:sqlite,INTERFACE_INCLUDE_DIRECTORIES>
    -DSQLITE3_LIBRARY=$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:sqlite,IMPORTED_LOCATION_DEBUG>,$<TARGET_PROPERTY:sqlite,IMPORTED_LOCATION_RELEASE>>
    $<$<BOOL:${APPLE}>:-DCMAKE_FIND_FRAMEWORK=LAST>
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_CXX_FLAGS=-GR -Gd -fp:precise -FS -EHa -MP"
      "-DCMAKE_CXX_FLAGS_DEBUG=-MTd"
      "-DCMAKE_CXX_FLAGS_RELEASE=-MT"
    >
    $<$<NOT:$<BOOL:${MSVC}>>:
      "-DCMAKE_CXX_FLAGS=-Wno-deprecated-declarations"
    >
    # SEE: https://github.com/SOCI/soci/issues/640
    $<$<AND:$<BOOL:${is_gcc}>,$<VERSION_GREATER_EQUAL:${CMAKE_CXX_COMPILER_VERSION},8>>:
      "-DCMAKE_CXX_FLAGS=-Wno-deprecated-declarations -Wno-error=format-overflow -Wno-format-overflow -Wno-error=format-truncation"
    >
  LOG_BUILD ON
  LOG_CONFIGURE ON
  BUILD_COMMAND
    ${CMAKE_COMMAND}
    --build .
    --config $<CONFIG>
    $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel$<$<BOOL:${is_xcode}>: ${num_procs}>>
    $<$<BOOL:${is_multiconfig}>:
      COMMAND
        ${CMAKE_COMMAND} -E copy
        <BINARY_DIR>/lib/$<CONFIG>/${soci_lib_pre}soci_core${soci_lib_post}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
        <BINARY_DIR>/lib/$<CONFIG>/${soci_lib_pre}soci_empty${soci_lib_post}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
        <BINARY_DIR>/lib/$<CONFIG>/${soci_lib_pre}soci_sqlite3${soci_lib_post}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
        <BINARY_DIR>/lib
      >
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  DEPENDS sqlite3
  BUILD_BYPRODUCTS
    <BINARY_DIR>/lib/${soci_lib_pre}soci_core${soci_lib_post}${ep_lib_suffix}
    <BINARY_DIR>/lib/${soci_lib_pre}soci_core${soci_lib_post}_d${ep_lib_suffix}
    <BINARY_DIR>/lib/${soci_lib_pre}soci_empty${soci_lib_post}${ep_lib_suffix}
    <BINARY_DIR>/lib/${soci_lib_pre}soci_empty${soci_lib_post}_d${ep_lib_suffix}
    <BINARY_DIR>/lib/${soci_lib_pre}soci_sqlite3${soci_lib_post}${ep_lib_suffix}
    <BINARY_DIR>/lib/${soci_lib_pre}soci_sqlite3${soci_lib_post}_d${ep_lib_suffix}
)
ExternalProject_Get_Property (soci BINARY_DIR)
ExternalProject_Get_Property (soci SOURCE_DIR)
if (CMAKE_VERBOSE_MAKEFILE)
  print_ep_logs (soci)
endif ()
file (MAKE_DIRECTORY ${SOURCE_DIR}/include)
file (MAKE_DIRECTORY ${BINARY_DIR}/include)
foreach (_comp core empty sqlite3)
  add_library ("soci_${_comp}" STATIC IMPORTED GLOBAL)
  set_target_properties ("soci_${_comp}" PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${BINARY_DIR}/lib/${soci_lib_pre}soci_${_comp}${soci_lib_post}_d${ep_lib_suffix}
    IMPORTED_LOCATION_RELEASE
      ${BINARY_DIR}/lib/${soci_lib_pre}soci_${_comp}${soci_lib_post}${ep_lib_suffix}
    INTERFACE_INCLUDE_DIRECTORIES
      "${SOURCE_DIR}/include;${BINARY_DIR}/include")
  add_dependencies ("soci_${_comp}" soci) # something has to depend on the ExternalProject to trigger it
  target_link_libraries (casinocoin_libs INTERFACE "soci_${_comp}")
  if (NOT _comp STREQUAL "core")
    target_link_libraries ("soci_${_comp}" INTERFACE soci_core)
  endif ()
  exclude_if_included ("soci_${_comp}")
endforeach ()
exclude_if_included (soci)

#[===================================================================[
   NIH dep: snappy
#]===================================================================]

ExternalProject_Add (snappy
  PREFIX ${nih_cache_path}
  GIT_REPOSITORY https://github.com/google/snappy.git
  GIT_TAG 1.1.7
  CMAKE_ARGS
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
    -DCMAKE_DEBUG_POSTFIX=_d
    $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
    -DBUILD_SHARED_LIBS=OFF
    -DSNAPPY_BUILD_TESTS=OFF
    $<$<BOOL:${MSVC}>:
      "-DCMAKE_C_FLAGS=-GR -Gd -fp:precise -FS -MP"
      "-DCMAKE_C_FLAGS_DEBUG=-MTd"
      "-DCMAKE_C_FLAGS_RELEASE=-MT"
    >
  LOG_BUILD ON
  LOG_CONFIGURE ON
  BUILD_COMMAND
    ${CMAKE_COMMAND}
    --build .
    --config $<CONFIG>
    $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel$<$<BOOL:${is_xcode}>: ${num_procs}>>
    $<$<BOOL:${is_multiconfig}>:
      COMMAND
        ${CMAKE_COMMAND} -E copy
        <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}snappy$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
        <BINARY_DIR>
      >
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS
    <BINARY_DIR>/${ep_lib_prefix}snappy${ep_lib_suffix}
    <BINARY_DIR>/${ep_lib_prefix}snappy_d${ep_lib_suffix}
)
ExternalProject_Get_Property (snappy BINARY_DIR)
ExternalProject_Get_Property (snappy SOURCE_DIR)
ExternalProject_Get_Property (snappy STAMP_DIR)
if (CMAKE_VERBOSE_MAKEFILE)
  print_ep_logs (snappy)
endif ()
add_library (snappy_lib STATIC IMPORTED GLOBAL)
file (MAKE_DIRECTORY ${SOURCE_DIR}/snappy)
set_target_properties (snappy_lib PROPERTIES
  IMPORTED_LOCATION_DEBUG
    ${BINARY_DIR}/${ep_lib_prefix}snappy_d${ep_lib_suffix}
  IMPORTED_LOCATION_RELEASE
    ${BINARY_DIR}/${ep_lib_prefix}snappy${ep_lib_suffix}
  INTERFACE_INCLUDE_DIRECTORIES
    "${SOURCE_DIR};${BINARY_DIR}")
add_dependencies (snappy_lib snappy)
target_link_libraries (casinocoin_libs INTERFACE snappy_lib)
exclude_if_included (snappy)
exclude_if_included (snappy_lib)

#[===================================================================[
   NIH dep: rocksdb
#]===================================================================]

if (NOT MSVC)
  add_library (rocksdb STATIC
    #[=========================================================[
       this unity file is our interpretation of what sources
       constitute rocksdb
    #]=========================================================]
    src/casinocoin/unity/rocksdb.cpp)
  target_compile_options (rocksdb
    PRIVATE
      $<$<BOOL:${is_gcc}>:-w>
    PUBLIC
      $<$<AND:$<CXX_COMPILER_ID:Clang>,$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,7>>:
        -Wno-inconsistent-missing-override
        -Wno-uninitialized
      >
      $<$<BOOL:${is_clang}>:--system-header-prefix=rocksdb2>)
  target_include_directories (rocksdb
    PRIVATE
      src
      src/rocksdb2
    PUBLIC
      src/rocksdb2/include)
  target_compile_definitions (rocksdb
    PUBLIC
      SNAPPY
      casinocoin_ROCKSDB_AVAILABLE=1
      $<$<BOOL:${WIN32}>:ROCKSDB_PLATFORM_WINDOWS>
      $<$<NOT:$<BOOL:${WIN32}>>:
        ROCKSDB_PLATFORM_POSIX
        ROCKSDB_LIB_IO_POSIX
      >
      $<$<BOOL:${APPLE}>:OS_MACOSX=1>
      $<$<PLATFORM_ID:Linux>:OS_LINUX>
    )
  target_link_libraries (rocksdb snappy_lib)
  exclude_if_included (rocksdb)
else ()
  add_library (rocksdb INTERFACE)
  target_compile_definitions (rocksdb INTERFACE casinocoin_ROCKSDB_AVAILABLE=0)
endif ()
add_library (NIH::rocksdb ALIAS rocksdb)
target_link_libraries (casinocoin_libs INTERFACE NIH::rocksdb)

#[===================================================================[
   NIH dep: nudb

   NuDB is header-only, thus is an INTERFACE lib in CMake.
   TODO: move the library definition into NuDB repo and add
   proper targets and export/install
#]===================================================================]

add_library (nudb INTERFACE)
if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.11)
  FetchContent_Declare(
    nudb_src
    GIT_REPOSITORY https://github.com/vinniefalco/NuDB.git
    GIT_TAG        1.0.0
  )
  FetchContent_GetProperties(nudb_src)
  if(NOT nudb_src_POPULATED)
    message (STATUS "Pausing to download NuDB...")
    FetchContent_Populate(nudb_src)
  endif()
else ()
  ExternalProject_Add (nudb_src
    PREFIX ${nih_cache_path}
    GIT_REPOSITORY https://github.com/vinniefalco/NuDB.git
    GIT_TAG 1.0.0
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    TEST_COMMAND ""
    INSTALL_COMMAND ""
  )
  ExternalProject_Get_Property (nudb_src SOURCE_DIR)
  set (nudb_src_SOURCE_DIR "${SOURCE_DIR}")
  file (MAKE_DIRECTORY ${nudb_src_SOURCE_DIR}/include)
  add_dependencies (nudb nudb_src)
endif ()

file(TO_CMAKE_PATH "${nudb_src_SOURCE_DIR}" nudb_src_SOURCE_DIR)
# specify as system includes so as to avoid warnings
target_include_directories (nudb SYSTEM INTERFACE ${nudb_src_SOURCE_DIR}/include)
target_link_libraries (nudb
  INTERFACE
    Boost::thread
    Boost::system)
add_library (NIH::nudb ALIAS nudb)
target_link_libraries (casinocoin_libs INTERFACE NIH::nudb)

#[===================================================================[
   import protobuf (lib and compiler) and create a lib
   from our proto message definitions. If the system protobuf
   is not found, fallback on EP to download and build a version
   from official source.
#]===================================================================]

if (static)
  set (Protobuf_USE_STATIC_LIBS ON)
endif ()
find_package (Protobuf)
if (local_protobuf OR NOT TARGET protobuf::libprotobuf)
  message (STATUS "using local protobuf build.")
  if (WIN32)
    # protobuf prepends lib even on windows
    set (pbuf_lib_pre "lib")
  else ()
    set (pbuf_lib_pre ${ep_lib_prefix})
  endif ()
  # for the external project build of protobuf, we currently ignore the
  # static option and always build static libs here. This is consistent
  # with our other EP builds. Dynamic libs in an EP would add complexity
  # because we'd need to get them into the runtime path, and probably
  # install them.
  ExternalProject_Add (protobuf_src
    PREFIX ${nih_cache_path}
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG v3.6.1
    SOURCE_SUBDIR cmake
    CMAKE_ARGS
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      -Dprotobuf_BUILD_TESTS=OFF
      -Dprotobuf_BUILD_EXAMPLES=OFF
      -Dprotobuf_BUILD_PROTOC_BINARIES=ON
      -Dprotobuf_MSVC_STATIC_RUNTIME=ON
      -DBUILD_SHARED_LIBS=OFF
      -Dprotobuf_BUILD_SHARED_LIBS=OFF
      -DCMAKE_DEBUG_POSTFIX=_d
      -Dprotobuf_DEBUG_POSTFIX=_d
      -Dprotobuf_WITH_ZLIB=$<IF:$<BOOL:${has_zlib}>,ON,OFF>
      $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
      $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
      $<$<BOOL:${MSVC}>:
	"-DCMAKE_CXX_FLAGS=-GR -Gd -fp:precise -FS -EHa -MP"
      >
    LOG_BUILD ON
    LOG_CONFIGURE ON
    BUILD_COMMAND
      ${CMAKE_COMMAND}
      --build .
      --config $<CONFIG>
      $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel$<$<BOOL:${is_xcode}>: ${num_procs}>>
      $<$<BOOL:${is_multiconfig}>:
        COMMAND
          ${CMAKE_COMMAND} -E copy
          <BINARY_DIR>/$<CONFIG>/${pbuf_lib_pre}protobuf$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>
        COMMAND
          ${CMAKE_COMMAND} -E copy
          <BINARY_DIR>/$<CONFIG>/protoc${CMAKE_EXECUTABLE_SUFFIX}
          <BINARY_DIR>
        >
    TEST_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
      <BINARY_DIR>/${pbuf_lib_pre}protobuf${ep_lib_suffix}
      <BINARY_DIR>/${pbuf_lib_pre}protobuf_d${ep_lib_suffix}
      <BINARY_DIR>/protoc${CMAKE_EXECUTABLE_SUFFIX}
  )
  ExternalProject_Get_Property (protobuf_src BINARY_DIR)
  ExternalProject_Get_Property (protobuf_src SOURCE_DIR)
  ExternalProject_Get_Property (protobuf_src STAMP_DIR)
  if (CMAKE_VERBOSE_MAKEFILE)
    print_ep_logs (protobuf_src)
  endif ()

  if (NOT TARGET protobuf::libprotobuf)
    add_library (protobuf::libprotobuf STATIC IMPORTED GLOBAL)
  endif ()
  file (MAKE_DIRECTORY ${SOURCE_DIR}/src)
  set_target_properties (protobuf::libprotobuf PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${BINARY_DIR}/${pbuf_lib_pre}protobuf_d${ep_lib_suffix}
    IMPORTED_LOCATION_RELEASE
      ${BINARY_DIR}/${pbuf_lib_pre}protobuf${ep_lib_suffix}
    INTERFACE_INCLUDE_DIRECTORIES
      ${SOURCE_DIR}/src)
  add_dependencies (protobuf::libprotobuf protobuf_src)
  exclude_if_included (protobuf_src)
  exclude_if_included (protobuf::libprotobuf)

  if (NOT TARGET protobuf::protoc)
    add_executable (protobuf::protoc IMPORTED)
    exclude_if_included (protobuf::protoc)
  endif ()
  set_target_properties (protobuf::protoc PROPERTIES
    IMPORTED_LOCATION "${BINARY_DIR}/protoc${CMAKE_EXECUTABLE_SUFFIX}")
  add_dependencies (protobuf::protoc protobuf_src)
endif ()

file (MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/proto_gen)
set (save_CBD ${CMAKE_CURRENT_BINARY_DIR})
set (CMAKE_CURRENT_BINARY_DIR ${CMAKE_BINARY_DIR}/proto_gen)
protobuf_generate_cpp (
  PROTO_SRCS
  PROTO_HDRS
  src/casinocoin/proto/casinocoin.proto)
set (CMAKE_CURRENT_BINARY_DIR ${save_CBD})

add_library (pbufs STATIC ${PROTO_SRCS} ${PROTO_HDRS})

target_include_directories (pbufs PRIVATE src)
target_include_directories (pbufs
  SYSTEM PUBLIC ${CMAKE_BINARY_DIR}/proto_gen)
target_link_libraries (pbufs protobuf::libprotobuf)
target_compile_options (pbufs
  PUBLIC
    $<$<BOOL:${is_xcode}>:
      --system-header-prefix="google/protobuf"
      -Wno-deprecated-dynamic-exception-spec
    >)
add_library (casinocoin::pbufs ALIAS pbufs)
target_link_libraries (casinocoin_libs INTERFACE casinocoin::pbufs)
exclude_if_included (pbufs)

#[===================================================================[
   xrpl_core
   core functionality, useable by some client software perhaps
#]===================================================================]

file (GLOB_RECURSE rb_headers
  src/casinocoin/beast/*.h
  src/casinocoin/beast/*.hpp)

add_library (xrpl_core
  ${rb_headers}) ## headers added here for benefit of IDEs

#[===============================[
    beast/legacy FILES:
    TODO: review these sources for removal or replacement
#]===============================]
if (unity)
  target_sources (xrpl_core PRIVATE
    src/casinocoin/beast/core/core.unity.cpp
    src/casinocoin/beast/unity/beast_hash_unity.cpp
    src/casinocoin/beast/unity/beast_insight_unity.cpp
    src/casinocoin/beast/unity/beast_net_unity.cpp
    src/casinocoin/beast/unity/beast_utility_unity.cpp)
else ()
  target_sources (xrpl_core PRIVATE
    src/casinocoin/beast/core/CurrentThreadName.cpp
    src/casinocoin/beast/core/SemanticVersion.cpp
    src/casinocoin/beast/hash/impl/xxhash.cpp
    src/casinocoin/beast/insight/impl/Collector.cpp
    src/casinocoin/beast/insight/impl/Groups.cpp
    src/casinocoin/beast/insight/impl/Hook.cpp
    src/casinocoin/beast/insight/impl/Metric.cpp
    src/casinocoin/beast/insight/impl/NullCollector.cpp
    src/casinocoin/beast/insight/impl/StatsDCollector.cpp
    src/casinocoin/beast/net/impl/IPAddressConversion.cpp
    src/casinocoin/beast/net/impl/IPAddressV4.cpp
    src/casinocoin/beast/net/impl/IPAddressV6.cpp
    src/casinocoin/beast/net/impl/IPEndpoint.cpp
    src/casinocoin/beast/utility/src/beast_Journal.cpp
    src/casinocoin/beast/utility/src/beast_PropertyStream.cpp)
endif ()

#[===============================[
    core sources
#]===============================]
if (unity)
  target_sources (xrpl_core PRIVATE
    src/casinocoin/unity/basics1.cpp
    src/casinocoin/unity/json.cpp
    src/casinocoin/unity/protocol.cpp
    src/casinocoin/unity/crypto.cpp)
else ()
  target_sources (xrpl_core PRIVATE
    #[===============================[
      nounity, main sources:
        subdir: basics (partial)
    #]===============================]
    src/casinocoin/basics/impl/base64.cpp
    src/casinocoin/basics/impl/contract.cpp
    src/casinocoin/basics/impl/CountedObject.cpp
    src/casinocoin/basics/impl/FileUtilities.cpp
    src/casinocoin/basics/impl/Log.cpp
    src/casinocoin/basics/impl/strHex.cpp
    src/casinocoin/basics/impl/StringUtilities.cpp
    #[===============================[
      nounity, main sources:
        subdir: json
    #]===============================]
    src/casinocoin/json/impl/JsonPropertyStream.cpp
    src/casinocoin/json/impl/Object.cpp
    src/casinocoin/json/impl/Output.cpp
    src/casinocoin/json/impl/Writer.cpp
    src/casinocoin/json/impl/json_reader.cpp
    src/casinocoin/json/impl/json_value.cpp
    src/casinocoin/json/impl/json_valueiterator.cpp
    src/casinocoin/json/impl/json_writer.cpp
    src/casinocoin/json/impl/to_string.cpp
    #[===============================[
      nounity, main sources:
        subdir: protocol
    #]===============================]
    src/casinocoin/protocol/impl/AccountID.cpp
    src/casinocoin/protocol/impl/Book.cpp
    src/casinocoin/protocol/impl/BuildInfo.cpp
    src/casinocoin/protocol/impl/ErrorCodes.cpp
    src/casinocoin/protocol/impl/Feature.cpp
    src/casinocoin/protocol/impl/HashPrefix.cpp
    src/casinocoin/protocol/impl/IOUAmount.cpp
    src/casinocoin/protocol/impl/Indexes.cpp
    src/casinocoin/protocol/impl/InnerObjectFormats.cpp
    src/casinocoin/protocol/impl/Issue.cpp
    src/casinocoin/protocol/impl/Keylet.cpp
    src/casinocoin/protocol/impl/LedgerFormats.cpp
    src/casinocoin/protocol/impl/PublicKey.cpp
    src/casinocoin/protocol/impl/Quality.cpp
    src/casinocoin/protocol/impl/Rate2.cpp
    src/casinocoin/protocol/impl/SField.cpp
    src/casinocoin/protocol/impl/SOTemplate.cpp
    src/casinocoin/protocol/impl/STAccount.cpp
    src/casinocoin/protocol/impl/STAmount.cpp
    src/casinocoin/protocol/impl/STArray.cpp
    src/casinocoin/protocol/impl/STBase.cpp
    src/casinocoin/protocol/impl/STBlob.cpp
    src/casinocoin/protocol/impl/STInteger.cpp
    src/casinocoin/protocol/impl/STLedgerEntry.cpp
    src/casinocoin/protocol/impl/STObject.cpp
    src/casinocoin/protocol/impl/STParsedJSON.cpp
    src/casinocoin/protocol/impl/STPathSet.cpp
    src/casinocoin/protocol/impl/STTx.cpp
    src/casinocoin/protocol/impl/STValidation.cpp
    src/casinocoin/protocol/impl/STVar.cpp
    src/casinocoin/protocol/impl/STVector256.cpp
    src/casinocoin/protocol/impl/SecretKey.cpp
    src/casinocoin/protocol/impl/Seed.cpp
    src/casinocoin/protocol/impl/Serializer.cpp
    src/casinocoin/protocol/impl/Sign.cpp
    src/casinocoin/protocol/impl/TER.cpp
    src/casinocoin/protocol/impl/TxFormats.cpp
    src/casinocoin/protocol/impl/UintTypes.cpp
    src/casinocoin/protocol/impl/digest.cpp
    src/casinocoin/protocol/impl/tokens.cpp
    #[===============================[
      nounity, main sources:
        subdir: crypto
    #]===============================]
    src/casinocoin/crypto/impl/GenerateDeterministicKey.cpp
    src/casinocoin/crypto/impl/RFC1751.cpp
    src/casinocoin/crypto/impl/csprng.cpp
    src/casinocoin/crypto/impl/ec_key.cpp
    src/casinocoin/crypto/impl/openssl.cpp)
endif ()
add_library (casinocoin::xrpl_core ALIAS xrpl_core)
target_include_directories (xrpl_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/casinocoin>
    # this one is for beast/legacy files:
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/beast/extras>
    $<INSTALL_INTERFACE:include>)
target_compile_options (xrpl_core
  PUBLIC
    $<$<BOOL:${is_gcc}>:-Wno-maybe-uninitialized>)
target_link_libraries (xrpl_core
  PUBLIC
    OpenSSL::Crypto
    NIH::secp256k1
    NIH::ed25519-donna
    casinocoin::syslibs
    casinocoin::boost
    casinocoin::opts)
#[=================================[
   main/core headers installation
#]=================================]
install (
  FILES
    src/casinocoin/basics/base64.h
    src/casinocoin/basics/Blob.h
    src/casinocoin/basics/Buffer.h
    src/casinocoin/basics/CountedObject.h
    src/casinocoin/basics/FileUtilities.h
    src/casinocoin/basics/LocalValue.h
    src/casinocoin/basics/Log.h
    src/casinocoin/basics/safe_cast.h
    src/casinocoin/basics/Slice.h
    src/casinocoin/basics/StringUtilities.h
    src/casinocoin/basics/ToString.h
    src/casinocoin/basics/UnorderedContainers.h
    src/casinocoin/basics/base_uint.h
    src/casinocoin/basics/chrono.h
    src/casinocoin/basics/contract.h
    src/casinocoin/basics/date.h
    src/casinocoin/basics/hardened_hash.h
    src/casinocoin/basics/strHex.h
  DESTINATION include/casinocoin/basics)
install (
  FILES
    src/casinocoin/crypto/GenerateDeterministicKey.h
    src/casinocoin/crypto/KeyType.h
    src/casinocoin/crypto/RFC1751.h
    src/casinocoin/crypto/csprng.h
  DESTINATION include/casinocoin/crypto)
install (
  FILES
    src/casinocoin/crypto/impl/ec_key.h
    src/casinocoin/crypto/impl/openssl.h
  DESTINATION include/casinocoin/crypto/impl)
install (
  FILES
    src/casinocoin/json/JsonPropertyStream.h
    src/casinocoin/json/Object.h
    src/casinocoin/json/Output.h
    src/casinocoin/json/Writer.h
    src/casinocoin/json/json_forwards.h
    src/casinocoin/json/json_reader.h
    src/casinocoin/json/json_value.h
    src/casinocoin/json/json_writer.h
    src/casinocoin/json/to_string.h
  DESTINATION include/casinocoin/json)
install (
  FILES
    src/casinocoin/json/impl/json_assert.h
  DESTINATION include/casinocoin/json/impl)
install (
  FILES
    src/casinocoin/protocol/AccountID.h
    src/casinocoin/protocol/AmountConversions.h
    src/casinocoin/protocol/Book.h
    src/casinocoin/protocol/BuildInfo.h
    src/casinocoin/protocol/ErrorCodes.h
    src/casinocoin/protocol/Feature.h
    src/casinocoin/protocol/HashPrefix.h
    src/casinocoin/protocol/IOUAmount.h
    src/casinocoin/protocol/Indexes.h
    src/casinocoin/protocol/InnerObjectFormats.h
    src/casinocoin/protocol/Issue.h
    src/casinocoin/protocol/JsonFields.h
    src/casinocoin/protocol/Keylet.h
    src/casinocoin/protocol/KnownFormats.h
    src/casinocoin/protocol/LedgerFormats.h
    src/casinocoin/protocol/Protocol.h
    src/casinocoin/protocol/PublicKey.h
    src/casinocoin/protocol/Quality.h
    src/casinocoin/protocol/Rate.h
    src/casinocoin/protocol/SField.h
    src/casinocoin/protocol/SOTemplate.h
    src/casinocoin/protocol/STAccount.h
    src/casinocoin/protocol/STAmount.h
    src/casinocoin/protocol/STArray.h
    src/casinocoin/protocol/STBase.h
    src/casinocoin/protocol/STBitString.h
    src/casinocoin/protocol/STBlob.h
    src/casinocoin/protocol/STExchange.h
    src/casinocoin/protocol/STInteger.h
    src/casinocoin/protocol/STLedgerEntry.h
    src/casinocoin/protocol/STObject.h
    src/casinocoin/protocol/STParsedJSON.h
    src/casinocoin/protocol/STPathSet.h
    src/casinocoin/protocol/STTx.h
    src/casinocoin/protocol/STValidation.h
    src/casinocoin/protocol/STVector256.h
    src/casinocoin/protocol/SecretKey.h
    src/casinocoin/protocol/Seed.h
    src/casinocoin/protocol/Serializer.h
    src/casinocoin/protocol/Sign.h
    src/casinocoin/protocol/SystemParameters.h
    src/casinocoin/protocol/TER.h
    src/casinocoin/protocol/TxFlags.h
    src/casinocoin/protocol/TxFormats.h
    src/casinocoin/protocol/UintTypes.h
    src/casinocoin/protocol/XRPAmount.h
    src/casinocoin/protocol/digest.h
    src/casinocoin/protocol/tokens.h
  DESTINATION include/casinocoin/protocol)
install (
  FILES
    src/casinocoin/protocol/impl/STVar.h
    src/casinocoin/protocol/impl/secp256k1.h
  DESTINATION include/casinocoin/protocol/impl)

#[===================================[
   beast/legacy headers installation
#]===================================]
install (
  FILES
    src/casinocoin/beast/clock/abstract_clock.h
    src/casinocoin/beast/clock/basic_seconds_clock.h
    src/casinocoin/beast/clock/manual_clock.h
  DESTINATION include/casinocoin/beast/clock)
install (
  FILES
    src/casinocoin/beast/core/Config.h
    src/casinocoin/beast/core/ConfigCheck.h
    src/casinocoin/beast/core/LexicalCast.h
    src/casinocoin/beast/core/List.h
    src/casinocoin/beast/core/PlatformConfig.h
    src/casinocoin/beast/core/SemanticVersion.h
    src/casinocoin/beast/core/StandardConfig.h
  DESTINATION include/casinocoin/beast/core)
install (
  FILES
    src/casinocoin/beast/crypto/detail/mac_facade.h
    src/casinocoin/beast/crypto/detail/ripemd_context.h
    src/casinocoin/beast/crypto/detail/sha2_context.h
    src/casinocoin/beast/crypto/ripemd.h
    src/casinocoin/beast/crypto/secure_erase.h
    src/casinocoin/beast/crypto/sha2.h
  DESTINATION include/casinocoin/beast/crypto)
install (
  FILES
    src/casinocoin/beast/hash/endian.h
    src/casinocoin/beast/hash/hash_append.h
    src/casinocoin/beast/hash/meta.h
    src/casinocoin/beast/hash/uhash.h
    src/casinocoin/beast/hash/xxhasher.h
  DESTINATION include/casinocoin/beast/hash)
install (
  FILES src/casinocoin/beast/hash/impl/xxhash.h
  DESTINATION include/casinocoin/beast/hash/impl)
install (
  FILES
    src/casinocoin/beast/rfc2616.h
    src/casinocoin/beast/type_name.h
    src/casinocoin/beast/unit_test.h
    src/casinocoin/beast/xor_shift_engine.h
  DESTINATION include/casinocoin/beast)
install (
  FILES
    src/casinocoin/beast/utility/Journal.h
    src/casinocoin/beast/utility/PropertyStream.h
    src/casinocoin/beast/utility/Zero.h
    src/casinocoin/beast/utility/rngfill.h
  DESTINATION include/casinocoin/beast/utility)
# WARNING!! -- horrible levelization ahead
# (these files should be isolated or moved...but
#  unfortunately unit_test.h above creates this dependency)
install (
  FILES
    src/beast/extras/beast/unit_test/amount.hpp
    src/beast/extras/beast/unit_test/dstream.hpp
    src/beast/extras/beast/unit_test/global_suites.hpp
    src/beast/extras/beast/unit_test/match.hpp
    src/beast/extras/beast/unit_test/recorder.hpp
    src/beast/extras/beast/unit_test/reporter.hpp
    src/beast/extras/beast/unit_test/results.hpp
    src/beast/extras/beast/unit_test/runner.hpp
    src/beast/extras/beast/unit_test/suite.hpp
    src/beast/extras/beast/unit_test/suite_info.hpp
    src/beast/extras/beast/unit_test/suite_list.hpp
    src/beast/extras/beast/unit_test/thread.hpp
  DESTINATION include/beast/unit_test)
install (
  FILES
    src/beast/extras/beast/unit_test/detail/const_container.hpp
  DESTINATION include/beast/unit_test/detail)

#[===================================================================[
   casinocoind executable
#]===================================================================]

#[=========================================================[
   this one header is added as source just to keep older
   versions of cmake happy. cmake 3.10+ allows
   add_executable with no sources
#]=========================================================]
add_executable (casinocoind src/casinocoin/app/main/Application.h)
if (unity)
  target_sources (casinocoind PRIVATE
    #[===============================[
       unity, main sources
    #]===============================]
    src/casinocoin/unity/app_consensus.cpp
    src/casinocoin/unity/app_ledger.cpp
    src/casinocoin/unity/app_ledger_impl.cpp
    src/casinocoin/unity/app_main1.cpp
    src/casinocoin/unity/app_main2.cpp
    src/casinocoin/unity/app_misc.cpp
    src/casinocoin/unity/app_misc_impl.cpp
    src/casinocoin/unity/app_paths.cpp
    src/casinocoin/unity/app_tx.cpp
    src/casinocoin/unity/conditions.cpp
    src/casinocoin/unity/consensus.cpp
    src/casinocoin/unity/core.cpp
    src/casinocoin/unity/basics2.cpp
    src/casinocoin/unity/ledger.cpp
    src/casinocoin/unity/net.cpp
    src/casinocoin/unity/nodestore.cpp
    src/casinocoin/unity/overlay1.cpp
    src/casinocoin/unity/overlay2.cpp
    src/casinocoin/unity/peerfinder.cpp
    src/casinocoin/unity/resource.cpp
    src/casinocoin/unity/rpcx1.cpp
    src/casinocoin/unity/rpcx2.cpp
    src/casinocoin/unity/shamap.cpp
    src/casinocoin/unity/server.cpp
    src/casinocoin/unity/soci_casinocoin.cpp
    #[===============================[
       unity, test sources
    #]===============================]
    src/test/unity/app_test_unity1.cpp
    src/test/unity/app_test_unity2.cpp
    src/test/unity/basics_test_unity.cpp
    src/test/unity/beast_test_unity1.cpp
    src/test/unity/beast_test_unity2.cpp
    src/test/unity/conditions_test_unity.cpp
    src/test/unity/consensus_test_unity.cpp
    src/test/unity/core_test_unity.cpp
    src/test/unity/crypto_test_unity.cpp
    src/test/unity/json_test_unity.cpp
    src/test/unity/ledger_test_unity.cpp
    src/test/unity/nodestore_test_unity.cpp
    src/test/unity/overlay_test_unity.cpp
    src/test/unity/peerfinder_test_unity.cpp
    src/test/unity/protocol_test_unity.cpp
    src/test/unity/resource_test_unity.cpp
    src/test/unity/rpc_test_unity.cpp
    src/test/unity/server_test_unity.cpp
    src/test/unity/server_status_test_unity.cpp
    src/test/unity/shamap_test_unity.cpp
    src/test/unity/jtx_unity1.cpp
    src/test/unity/jtx_unity2.cpp
    src/test/unity/csf_unity.cpp)
else ()
  target_sources (casinocoind PRIVATE
    #[===============================[
       nounity, main sources:
         subdir: app
    #]===============================]
    src/casinocoin/app/consensus/RCLConsensus.cpp
    src/casinocoin/app/consensus/RCLCxPeerPos.cpp
    src/casinocoin/app/consensus/RCLValidations.cpp
    src/casinocoin/app/ledger/AcceptedLedger.cpp
    src/casinocoin/app/ledger/AcceptedLedgerTx.cpp
    src/casinocoin/app/ledger/AccountStateSF.cpp
    src/casinocoin/app/ledger/BookListeners.cpp
    src/casinocoin/app/ledger/ConsensusTransSetSF.cpp
    src/casinocoin/app/ledger/Ledger.cpp
    src/casinocoin/app/ledger/LedgerHistory.cpp
    src/casinocoin/app/ledger/OrderBookDB.cpp
    src/casinocoin/app/ledger/TransactionStateSF.cpp
    src/casinocoin/app/ledger/impl/BuildLedger.cpp
    src/casinocoin/app/ledger/impl/InboundLedger.cpp
    src/casinocoin/app/ledger/impl/InboundLedgers.cpp
    src/casinocoin/app/ledger/impl/InboundTransactions.cpp
    src/casinocoin/app/ledger/impl/LedgerCleaner.cpp
    src/casinocoin/app/ledger/impl/LedgerMaster.cpp
    src/casinocoin/app/ledger/impl/LedgerReplay.cpp
    src/casinocoin/app/ledger/impl/LedgerToJson.cpp
    src/casinocoin/app/ledger/impl/LocalTxs.cpp
    src/casinocoin/app/ledger/impl/OpenLedger.cpp
    src/casinocoin/app/ledger/impl/TransactionAcquire.cpp
    src/casinocoin/app/ledger/impl/TransactionMaster.cpp
    src/casinocoin/app/main/Application.cpp
    src/casinocoin/app/main/BasicApp.cpp
    src/casinocoin/app/main/CollectorManager.cpp
    src/casinocoin/app/main/DBInit.cpp
    src/casinocoin/app/main/LoadManager.cpp
    src/casinocoin/app/main/Main.cpp
    src/casinocoin/app/main/NodeIdentity.cpp
    src/casinocoin/app/main/NodeStoreScheduler.cpp
    src/casinocoin/app/misc/CanonicalTXSet.cpp
    src/casinocoin/app/misc/FeeVoteImpl.cpp
    src/casinocoin/app/misc/HashRouter.cpp
    src/casinocoin/app/misc/NetworkOPs.cpp
    src/casinocoin/app/misc/SHAMapStoreImp.cpp
    src/casinocoin/app/misc/impl/AccountTxPaging.cpp
    src/casinocoin/app/misc/impl/AmendmentTable.cpp
    src/casinocoin/app/misc/impl/LoadFeeTrack.cpp
    src/casinocoin/app/misc/impl/Manifest.cpp
    src/casinocoin/app/misc/impl/Transaction.cpp
    src/casinocoin/app/misc/impl/TxQ.cpp
    src/casinocoin/app/misc/impl/ValidatorKeys.cpp
    src/casinocoin/app/misc/impl/ValidatorList.cpp
    src/casinocoin/app/misc/impl/ValidatorSite.cpp
    src/casinocoin/app/paths/AccountCurrencies.cpp
    src/casinocoin/app/paths/Credit.cpp
    src/casinocoin/app/paths/Flow.cpp
    src/casinocoin/app/paths/Node.cpp
    src/casinocoin/app/paths/PathRequest.cpp
    src/casinocoin/app/paths/PathRequests.cpp
    src/casinocoin/app/paths/PathState.cpp
    src/casinocoin/app/paths/Pathfinder.cpp
    src/casinocoin/app/paths/casinocoinCalc.cpp
    src/casinocoin/app/paths/casinocoinLineCache.cpp
    src/casinocoin/app/paths/casinocoinState.cpp
    src/casinocoin/app/paths/cursor/AdvanceNode.cpp
    src/casinocoin/app/paths/cursor/DeliverNodeForward.cpp
    src/casinocoin/app/paths/cursor/DeliverNodeReverse.cpp
    src/casinocoin/app/paths/cursor/EffectiveRate.cpp
    src/casinocoin/app/paths/cursor/ForwardLiquidity.cpp
    src/casinocoin/app/paths/cursor/ForwardLiquidityForAccount.cpp
    src/casinocoin/app/paths/cursor/Liquidity.cpp
    src/casinocoin/app/paths/cursor/NextIncrement.cpp
    src/casinocoin/app/paths/cursor/ReverseLiquidity.cpp
    src/casinocoin/app/paths/cursor/ReverseLiquidityForAccount.cpp
    src/casinocoin/app/paths/cursor/casinocoinLiquidity.cpp
    src/casinocoin/app/paths/impl/BookStep.cpp
    src/casinocoin/app/paths/impl/DirectStep.cpp
    src/casinocoin/app/paths/impl/PaySteps.cpp
    src/casinocoin/app/paths/impl/XRPEndpointStep.cpp
    src/casinocoin/app/tx/impl/ApplyContext.cpp
    src/casinocoin/app/tx/impl/BookTip.cpp
    src/casinocoin/app/tx/impl/CancelCheck.cpp
    src/casinocoin/app/tx/impl/CancelOffer.cpp
    src/casinocoin/app/tx/impl/CancelTicket.cpp
    src/casinocoin/app/tx/impl/CashCheck.cpp
    src/casinocoin/app/tx/impl/Change.cpp
    src/casinocoin/app/tx/impl/CreateCheck.cpp
    src/casinocoin/app/tx/impl/CreateOffer.cpp
    src/casinocoin/app/tx/impl/CreateTicket.cpp
    src/casinocoin/app/tx/impl/DepositPreauth.cpp
    src/casinocoin/app/tx/impl/Escrow.cpp
    src/casinocoin/app/tx/impl/InvariantCheck.cpp
    src/casinocoin/app/tx/impl/OfferStream.cpp
    src/casinocoin/app/tx/impl/PayChan.cpp
    src/casinocoin/app/tx/impl/Payment.cpp
    src/casinocoin/app/tx/impl/SetAccount.cpp
    src/casinocoin/app/tx/impl/SetRegularKey.cpp
    src/casinocoin/app/tx/impl/SetSignerList.cpp
    src/casinocoin/app/tx/impl/SetTrust.cpp
    src/casinocoin/app/tx/impl/SignerEntries.cpp
    src/casinocoin/app/tx/impl/Taker.cpp
    src/casinocoin/app/tx/impl/Transactor.cpp
    src/casinocoin/app/tx/impl/apply.cpp
    src/casinocoin/app/tx/impl/applySteps.cpp
    #[===============================[
       nounity, main sources:
         subdir: basics (partial)
    #]===============================]
    src/casinocoin/basics/impl/Archive.cpp
    src/casinocoin/basics/impl/BasicConfig.cpp
    src/casinocoin/basics/impl/PerfLogImp.cpp
    src/casinocoin/basics/impl/ResolverAsio.cpp
    src/casinocoin/basics/impl/Sustain.cpp
    src/casinocoin/basics/impl/UptimeClock.cpp
    src/casinocoin/basics/impl/make_SSLContext.cpp
    src/casinocoin/basics/impl/mulDiv.cpp
    #[===============================[
       nounity, main sources:
         subdir: conditions
    #]===============================]
    src/casinocoin/conditions/impl/Condition.cpp
    src/casinocoin/conditions/impl/Fulfillment.cpp
    src/casinocoin/conditions/impl/error.cpp
    #[===============================[
       nounity, main sources:
         subdir: core
    #]===============================]
    src/casinocoin/core/impl/Config.cpp
    src/casinocoin/core/impl/DatabaseCon.cpp
    src/casinocoin/core/impl/Job.cpp
    src/casinocoin/core/impl/JobQueue.cpp
    src/casinocoin/core/impl/LoadEvent.cpp
    src/casinocoin/core/impl/LoadMonitor.cpp
    src/casinocoin/core/impl/SNTPClock.cpp
    src/casinocoin/core/impl/SociDB.cpp
    src/casinocoin/core/impl/Stoppable.cpp
    src/casinocoin/core/impl/TimeKeeper.cpp
    src/casinocoin/core/impl/Workers.cpp
    #[===============================[
       nounity, main sources:
         subdir: consensus
    #]===============================]
    src/casinocoin/consensus/Consensus.cpp
    #[===============================[
       nounity, main sources:
         subdir: ledger
    #]===============================]
    src/casinocoin/ledger/impl/ApplyStateTable.cpp
    src/casinocoin/ledger/impl/ApplyView.cpp
    src/casinocoin/ledger/impl/ApplyViewBase.cpp
    src/casinocoin/ledger/impl/ApplyViewImpl.cpp
    src/casinocoin/ledger/impl/BookDirs.cpp
    src/casinocoin/ledger/impl/CachedSLEs.cpp
    src/casinocoin/ledger/impl/CachedView.cpp
    src/casinocoin/ledger/impl/CashDiff.cpp
    src/casinocoin/ledger/impl/Directory.cpp
    src/casinocoin/ledger/impl/OpenView.cpp
    src/casinocoin/ledger/impl/PaymentSandbox.cpp
    src/casinocoin/ledger/impl/RawStateTable.cpp
    src/casinocoin/ledger/impl/ReadView.cpp
    src/casinocoin/ledger/impl/TxMeta.cpp
    src/casinocoin/ledger/impl/View.cpp
    #[===============================[
       nounity, main sources:
         subdir: net
    #]===============================]
    src/casinocoin/net/impl/HTTPClient.cpp
    src/casinocoin/net/impl/InfoSub.cpp
    src/casinocoin/net/impl/RPCCall.cpp
    src/casinocoin/net/impl/RPCErr.cpp
    src/casinocoin/net/impl/RPCSub.cpp
    src/casinocoin/net/impl/RegisterSSLCerts.cpp
    src/casinocoin/net/impl/SSLHTTPDownloader.cpp
    #[===============================[
       nounity, main sources:
         subdir: nodestore
    #]===============================]
    src/casinocoin/nodestore/backend/MemoryFactory.cpp
    src/casinocoin/nodestore/backend/NuDBFactory.cpp
    src/casinocoin/nodestore/backend/NullFactory.cpp
    src/casinocoin/nodestore/backend/RocksDBFactory.cpp
    src/casinocoin/nodestore/backend/RocksDBQuickFactory.cpp
    src/casinocoin/nodestore/impl/BatchWriter.cpp
    src/casinocoin/nodestore/impl/Database.cpp
    src/casinocoin/nodestore/impl/DatabaseNodeImp.cpp
    src/casinocoin/nodestore/impl/DatabaseRotatingImp.cpp
    src/casinocoin/nodestore/impl/DatabaseShardImp.cpp
    src/casinocoin/nodestore/impl/DecodedBlob.cpp
    src/casinocoin/nodestore/impl/DummyScheduler.cpp
    src/casinocoin/nodestore/impl/EncodedBlob.cpp
    src/casinocoin/nodestore/impl/ManagerImp.cpp
    src/casinocoin/nodestore/impl/NodeObject.cpp
    src/casinocoin/nodestore/impl/Shard.cpp
    #[===============================[
       nounity, main sources:
         subdir: overlay
    #]===============================]
    src/casinocoin/overlay/impl/Cluster.cpp
    src/casinocoin/overlay/impl/ConnectAttempt.cpp
    src/casinocoin/overlay/impl/Message.cpp
    src/casinocoin/overlay/impl/OverlayImpl.cpp
    src/casinocoin/overlay/impl/PeerImp.cpp
    src/casinocoin/overlay/impl/PeerSet.cpp
    src/casinocoin/overlay/impl/TMHello.cpp
    src/casinocoin/overlay/impl/TrafficCount.cpp
    #[===============================[
       nounity, main sources:
         subdir: peerfinder
    #]===============================]
    src/casinocoin/peerfinder/impl/Bootcache.cpp
    src/casinocoin/peerfinder/impl/Endpoint.cpp
    src/casinocoin/peerfinder/impl/PeerfinderConfig.cpp
    src/casinocoin/peerfinder/impl/PeerfinderManager.cpp
    src/casinocoin/peerfinder/impl/SlotImp.cpp
    src/casinocoin/peerfinder/impl/SourceStrings.cpp
    #[===============================[
       nounity, main sources:
         subdir: resource
    #]===============================]
    src/casinocoin/resource/impl/Charge.cpp
    src/casinocoin/resource/impl/Consumer.cpp
    src/casinocoin/resource/impl/Fees.cpp
    src/casinocoin/resource/impl/ResourceManager.cpp
    #[===============================[
       nounity, main sources:
         subdir: rpc
    #]===============================]
    src/casinocoin/rpc/handlers/AccountChannels.cpp
    src/casinocoin/rpc/handlers/AccountCurrenciesHandler.cpp
    src/casinocoin/rpc/handlers/AccountInfo.cpp
    src/casinocoin/rpc/handlers/AccountLines.cpp
    src/casinocoin/rpc/handlers/AccountObjects.cpp
    src/casinocoin/rpc/handlers/AccountOffers.cpp
    src/casinocoin/rpc/handlers/AccountTx.cpp
    src/casinocoin/rpc/handlers/AccountTxOld.cpp
    src/casinocoin/rpc/handlers/AccountTxSwitch.cpp
    src/casinocoin/rpc/handlers/BlackList.cpp
    src/casinocoin/rpc/handlers/BookOffers.cpp
    src/casinocoin/rpc/handlers/CanDelete.cpp
    src/casinocoin/rpc/handlers/Connect.cpp
    src/casinocoin/rpc/handlers/ConsensusInfo.cpp
    src/casinocoin/rpc/handlers/CrawlShards.cpp
    src/casinocoin/rpc/handlers/DepositAuthorized.cpp
    src/casinocoin/rpc/handlers/DownloadShard.cpp
    src/casinocoin/rpc/handlers/Feature1.cpp
    src/casinocoin/rpc/handlers/Fee1.cpp
    src/casinocoin/rpc/handlers/FetchInfo.cpp
    src/casinocoin/rpc/handlers/GatewayBalances.cpp
    src/casinocoin/rpc/handlers/GetCounts.cpp
    src/casinocoin/rpc/handlers/LedgerAccept.cpp
    src/casinocoin/rpc/handlers/LedgerCleanerHandler.cpp
    src/casinocoin/rpc/handlers/LedgerClosed.cpp
    src/casinocoin/rpc/handlers/LedgerCurrent.cpp
    src/casinocoin/rpc/handlers/LedgerData.cpp
    src/casinocoin/rpc/handlers/LedgerEntry.cpp
    src/casinocoin/rpc/handlers/LedgerHandler.cpp
    src/casinocoin/rpc/handlers/LedgerHeader.cpp
    src/casinocoin/rpc/handlers/LedgerRequest.cpp
    src/casinocoin/rpc/handlers/LogLevel.cpp
    src/casinocoin/rpc/handlers/LogRotate.cpp
    src/casinocoin/rpc/handlers/NocasinocoinCheck.cpp
    src/casinocoin/rpc/handlers/OwnerInfo.cpp
    src/casinocoin/rpc/handlers/PathFind.cpp
    src/casinocoin/rpc/handlers/PayChanClaim.cpp
    src/casinocoin/rpc/handlers/Peers.cpp
    src/casinocoin/rpc/handlers/Ping.cpp
    src/casinocoin/rpc/handlers/Print.cpp
    src/casinocoin/rpc/handlers/Random.cpp
    src/casinocoin/rpc/handlers/casinocoinPathFind.cpp
    src/casinocoin/rpc/handlers/ServerInfo.cpp
    src/casinocoin/rpc/handlers/ServerState.cpp
    src/casinocoin/rpc/handlers/SignFor.cpp
    src/casinocoin/rpc/handlers/SignHandler.cpp
    src/casinocoin/rpc/handlers/Stop.cpp
    src/casinocoin/rpc/handlers/Submit.cpp
    src/casinocoin/rpc/handlers/SubmitMultiSigned.cpp
    src/casinocoin/rpc/handlers/Subscribe.cpp
    src/casinocoin/rpc/handlers/TransactionEntry.cpp
    src/casinocoin/rpc/handlers/Tx.cpp
    src/casinocoin/rpc/handlers/TxHistory.cpp
    src/casinocoin/rpc/handlers/UnlList.cpp
    src/casinocoin/rpc/handlers/Unsubscribe.cpp
    src/casinocoin/rpc/handlers/ValidationCreate.cpp
    src/casinocoin/rpc/handlers/ValidatorListSites.cpp
    src/casinocoin/rpc/handlers/Validators.cpp
    src/casinocoin/rpc/handlers/WalletPropose.cpp
    src/casinocoin/rpc/impl/DeliveredAmount.cpp
    src/casinocoin/rpc/impl/Handler.cpp
    src/casinocoin/rpc/impl/LegacyPathFind.cpp
    src/casinocoin/rpc/impl/RPCHandler.cpp
    src/casinocoin/rpc/impl/RPCHelpers.cpp
    src/casinocoin/rpc/impl/Role.cpp
    src/casinocoin/rpc/impl/ServerHandlerImp.cpp
    src/casinocoin/rpc/impl/ShardArchiveHandler.cpp
    src/casinocoin/rpc/impl/Status.cpp
    src/casinocoin/rpc/impl/TransactionSign.cpp
    #[===============================[
       nounity, main sources:
         subdir: server
    #]===============================]
    src/casinocoin/server/impl/JSONRPCUtil.cpp
    src/casinocoin/server/impl/Port.cpp
    #[===============================[
       nounity, main sources:
         subdir: shamap
    #]===============================]
    src/casinocoin/shamap/impl/SHAMap.cpp
    src/casinocoin/shamap/impl/SHAMapDelta.cpp
    src/casinocoin/shamap/impl/SHAMapItem.cpp
    src/casinocoin/shamap/impl/SHAMapMissingNode.cpp
    src/casinocoin/shamap/impl/SHAMapNodeID.cpp
    src/casinocoin/shamap/impl/SHAMapSync.cpp
    src/casinocoin/shamap/impl/SHAMapTreeNode.cpp
    #[===============================[
       nounity, test sources:
         subdir: app
    #]===============================]
    src/test/app/AccountTxPaging_test.cpp
    src/test/app/AmendmentTable_test.cpp
    src/test/app/Check_test.cpp
    src/test/app/CrossingLimits_test.cpp
    src/test/app/DeliverMin_test.cpp
    src/test/app/DepositAuth_test.cpp
    src/test/app/Discrepancy_test.cpp
    src/test/app/Escrow_test.cpp
    src/test/app/Flow_test.cpp
    src/test/app/Freeze_test.cpp
    src/test/app/HashRouter_test.cpp
    src/test/app/LedgerHistory_test.cpp
    src/test/app/LedgerLoad_test.cpp
    src/test/app/LedgerReplay_test.cpp
    src/test/app/LoadFeeTrack_test.cpp
    src/test/app/Manifest_test.cpp
    src/test/app/MultiSign_test.cpp
    src/test/app/OfferStream_test.cpp
    src/test/app/Offer_test.cpp
    src/test/app/OversizeMeta_test.cpp
    src/test/app/Path_test.cpp
    src/test/app/PayChan_test.cpp
    src/test/app/PayStrand_test.cpp
    src/test/app/PseudoTx_test.cpp
    src/test/app/RCLCensorshipDetector_test.cpp
    src/test/app/RCLValidations_test.cpp
    src/test/app/Regression_test.cpp
    src/test/app/SHAMapStore_test.cpp
    src/test/app/SetAuth_test.cpp
    src/test/app/SetRegularKey_test.cpp
    src/test/app/SetTrust_test.cpp
    src/test/app/Taker_test.cpp
    src/test/app/Ticket_test.cpp
    src/test/app/Transaction_ordering_test.cpp
    src/test/app/TrustAndBalance_test.cpp
    src/test/app/TxQ_test.cpp
    src/test/app/ValidatorKeys_test.cpp
    src/test/app/ValidatorList_test.cpp
    src/test/app/ValidatorSite_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: basics
    #]===============================]
    src/test/basics/Buffer_test.cpp
    src/test/basics/DetectCrash_test.cpp
    src/test/basics/FileUtilities_test.cpp
    src/test/basics/KeyCache_test.cpp
    src/test/basics/PerfLog_test.cpp
    src/test/basics/RangeSet_test.cpp
    src/test/basics/Slice_test.cpp
    src/test/basics/StringUtilities_test.cpp
    src/test/basics/TaggedCache_test.cpp
    src/test/basics/base64_test.cpp
    src/test/basics/base_uint_test.cpp
    src/test/basics/contract_test.cpp
    src/test/basics/hardened_hash_test.cpp
    src/test/basics/mulDiv_test.cpp
    src/test/basics/qalloc_test.cpp
    src/test/basics/tagged_integer_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: beast
    #]===============================]
    src/test/beast/IPEndpoint_test.cpp
    src/test/beast/LexicalCast_test.cpp
    src/test/beast/SemanticVersion_test.cpp
    src/test/beast/aged_associative_container_test.cpp
    src/test/beast/beast_CurrentThreadName_test.cpp
    src/test/beast/beast_Debug_test.cpp
    src/test/beast/beast_Journal_test.cpp
    src/test/beast/beast_PropertyStream_test.cpp
    src/test/beast/beast_Zero_test.cpp
    src/test/beast/beast_abstract_clock_test.cpp
    src/test/beast/beast_asio_error_test.cpp
    src/test/beast/beast_basic_seconds_clock_test.cpp
    src/test/beast/beast_io_latency_probe_test.cpp
    src/test/beast/define_print.cpp
    #[===============================[
       nounity, test sources:
         subdir: conditions
    #]===============================]
    src/test/conditions/PreimageSha256_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: consensus
    #]===============================]
    src/test/consensus/ByzantineFailureSim_test.cpp
    src/test/consensus/Consensus_test.cpp
    src/test/consensus/DistributedValidatorsSim_test.cpp
    src/test/consensus/LedgerTiming_test.cpp
    src/test/consensus/LedgerTrie_test.cpp
    src/test/consensus/ScaleFreeSim_test.cpp
    src/test/consensus/Validations_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: core
    #]===============================]
    src/test/core/ClosureCounter_test.cpp
    src/test/core/Config_test.cpp
    src/test/core/Coroutine_test.cpp
    src/test/core/CryptoPRNG_test.cpp
    src/test/core/JobQueue_test.cpp
    src/test/core/SociDB_test.cpp
    src/test/core/Stoppable_test.cpp
    src/test/core/Workers_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: crypto
    #]===============================]
    src/test/crypto/Openssl_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: csf
    #]===============================]
    src/test/csf/BasicNetwork_test.cpp
    src/test/csf/Digraph_test.cpp
    src/test/csf/Histogram_test.cpp
    src/test/csf/Scheduler_test.cpp
    src/test/csf/impl/Sim.cpp
    src/test/csf/impl/ledgers.cpp
    #[===============================[
       nounity, test sources:
         subdir: json
    #]===============================]
    src/test/json/Object_test.cpp
    src/test/json/Output_test.cpp
    src/test/json/Writer_test.cpp
    src/test/json/json_value_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: jtx
    #]===============================]
    src/test/jtx/Env_test.cpp
    src/test/jtx/WSClient_test.cpp
    src/test/jtx/impl/Account.cpp
    src/test/jtx/impl/Env.cpp
    src/test/jtx/impl/JSONRPCClient.cpp
    src/test/jtx/impl/ManualTimeKeeper.cpp
    src/test/jtx/impl/WSClient.cpp
    src/test/jtx/impl/amount.cpp
    src/test/jtx/impl/balance.cpp
    src/test/jtx/impl/delivermin.cpp
    src/test/jtx/impl/deposit.cpp
    src/test/jtx/impl/envconfig.cpp
    src/test/jtx/impl/fee.cpp
    src/test/jtx/impl/flags.cpp
    src/test/jtx/impl/jtx_json.cpp
    src/test/jtx/impl/memo.cpp
    src/test/jtx/impl/multisign.cpp
    src/test/jtx/impl/offer.cpp
    src/test/jtx/impl/owners.cpp
    src/test/jtx/impl/paths.cpp
    src/test/jtx/impl/pay.cpp
    src/test/jtx/impl/quality2.cpp
    src/test/jtx/impl/rate.cpp
    src/test/jtx/impl/regkey.cpp
    src/test/jtx/impl/sendmax.cpp
    src/test/jtx/impl/seq.cpp
    src/test/jtx/impl/sig.cpp
    src/test/jtx/impl/tag.cpp
    src/test/jtx/impl/ticket.cpp
    src/test/jtx/impl/trust.cpp
    src/test/jtx/impl/txflags.cpp
    src/test/jtx/impl/utility.cpp
    #[===============================[
       nounity, test sources:
         subdir: ledger
    #]===============================]
    src/test/ledger/BookDirs_test.cpp
    src/test/ledger/CashDiff_test.cpp
    src/test/ledger/Directory_test.cpp
    src/test/ledger/Invariants_test.cpp
    src/test/ledger/PaymentSandbox_test.cpp
    src/test/ledger/PendingSaves_test.cpp
    src/test/ledger/SHAMapV2_test.cpp
    src/test/ledger/SkipList_test.cpp
    src/test/ledger/View_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: nodestore
    #]===============================]
    src/test/nodestore/Backend_test.cpp
    src/test/nodestore/Basics_test.cpp
    src/test/nodestore/Database_test.cpp
    src/test/nodestore/Timing_test.cpp
    src/test/nodestore/import_test.cpp
    src/test/nodestore/varint_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: overlay
    #]===============================]
    src/test/overlay/TMHello_test.cpp
    src/test/overlay/cluster_test.cpp
    src/test/overlay/short_read_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: peerfinder
    #]===============================]
    src/test/peerfinder/Livecache_test.cpp
    src/test/peerfinder/PeerFinder_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: protocol
    #]===============================]
    src/test/protocol/BuildInfo_test.cpp
    src/test/protocol/IOUAmount_test.cpp
    src/test/protocol/InnerObjectFormats_test.cpp
    src/test/protocol/Issue_test.cpp
    src/test/protocol/PublicKey_test.cpp
    src/test/protocol/Quality_test.cpp
    src/test/protocol/STAccount_test.cpp
    src/test/protocol/STAmount_test.cpp
    src/test/protocol/STObject_test.cpp
    src/test/protocol/STTx_test.cpp
    src/test/protocol/STValidation_test.cpp
    src/test/protocol/SecretKey_test.cpp
    src/test/protocol/Seed_test.cpp
    src/test/protocol/TER_test.cpp
    src/test/protocol/XRPAmount_test.cpp
    src/test/protocol/digest_test.cpp
    src/test/protocol/types_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: resource
    #]===============================]
    src/test/resource/Logic_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: rpc
    #]===============================]
    src/test/rpc/AccountCurrencies_test.cpp
    src/test/rpc/AccountInfo_test.cpp
    src/test/rpc/AccountLinesRPC_test.cpp
    src/test/rpc/AccountObjects_test.cpp
    src/test/rpc/AccountOffers_test.cpp
    src/test/rpc/AccountSet_test.cpp
    src/test/rpc/AccountTx_test.cpp
    src/test/rpc/AmendmentBlocked_test.cpp
    src/test/rpc/Book_test.cpp
    src/test/rpc/DepositAuthorized_test.cpp
    src/test/rpc/DeliveredAmount_test.cpp
    src/test/rpc/Feature_test.cpp
    src/test/rpc/GatewayBalances_test.cpp
    src/test/rpc/GetCounts_test.cpp
    src/test/rpc/JSONRPC_test.cpp
    src/test/rpc/KeyGeneration_test.cpp
    src/test/rpc/LedgerClosed_test.cpp
    src/test/rpc/LedgerData_test.cpp
    src/test/rpc/LedgerRPC_test.cpp
    src/test/rpc/LedgerRequestRPC_test.cpp
    src/test/rpc/NocasinocoinCheck_test.cpp
    src/test/rpc/Nocasinocoin_test.cpp
    src/test/rpc/OwnerInfo_test.cpp
    src/test/rpc/Peers_test.cpp
    src/test/rpc/RPCCall_test.cpp
    src/test/rpc/RPCOverload_test.cpp
    src/test/rpc/RobustTransaction_test.cpp
    src/test/rpc/ServerInfo_test.cpp
    src/test/rpc/Status_test.cpp
    src/test/rpc/Subscribe_test.cpp
    src/test/rpc/TransactionEntry_test.cpp
    src/test/rpc/TransactionHistory_test.cpp
    src/test/rpc/ValidatorRPC_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: server
    #]===============================]
    src/test/server/ServerStatus_test.cpp
    src/test/server/Server_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: shamap
    #]===============================]
    src/test/shamap/FetchPack_test.cpp
    src/test/shamap/SHAMapSync_test.cpp
    src/test/shamap/SHAMap_test.cpp
    #[===============================[
       nounity, test sources:
         subdir: unit_test
    #]===============================]
    src/test/unit_test/multi_runner.cpp)
endif ()
target_link_libraries (casinocoind
  casinocoin::opts
  casinocoin::libs
  casinocoin::boost
  casinocoin::xrpl_core)
exclude_if_included (casinocoind)

#[===================================================================[
   install stuff
#]===================================================================]

install (
  TARGETS
    secp256k1
    ed25519-donna
    common
    opts
    casinocoin_syslibs
    casinocoin_boost
    xrpl_core
  EXPORT casinocoinExports
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include)
install (EXPORT casinocoinExports
  FILE casinocoinTargets.cmake
  NAMESPACE casinocoin::
  DESTINATION lib/cmake/casinocoin)
include (CMakePackageConfigHelpers)
write_basic_package_version_file (
  casinocoinConfigVersion.cmake
  VERSION ${casinocoind_version}
  COMPATIBILITY SameMajorVersion)

if (is_root_project)
  install (TARGETS casinocoind RUNTIME DESTINATION bin)
  set_target_properties(casinocoind PROPERTIES INSTALL_RPATH_USE_LINK_PATH ON)
  install (
    FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake/casinocoinConfig.cmake
      ${CMAKE_CURRENT_BINARY_DIR}/casinocoinConfigVersion.cmake
    DESTINATION lib/cmake/casinocoin)
  # sample configs should not overwrite existing files
  # install if-not-exists workaround as suggested by
  # https://cmake.org/Bug/view.php?id=12646
  install(CODE "
    macro (copy_if_not_exists SRC DEST NEWNAME)
      if (NOT EXISTS \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/\${DEST}/\${NEWNAME}\")
        file (INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}/\${DEST}\" FILES \"\${SRC}\" RENAME \"\${NEWNAME}\")
      else ()
        message (\"-- Skipping : \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/\${DEST}/\${NEWNAME}\")
      endif ()
    endmacro()
    copy_if_not_exists(\"${CMAKE_CURRENT_SOURCE_DIR}/cfg/casinocoind-example.cfg\" etc casinocoind.cfg)
    copy_if_not_exists(\"${CMAKE_CURRENT_SOURCE_DIR}/cfg/validators-example.txt\" etc validators.txt)
  ")
endif ()

#[===================================================================[
   coverage report target
#]===================================================================]

if (coverage)
  if (is_clang)
    if (APPLE)
      execute_process (COMMAND xcrun -f llvm-profdata
        OUTPUT_VARIABLE LLVM_PROFDATA
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    else ()
      find_program (LLVM_PROFDATA llvm-profdata)
    endif ()
    if (NOT LLVM_PROFDATA)
      message (WARNING "unable to find llvm-profdata - skipping coverage_report target")
    endif ()

    if (APPLE)
      execute_process (COMMAND xcrun -f llvm-cov
        OUTPUT_VARIABLE LLVM_COV
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    else ()
      find_program (LLVM_COV llvm-cov)
    endif ()
    if (NOT LLVM_COV)
      message (WARNING "unable to find llvm-cov - skipping coverage_report target")
    endif ()

    set (extract_pattern "")
    if (coverage_core_only)
      set (extract_pattern "${CMAKE_SOURCE_DIR}/src/casinocoin/")
    endif ()

    if (LLVM_COV AND LLVM_PROFDATA)
      add_custom_target (coverage_report
        USES_TERMINAL
        COMMAND ${CMAKE_COMMAND} -E echo "Generating coverage - results will be in ${CMAKE_BINARY_DIR}/coverage/index.html."
        COMMAND ${CMAKE_COMMAND} -E echo "Running casinocoind tests."
        COMMAND casinocoind --unittest$<$<BOOL:${coverage_test}>:=${coverage_test}> --quiet --unittest-log
        COMMAND ${LLVM_PROFDATA}
          merge -sparse default.profraw -o rip.profdata
        COMMAND ${LLVM_COV}
          show -format=html -output-dir=${CMAKE_BINARY_DIR}/coverage
          -instr-profile=rip.profdata
          $<TARGET_FILE:casinocoind> ${extract_pattern}
        BYPRODUCTS coverage/index.html)
    endif ()
  elseif (is_gcc)
    find_program (LCOV lcov)
    if (NOT LCOV)
      message (WARNING "unable to find lcov - skipping coverage_report target")
    endif ()

    find_program (GENHTML genhtml)
    if (NOT GENHTML)
      message (WARNING "unable to find genhtml - skipping coverage_report target")
    endif ()

    set (extract_pattern "*")
    if (coverage_core_only)
      set (extract_pattern "*/src/casinocoin/*")
    endif ()

    if (LCOV AND GENHTML)
      add_custom_target (coverage_report
        USES_TERMINAL
        COMMAND ${CMAKE_COMMAND} -E echo "Generating coverage- results will be in ${CMAKE_BINARY_DIR}/coverage/index.html."
        # create baseline info file
        COMMAND ${LCOV}
          --no-external -d "${CMAKE_SOURCE_DIR}" -c -d . -i -o baseline.info
          | grep -v "ignoring data for external file"
        # run tests
        COMMAND ${CMAKE_COMMAND} -E echo "Running casinocoind tests for coverage report."
        COMMAND casinocoind --unittest$<$<BOOL:${coverage_test}>:=${coverage_test}> --quiet --unittest-log
        # Create test coverage data file
        COMMAND ${LCOV}
          --no-external -d "${CMAKE_SOURCE_DIR}" -c -d . -o tests.info
          | grep -v "ignoring data for external file"
        # Combine baseline and test coverage data
        COMMAND ${LCOV}
          -a baseline.info -a tests.info -o lcov-all.info
        # extract our files
        COMMAND ${LCOV}
          -e lcov-all.info "${extract_pattern}" -o lcov.info
        # generate HTML report
        COMMAND ${GENHTML}
          -o ${CMAKE_BINARY_DIR}/coverage lcov.info
        BYPRODUCTS coverage/index.html)
    endif ()
  endif ()
endif ()

#[===================================================================[
   multiconfig misc
#]===================================================================]

if (is_multiconfig)
  # This code finds all source files in the src subdirectory for inclusion
  # in the IDE file tree as non-compiled sources. Since this file list will
  # have some overlap with files we have already added to our targets to
  # be compiled, we explicitly remove any of these target source files from
  # this list.
  file (GLOB_RECURSE all_sources RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    CONFIGURE_DEPENDS
    src/*.* Builds/*.md docs/*.md src/*.md Builds/*.cmake)
  file(GLOB md_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    *.md)
  LIST(APPEND all_sources ${md_files})
  foreach (_target secp256k1 ed25519-donna rocksdb pbufs xrpl_core casinocoind)
    get_target_property (_type ${_target} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY")
      continue()
    endif()
    get_target_property (_src ${_target} SOURCES)
    list (REMOVE_ITEM all_sources ${_src})
  endforeach ()
  target_sources (casinocoind PRIVATE ${all_sources})
  set_property (
    SOURCE ${all_sources}
    APPEND
    PROPERTY HEADER_FILE_ONLY true)
  if (MSVC)
    set_property(
      DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      PROPERTY VS_STARTUP_PROJECT casinocoind)
  endif ()

  group_sources(src)
  group_sources(docs)
  group_sources(Builds)
endif ()

#[===================================================================[
   docs target (optional)
#]===================================================================]

find_package (Doxygen)
if (TARGET Doxygen::doxygen)
  set (doc_srcs docs/source.dox)
  file (GLOB_RECURSE other_docs docs/*.md)
  list (APPEND doc_srcs "${other_docs}")
  # read the source config and make a modified one
  # that points the output files to our build directory
  file (READ "${CMAKE_CURRENT_SOURCE_DIR}/docs/source.dox" dox_content)
  string (REGEX REPLACE "[\t ]*OUTPUT_DIRECTORY[\t ]*=(.*)"
    "OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}\n\\1"
    new_config "${dox_content}")
  file (WRITE "${CMAKE_BINARY_DIR}/source.dox" "${new_config}")
  add_custom_target (docs
    COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_BINARY_DIR}/source.dox"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/html_doc/index.html"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    SOURCES "${doc_srcs}")
  if (is_multiconfig)
    set_property (
      SOURCE ${doc_srcs}
      APPEND
      PROPERTY HEADER_FILE_ONLY
      true)
  endif ()
else ()
  message (STATUS "doxygen executable not found -- skipping docs target")
endif ()

