// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2011-2014 The Litecoin developers
// Copyright (c) 2014-2016 The Dogecoin Core developers
// Copyright (c) 2026 The ElevenSeventyFive Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <crypto/common.h>
#include <hash.h>
#include <logging.h>
#include <primitives/transaction.h>
#include <tinyformat.h>
#include <util/strencodings.h>

#include <algorithm>
#include <stdexcept>

namespace {
// v29 removed the classic Bitcoin Core error() helper (which lived in logging.h /
// util/system.h). The AuxPoW validation below is ported verbatim from 1175, which
// relies on `return error("...")` to log-and-return-false. Reintroduce it as a
// TU-local helper. Every call site passes a plain literal message (no format args),
// and v29's LogError requires a compile-time format string, so route the runtime
// message through a constant "%s\n" format.
bool error(const char* msg)
{
    LogError("%s\n", msg);
    return false;
}
} // namespace

uint256 CPureBlockHeader::GetHash() const
{
    // Hash the pure 80-byte header only — never the auxpow. Byte-identical to the
    // pre-auxpow serialization, so the block id is unchanged.
    return (HashWriter{} << *this).GetHash();
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}

// Compute merkle branch hash: follow the branch from hash using nIndex bitmask
uint256 CAuxPow::CheckMerkleBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex)
{
    if (nIndex == -1)
        return uint256();
    // Any other negative nIndex would right-shift to -1 and loop forever.
    // Callers validate this, but guard here for defence-in-depth.
    if (nIndex < 0)
        throw std::invalid_argument(strprintf("CheckMerkleBranch: invalid nIndex %d", nIndex));
    for (const auto& branch : vMerkleBranch) {
        if (nIndex & 1) {
            hash = Hash(branch, hash);
        } else {
            hash = Hash(hash, branch);
        }
        nIndex >>= 1;
    }
    return hash;
}

bool CAuxPow::Check(const uint256& hashAuxBlock, int nChainId, const Consensus::Params& params) const
{
    // Reject empty coinbase (would crash on vin[0] access below)
    if (coinbaseTx.vin.empty())
        return error("AuxPow: coinbase has no inputs");

    // hashBlock must match the parent block header actually provided
    if (hashBlock != parentBlock.GetHash())
        return error("AuxPow: hashBlock does not match parentBlock hash");

    // Enforce chain ID in parent block version to prevent one chain from
    // accidentally mining another via the same merged mining commitment.
    // Use unsigned right-shift (not signed division) to extract bits [31:16]
    // correctly for all nVersion values including negative (bit-31-set) ones.
    if (((uint32_t)parentBlock.nVersion >> 16) == (uint32_t)nChainId)
        return error("AuxPow: chain ID in parent block is the same as aux chain ID");

    // Check the merkle branch connects coinbase to parent block merkle root
    if (vMerkleBranch.size() > 30)
        return error("AuxPow: merkle branch too long");

    // Chain merkle branch also has bounded depth
    if (vChainMerkleBranch.size() > 30)
        return error("AuxPow: chain merkle branch too long");

    // The coinbase must be the first transaction
    if (nIndex != 0)
        return error("AuxPow: coinbase is not first tx (nIndex != 0)");

    // Negative nChainIndex bypasses CheckMerkleBranch (-1 returns zero hash)
    if (nChainIndex < 0)
        return error("AuxPow: negative nChainIndex");

    // Verify coinbase tx hash is at the root of the merkle branch
    const uint256 nRootHash = CheckMerkleBranch(
        CTransaction(coinbaseTx).GetHash(), vMerkleBranch, nIndex);
    if (nRootHash != parentBlock.hashMerkleRoot)
        return error("AuxPow: coinbase merkle branch does not match parent block merkle root");

    // Find the merged mining header in the coinbase scriptSig
    // Format: {0xfa, 0xbe, 0x6d, 0x6d} {child_hash 32B} {merkle_size 4B} {merkle_nonce 4B}
    const CScript& coinbaseData = coinbaseTx.vin[0].scriptSig;
    const std::vector<unsigned char> mergedMiningHeader = {0xfa, 0xbe, 0x6d, 0x6d};

    auto pc = std::search(coinbaseData.begin(), coinbaseData.end(),
                          mergedMiningHeader.begin(), mergedMiningHeader.end());

    if (pc == coinbaseData.end())
        return error("AuxPow: merged mining header not found in coinbase");

    // Ensure header appears only once
    auto pc2 = std::search(pc + 1, coinbaseData.end(),
                           mergedMiningHeader.begin(), mergedMiningHeader.end());
    if (pc2 != coinbaseData.end())
        return error("AuxPow: multiple merged mining headers in coinbase");

    // Skip past the 4-byte magic header
    pc += mergedMiningHeader.size();

    // Must have at least 32 (hash) + 4 (size) + 4 (nonce) = 40 bytes remaining
    if (coinbaseData.end() - pc < 40)
        return error("AuxPow: coinbase too short after merged mining header");

    // Read the child block hash from coinbase (little-endian)
    uint256 nRootHashCoinbase;
    std::copy(pc, pc + 32, nRootHashCoinbase.begin());
    pc += 32;

    // Compute expected chain merkle root from the aux block hash + branch
    const uint256 expectedRoot = CheckMerkleBranch(hashAuxBlock, vChainMerkleBranch, nChainIndex);

    // Read merkle tree size from coinbase (little-endian uint32)
    uint32_t nSize = ReadLE32(reinterpret_cast<const unsigned char*>(&*pc));
    pc += 4;

    // Validate chain merkle tree size: must be a power of 2 >= 1
    if (nSize < 1 || (nSize & (nSize - 1)) != 0)
        return error("AuxPow: merkle tree size is not a power of 2");

    // Cap tree size at 2^30 to prevent log2 iteration from consuming 31 steps
    // and to guard against nSize=0x80000000 silently passing the power-of-2 check.
    if (nSize > (1u << 30))
        return error("AuxPow: merkle tree size too large (max 2^30)");

    // Read the merkle nonce from the coinbase and validate it is consistent with
    // nChainIndex. Standard AuxPoW requires: nChainIndex == nNonce % nSize.
    // For the current single-chain case (nSize=1) this is always 0==0, but
    // multi-chain merged mining requires the constraint to be enforced.
    uint32_t nNonce = ReadLE32(reinterpret_cast<const unsigned char*>(&*pc));
    pc += 4;
    if (nChainIndex != (int)(nNonce % nSize))
        return error("AuxPow: chain index does not match nonce mod size");

    // Chain merkle branch length must be consistent with tree size
    // Compute log2(nSize): nSize is a power of 2, so count trailing zeros
    unsigned int nLog2Size = 0;
    for (uint32_t tmp = nSize; tmp > 1; tmp >>= 1) ++nLog2Size;
    if (vChainMerkleBranch.size() != (size_t)nLog2Size)
        return error("AuxPow: chain merkle branch length does not match tree size");

    // Chain index must be within tree bounds (use unsigned comparison to avoid
    // signed-overflow when nSize == 0x80000000, where (int)nSize == INT_MIN)
    if ((uint32_t)nChainIndex >= nSize)
        return error("AuxPow: chain index out of range");

    // The merkle root in the coinbase must match what we computed
    if (nRootHashCoinbase != expectedRoot)
        return error("AuxPow: chain merkle root does not match coinbase commitment");

    // NOTE: parentBlock.nTime is deliberately NOT validated here.
    // The parent blockchain has its own timestamp rules; ESF only cares
    // that the parent block's hash satisfies the ESF difficulty target
    // (checked by CheckAuxProofOfWork before calling Check).  Rejecting
    // parent blocks based on their timestamp would break compatibility with
    // any parent chain whose block times are far in the past or future.
    return true;
}
