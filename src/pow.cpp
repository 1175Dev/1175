// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Bitcoin developers (BCHN ASERT)
// Copyright (c) 2026 The ElevenSeventyFive Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <serialize.h>
#include <uint256.h>
#include <util/check.h>
#include <logging.h>
#include <stdexcept>

/**
 * ASERT Difficulty Adjustment Algorithm (aserti3-2d)
 * Exact copy from Bitcoin Cash Node (BCHN).
 *
 * Compute the next required proof of work using an absolutely scheduled
 * exponentially weighted target (ASERT).
 *
 *   target = refTarget * 2^((timeDiff - (heightDiff + 1) * spacing) / halfLife)
 *
 * Where:
 *   timeDiff   = timestamp of parent block - anchor block's parent timestamp
 *   heightDiff = (height of parent block) - (anchor block height)
 */
arith_uint256 CalculateASERT(const arith_uint256& refTarget,
                              const int64_t nPowTargetSpacing,
                              const int64_t nTimeDiff,
                              const int64_t nHeightDiff,
                              const arith_uint256& powLimit,
                              const int64_t nHalfLife) noexcept
{
    assert(refTarget > 0 && refTarget <= powLimit);
    // NOTE: BCHN's assert((powLimit >> 224) == 0) is intentionally omitted.
    // ESF's powLimit has only 20 leading zero bits, not 32, so that assert
    // would fire and abort every node at block 30200. The polynomial
    // approximation is mathematically correct regardless of powLimit's
    // leading bits; overflow is bounded by the clamp at the end.
    assert(nHeightDiff >= 0);

    assert(llabs(nTimeDiff - nPowTargetSpacing * nHeightDiff) < (1ll << (63 - 16)));
    const int64_t exponent = ((nTimeDiff - nPowTargetSpacing * (nHeightDiff + 1)) * 65536) / nHalfLife;

    static_assert(int64_t(-1) >> 1 == int64_t(-1),
                  "ASERT algorithm needs arithmetic shift support");

    int64_t shifts = exponent >> 16;
    const auto frac = uint16_t(exponent);
    assert(exponent == (shifts * 65536) + frac);

    // 2^x ~= (1 + 0.695502049*x + 0.2262698*x^2 + 0.0782318*x^3) for 0 <= x < 1
    const uint32_t factor = 65536 + ((
        + 195766423245049ull * frac
        + 971821376ull * frac * frac
        + 5127ull * frac * frac * frac
        + (1ull << 47)
        ) >> 48);

    arith_uint256 nextTarget = refTarget * factor;

    shifts -= 16;
    if (shifts <= 0) {
        nextTarget >>= -shifts;
    } else {
        const auto nextTargetShifted = nextTarget << shifts;
        if ((nextTargetShifted >> shifts) != nextTarget) {
            nextTarget = powLimit;
        } else {
            nextTarget = nextTargetShifted;
        }
    }

    if (nextTarget == 0) {
        nextTarget = arith_uint256(1);
    } else if (nextTarget > powLimit) {
        nextTarget = powLimit;
    }

    return nextTarget;
}

unsigned int GetNextASERTWorkRequired(const CBlockIndex* pindexPrev,
                                       const CBlockHeader* pblock,
                                       const Consensus::Params& params) noexcept
{
    assert(pindexPrev != nullptr);
    assert(params.asertAnchorParams.has_value());

    const Consensus::Params::ASERTAnchor& anchor = *params.asertAnchorParams;
    assert(pindexPrev->nHeight >= anchor.nHeight);

    // Testnet: allow min-difficulty blocks after long gap
    if (params.fPowAllowMinDifficultyBlocks &&
        pblock->GetBlockTime() > pindexPrev->GetBlockTime() + 2 * params.nPowTargetSpacing) {
        return UintToArith256(params.powLimit).GetCompact();
    }

    // NOTE: no special-case for pindexPrev at the anchor height. Returning
    // anchor.nBits unconditionally there would be wrong for an off-schedule anchor;
    // CalculateASERT below computes the correct target for the block after the anchor.
    // (On shipped networks this height is never reached anyway, since activation is
    // strictly above the anchor.)

    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    arith_uint256 refTarget;
    bool fNeg{false}, fOvf{false};
    refTarget.SetCompact(anchor.nBits, &fNeg, &fOvf);
    if (fNeg || fOvf || refTarget == 0 || refTarget > powLimit) {
        // Misconfigured chainparams. Abort rather than silently producing minimum-difficulty
        // blocks, which would collapse network security with no visible indication.
        LogPrintf("FATAL: ASERT anchor nBits 0x%08x is invalid — aborting to prevent consensus failure.\n", anchor.nBits);
        std::abort();
    }

    // timeDiff: parent block time minus anchor's parent time
    const int64_t nTimeDiff = pindexPrev->GetBlockTime() - anchor.nPrevBlockTime;
    // heightDiff: parent height minus anchor height
    const int64_t nHeightDiff = pindexPrev->nHeight - anchor.nHeight;

    arith_uint256 nextTarget = CalculateASERT(refTarget,
                                               params.nPowTargetSpacing,
                                               nTimeDiff,
                                               nHeightDiff,
                                               powLimit,
                                               params.nASERTHalfLife);
    return nextTarget.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    const int nNextHeight = pindexLast->nHeight + 1;

    // Use ASERT once active
    if (params.IsASERTActive(nNextHeight)) {
        if (!params.asertAnchorParams) {
            // Configuration error: ASERT activated without anchor params set.
            // Continuing would cause a permanent consensus split with any peer
            // that has correct chainparams. Fail hard.
            throw std::runtime_error(strprintf(
                "ASERT is active at height %d but asertAnchorParams is not set — "
                "network consensus split guaranteed; check chainparams", nNextHeight));
        }
        if (!params.fPowNoRetargeting) {
            return GetNextASERTWorkRequired(pindexLast, pblock, params);
        }
    }

    // Legacy 2016-block retarget (original Bitcoin DAA)
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    if ((pindexLast->nHeight + 1) % params.DifficultyAdjustmentInterval() != 0) {
        if (params.fPowAllowMinDifficultyBlocks) {
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2)
                return nProofOfWorkLimit;
            else {
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval() - 1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;

    // Special difficulty rule for Testnet4
    if (params.enforce_BIP94) {
        // Here we use the first block of the difficulty period. This way
        // the real difficulty is always preserved in the first block as
        // it is not allowed to use the min-difficulty exception.
        int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
        const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
        bnNew.SetCompact(pindexFirst->nBits);
    } else {
        bnNew.SetCompact(pindexLast->nBits);
    }

    // Guard against uint256 overflow: ESF mainnet/testnet have a large powLimit
    // (only 20 leading zero bits), so bnNew * nActualTimespan can exceed 2^256.
    // In that case the adjusted target is far above powLimit; return it directly.
    // (v29 has no arith_uint256 string constructor; ~arith_uint256(0) == 2^256-1,
    //  the same all-ones value 1175 built from a hex string.)
    arith_uint256 kOverflowGuard = ~arith_uint256(0);
    kOverflowGuard /= arith_uint256((uint64_t)nActualTimespan);
    if (bnNew > kOverflowGuard) {
        return bnPowLimit.GetCompact();
    }

    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    // ASERT adjusts every block — any target is valid once active
    if (params.IsASERTActive((int)height))
        return true;

    if (params.fPowAllowMinDifficultyBlocks) return true;

    if (height % params.DifficultyAdjustmentInterval() == 0) {
        int64_t smallest_timespan = params.nPowTargetTimespan/4;
        int64_t largest_timespan = params.nPowTargetTimespan*4;

        const arith_uint256 pow_limit = UintToArith256(params.powLimit);
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // Calculate the largest difficulty value possible:
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        // Guard against uint256 overflow before the multiply, mirroring the guard in
        // CalculateNextWorkRequired. ESF's powLimit has only 20 leading zero bits, so
        // old_target * largest_timespan can exceed 2^256 and wrap mod 2^256, which would
        // collapse the bound and FALSE-REJECT a valid difficulty decrease (observed
        // during headers-sync at early-chain retarget boundaries). When it would
        // overflow, the true bound is far above powLimit, so clamp to powLimit directly.
        const arith_uint256 kOverflowGuardLargest = (~arith_uint256(0)) / arith_uint256((uint64_t)largest_timespan);
        if (largest_difficulty_target > kOverflowGuardLargest) {
            largest_difficulty_target = pow_limit;
        } else {
            largest_difficulty_target *= largest_timespan;
            largest_difficulty_target /= params.nPowTargetTimespan;
            if (largest_difficulty_target > pow_limit) {
                largest_difficulty_target = pow_limit;
            }
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        // Calculate the smallest difficulty value possible:
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        // Same overflow guard for symmetry. smallest_timespan is 16x smaller than
        // largest_timespan so this multiply cannot overflow at current parameters, but
        // guard unconditionally so a future timespan change stays safe.
        const arith_uint256 kOverflowGuardSmallest = (~arith_uint256(0)) / arith_uint256((uint64_t)smallest_timespan);
        if (smallest_difficulty_target > kOverflowGuardSmallest) {
            smallest_difficulty_target = pow_limit;
        } else {
            smallest_difficulty_target *= smallest_timespan;
            smallest_difficulty_target /= params.nPowTargetTimespan;
            if (smallest_difficulty_target > pow_limit) {
                smallest_difficulty_target = pow_limit;
            }
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
}

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    if constexpr (G_FUZZING) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit))
        return {};

    return bnTarget;
}

bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    auto bnTarget{DeriveTarget(nBits, params.powLimit)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

namespace {
// v29 removed the classic Bitcoin Core error() log-and-return-false helper, which
// the AuxPoW check below (ported from 1175) relies on. Reintroduce it TU-locally.
// Call sites pass a plain literal message (no format args); v29's LogError needs a
// compile-time format string, so route the runtime message through a constant "%s\n".
bool error(const char* msg)
{
    // AuxPoW proofs on peer-relayed blocks/headers are untrusted input and fail here
    // during ordinary relay/mutation; log at debug level so a remote peer cannot spam
    // the node's error log. Genuine local misconfiguration surfaces via other paths.
    LogDebug(BCLog::VALIDATION, "%s\n", msg);
    return false;
}
} // namespace

/**
 * Validate AuxPoW on a merged-mined block.
 * The parent block header must satisfy PoW, and its coinbase must commit
 * to this block's hash via the merged mining header format.
 */
bool CheckAuxProofOfWork(const CBlockHeader& block, const Consensus::Params& params)
{
    const bool fAuxpow = (block.nVersion & BLOCK_VERSION_AUXPOW) != 0;

    if (!fAuxpow) {
        // Regular block — normal PoW check
        return CheckProofOfWork(block.GetHash(), block.nBits, params);
    }

    if (!block.auxpow)
        return error("CheckAuxProofOfWork: AuxPoW bit set but no auxpow data");

    // Consensus bound on the serialized proof size. Enforced here so every aux check
    // (header admission, block body, disk read) rejects an oversized proof, which keeps a
    // full `headers` batch under the P2P message-size limit and bounds per-header work.
    // Checked before the (more expensive) PoW hashing.
    if (::GetSerializeSize(TX_WITH_WITNESS(*block.auxpow)) > MAX_AUXPOW_SERIALIZED_SIZE)
        return error("CheckAuxProofOfWork: auxpow proof exceeds maximum serialized size");

    // Parent block header must satisfy PoW
    if (!CheckProofOfWork(block.auxpow->parentBlock.GetHash(), block.nBits, params))
        return error("CheckAuxProofOfWork: parent block does not satisfy PoW");

    // Validate the merged mining commitment
    const int nChainId = params.nAuxpowChainId;
    if (!block.auxpow->Check(block.GetHash(), nChainId, params))
        return error("CheckAuxProofOfWork: AuxPoW check failed");

    return true;
}
