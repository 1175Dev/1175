// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <hash.h>
#include <kernel/messagestartchars.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>

using namespace util::hex_literals;

// Workaround MSVC bug triggering C7595 when calling consteval constructors in
// initializer lists.
// A fix may be on the way:
// https://developercommunity.visualstudio.com/t/consteval-conversion-function-fails/1579014
#if defined(_MSC_VER)
auto consteval_ctor(auto&& input) { return input; }
#else
#define consteval_ctor(input) (input)
#endif

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.version = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000009975e72bc3c40def7d28fefc84195c30b44f84132d165e38983ea8a3fa6, ver=1, hashPrevBlock=00000000000000, nTime=1769468770, nBits=1d00ffff, vtx=1)
 *   CTransaction(ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase "When crowns claimed the ledger local sovereignty paid the price")
 *     CTxOut(nValue=50.00000000)
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "When crowns claimed the ledger local sovereignty paid the price";
    const CScript genesisOutputScript = CScript() << ParseHex("047a5db3f73f3ef229b147836d5c9172047e752a47af7bae5941cb255b5f6cef888ca360e91c4814686b2f421b6c58296e12b9b0669fc67aab6b98808508aee43b") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * Main network on which people trade goods and services.
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        m_chain_type = ChainType::MAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.script_flag_exceptions.emplace( // BIP16 exception
            uint256{"00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22"}, SCRIPT_VERIFY_NONE);
        consensus.script_flag_exceptions.emplace( // Taproot exception
            uint256{"0000000000000000000f14c35b2d841e986ab5441de8c585d5ffe55ea1e395ad"}, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS);
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.CSVHeight = 0;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0; // segwit activation height + miner confirmation window
        consensus.powLimit = uint256{"00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetTimespan =  1175 * 10 * 60;
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.enforce_BIP94 = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1058; // 90% of 1175
        consensus.nMinerConfirmationWindow = 1175; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0;

        // Conservative floor (2^70) that the honest chain's real cumulative work
        // provably exceeds. The prior value (0x...09<<...> = ~2^75) was computed as
        // block_count x TIP difficulty, i.e. the work the chain would have IF every
        // block since genesis had tip difficulty. That is unreachable: genesis
        // difficulty is ~0.000244 and the legacy DAA clamps each retarget to <=4x, so
        // real cumulative work at block 30141 is ~2^71 -- setting the threshold above
        // Real cumulative work at block 31728 (getblockheader.chainwork, ~2^72), one
        // below the current live tip 31729 — reachable by the honest chain, blocks
        // low-work fake chains during sync.
        consensus.nMinimumChainWork = uint256{"000000000000000000000000000000000000000000000109f062107f9082c503"};
        // Block 31729 (current live tip; validated by full IBD of the live chain 2026-07-15)
        consensus.defaultAssumeValid = uint256{"0000000000000004aacd6357a54d79de573a39548ee8e08284b86f3ead2a6a74"};

        // AuxPoW: chain ID = 1175, activates at the v29 hard-fork height 31733.
        // The live v25.1 chain ran the legacy 1175-block DAA to its tip (31729) and never
        // activated ASERT/AuxPoW (the old source value 30200 was never honoured on-chain).
        // Activation is the coordinated fork height (just above the tip): the full existing
        // history validates under the legacy DAA and the new rules begin at the fork.
        // Any SHA-256 merged miner (e.g. BCH2 Forge pool) can secure this chain from here.
        consensus.nAuxpowChainId = 1175;
        consensus.nAuxpowStartHeight = 31733;
        assert(consensus.nAuxpowChainId >= 1 && consensus.nAuxpowChainId <= 65535);

        // ASERT (aserti3-2d, BCHN standard): activates at block 31733 alongside AuxPoW.
        // Half-life = 3600s (1 hour) — fast difficulty response for a young, low-hashrate
        // chain (BCH2 uses this same 1-hour value in its early ASERT phase). Anchor = block 31729.
        // nPrevBlockTime is SMOOTHED to time(31729) - one spacing rather than block
        // 31728's real timestamp. Block 31729 was mined ~7 days after 31728 (the parked
        // gap); feeding ASERT the real 31728 time would make it read that gap as "chain
        // fell behind" and open the fork ~11x too easy. Using 1784157876 (block 31729's
        // own time) - 600 makes the anchor on-schedule, so the first post-fork ASERT
        // target equals the anchor difficulty (0x190d1fce) instead of easing. Blocks
        // below the activation height replay under the legacy DAA and never consult this
        // anchor, so this value only affects targets at/after height 31733.
        // Note: the first ASERT target also depends on the real timestamps of the pre-
        // activation blocks 31730-31732 (mined under the legacy DAA); an unusually early
        // or late block 31732 shifts that first target, but it self-corrects within one
        // block.
        consensus.nASERTActivationHeight = 31733;
        consensus.nASERTHalfLife = 60 * 60; // 3600s (1 hour)
        consensus.asertAnchorParams = Consensus::Params::ASERTAnchor{
            31729,      // nHeight        — block 31729 (live tip, full-IBD verified)
            0x190d1fce, // nBits          — block 31729 bits (legacy retarget value)
            1784157276, // nPrevBlockTime — SMOOTHED: block 31729 time (1784157876) - 600s spacing
        };
        // ASERT and AuxPoW must co-activate to avoid any block receiving a legacy-DAA
        // target while being merged-mined (or vice-versa).
        assert(consensus.nASERTActivationHeight == consensus.nAuxpowStartHeight);

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x74;
        pchMessageStart[1] = 0xba;
        pchMessageStart[2] = 0x1b;
        pchMessageStart[3] = 0xe4;
        nDefaultPort = 25360;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 5;
        m_assumed_chain_state_size = 5;

        genesis = CreateGenesisBlock(1769468770, 2263714, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"000009975e72bc3c40def7d28fefc84195c30b44f84132d165e38983ea8a3fa6"});
        assert(genesis.hashMerkleRoot == uint256{"b3e23111bac7123833f15acf8eeeeecf66685a5f34e86efaedc53695aa3bfb34"});

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as an addrfetch if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        vSeeds.emplace_back("seed.1175-coin.com");
        vSeeds.emplace_back("node.1175-coin.com");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,51);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,50);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,178);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "esf";

        vFixedSeeds.clear();

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                { 0, uint256{"000009975e72bc3c40def7d28fefc84195c30b44f84132d165e38983ea8a3fa6"}},
            }
        };

        m_assumeutxo_data = {
            // TODO to be specified in a future patch.
        };

        chainTxData = ChainTxData{
            // Data from RPC: getchaintxstats 4096 000009975e72bc3c40def7d28fefc84195c30b44f84132d165e38983ea8a3fa6
            .nTime    = 1769468770,
            .tx_count = 0,
            .dTxRate  = 0,
        };
    }
};

/**
 * Testnet (v3): public test network which is reset from time to time.
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        m_chain_type = ChainType::TESTNET;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.script_flag_exceptions.emplace( // BIP16 exception
            uint256{"00000000dd30457c001f4095d208cc1296b0eed002427aa599874af7a432b105"}, SCRIPT_VERIFY_NONE);
        // Set all buried deployments to height 1 (always active from near-genesis).
        // The prior values (21111, 330776, etc.) were Bitcoin testnet3 heights that
        // ESF testnet will never organically reach, making BIP34/65/66/CSV permanently
        // inactive and causing test results to diverge from mainnet behavior.
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetTimespan =  1175 * 10 * 60;
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 882; // 75% for testchains
        consensus.nMinerConfirmationWindow = 1175; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256{"0000000000000000000000000000000000000000000000000000000000100010"};
        consensus.defaultAssumeValid = uint256{"00000cc5e5c9151d36a6aba144b8b00b233650b506db84835feaf4186388962b"};

        // AuxPoW + ASERT active from block 1000 on testnet.
        // Chain ID matches mainnet so the same merged-mining pools can serve both.
        consensus.nAuxpowChainId = 1175;
        consensus.nAuxpowStartHeight = 1000;
        consensus.nASERTActivationHeight = 1000;
        consensus.nASERTHalfLife = 60 * 60; // 3600s (1 hour) — same as mainnet
        // Anchor = genesis block (height 0, nBits from testnet genesis, nPrevBlockTime = nTime - spacing)
        consensus.asertAnchorParams = Consensus::Params::ASERTAnchor{
            0,          // nHeight
            0x1e0ffff0, // nBits  — testnet genesis bits
            1769468175, // nPrevBlockTime — testnet genesis nTime (1769468775) minus one 10-min spacing
        };

        pchMessageStart[0] = 0x1e;
        pchMessageStart[1] = 0x3c;
        pchMessageStart[2] = 0xa7;
        pchMessageStart[3] = 0x46;
        nDefaultPort = 35360;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 42;
        m_assumed_chain_state_size = 3;

        genesis = CreateGenesisBlock(1769468775, 2355355, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"00000cc5e5c9151d36a6aba144b8b00b233650b506db84835feaf4186388962b"});
        assert(genesis.hashMerkleRoot == uint256{"b3e23111bac7123833f15acf8eeeeecf66685a5f34e86efaedc53695aa3bfb34"});

        vFixedSeeds.clear();
        vSeeds.clear();

        // Distinct prefixes from mainnet (51/50/178) so cross-network key imports
        // fail visibly rather than silently. Standard testnet values: P2PKH=111 (m/n),
        // P2SH=196, WIF=239.
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tesf";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {0, uint256{"00000cc5e5c9151d36a6aba144b8b00b233650b506db84835feaf4186388962b"}},
            }
        };

        m_assumeutxo_data = {
            // TODO to be specified in a future patch.
        };

        chainTxData = ChainTxData{
            // Data from RPC: getchaintxstats 4096 00000cc5e5c9151d36a6aba144b8b00b233650b506db84835feaf4186388962b
            .nTime    = 1769468775,
            .tx_count = 0,
            .dTxRate  = 0,
        };
    }
};

/**
 * Testnet (v4): public test network which is reset from time to time.
 */
class CTestNet4Params : public CChainParams {
public:
    CTestNet4Params() {
        // ESF (1175) testnet4 — an independent second testnet, parallel to testnet3.
        // Same 1175 consensus rules (powLimit, 1175x10min timespan, ASERT + AuxPoW)
        // but its own genesis, magic, port and bech32 HRP so it never cross-connects
        // with testnet3. v25 had no testnet4; this net is v29-native.
        m_chain_type = ChainType::TESTNET4;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 210000;
        // Buried deployments active from height 1 (mirrors testnet3).
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetTimespan =  1175 * 10 * 60;
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 882; // 75% for testchains
        consensus.nMinerConfirmationWindow = 1175; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Deployment of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        // Fresh chain: make no minimum-work or assume-valid assumptions.
        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        // AuxPoW + ASERT active from block 1000 (mirrors testnet3).
        // Chain ID matches mainnet so the same merged-mining pools can serve both.
        consensus.nAuxpowChainId = 1175;
        consensus.nAuxpowStartHeight = 1000;
        consensus.nASERTActivationHeight = 1000;
        consensus.nASERTHalfLife = 60 * 60; // 3600s (1 hour) — same as mainnet
        // Anchor = genesis block (height 0, nBits from testnet4 genesis, nPrevBlockTime = nTime - spacing)
        consensus.asertAnchorParams = Consensus::Params::ASERTAnchor{
            0,          // nHeight
            0x1e0ffff0, // nBits  — testnet4 genesis bits
            1769468185, // nPrevBlockTime — testnet4 genesis nTime (1769468785) minus one 10-min spacing
        };

        pchMessageStart[0] = 0x1e;
        pchMessageStart[1] = 0x3c;
        pchMessageStart[2] = 0xa7;
        pchMessageStart[3] = 0x74; // distinct from testnet3 (…46) so the two testnets never cross-connect
        nDefaultPort = 55360;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 0;

        genesis = CreateGenesisBlock(1769468785, 166611, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"000002abcb7b22d314e998c6c07af8003b5a0b09c699d1e85a86331303ca864c"});
        assert(genesis.hashMerkleRoot == uint256{"b3e23111bac7123833f15acf8eeeeecf66685a5f34e86efaedc53695aa3bfb34"});

        vFixedSeeds.clear();
        vSeeds.clear();

        // Standard testnet values: P2PKH=111 (m/n), P2SH=196, WIF=239.
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "t4esf";

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {0, uint256{"000002abcb7b22d314e998c6c07af8003b5a0b09c699d1e85a86331303ca864c"}},
            }
        };

        m_assumeutxo_data = {
            // TODO to be specified in a future patch.
        };

        chainTxData = ChainTxData{
            // Data from RPC: getchaintxstats 4096 000002abcb7b22d314e998c6c07af8003b5a0b09c699d1e85a86331303ca864c
            .nTime    = 1769468785,
            .tx_count = 0,
            .dTxRate  = 0,
        };
    }
};

/**
 * Signet: test network with an additional consensus parameter (see BIP325).
 */
class SigNetParams : public CChainParams {
public:
    explicit SigNetParams(const SigNetOptions& options)
    {
        std::vector<uint8_t> bin;
        vFixedSeeds.clear();
        vSeeds.clear();

        if (!options.challenge) {
            bin = "512103ad5e0edad18cb1f0fc0d28a3d4f1f3e445640337489abb10404f2d1e086be430210359ef5021964fe22d6f8e05b2463c9540ce96883fe3b278760f048f5189f2e6c452ae"_hex_v_u8;

            // Hardcoded nodes can be removed once there are more DNS seeds

            consensus.nMinimumChainWork = uint256{"0000000000000000000000000000000000000000000000000000000000100010"};
            consensus.defaultAssumeValid = uint256{"000001b13e6302956fa08c9d27f068ffa4db83f8a786a6a8b85ba7b19f57065f"};
            m_assumed_blockchain_size = 1;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                // Data from RPC: getchaintxstats 4096 000001b13e6302956fa08c9d27f068ffa4db83f8a786a6a8b85ba7b19f57065f
                .nTime    = 1769468780,
                .tx_count = 0,
                .dTxRate  = 0,
            };
        } else {
            bin = *options.challenge;
            consensus.nMinimumChainWork = uint256{};
            consensus.defaultAssumeValid = uint256{};
            m_assumed_blockchain_size = 0;
            m_assumed_chain_state_size = 0;
            chainTxData = ChainTxData{
                0,
                0,
                0,
            };
            LogPrintf("Signet with challenge %s\n", HexStr(bin));
        }

        if (options.seeds) {
            vSeeds = *options.seeds;
        }

        m_chain_type = ChainType::SIGNET;
        consensus.signet_blocks = true;
        consensus.signet_challenge.assign(bin.begin(), bin.end());
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256{};
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;
        consensus.nPowTargetTimespan =  1175 * 10 * 60;
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.enforce_BIP94 = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1058; // 90% of 1175
        consensus.nMinerConfirmationWindow = 1175; // nPowTargetTimespan / nPowTargetSpacing
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"00000377ae000000000000000000000000000000000000000000000000000000"};
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        // Activation of Taproot (BIPs 340-342)
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        // message start is defined as the first 4 bytes of the sha256d of the block script
        HashWriter h{};
        h << consensus.signet_challenge;
        uint256 hash = h.GetHash();
        std::copy_n(hash.begin(), 4, pchMessageStart.begin());

        nDefaultPort = 45360; // 325360 overflows uint16_t; use 45360 (distinct from mainnet 25360, testnet 35360)
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1769468780, 6101482, 0x1e0377ae, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"000001b13e6302956fa08c9d27f068ffa4db83f8a786a6a8b85ba7b19f57065f"});
        assert(genesis.hashMerkleRoot == uint256{"b3e23111bac7123833f15acf8eeeeecf66685a5f34e86efaedc53695aa3bfb34"});

        vFixedSeeds.clear();

        m_assumeutxo_data = {
            // TODO to be specified in a future patch.
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,48); // distinct from testnet(51) and mainnet
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,47);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,176);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "sesf"; // distinct from testnet "tesf"

        // AuxPoW + ASERT: disabled by default (INT_MAX); can be enabled for a
        // custom signet by subclassing.  Anchor supplied so GetNextASERTWorkRequired
        // does not assert-abort if a future signet operator sets nASERTActivationHeight
        // without realising the anchor must also be supplied.
        consensus.nAuxpowChainId = 1175;
        consensus.nAuxpowStartHeight = std::numeric_limits<int>::max();
        consensus.nASERTActivationHeight = std::numeric_limits<int>::max();
        consensus.nASERTHalfLife = 60 * 60;
        consensus.asertAnchorParams = Consensus::Params::ASERTAnchor{
            0,          // nHeight
            0x1e0377ae, // nBits — signet genesis bits
            1769468180, // nPrevBlockTime — genesis nTime (1769468780) minus one spacing
        };

        fDefaultConsistencyChecks = false;
        m_is_mockable_chain = false;
    }
};

/**
 * Regression test: intended for private networks only. Has minimal difficulty to ensure that
 * blocks can be found instantly.
 */
class CRegTestParams : public CChainParams
{
public:
    explicit CRegTestParams(const RegTestOptions& opts)
    {
        m_chain_type = ChainType::REGTEST;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP34Height = 1; // Always active unless overridden
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1;  // Always active unless overridden
        consensus.BIP66Height = 1;  // Always active unless overridden
        consensus.CSVHeight = 1;    // Always active unless overridden
        consensus.SegwitHeight = 0; // Always active unless overridden
        consensus.MinBIP9WarningHeight = 0;
        consensus.powLimit = uint256{"7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
        consensus.nPowTargetTimespan =  1175 * 10 * 60;
        consensus.nPowTargetSpacing = 10 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.enforce_BIP94 = opts.enforce_bip94;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)

        // AuxPoW: activate at block 200 for regtest (low enough to test easily)
        consensus.nAuxpowChainId = 1175;
        consensus.nAuxpowStartHeight = 200;
        // ASERT: inactive in regtest (fPowNoRetargeting guards it), but set anchor
        // so unit tests can call GetNextASERTWorkRequired without a null-dereference.
        consensus.nASERTActivationHeight = std::numeric_limits<int>::max();
        consensus.nASERTHalfLife = 60 * 60;
        consensus.asertAnchorParams = Consensus::Params::ASERTAnchor{
            199,          // nHeight
            0x207fffff,   // nBits (regtest easy target)
            0,            // nPrevBlockTime (genesis)
        };

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].min_activation_height = 0; // No activation delay

        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].min_activation_height = 0; // No activation delay

        consensus.nMinimumChainWork = uint256{};
        consensus.defaultAssumeValid = uint256{};

        pchMessageStart[0] = 0x64;
        pchMessageStart[1] = 0x3f;
        pchMessageStart[2] = 0x9c;
        pchMessageStart[3] = 0x8f;
        nDefaultPort = 18175; // distinct from Bitcoin Core regtest (18444)
        nPruneAfterHeight = opts.fastprune ? 100 : 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        for (const auto& [dep, height] : opts.activation_heights) {
            switch (dep) {
            case Consensus::BuriedDeployment::DEPLOYMENT_SEGWIT:
                consensus.SegwitHeight = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_HEIGHTINCB:
                consensus.BIP34Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_DERSIG:
                consensus.BIP66Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CLTV:
                consensus.BIP65Height = int{height};
                break;
            case Consensus::BuriedDeployment::DEPLOYMENT_CSV:
                consensus.CSVHeight = int{height};
                break;
            }
        }

        for (const auto& [deployment_pos, version_bits_params] : opts.version_bits_parameters) {
            consensus.vDeployments[deployment_pos].nStartTime = version_bits_params.start_time;
            consensus.vDeployments[deployment_pos].nTimeout = version_bits_params.timeout;
            consensus.vDeployments[deployment_pos].min_activation_height = version_bits_params.min_activation_height;
        }

        genesis = CreateGenesisBlock(1769468775, 1, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256{"79f89eecf43f07b684f5c00382fe7a26455f921d51973282ba9ea5ff69f7bcc9"});
        assert(genesis.hashMerkleRoot == uint256{"b3e23111bac7123833f15acf8eeeeecf66685a5f34e86efaedc53695aa3bfb34"});

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();
        vSeeds.emplace_back("dummySeed.invalid.");

        fDefaultConsistencyChecks = true;
        m_is_mockable_chain = true;

        checkpointData = {
            {
                {0, uint256{"79f89eecf43f07b684f5c00382fe7a26455f921d51973282ba9ea5ff69f7bcc9"}},
            }
        };

        m_assumeutxo_data = {
            {   // For use by the assumeutxo unit tests (validation_chainstate_tests,
                // validation_chainstatemanager_tests, validation_tests): the height-110
                // regtest chain (TestChain100Setup + 10 blocks). hash_serialized equals
                // Bitcoin's (the regtest coinbase key/amounts/maturity are identical, so the
                // coin set is identical); only the base blockhash is 1175-specific.
                .height = 110,
                .hash_serialized = AssumeutxoHash{uint256{"6657b736d4fe4db0cbc796789e812d5dba7f5c143764b1b6905612f1830609d1"}},
                .m_chain_tx_count = 111,
                .blockhash = consteval_ctor(uint256{"17e4169766421e62358e27ade90025a55adcb40c34eee4af599cba485eb4ba71"}),
            },
            {   // Referenced by feature_assumeutxo.py's "available snapshot heights" listing.
                // No test loads this snapshot; it is a deterministic 1175 regtest 200-block
                // chain (mined to the framework's deterministic key with mocktime=genesis).
                // Both hashes are 1175-specific (this chain differs from Bitcoin's height-200
                // reference chain), captured with dumptxoutset.
                .height = 200,
                .hash_serialized = AssumeutxoHash{uint256{"d4b1b585cf3a5536dcf5fc3f7e69bae904ed5e2a50d11988a0ff5a13fe30d50c"}},
                .m_chain_tx_count = 201,
                .blockhash = consteval_ctor(uint256{"7a412d4f2b875c4c2df8938e903e70434d4dcd422fc621b70f4f2007b18bad2a"}),
            },
            {   // For use by feature_assumeutxo.py / wallet_assumeutxo.py, which build a
                // deterministic 299-block regtest chain (199-block cache + 100 blocks with
                // MiniWallet self-transfers) and load a snapshot at height 299. hash_serialized
                // equals Bitcoin's (identical coin set); only the base blockhash is 1175-specific.
                .height = 299,
                .hash_serialized = AssumeutxoHash{uint256{"a4bf3407ccb2cc0145c49ebba8fa91199f8a3903daf0883875941497d2493c27"}},
                .m_chain_tx_count = 334,
                .blockhash = consteval_ctor(uint256{"4a0be6bfa0cab646be75a04a4db02e9624183f5929e623eef5878fe17c9a32f6"}),
            },
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        // Distinct from mainnet (51/50/178) and testnet (111/196/239).
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,100);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,240);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "resf";
    }
};

std::unique_ptr<const CChainParams> CChainParams::SigNet(const SigNetOptions& options)
{
    return std::make_unique<const SigNetParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::RegTest(const RegTestOptions& options)
{
    return std::make_unique<const CRegTestParams>(options);
}

std::unique_ptr<const CChainParams> CChainParams::Main()
{
    return std::make_unique<const CMainParams>();
}

std::unique_ptr<const CChainParams> CChainParams::TestNet()
{
    return std::make_unique<const CTestNetParams>();
}

std::unique_ptr<const CChainParams> CChainParams::TestNet4()
{
    return std::make_unique<const CTestNet4Params>();
}

std::vector<int> CChainParams::GetAvailableSnapshotHeights() const
{
    std::vector<int> heights;
    heights.reserve(m_assumeutxo_data.size());

    for (const auto& data : m_assumeutxo_data) {
        heights.emplace_back(data.height);
    }
    return heights;
}

std::optional<ChainType> GetNetworkForMagic(const MessageStartChars& message)
{
    const auto mainnet_msg = CChainParams::Main()->MessageStart();
    const auto testnet_msg = CChainParams::TestNet()->MessageStart();
    const auto testnet4_msg = CChainParams::TestNet4()->MessageStart();
    const auto regtest_msg = CChainParams::RegTest({})->MessageStart();
    const auto signet_msg = CChainParams::SigNet({})->MessageStart();

    if (std::ranges::equal(message, mainnet_msg)) {
        return ChainType::MAIN;
    } else if (std::ranges::equal(message, testnet_msg)) {
        return ChainType::TESTNET;
    } else if (std::ranges::equal(message, testnet4_msg)) {
        return ChainType::TESTNET4;
    } else if (std::ranges::equal(message, regtest_msg)) {
        return ChainType::REGTEST;
    } else if (std::ranges::equal(message, signet_msg)) {
        return ChainType::SIGNET;
    }
    return std::nullopt;
}
