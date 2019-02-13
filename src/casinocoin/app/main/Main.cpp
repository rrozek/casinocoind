//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

//==============================================================================
/*
    2017-06-26  ajochems        Refactored for casinocoin
*/
//==============================================================================



#include <casinocoin/basics/Log.h>
#include <casinocoin/protocol/digest.h>
#include <casinocoin/app/main/Application.h>
#include <casinocoin/basics/CheckLibraryVersions.h>
#include <casinocoin/basics/contract.h>
#include <casinocoin/basics/StringUtilities.h>
#include <casinocoin/basics/Sustain.h>
#include <casinocoin/core/Config.h>
#include <casinocoin/core/ConfigSections.h>
#include <casinocoin/core/TerminateHandler.h>
#include <casinocoin/core/TimeKeeper.h>
#include <casinocoin/crypto/csprng.h>
#include <casinocoin/json/to_string.h>
#include <casinocoin/net/RPCCall.h>
#include <casinocoin/resource/Fees.h>
#include <casinocoin/rpc/RPCHandler.h>
#include <casinocoin/protocol/BuildInfo.h>
#include <casinocoin/beast/clock/basic_seconds_clock.h>
#include <casinocoin/beast/core/CurrentThreadName.h>
#include <casinocoin/beast/utility/Debug.h>

#include <beast/unit_test/dstream.hpp>
#include <beast/unit_test/global_suites.hpp>
#include <beast/unit_test/match.hpp>
#include <beast/unit_test/reporter.hpp>
#include <test/unit_test/multi_runner.h>

#include <google/protobuf/stubs/common.h>

#include <boost/program_options.hpp>
#include <boost/version.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

#include <cstdlib>
#include <iostream>
#include <utility>
#include <stdexcept>

#ifdef _MSC_VER
#include <sys/types.h>
#include <sys/timeb.h>
#endif

#if BOOST_VERSION >= 106400
#define HAS_BOOST_PROCESS 1
#endif

#if HAS_BOOST_PROCESS
#include <boost/process.hpp>
#endif

namespace po = boost::program_options;

namespace casinocoin {

boost::filesystem::path
getEntropyFile(Config const& config)
{
    auto const path = config.legacy("database_path");
    if (path.empty ())
        return {};
    return boost::filesystem::path (path) / "random.seed";
}

bool
adjustDescriptorLimit(int needed, beast::Journal j)
{
#ifdef RLIMIT_NOFILE
    // Get the current limit, then adjust it to what we need.
    struct rlimit rl;

    int available = 0;

    if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
    {
        // If the limit is infinite, then we are good.
        if (rl.rlim_cur == RLIM_INFINITY)
            available = needed;
        else
            available = rl.rlim_cur;

        if (available < needed)
        {
            // Ignore the rlim_max, as the process may
            // be configured to override it anyways. We
            // ask for the number descriptors we need.
            rl.rlim_cur = needed;

            if (setrlimit(RLIMIT_NOFILE, &rl) == 0)
                available = rl.rlim_cur;
        }
    }

    if (needed > available)
    {
        j.fatal() <<
            "Insufficient number of file descriptors: " <<
            needed << " are needed, but only " <<
            available << " are available.";

        std::cerr <<
            "Insufficient number of file descriptors: " <<
            needed << " are needed, but only " <<
            available << " are available.\n";

        return false;
    }
#endif

    return true;
}

void printHelp (const po::options_description& desc)
{
    std::cerr
        << systemName () << "d [options] <command> <params>\n"
        << desc << std::endl
        << "Commands: \n"
           "     account_currencies <account> [<ledger>] [strict]\n"
           "     account_info <account>|<seed>|<pass_phrase>|<key> [<ledger>] [strict]\n"
           "     account_lines <account> <account>|\"\" [<ledger>]\n"
           "     account_channels <account> <account>|\"\" [<ledger>]\n"
           "     account_objects <account> [<ledger>] [strict]\n"
           "     account_offers <account>|<account_public_key> [<ledger>]\n"
           "     account_tx accountID [ledger_min [ledger_max [limit [offset]]]] [binary] [count] [descending]\n"
           "     book_offers <taker_pays> <taker_gets> [<taker [<ledger> [<limit> [<proof> [<marker>]]]]]\n"
           "     can_delete [<ledgerid>|<ledgerhash>|now|always|never]\n"
           "     channel_authorize <private_key> <channel_id> <drops>\n"
           "     channel_verify <public_key> <channel_id> <drops> <signature>\n"
           "     connect <ip> [<port>]\n"
           "     consensus_info\n"
           "     deposit_authorized <source_account> <destination_account> [<ledger>]"
           "     feature [<feature> [accept|reject]]\n"
           "     fetch_info [clear]\n"
           "     gateway_balances [<ledger>] <issuer_account> [ <hotwallet> [ <hotwallet> ]]\n"
           "     get_counts\n"
           "     json <method> <json>\n"
           "     ledger [<id>|current|closed|validated] [full]\n"
           "     ledger_accept\n"
           "     ledger_closed\n"
           "     ledger_current\n"
           "     ledger_request <ledger>\n"
           "     log_level [[<partition>] <severity>]\n"
           "     logrotate \n"
           "     peers\n"
           "     ping\n"
           "     random\n"
           "     casincoin ...\n"
           "     casinocoin_path_find <json> [<ledger>]\n"
           "     version\n"
           "     server_info [counters]\n"
           "     server_state [counters]\n"
           "     sign <private_key> <tx_json> [offline]\n"
           "     sign_msg <private_key> <message>\n"
           "     sign_for <signer_address> <signer_private_key> <tx_json> [offline]\n"
           "     stop\n"
           "     submit <tx_blob>|[<private_key> <tx_json>]\n"
           "     submit_multisigned <tx_json>\n"
           "     tx <id>\n"
           "     validation_create [<seed>|<pass_phrase>|<key>]\n"
           "     validation_seed [<seed>|<pass_phrase>|<key>]\n"
           "     verify_msg <message> <signature> <public_key_hex>\n"
           "     wallet_propose [<passphrase>]\n";
}

//------------------------------------------------------------------------------

/* simple unit test selector that allows a comma separated list
 * of selectors
 */
class multi_selector
{
private:
    std::vector<beast::unit_test::selector> selectors_;
public:
    explicit
    multi_selector(std::string const& patterns = "")
    {
        std::vector<std::string> v;
        boost::algorithm::split (v, patterns, boost::algorithm::is_any_of (","));
        selectors_.reserve(v.size());
        std::for_each(v.begin(), v.end(),
            [this](std::string s)
            {
                boost::algorithm::trim (s);
                if (selectors_.empty() || !s.empty())
                    selectors_.emplace_back(
                        beast::unit_test::selector::automatch, s);
            });
    }

    bool
    operator()(beast::unit_test::suite_info const& s)
    {
        for (auto& sel : selectors_)
            if (sel(s))
                return true;
        return false;
    }
};

namespace test{ extern std::atomic<bool> envUseIPv4; }

static int runUnitTests(
    std::string const& pattern,
    std::string const& argument,
    bool quiet,
    bool log,
    bool child,
    bool ipv4,
    std::size_t num_jobs,
    int argc,
    char** argv)
{
    using namespace beast::unit_test;
    using namespace casinocoin::test;
    if (ipv4)
        casinocoin::test::envUseIPv4 = true;

#if HAS_BOOST_PROCESS
    if (!child && num_jobs == 1)
#endif
    {
        multi_runner_parent parent_runner;

        multi_runner_child child_runner{num_jobs, quiet, log};
        child_runner.arg(argument);
        auto const any_failed = child_runner.run_multi(multi_selector(pattern));

        if (any_failed)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
#if HAS_BOOST_PROCESS
    if (!child)
    {
        multi_runner_parent parent_runner;
        std::vector<boost::process::child> children;

        std::string const exe_name = argv[0];
        std::vector<std::string> args;
        {
            args.reserve(argc);
            for (int i = 1; i < argc; ++i)
                args.emplace_back(argv[i]);
            args.emplace_back("--unittest-child");
        }

        for (std::size_t i = 0; i < num_jobs; ++i)
            children.emplace_back(
                boost::process::exe = exe_name, boost::process::args = args);

        int bad_child_exits = 0;
        for(auto& c : children)
        {
            try
            {
                c.wait();
                if (c.exit_code())
                    ++bad_child_exits;
            }
            catch (...)
            {
                // wait throws if process was terminated with a signal
                ++bad_child_exits;
            }
        }

        if (parent_runner.any_failed() || bad_child_exits)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
    else
    {
        // child
        multi_runner_child runner{num_jobs, quiet, log};
        runner.arg(argument);
        auto const anyFailed = runner.run_multi(multi_selector(pattern));

        if (anyFailed)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
#endif
}

//------------------------------------------------------------------------------

int run (int argc, char** argv)
{
    // Make sure that we have the right OpenSSL and Boost libraries.
    version::checkLibraryVersions();

    using namespace std;

    beast::setCurrentThreadName ("casinocoind: main");

    po::variables_map vm;

    std::string importText;
    {
        importText += "Import an existing node database (specified in the [";
        importText += ConfigSection::importNodeDatabase ();
        importText += "] configuration file section) into the current ";
        importText += "node database (specified in the [";
        importText += ConfigSection::nodeDatabase ();
        importText += "] configuration file section).";
    }
    std::string shardsText;
    {
        shardsText += "Validate an existing shard database (specified in the [";
        shardsText += ConfigSection::shardDatabase();
        shardsText += "] configuration file section).";
    }

    // Set up option parsing.
    //
    po::options_description gen ("General Options");
    gen.add_options ()
    ("conf", po::value<std::string> (), "Specify the configuration file.")
    ("debug", "Enable normally suppressed debug logging")
    ("fg", "Run in the foreground.")
    ("help,h", "Display this message.")
    ("quorum", po::value <std::size_t> (),
        "Override the minimum validation quorum.")
    ("silent", "No output to the console after startup.")
    ("standalone,a", "Run with no peers.")
    ("verbose,v", "Verbose logging.")
    ("version", "Display the build version.")
    ;

    po::options_description data ("Ledger/Data Options");
    data.add_options ()
    ("import", importText.c_str ())
    ("ledger", po::value<std::string> (),
        "Load the specified ledger and start from the value given.")
    ("ledgerfile", po::value<std::string> (), "Load the specified ledger file.")
    ("load", "Load the current ledger from the local DB.")
    ("net", "Get the initial ledger from the network.")
    ("nodetoshard", "Import node store into shards")
    ("replay","Replay a ledger close.")
    ("start", "Start from a fresh Ledger.")
    ("valid", "Consider the initial ledger a valid network ledger.")
    ("validateShards", shardsText.c_str ())
    ;

    po::options_description rpc ("RPC Client Options");
    rpc.add_options()
    ("rpc",
        "Perform rpc command - see below for available commands. "
        "This is assumed if any positional parameters are provided.")
    ("rpc_ip", po::value <std::string> (),
        "Specify the IP address for RPC command. "
        "Format: <ip-address>[':'<port-number>]")
    ("rpc_port", po::value <std::uint16_t> (),
        "DEPRECATED: include with rpc_ip instead. "
        "Specify the port number for RPC command.")
    ;

    po::options_description test ("Unit Test Options");
    test.add_options()
    ("quiet,q",
        "Suppress test suite messages, "
        "including suite/case name (at start) and test log messages.")
    ("unittest,u", po::value <std::string> ()->implicit_value (""),
        "Perform unit tests. The optional argument specifies one or "
        "more comma-separated selectors. Each selector specifies a suite name, "
        "full-name (lib.module.suite), module, or library "
        "(checked in that ""order).")
    ("unittest-arg", po::value <std::string> ()->implicit_value (""),
        "Supplies an argument string to unit tests. If provided, this argument "
        "is made available to each suite that runs. Interpretation of the "
        "argument is handled individually by any suite that accesses it -- "
        "as such, it typically only make sense to provide this when running "
        "a single suite.")
    ("unittest-ipv4", "Use IPv4 localhost when running unittests (default is IPv6).")
    ("unittest-log",
        "Force unit test log message output. Only useful in combination with "
        "--quiet, in which case log messages will print but suite/case names "
        "will not.")
#if HAS_BOOST_PROCESS
    ("unittest-jobs", po::value <std::size_t> (),
        "Number of unittest jobs to run in parallel (child processes).")
#endif
    ;

    // These are hidden options, not intended to be shown in the usage/help message
    po::options_description hidden ("Hidden Options");
    hidden.add_options()
    ("parameters", po::value< vector<string> > (),
        "Specify rpc command and parameters. This option must be repeated "
        "for each command/param. Positional parameters also serve this purpose, "
        "so this option is not needed for users")
    ("unittest-child",
        "For internal use only when spawning child unit test processes.")
    ;

    // Interpret positional arguments as --parameters.
    po::positional_options_description p;
    p.add ("parameters", -1);

    po::options_description all;
    all.add(gen).add(rpc).add(data).add(test).add(hidden);

    po::options_description desc;
    desc.add(gen).add(rpc).add(data).add(test);

    // Parse options, if no error.
    try
    {
        po::store (po::command_line_parser (argc, argv)
            .options (all)                // Parse options.
            .positional (p)               // Remainder as --parameters.
            .run (),
            vm);
        po::notify (vm);                  // Invoke option notify functions.
    }
    catch (std::exception const&)
    {
        std::cerr << "casinocoind: Incorrect command line syntax." << std::endl;
        std::cerr << "Use '--help' for a list of options." << std::endl;
        return 1;
    }

    if (vm.count ("help"))
    {
        printHelp (desc);
        return 0;
    }

    if (vm.count ("version"))
    {
        std::cout << "casinocoind version " <<
            BuildInfo::getVersionString () << std::endl;
        return 0;
    }

    // Run the unit tests if requested.
    // The unit tests will exit the application with an appropriate return code.
    //
    if (vm.count ("unittest"))
    {
        std::string argument;

        if (vm.count("unittest-arg"))
            argument = vm["unittest-arg"].as<std::string>();

        std::size_t numJobs = 1;
        bool unittestChild = false;
#if HAS_BOOST_PROCESS
        if (vm.count("unittest-jobs"))
            numJobs = std::max(numJobs, vm["unittest-jobs"].as<std::size_t>());
        unittestChild = bool (vm.count("unittest-child"));
#endif

        return runUnitTests(
            vm["unittest"].as<std::string>(), argument,
            bool (vm.count ("quiet")),
            bool (vm.count ("unittest-log")),
            unittestChild,
            bool (vm.count ("unittest-ipv4")),
            numJobs,
            argc,
            argv);
    }
    else
    {
#if HAS_BOOST_PROCESS
        if (vm.count("unittest-jobs"))
        {
            // unittest jobs only makes sense with `unittest`
            std::cerr << "rippled: '--unittest-jobs' specified without '--unittest'.\n";
            std::cerr << "To run the unit tests the '--unittest' option must be present.\n";
            return 1;
        }
#endif
    }

    auto config = std::make_unique<Config>();

    auto configFile = vm.count ("conf") ?
            vm["conf"].as<std::string> () : std::string();

    // config file, quiet flag.
    config->setup (configFile, bool (vm.count ("quiet")),
        bool(vm.count("silent")), bool(vm.count("standalone")));

    {
        // Stir any previously saved entropy into the pool:
        auto entropy = getEntropyFile (*config);
        if (!entropy.empty ())
            crypto_prng().load_state(entropy.string ());
    }

    if (vm.count ("start"))
        config->START_UP = Config::FRESH;

    if (vm.count ("import"))
        config->doImport = true;

    if (vm.count("nodetoshard"))
        config->nodeToShard = true;

    if (vm.count ("validateShards "))
        config->validateShards = true;

    if (vm.count ("ledger"))
    {
        config->START_LEDGER = vm["ledger"].as<std::string> ();
        if (vm.count("replay"))
            config->START_UP = Config::REPLAY;
        else
            config->START_UP = Config::LOAD;
    }
    else if (vm.count ("ledgerfile"))
    {
        config->START_LEDGER = vm["ledgerfile"].as<std::string> ();
        config->START_UP = Config::LOAD_FILE;
    }
    else if (vm.count ("load"))
    {
        config->START_UP = Config::LOAD;
    }

    if (vm.count ("valid"))
    {
        config->START_VALID = true;
    }

    if (vm.count ("net"))
    {
        if ((config->START_UP == Config::LOAD) ||
            (config->START_UP == Config::REPLAY))
        {
            std::cerr <<
                "Net and load/reply options are incompatible" << std::endl;
            return -1;
        }

        config->START_UP = Config::NETWORK;
    }

    // Override the RPC destination IP address. This must
    // happen after the config file is loaded.
    if (vm.count ("rpc_ip"))
    {
        auto res = beast::IP::Endpoint::from_string_checked(
            vm["rpc_ip"].as<std::string>());
        if (! res.second)
        {
            std::cerr << "Invalid rpc_ip = " <<
                vm["rpc_ip"].as<std::string>() << "\n";
            return -1;
        }

        if (res.first.port() == 0)
        {
            std::cerr << "No port specified in rpc_ip.\n";
            if (vm.count ("rpc_port"))
            {
                std::cerr << "WARNING: using deprecated rpc_port param.\n";
                try
                {
                    res.first.at_port(vm["rpc_port"].as<std::uint16_t>());
                    if (res.first.port() == 0)
                        throw std::domain_error("0");
                }
                catch(std::exception const& e)
                {
                    std::cerr << "Invalid rpc_port = " << e.what() << "\n";
                    return -1;
                }
            }
            else
                return -1;
        }

        config->rpc_ip = std::move(res.first);
    }

    if (vm.count ("quorum"))
    {
        try
        {
            config->VALIDATION_QUORUM = vm["quorum"].as <std::size_t> ();
            if (config->VALIDATION_QUORUM == std::size_t{})
            {
                throw std::domain_error("0");
            }
        }
        catch(std::exception const& e)
        {
            std::cerr << "Invalid value specified for --quorum ("
                      << e.what() << ")\n";
            return -1;
        }
    }

    // Construct the logs object at the configured severity
    using namespace beast::severities;
    Severity thresh = kInfo;

    if (vm.count ("quiet"))
        thresh = kFatal;
    else if (vm.count ("verbose"))
        thresh = kTrace;

    auto logs = std::make_unique<Logs>(thresh);

    // No arguments. Run server.
    if (!vm.count ("parameters"))
    {
        // We want at least 1024 file descriptors. We'll
        // tweak this further.
        if (!adjustDescriptorLimit(1024, logs->journal("Application")))
            return -1;

        if (HaveSustain() && !vm.count ("fg") && !config->standalone())
        {
            auto const ret = DoSustain ();

            if (!ret.empty ())
                std::cerr << "Watchdog: " << ret << std::endl;
        }

        if (vm.count ("debug"))
        {
            setDebugLogSink (logs->makeSink (
                "Debug", beast::severities::kTrace));
        }

        auto timeKeeper = make_TimeKeeper(
            logs->journal("TimeKeeper"));

        auto app = make_Application(
            std::move(config),
            std::move(logs),
            std::move(timeKeeper));

        if (!app->setup ())
        {
            StopSustain();
            return -1;
        }

        // With our configuration parsed, ensure we have
        // enough file descriptors available:
        if (!adjustDescriptorLimit(
            app->fdlimit(),
            app->logs().journal("Application")))
        {
            StopSustain();
            return -1;
        }

        // Start the server
        app->doStart(true /*start timers*/);

        // Block until we get a stop RPC.
        app->run();

        // Try to write out some entropy to use the next time we start.
        auto entropy = getEntropyFile (app->config());
        if (!entropy.empty ())
            crypto_prng().save_state(entropy.string ());

        return 0;
    }

    // We have an RPC command to process:
    beast::setCurrentThreadName ("casinocoind: rpc");
    return RPCCall::fromCommandLine (
        *config,
        vm["parameters"].as<std::vector<std::string>>(),
        *logs);
}

} // casinocoin

// Must be outside the namespace for obvious reasons
//
int main (int argc, char** argv)
{
#ifdef _MSC_VER
    {
        // Work around for https://svn.boost.org/trac/boost/ticket/10657
        // Reported against boost version 1.56.0.  If an application's
        // first call to GetTimeZoneInformation is from a coroutine, an
        // unhandled exception is generated.  A workaround is to call
        // GetTimeZoneInformation at least once before launching any
        // coroutines.  At the time of this writing the _ftime call is
        // used to initialize the timezone information.
        struct _timeb t;
    #ifdef _INC_TIME_INL
            _ftime_s (&t);
    #else
            _ftime (&t);
    #endif
    }
    ripple::sha512_deprecatedMSVCWorkaround();
#endif

#if defined(__GNUC__) && !defined(__clang__)
    auto constexpr gccver = (__GNUC__ * 100 * 100) +
                            (__GNUC_MINOR__ * 100) +
                            __GNUC_PATCHLEVEL__;

    static_assert (gccver >= 50100,
        "GCC version 5.1.0 or later is required to compile casinocoind.");
#endif

    static_assert (BOOST_VERSION >= 105700,
        "Boost version 1.57 or later is required to compile casinocoind");

    //
    // These debug heap calls do nothing in release or non Visual Studio builds.
    //

    // Checks the heap at every allocation and deallocation (slow).
    //
    //beast::Debug::setAlwaysCheckHeap (false);

    // Keeps freed memory blocks and fills them with a guard value.
    //
    //beast::Debug::setHeapDelayedFree (false);

    // At exit, reports all memory blocks which have not been freed.
    //
#if CASINOCOIN_DUMP_LEAKS_ON_EXIT
    beast::Debug::setHeapReportLeaks (true);
#else
    beast::Debug::setHeapReportLeaks (false);
#endif

    atexit(&google::protobuf::ShutdownProtobufLibrary);

    std::set_terminate(casinocoin::terminateHandler);

    auto const result (casinocoin::run (argc, argv));

    beast::basic_seconds_clock_main_hook();

    return result;
}

