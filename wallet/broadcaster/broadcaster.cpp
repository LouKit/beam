// Copyright 2020 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "broadcaster.h"

#include "version.h"


#include "utility/cli/options.h"
#include "utility/log_rotation.h"

#include "keykeeper/local_private_key_keeper.h"
#include "wallet/core/wallet_network.h"
#include "wallet/client/extensions/newscast/newscast_protocol_builder.h"
#include "wallet/client/extensions/newscast/newscast.h"
#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

using namespace beam;
using namespace beam::wallet;

static const unsigned LOG_ROTATION_PERIOD_SEC = 3*60*60; // 3 hours

io::Reactor::Ptr reactor;

WalletID channelToWalletID(BbsChannel channel)
{
    WalletID dummyWalletID;
    dummyWalletID.m_Channel = channel;
    return dummyWalletID;
}

int main_impl(int argc, char* argv[])
{
    using namespace beam;
    namespace po = boost::program_options;

    const auto path = boost::filesystem::system_complete("./logs");
    auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, "broadcast_", path.string());

    try
    {
        struct
        {
            std::string walletPath;
            std::string nodeURI;
            Nonnegative<uint32_t> pollPeriod_ms;
            uint32_t logCleanupPeriod;

            std::string bbsMessage;
            std::string privateKey;
        } options;

        io::Address nodeAddress;
        IWalletDB::Ptr walletDB;
        reactor = io::Reactor::create();

        {
            po::options_description desc("Beam BBS general options");
            desc.add_options()
                (cli::HELP_FULL, "list of all options")
                (cli::NODE_ADDR_FULL, po::value<std::string>(&options.nodeURI), "address of node")
                (cli::WALLET_STORAGE, po::value<std::string>(&options.walletPath)->default_value("wallet.db"), "path to wallet file")
                (cli::PASS, po::value<std::string>(), "password for the wallet")
                (cli::LOG_CLEANUP_DAYS, po::value<uint32_t>(&options.logCleanupPeriod)->default_value(5), "old logfiles cleanup period(days)")
                (cli::NODE_POLL_PERIOD, po::value<Nonnegative<uint32_t>>(&options.pollPeriod_ms)->default_value(Nonnegative<uint32_t>(0)), "Node poll period in milliseconds. Set to 0 to keep connection. Anyway poll period would be no less than the expected rate of blocks if it is less then it will be rounded up to block rate value.")
            ;

            po::options_description bbsDesc("BBS message options");
            bbsDesc.add_options()
                (cli::BBS_MESSAGE, po::value<std::string>(&options.bbsMessage), "BBS message to broadcast")
                (cli::PRIVATE_KEY, po::value<std::string>(&options.privateKey), "private key to sign BBS message")
            ;
            
            desc.add(bbsDesc);
            desc.add(createRulesOptionsDescription());

            po::variables_map vm;

            po::store(po::command_line_parser(argc, argv)
                .options(desc)
                .style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing)
                .run(), vm);

            if (vm.count(cli::HELP))
            {
                std::cout << desc << std::endl;
                return 0;
            }

            {
                std::ifstream cfg("bbs.cfg");

                if (cfg)
                {
                    po::store(po::parse_config_file(cfg, desc), vm);
                }
            }

            vm.notify();

            getRulesOptions(vm);

            Rules::get().UpdateChecksum();
            LOG_INFO() << "BBS broadcasting utility " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
            LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();
            
            if (vm.count(cli::NODE_ADDR) == 0)
            {
                LOG_ERROR() << "node address should be specified";
                return -1;
            }

            if (vm.count(cli::BBS_MESSAGE) == 0)
            {
                LOG_ERROR() << "message has to be specified";
                return -1;
            }

            if (vm.count(cli::PRIVATE_KEY) == 0)
            {
                LOG_ERROR() << "private key has to be specified";
                return -1;
            }

            if (!nodeAddress.resolve(options.nodeURI.c_str()))
            {
                LOG_ERROR() << "unable to resolve node address: " << options.nodeURI;
                return -1;
            }

            if (!WalletDB::isInitialized(options.walletPath))
            {
                LOG_ERROR() << "Wallet not found, path is: " << options.walletPath;
                return -1;
            }

            SecString pass;
            if (!beam::read_wallet_pass(pass, vm))
            {
                LOG_ERROR() << "Please, provide password for the wallet.";
                return -1;
            }

            walletDB = WalletDB::open(options.walletPath, pass, reactor);

            LOG_INFO() << "wallet sucessfully opened...";
        }

        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);

        LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD_SEC, options.logCleanupPeriod);

        auto keyKeeper = std::make_shared<LocalPrivateKeyKeeper>(walletDB, walletDB->get_MasterKdf());
        Wallet wallet{ walletDB, keyKeeper };

        wallet.ResumeAllTransactions();

        auto nnet = std::make_shared<proto::FlyClient::NetworkStd>(wallet);
        nnet->m_Cfg.m_PollPeriod_ms = options.pollPeriod_ms.value;
        
        if (nnet->m_Cfg.m_PollPeriod_ms)
        {
            LOG_INFO() << "Node poll period = " << nnet->m_Cfg.m_PollPeriod_ms << " ms";
            uint32_t timeout_ms = std::max(Rules::get().DA.Target_s * 1000, nnet->m_Cfg.m_PollPeriod_ms);
            if (timeout_ms != nnet->m_Cfg.m_PollPeriod_ms)
            {
                LOG_INFO() << "Node poll period has been automatically rounded up to block rate: " << timeout_ms << " ms";
            }
        }
        uint32_t responceTime_s = Rules::get().DA.Target_s * wallet::kDefaultTxResponseTime;
        if (nnet->m_Cfg.m_PollPeriod_ms >= responceTime_s * 1000)
        {
            LOG_WARNING() << "The \"--node_poll_period\" parameter set to more than " << uint32_t(responceTime_s / 3600) << " hours may cause transaction problems.";
        }
        nnet->m_Cfg.m_vNodes.push_back(nodeAddress);
        nnet->Connect();

        auto wnet = std::make_shared<WalletNetworkViaBbs>(wallet, nnet, walletDB, keyKeeper);
		wallet.AddMessageEndpoint(wnet);
        wallet.SetNodeEndpoint(nnet);

        std::shared_ptr<IWalletMessageEndpoint> messageEndpoint(wnet);

        auto optionalKey = NewscastProtocolBuilder::stringToPrivateKey(options.privateKey);
        if (!optionalKey)
        {
            LOG_ERROR() << "Invalid private key.";
            return -1;
        }

        NewsMessage newsMsg = {options.bbsMessage};
        ByteBuffer message = NewscastProtocolBuilder::createMessage(newsMsg, *optionalKey);
        messageEndpoint->SendRawMessage(channelToWalletID(Newscast::BbsChannelsOffset), message);

        io::Reactor::get_Current().run();

        LOG_INFO() << "Done";
    }
    catch (const DatabaseException&)
    {
        LOG_ERROR() << "Wallet not opened.";
        return -1;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR() << "EXCEPTION: " << e.what();
    }
    catch (...)
    {
        LOG_ERROR() << "NON_STD EXCEPTION";
    }

    return 0;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    return main_impl(argc, argv);
#else
    block_sigpipe();
    auto f = std::async(
        std::launch::async,
        [argc, argv]() -> int {
            // TODO: this hungs app on OSX
            //lock_signals_in_this_thread();
            int ret = main_impl(argc, argv);
            kill(0, SIGINT);
            return ret;
        }
    );

    wait_for_termination(0);

    if (reactor) reactor->stop();

    return f.get();
#endif
}
