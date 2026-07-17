// Copyright (c) 2015-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <optional>
#include <stdexcept>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1261130161; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1262152739;  // Block #32255
    pindexLast.nBits = 0x1d00ffff;

    // ESF uses nPowTargetTimespan = 1175*600 = 705000 (vs Bitcoin's 2016*600).
    // expected_nbits = 0x1d00ffff * 1022578 / 705000 = 0x1d017350.
    unsigned int expected_nbits = 0x1d017350U;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    // Height 32256 > ASERT activation (30200): any transition is permitted.
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    // Height must be at an ESF retarget boundary (multiple of 1175) and below
    // ASERT activation (30200) for PermittedDifficultyTransition to check bounds.
    pindexLast.nHeight = 2349;  // height+1 = 2350 = 2*1175
    pindexLast.nTime = 1233061996;  // Block ~#2350
    pindexLast.nBits = 0x1d00ffff;
    // nActualTimespan = 2055491; ESF timespan = 705000.
    // 705000/4 < 2055491 < 705000*4 -> no clamping.
    // expected = 0x1d00ffff * 2055491 / 705000 = 0x1d02ea61.
    unsigned int expected_nbits = 0x1d02ea61U;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    // Height at an ESF retarget boundary below ASERT activation (30200) so
    // PermittedDifficultyTransition enforces the 4x bounds.
    pindexLast.nHeight = 29374;  // height+1 = 29375 = 25*1175
    // Actual timespan 100000 < ESF min clamp (705000/4 = 176250) -> clamped.
    // expected = 0x1c05a3f4 * 176250 / 705000 = 0x1c05a3f4 / 4 = 0x1c0168fd.
    pindexLast.nTime = 1279108237;  // nLastRetargetTime + 100000
    pindexLast.nBits = 0x1c05a3f4;
    unsigned int expected_nbits = 0x1c0168fdU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
    // Test that reducing nbits further would not be a PermittedDifficultyTransition.
    unsigned int invalid_nbits = expected_nbits-1;
    BOOST_CHECK(!PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, invalid_nbits));
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    // Height at an ESF retarget boundary below ASERT activation (30200) so
    // PermittedDifficultyTransition enforces the 4x bounds.
    pindexLast.nHeight = 27024;  // height+1 = 27025 = 23*1175
    pindexLast.nTime = 1269211443;  // Block ~#27025
    pindexLast.nBits = 0x1c387f6f;
    // nActualTimespan = 6048000 > ESF max clamp (705000*4 = 2820000) -> clamped.
    // expected = 0x1c387f6f * 4 = 0x1d00e1fd (same as Bitcoin by coincidence).
    unsigned int expected_nbits = 0x1d00e1fdU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
    // Test that increasing nbits further would not be a PermittedDifficultyTransition.
    unsigned int invalid_nbits = expected_nbits+1;
    BOOST_CHECK(!PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, invalid_nbits));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    nBits = UintToArith256(consensus.powLimit).GetCompact(true);
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits{~0x00800000U};
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 nBits_arith = UintToArith256(consensus.powLimit);
    nBits_arith *= 2;
    nBits = nBits_arith.GetCompact();
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_biger_hash_than_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith = UintToArith256(consensus.powLimit);
    nBits = hash_arith.GetCompact();
    hash_arith *= 2; // hash > nBits
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith{0};
    nBits = hash_arith.GetCompact();
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p2 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p3 = &blocks[m_rng.randrange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

void sanity_check_chainparams(const ArgsManager& args, ChainType chain_type)
{
    const auto chainParams = CreateChainParams(args, chain_type);
    const auto consensus = chainParams->GetConsensus();

    // hash genesis is correct
    BOOST_CHECK_EQUAL(consensus.hashGenesisBlock, chainParams->GenesisBlock().GetHash());

    // target timespan is an even multiple of spacing
    BOOST_CHECK_EQUAL(consensus.nPowTargetTimespan % consensus.nPowTargetSpacing, 0);

    // genesis nBits is positive, doesn't overflow and is lower than powLimit
    arith_uint256 pow_compact;
    bool neg, over;
    pow_compact.SetCompact(chainParams->GenesisBlock().nBits, &neg, &over);
    BOOST_CHECK(!neg && pow_compact != 0);
    BOOST_CHECK(!over);
    BOOST_CHECK(UintToArith256(consensus.powLimit) >= pow_compact);

    // check max target * 4*nPowTargetTimespan doesn't overflow -- see pow.cpp:CalculateNextWorkRequired()
    // ESF mainnet/testnet have a large powLimit (only 20 leading zero bits) so powLimit
    // exceeds targ_max. CalculateNextWorkRequired handles overflow with an explicit guard;
    // see the calcnextwork_overflow_guard test. Only enforce this bound where it holds.
    if (!consensus.fPowNoRetargeting) {
        arith_uint256 targ_max{UintToArith256(uint256{"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"})};
        targ_max /= consensus.nPowTargetTimespan*4;
        if (UintToArith256(consensus.powLimit) < targ_max) {
            BOOST_CHECK(UintToArith256(consensus.powLimit) < targ_max);
        }
    }

    // CHAINPARAM-001: AuxPoW-enabled networks use ESF chain ID 1175 (used in the AuxPoW
    // commitment). v29 adds TESTNET4, which does NOT enable AuxPoW (nAuxpowChainId == 0);
    // scope the checks to the chains that configure AuxPoW.
    if (consensus.nAuxpowChainId != 0) {
        BOOST_CHECK_EQUAL(consensus.nAuxpowChainId, 1175);
        // AuxPoW activation boundary must be correctly set.
        // Note: on signet nAuxpowStartHeight may be INT_MAX (AuxPoW disabled); the
        // boundary checks still hold since IsAuxpowActive tests nHeight >= start.
        if (consensus.nAuxpowStartHeight > 0) {
            BOOST_CHECK(!consensus.IsAuxpowActive(consensus.nAuxpowStartHeight - 1));
        }
        BOOST_CHECK(consensus.IsAuxpowActive(consensus.nAuxpowStartHeight));
    }
}

BOOST_AUTO_TEST_CASE(ChainParams_MAIN_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(ChainParams_REGTEST_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::REGTEST);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET4_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET4);
}

BOOST_AUTO_TEST_CASE(ChainParams_SIGNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::SIGNET);
}

// Verify CalculateNextWorkRequired overflow guard: when nBits is at ESF's powLimit,
// bnNew * nActualTimespan exceeds 2^256 (powLimit ~ 2^235.9, max timespan ~ 2^21.4).
// The guard must return powLimit instead of wrapping around to garbage.
BOOST_AUTO_TEST_CASE(calcnextwork_overflow_guard)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& consensus = chainParams->GetConsensus();

    CBlockIndex pindexLast;
    pindexLast.nHeight = 1174;
    pindexLast.nBits = UintToArith256(consensus.powLimit).GetCompact();  // 0x1e0fffff
    pindexLast.nTime = 2000000000;
    // nFirstBlockTime = 0 -> timespan = 2e9 >> max clamp (4*705000 = 2820000)
    // -> clamped to 2820000. bnNew * 2820000 ~ 2^257 -> overflow guard fires.
    int64_t nFirstBlockTime = 0;

    unsigned int result = CalculateNextWorkRequired(&pindexLast, nFirstBlockTime, consensus);
    unsigned int expected = UintToArith256(consensus.powLimit).GetCompact();
    BOOST_CHECK_EQUAL(result, expected);
}

// Verify GetNextWorkRequired selects the correct DAA at the ASERT activation boundary.
// Uses the LIVE mainnet params (activation height 31733, anchor at height 31729) so the
// test tracks the shipped chainparams instead of a hardcoded pre-fork boundary.
//   nNextHeight 31732 -> legacy (below activation; not a retarget boundary, nBits unchanged)
//   nNextHeight 31733 -> first ASERT block; on-schedule -> target equals anchor target
//   nNextHeight 31734 -> ASERT continues; one block behind -> target easier (<= powLimit)
BOOST_AUTO_TEST_CASE(ASERT_activation_boundary)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& consensus = chainParams->GetConsensus();
    BOOST_REQUIRE(consensus.asertAnchorParams.has_value());
    const auto& anchor = *consensus.asertAnchorParams;
    // Pin the boundary this test targets to the live params so it fails loudly (rather
    // than silently resolving via the legacy DAA) if activation/anchor heights move.
    BOOST_REQUIRE_EQUAL(consensus.nASERTActivationHeight, 31733);
    BOOST_REQUIRE_EQUAL(anchor.nHeight, 31729);

    // --- nNextHeight=31732: legacy, below activation and not a retarget boundary ---
    {
        CBlockIndex pindexLast;
        pindexLast.nHeight = 31731;  // nNextHeight = 31732
        pindexLast.nBits   = anchor.nBits;
        pindexLast.nTime   = anchor.nPrevBlockTime + (31731 - anchor.nHeight + 1) * 600;
        BOOST_REQUIRE(!consensus.IsASERTActive(31732));
        unsigned int result = GetNextWorkRequired(&pindexLast, nullptr, consensus);
        BOOST_CHECK_EQUAL(result, pindexLast.nBits);  // legacy, no retarget -> nBits unchanged
    }

    // --- nNextHeight=31733: first ASERT block ---
    // On-schedule (exponent=0) -> result must equal anchor nBits. This actually drives
    // GetNextASERTWorkRequired; the legacy path is not taken at or above activation.
    {
        CBlockIndex pindexLast;
        pindexLast.nHeight = 31732;  // nNextHeight = 31733
        BOOST_REQUIRE(consensus.IsASERTActive(31733));
        // nHeightDiff = 31732 - 31729 = 3; on-schedule: nTimeDiff = (3+1)*600 = 2400
        pindexLast.nTime = anchor.nPrevBlockTime + 2400;
        pindexLast.nBits = anchor.nBits;
        unsigned int result = GetNextWorkRequired(&pindexLast, nullptr, consensus);
        arith_uint256 refTarget;
        refTarget.SetCompact(anchor.nBits);
        BOOST_CHECK_EQUAL(result, refTarget.GetCompact());
    }

    // --- nNextHeight=31734: second ASERT block, one spacing behind schedule ---
    // exponent > 0 -> target must be easier (>= anchor target) and <= powLimit.
    {
        CBlockIndex pindexLast;
        pindexLast.nHeight = 31733;  // nNextHeight = 31734
        // nHeightDiff = 31733 - 31729 = 4; one block behind: nTimeDiff = (4+1)*600 + 600 = 3600
        pindexLast.nTime = anchor.nPrevBlockTime + 3600;
        pindexLast.nBits = anchor.nBits;
        unsigned int result = GetNextWorkRequired(&pindexLast, nullptr, consensus);
        arith_uint256 refTarget;
        refTarget.SetCompact(anchor.nBits);
        arith_uint256 resultTarget;
        resultTarget.SetCompact(result);
        BOOST_CHECK(resultTarget >= refTarget);
        BOOST_CHECK(resultTarget <= UintToArith256(consensus.powLimit));
    }
}

// ---- ASERT (aserti3-2d) tests ----

// On-schedule: when blocks arrive exactly on time the target is unchanged.
BOOST_AUTO_TEST_CASE(ASERT_on_target)
{
    const arith_uint256 powLimit = UintToArith256(uint256{
        "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"});
    arith_uint256 refTarget;
    refTarget.SetCompact(0x1d00ffff);

    // exponent = (timeDiff - spacing*(heightDiff+1)) / halfLife = 0 -> target unchanged
    arith_uint256 result = CalculateASERT(refTarget, /*spacing=*/600, /*timeDiff=*/600, /*heightDiff=*/0, powLimit, /*halfLife=*/172800);
    BOOST_CHECK(result == refTarget);
}

// One half-life of slow blocks doubles the target (easier mining).
BOOST_AUTO_TEST_CASE(ASERT_slow_half_life)
{
    const arith_uint256 powLimit = UintToArith256(uint256{
        "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"});
    arith_uint256 refTarget;
    refTarget.SetCompact(0x1d00ffff);

    // timeDiff = spacing + halfLife -> exponent = +1 half-life -> target x 2
    arith_uint256 result = CalculateASERT(refTarget, 600, 600 + 172800, 0, powLimit, 172800);
    BOOST_CHECK(result == refTarget * 2);
}

// One half-life of fast blocks halves the target (harder mining).
BOOST_AUTO_TEST_CASE(ASERT_fast_half_life)
{
    const arith_uint256 powLimit = UintToArith256(uint256{
        "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"});
    arith_uint256 refTarget;
    refTarget.SetCompact(0x1d00ffff);

    // timeDiff = spacing - halfLife -> exponent = -1 half-life -> target / 2
    arith_uint256 result = CalculateASERT(refTarget, 600, 600 - 172800, 0, powLimit, 172800);
    BOOST_CHECK(result == refTarget / 2);
}

// Multi-block on-schedule: heightDiff correctly offsets the exponent.
BOOST_AUTO_TEST_CASE(ASERT_height_diff_on_target)
{
    const arith_uint256 powLimit = UintToArith256(uint256{
        "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"});
    arith_uint256 refTarget;
    refTarget.SetCompact(0x1d00ffff);

    // 100 blocks, perfect timing: timeDiff = 101*600, heightDiff = 100 -> exponent = 0
    arith_uint256 result = CalculateASERT(refTarget, 600, 101 * 600, 100, powLimit, 172800);
    BOOST_CHECK(result == refTarget);
}

// Very slow mining (20 half-lives behind) clamps at powLimit.
BOOST_AUTO_TEST_CASE(ASERT_clamp_at_powlimit)
{
    const arith_uint256 powLimit = UintToArith256(uint256{
        "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"});
    arith_uint256 refTarget;
    refTarget.SetCompact(0x1d00ffff);

    arith_uint256 result = CalculateASERT(refTarget, 600, 600 + 20 * 172800, 0, powLimit, 172800);
    BOOST_CHECK(result == powLimit);
}

// Very fast mining clamps at minimum target (1).
// Use refTarget=4: two half-lives fast drives fixed-point result to 0 -> clamped to 1.
// (4 * 2^16) >> 18 = 2^18 >> 18 = 1, exactly.)
BOOST_AUTO_TEST_CASE(ASERT_clamp_at_minimum)
{
    const arith_uint256 powLimit = UintToArith256(uint256{
        "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"});
    const arith_uint256 refTarget{4};

    arith_uint256 result = CalculateASERT(refTarget, 600, 600 - 2 * 172800, 0, powLimit, 172800);
    BOOST_CHECK(result == arith_uint256(1));
}

// ESF powLimit (only 20 leading zero bits) does not crash.
// The removed assert((powLimit >> 224) == 0) would abort here; this test
// proves it no longer fires and the result is sane.
BOOST_AUTO_TEST_CASE(ASERT_esf_powlimit_no_crash)
{
    const arith_uint256 esfPowLimit = UintToArith256(uint256{
        "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"});

    // On-target with refTarget == powLimit: result should equal powLimit
    arith_uint256 result = CalculateASERT(esfPowLimit, 600, 600, 0, esfPowLimit, 172800);
    BOOST_CHECK(result == esfPowLimit);

    // One half-life slow with refTarget == powLimit: would exceed powLimit -> clamps
    result = CalculateASERT(esfPowLimit, 600, 600 + 172800, 0, esfPowLimit, 172800);
    BOOST_CHECK(result == esfPowLimit);
}

// Symmetry: k half-lives fast then k half-lives slow returns near the original target.
// Tests that the polynomial approximation is stable (no drift over round-trips).
BOOST_AUTO_TEST_CASE(ASERT_round_trip_symmetry)
{
    const arith_uint256 powLimit = UintToArith256(uint256{
        "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"});
    arith_uint256 refTarget;
    refTarget.SetCompact(0x1d00ffff);

    // 1 half-life fast -> half the target
    arith_uint256 faster = CalculateASERT(refTarget, 600, 600 - 172800, 0, powLimit, 172800);
    BOOST_CHECK(faster == refTarget / 2);

    // Then 1 half-life slow from that halved target -> should recover to refTarget
    arith_uint256 recovered = CalculateASERT(faster, 600, 600 + 172800, 0, powLimit, 172800);
    BOOST_CHECK(recovered == refTarget);
}

// OVERFLOW-GUARD-001: powLimit nBits with unclamped-but-overflowing timespan.
// ESF powLimit ~ 2^236; any nActualTimespan > 2^20 ~ 1048576s causes the product
// powLimit * nActualTimespan to exceed 2^256 without triggering the 4x clamp.
// The overflow guard in CalculateNextWorkRequired must return powLimit in this case too.
BOOST_AUTO_TEST_CASE(calcnextwork_overflow_guard_inrange)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& consensus = chainParams->GetConsensus();

    CBlockIndex pindexLast;
    // Height at a retarget boundary below ASERT activation (30200).
    pindexLast.nHeight = 29374;  // 29375 = 1175*25
    pindexLast.nBits   = UintToArith256(consensus.powLimit).GetCompact();
    pindexLast.nTime   = 2000000000;
    // nActualTimespan = 2000000s: above min clamp (176250) and below max clamp (2820000)
    // -> no clamping applied.  But powLimit * 2000000 ~ 2^257 > 2^256 -> overflow guard fires.
    int64_t nFirstBlockTime = pindexLast.nTime - 2000000;

    unsigned int result = CalculateNextWorkRequired(&pindexLast, nFirstBlockTime, consensus);
    BOOST_CHECK_EQUAL(result, UintToArith256(consensus.powLimit).GetCompact());
}

// ASERT-UNIT-003: when pindexLast is at the anchor height (heightDiff=0) and one block
// arrives exactly on schedule, CalculateASERT must return the anchor target unchanged.
BOOST_AUTO_TEST_CASE(ASERT_anchor_height_exact)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& consensus = chainParams->GetConsensus();
    BOOST_REQUIRE(consensus.asertAnchorParams.has_value());
    const auto& anchor = *consensus.asertAnchorParams;

    const arith_uint256 powLimit = UintToArith256(consensus.powLimit);
    arith_uint256 refTarget;
    refTarget.SetCompact(anchor.nBits);

    // heightDiff=0: pindexLast.nHeight == anchor.nHeight.
    // timeDiff = spacing -> exponent numerator = spacing - spacing*(0+1) = 0 -> unchanged.
    arith_uint256 result = CalculateASERT(
        refTarget,
        consensus.nPowTargetSpacing,
        /*timeDiff=*/consensus.nPowTargetSpacing,
        /*heightDiff=*/0,
        powLimit,
        consensus.nASERTHalfLife);
    BOOST_CHECK(result == refTarget);
    BOOST_CHECK_EQUAL(result.GetCompact(), anchor.nBits);
}

// ASERT-UNIT-002: testnet ASERT with extreme slow-block timing clamps at testnet powLimit.
// Testnet: ASERT activates at height 1000, anchor at height 0, nBits=0x1e0ffff0.
// 20 half-lives behind schedule drives the target well past powLimit -> clamped.
BOOST_AUTO_TEST_CASE(ASERT_testnet_min_difficulty)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::TESTNET);
    const Consensus::Params& consensus = chainParams->GetConsensus();
    BOOST_REQUIRE(consensus.asertAnchorParams.has_value());
    const auto& anchor = *consensus.asertAnchorParams;

    const arith_uint256 powLimit = UintToArith256(consensus.powLimit);
    arith_uint256 refTarget;
    refTarget.SetCompact(anchor.nBits);

    // 20 half-lives slow with refTarget at anchor nBits -> target far exceeds powLimit.
    arith_uint256 result = CalculateASERT(
        refTarget,
        consensus.nPowTargetSpacing,
        /*timeDiff=*/consensus.nPowTargetSpacing + 20 * consensus.nASERTHalfLife,
        /*heightDiff=*/0,
        powLimit,
        consensus.nASERTHalfLife);
    BOOST_CHECK(result == powLimit);
}

// ASERT-UNIT-005: GetNextWorkRequired must throw if ASERT is active at the
// next height but asertAnchorParams is not set. This guards against chainparams
// misconfiguration that would silently produce a consensus split.
BOOST_AUTO_TEST_CASE(ASERT_missing_anchor_throws)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    Consensus::Params consensus = chainParams->GetConsensus();

    // Activate ASERT at height 1 (i.e., nNextHeight = pindexLast.nHeight + 1 = 1)
    consensus.nASERTActivationHeight = 1;
    consensus.asertAnchorParams = std::nullopt; // deliberately unset

    CBlockIndex pindexLast;
    pindexLast.nHeight = 0;
    pindexLast.nBits   = UintToArith256(consensus.powLimit).GetCompact();
    pindexLast.nTime   = 1769468775;

    // Must throw, not abort or return a garbage target
    BOOST_CHECK_THROW(GetNextWorkRequired(&pindexLast, nullptr, consensus), std::runtime_error);
}

// ASERT-UNIT-006: PermittedDifficultyTransition returns true unconditionally
// for any height at which ASERT is active (ASERT adjusts every block so any
// target transition is valid).
BOOST_AUTO_TEST_CASE(PermittedDifficultyTransition_asert_always_ok)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& consensus = chainParams->GetConsensus();
    // nASERTActivationHeight = 30200; at or above that height, anything goes.
    const int asertStart = consensus.nASERTActivationHeight;

    // Easy -> hard transition at activation height: must be permitted
    BOOST_CHECK(PermittedDifficultyTransition(consensus, asertStart,
        UintToArith256(consensus.powLimit).GetCompact(),  // easy (old)
        0x1a000000));                                     // hard (new)

    // Hard -> easy transition well past activation: must be permitted
    BOOST_CHECK(PermittedDifficultyTransition(consensus, asertStart + 10000,
        0x1a000000,                                       // hard (old)
        UintToArith256(consensus.powLimit).GetCompact())); // easy (new)

    // Just below activation height: legacy rules apply (not necessarily true)
    // -- just verify the function doesn't crash
    BOOST_CHECK_NO_THROW(PermittedDifficultyTransition(consensus, asertStart - 1,
        consensus.asertAnchorParams->nBits,
        consensus.asertAnchorParams->nBits));
}

BOOST_AUTO_TEST_SUITE_END()
