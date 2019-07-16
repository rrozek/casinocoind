
# !!! The official build system is SConstruct !!!
# This is an experimental cmake build file for rippled
#
# cmake support in rippled. Currently supports:
#
#  * unity/nounity debug/release
#  * running protobuf
#  * sanitizer builds
#  * optional release build with assert turned on
#  * `target` variable to easily set compiler/debug/unity
#     (i.e. -Dtarget=gcc.debug.nounity)
#  * gcc/clang/visual studio/xcode
#  * linux/mac/win
#  * gcc 4 ABI, when needed
#  * ninja builds
#  * check openssl version on linux
#  * static builds (swd TBD: needs to be tested by building & deploying on different systems)
#  * jemalloc enabled builds (linux and macos only)
#  * perf builds (linux only) - which just sets recommended compiler flags
#          for running perf on the executable
#
# Notes:
#  * Use the -G"Visual Studio 14 2015 Win64" generator, or the "VS2015 x86 x64
#    Cross Tools" Command Prompt on Windows. Without this a 32-bit project will be
#    created. There is no way to set the generator or force a 64-bit build in
#    CMakeLists.txt (setting CMAKE_GENERATOR_PLATFORM won't work). The best solution
#    may be to wrap cmake with a script.
#
#  * Ninja command line builds seem to work under Windows, but only from within
#    the "VS2015 x86 x64 Cross Tools" Command Prompt.
#
#  * It is not possible to generate a visual studio project on linux or
#    mac. The visual studio generator is only available on windows.
#
#  * The Visual Studio solution will be generated with two projects, one
#    unity, one non-unity. Which is default depends on the nounity flag in
#    -Dtarget. Unity targets will create `rippled` and `rippled_classic`.
#    Non-unity targets will create `rippled` and `rippled_unity`. In either
#    case, only the `rippled` build will be enabled by default. It does
#    not appear possible to include both unity and non-unity configs in one
#    project and disable compilation based on configuration.
#
#  * Language is _much_ worse than python, poor documentation and "quirky"
#    language support (for example, generator expressions can only be used
#    in limited contexts and seem to work differently based on
#    context (set_property can set multiple values, add_compile_options
#    can not/or is buggy)
#
#  * Could not call out to `sed` because cmake messed with the regular
#    expression before calling the external command. I did not see a way
#    around this.
#
#  * Makefile generators want to be single target. It wants a separate
#    directory for each target type. I saw some mentions on the web for
#    ways around this bug haven't look into it. The visual studio project
#    does support debug/release configurations in the same project (but
#    not unity/non-unity).

############################################################

#########################################################
# CMAKE_C_COMPILER and CMAKE_CXX_COMPILER must be defined
# before the project statement; However, the project
# statement will clear CMAKE_BUILD_TYPE. CACHE variables,
# along with the order of this code, are used to work
# around these constraints.
#
# Don't put any code above or in this block, unless it
# has similar constraints.
cmake_minimum_required(VERSION 3.1.0)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/Builds/CMake")
include(CMakeFuncs)
set(openssl_min 1.0.2)
parse_target()
project(rippled)
#########################################################

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  set(dir "build")
  set(cmd "cmake")
  if (target)
    set(dir "${dir}/${target}")
    set(cmd "${cmd} -Dtarget=${target}")
  elseif(CMAKE_BUILD_TYPE)
    set(dir "${dir}/${CMAKE_BUILD_TYPE}")
    set(cmd "${cmd} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
  else()
    set(dir "${dir}/default")
  endif()
  set(cmd "${cmd} ${CMAKE_SOURCE_DIR}")

  message(FATAL_ERROR "Builds are not allowed in ${CMAKE_SOURCE_DIR}.\n"
    "Instead:\n"
    "1) Remove the CMakeCache.txt file and CMakeFiles directory "
   "from ${CMAKE_SOURCE_DIR}.\n"
    "2) Create a directory to hold your build files, for example: ${dir}.\n"
    "3) Change to that directory.\n"
    "4) Run cmake targetting ${CMAKE_SOURCE_DIR}, for example: ${cmd}")
endif()
if("${CMAKE_GENERATOR}" MATCHES "Visual Studio" AND
    NOT ("${CMAKE_GENERATOR}" MATCHES .*Win64.*))
  message(FATAL_ERROR "Visual Studio 32-bit build is unsupported. Use
    -G\"${CMAKE_GENERATOR} Win64\"")
endif()

setup_build_cache()

if(nonunity)
    get_cmake_property(allvars VARIABLES)
    string(REGEX MATCHALL "[^;]*(DEBUG|RELEASE)[^;]*" matchvars "${allvars}")
    foreach(var IN LISTS matchvars)
        string(REGEX REPLACE "(DEBUG|RELEASE)" "\\1CLASSIC" newvar ${var})
        set(${newvar} ${${var}})
    endforeach()

    get_cmake_property(allvars CACHE_VARIABLES)
    string(REGEX MATCHALL "[^;]*(DEBUG|RELEASE)[^;]*" matchvars "${allvars}")
    foreach(var IN LISTS matchvars)
        string(REGEX REPLACE "(DEBUG|RELEASE)" "\\1CLASSIC" newvar ${var})
        set(${newvar} ${${var}} CACHE STRING "Copied from ${var}")
    endforeach()
endif()

determine_build_type()

check_gcc4_abi()

############################################################

include_directories(
  src
  src/beast
  src/beast/include
  src/beast/extras
  src/nudb/include
  src/soci/src
  src/soci/include)

special_build_flags()

############################################################

use_boost(
  # resist the temptation to alphabetize these. coroutine
  # must come before context.
  chrono
  coroutine
  context
  date_time
  filesystem
  program_options
  regex
  serialization
  system
  thread)

use_pthread()

use_openssl(${openssl_min})

use_protobuf()

setup_build_boilerplate()

############################################################

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

set(soci_extra_includes
  -I"${CMAKE_SOURCE_DIR}/"src/soci/src/core
  -I"${CMAKE_SOURCE_DIR}/"src/soci/include/private
  -I"${CMAKE_SOURCE_DIR}/"src/sqlite)

############################################################

# Unity sources
prepend(beast_unity_srcs
src/ripple/beast/unity/
beast_insight_unity.cpp
beast_net_unity.cpp
beast_hash_unity.cpp
beast_utility_unity.cpp)

prepend(ripple_unity_srcs
src/ripple/unity/
app_consensus.cpp
app_ledger.cpp
app_ledger_impl.cpp
app_main1.cpp
app_main2.cpp
app_misc.cpp
app_misc_impl.cpp
app_paths.cpp
app_tx.cpp
conditions.cpp
consensus.cpp
core.cpp
basics.cpp
crypto.cpp
ledger.cpp
net.cpp
overlay1.cpp
overlay2.cpp
peerfinder.cpp
json.cpp
protocol.cpp
resource.cpp
rpcx1.cpp
rpcx2.cpp
shamap.cpp
server.cpp)

prepend(test_unity_srcs
src/test/unity/
app_test_unity1.cpp
app_test_unity2.cpp
basics_test_unity.cpp
beast_test_unity1.cpp
beast_test_unity2.cpp
conditions_test_unity.cpp
consensus_test_unity.cpp
core_test_unity.cpp
crypto_test_unity.cpp
json_test_unity.cpp
ledger_test_unity.cpp
overlay_test_unity.cpp
peerfinder_test_unity.cpp
protocol_test_unity.cpp
resource_test_unity.cpp
rpc_test_unity.cpp
server_test_unity.cpp
server_status_test_unity.cpp
shamap_test_unity.cpp
jtx_unity1.cpp
jtx_unity2.cpp
csf_unity.cpp)

list(APPEND rippled_src_unity ${beast_unity_srcs} ${ripple_unity_srcs} ${test_unity_srcs})

add_with_props(rippled_src_unity src/test/unity/nodestore_test_unity.cpp
-I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
-I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
-I"${CMAKE_SOURCE_DIR}/"src/snappy/config
${rocks_db_system_header})

add_with_props(rippled_src_unity src/ripple/unity/nodestore.cpp
-I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
-I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
-I"${CMAKE_SOURCE_DIR}/"src/snappy/config
${rocks_db_system_header})

add_with_props(rippled_src_unity src/ripple/unity/soci_ripple.cpp ${soci_extra_includes})

list(APPEND ripple_unity_srcs ${beast_unity_srcs} ${test_unity_srcs}
src/ripple/unity/nodestore.cpp
src/ripple/unity/soci_ripple.cpp
src/test/unity/nodestore_test_unity.cpp)

############################################################

# Non-unity sources
file(GLOB_RECURSE core_srcs src/ripple/core/*.cpp)
add_with_props(rippled_src_nonunity "${core_srcs}"
    -I"${CMAKE_SOURCE_DIR}/"src/soci/src/core
    -I"${CMAKE_SOURCE_DIR}/"src/sqlite)

set(non_unity_srcs ${core_srcs})

foreach(curdir
        beast/clock
        beast/container
        beast/hash
        beast/insight
        beast/net
        beast/utility
        app
        basics
        conditions
        consensus
        crypto
        json
        ledger
        legacy
        net
        overlay
        peerfinder
        protocol
        resource
        rpc
        server
        shamap)
    file(GLOB_RECURSE cursrcs src/ripple/${curdir}/*.cpp)
    list(APPEND rippled_src_nonunity "${cursrcs}")
    list(APPEND non_unity_srcs "${cursrcs}")
endforeach()

file(GLOB_RECURSE nodestore_srcs src/ripple/nodestore/*.cpp
    src/test/nodestore/*.cpp)

add_with_props(rippled_src_nonunity "${nodestore_srcs}"
    -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
    -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
    -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
    ${rocks_db_system_header})

list(APPEND non_unity_srcs "${nodestore_srcs}")

# unit test sources
foreach(curdir
        app
        basics
        beast
        conditions
        consensus
        core
        crypto
        csf
        json
        jtx
        ledger
        nodestore
        overlay
        peerfinder
        protocol
        resource
        rpc
        server
        shamap
        unit_test)
    file(GLOB_RECURSE cursrcs src/test/${curdir}/*.cpp)
    list(APPEND test_srcs "${cursrcs}")
endforeach()

add_with_props(rippled_src_nonunity "${test_srcs}"
    -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
    -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
    -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
    ${rocks_db_system_header})

list(APPEND non_unity_srcs "${test_srcs}")

if(WIN32 OR is_xcode)
    # Rippled headers. Only needed for IDEs.
    file(GLOB_RECURSE rippled_headers src/*.h src/*.hpp *.md)
    list(APPEND rippled_headers Builds/CMake/CMakeFuncs.cmake)
    foreach(curdir
            beast/asio
            beast/core
            beast/crypto
            beast/cxx17
            proto
            validators
            websocket)
        file(GLOB_RECURSE cursrcs src/ripple/${curdir}/*.cpp)
        list(APPEND rippled_headers "${cursrcs}")
    endforeach()
    list(APPEND rippled_src_nonunity "${rippled_headers}")

    set_property(
        SOURCE ${rippled_headers}
        APPEND
        PROPERTY HEADER_FILE_ONLY
        true)
        # Doesn't work
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

    ${rocks_db_system_header}
    )

endforeach()

if (NOT is_msvc)
  set(extra_props -Wno-array-bounds)
else()
  unset(extra_props)
endif()

add_with_props(rippled_src_all src/sqlite/sqlite_unity.c
  ${extra_props})

add_with_props(rippled_src_all src/ripple/unity/ed25519_donna.c
  -I"${CMAKE_SOURCE_DIR}/"src/ed25519-donna)

if (is_gcc)
  set(no_gcc_warnings -w)
else()
  unset(no_gcc_warnings)
endif()

add_with_props(rippled_src_all src/ripple/unity/rocksdb.cpp
  -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2
  -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
  ${no_gcc_warnings} ${rocks_db_system_header})

if (NOT is_msvc)
  set(no_unused_w -Wno-unused-function)
endif()

add_with_props(rippled_src_all src/ripple/unity/snappy.cpp
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
  ${no_unused_w})

list(APPEND rippled_src_unity "${rippled_src_all}")
list(APPEND rippled_src_nonunity "${rippled_src_all}")

############################################################

if (WIN32 OR is_xcode)
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

