// Copyright (c) 2026 The ElevenSeventyFive Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <crypto/common.h>
#include <hash.h>
#include <pow.h>
#include <primitives/block.h>
#include <script/script.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <validation.h> // HasValidProofOfWork (header-layer anti-DoS gate)

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(auxpow_tests, BasicTestingSetup)

// Build a coinbase scriptSig containing the merged mining commitment:
//   {0xfa,0xbe,0x6d,0x6d} + childHash(32B LE) + merkleSize(4B LE) + merkleNonce(4B LE)
static CScript MakeAuxCoinbaseScript(const uint256& childHash, uint32_t nSize, uint32_t nNonce)
{
    std::vector<unsigned char> raw;
    // magic header
    raw.push_back(0xfa); raw.push_back(0xbe);
    raw.push_back(0x6d); raw.push_back(0x6d);
    // child block hash (raw LE bytes from uint256)
    raw.insert(raw.end(), childHash.begin(), childHash.end());
    // merkle tree size (4 bytes LE)
    unsigned char buf[4];
    WriteLE32(buf, nSize);
    raw.insert(raw.end(), buf, buf + 4);
    // merkle nonce (4 bytes LE)
    WriteLE32(buf, nNonce);
    raw.insert(raw.end(), buf, buf + 4);
    return CScript(raw.begin(), raw.end());
}

// Construct a minimal valid CAuxPow for a single merged-mined chain (size=1).
// Returns an auxpow where:
//   - coinbase commits to childHash via magic header
//   - parentBlock has hashMerkleRoot = coinbase tx hash, nVersion=1 (no chain ID)
//   - all merkle branches are empty (single tx, single chain)
static CAuxPow MakeValidAuxPow(const uint256& childHash, int nChainId)
{
    CAuxPow aux;

    // Build coinbase tx with merged mining commitment
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].scriptSig = MakeAuxCoinbaseScript(childHash, /*nSize=*/1, /*nNonce=*/0);
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    aux.coinbaseTx = coinbase;

    // Single tx in parent block: merkle root = coinbase hash
    const uint256 coinbaseHash = CTransaction(coinbase).GetHash();
    aux.hashBlock = uint256(); // not validated, set to zero

    // Merkle branch: empty (coinbase is the only tx)
    aux.vMerkleBranch.clear();
    aux.nIndex = 0;

    // Chain merkle branch: empty (size=1, log2=0)
    aux.vChainMerkleBranch.clear();
    aux.nChainIndex = 0;

    // Parent block: version=1 (no chain ID in bits 31-16, avoids same-chain-ID rejection)
    aux.parentBlock.nVersion = 1;
    aux.parentBlock.hashPrevBlock.SetNull();
    aux.parentBlock.hashMerkleRoot = coinbaseHash;
    aux.parentBlock.nTime = 1000000;
    aux.parentBlock.nBits = 0x207fffff; // regtest easy target
    aux.parentBlock.nNonce = 0;

    // hashBlock must match parentBlock.GetHash() (checked by CAuxPow::Check)
    aux.hashBlock = aux.parentBlock.GetHash();

    return aux;
}

// ---- Tests for CAuxPow::Check ----

BOOST_AUTO_TEST_CASE(auxpow_valid)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    const int nChainId = params.nAuxpowChainId; // 1175

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");

    CAuxPow aux = MakeValidAuxPow(childHash, nChainId);
    BOOST_CHECK(aux.Check(childHash, nChainId, params));
}

BOOST_AUTO_TEST_CASE(auxpow_empty_coinbase)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");

    CAuxPow aux = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    aux.coinbaseTx.vin.clear(); // remove all inputs
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

BOOST_AUTO_TEST_CASE(auxpow_same_chain_id_in_parent)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    const int nChainId = params.nAuxpowChainId;

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");

    CAuxPow aux = MakeValidAuxPow(childHash, nChainId);
    // Embed the child chain ID in parent block version bits [31:16]
    aux.parentBlock.nVersion = 1 | (nChainId << 16);
    BOOST_CHECK(!aux.Check(childHash, nChainId, params));
}

BOOST_AUTO_TEST_CASE(auxpow_merkle_branch_too_long)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");

    CAuxPow aux = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    aux.vMerkleBranch.assign(31, uint256()); // 31 > max 30
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

BOOST_AUTO_TEST_CASE(auxpow_chain_merkle_branch_too_long)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");

    CAuxPow aux = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    aux.vChainMerkleBranch.assign(31, uint256()); // 31 > max 30
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

BOOST_AUTO_TEST_CASE(auxpow_nindex_nonzero)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");

    CAuxPow aux = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    aux.nIndex = 1; // coinbase must be at index 0
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

BOOST_AUTO_TEST_CASE(auxpow_negative_chain_index)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");

    CAuxPow aux = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    aux.nChainIndex = -1;
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

BOOST_AUTO_TEST_CASE(auxpow_no_magic_header)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");

    CAuxPow aux = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    // Replace coinbase script with something that has no magic header
    aux.coinbaseTx.vin[0].scriptSig = CScript() << OP_1;
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

BOOST_AUTO_TEST_CASE(auxpow_duplicate_magic_header)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");

    CAuxPow aux = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    // Append a second copy of the commitment to the coinbase script
    const CScript& orig = aux.coinbaseTx.vin[0].scriptSig;
    std::vector<unsigned char> doubled(orig.begin(), orig.end());
    doubled.insert(doubled.end(), orig.begin(), orig.end());
    aux.coinbaseTx.vin[0].scriptSig = CScript(doubled.begin(), doubled.end());
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

BOOST_AUTO_TEST_CASE(auxpow_wrong_child_hash_in_coinbase)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");

    // Build auxpow committing to a DIFFERENT hash
    uint256 wrongHash;
    wrongHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000dead");
    CAuxPow aux = MakeValidAuxPow(wrongHash, params.nAuxpowChainId);

    // But validate against the correct childHash → should fail
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

BOOST_AUTO_TEST_CASE(auxpow_non_power_of_two_size)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");

    CAuxPow aux = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    // Replace merkle size with 3 (not a power of 2) in the coinbase script
    const CScript& orig = aux.coinbaseTx.vin[0].scriptSig;
    std::vector<unsigned char> raw(orig.begin(), orig.end());
    // The size field starts at byte 4 (magic) + 32 (hash) = offset 36
    WriteLE32(raw.data() + 36, 3);
    aux.coinbaseTx.vin[0].scriptSig = CScript(raw.begin(), raw.end());
    // Re-sync the parent commitment to the mutated coinbase, otherwise Check bails at
    // the coinbase-merkle-root comparison before it ever decodes the tree size and
    // reaches the power-of-2 guard this test is meant to exercise.
    aux.parentBlock.hashMerkleRoot = CTransaction(aux.coinbaseTx).GetHash();
    aux.hashBlock = aux.parentBlock.GetHash();
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

BOOST_AUTO_TEST_CASE(auxpow_chain_index_out_of_range)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");

    // Build auxpow with size=2 (need 1-entry chain branch), nChainIndex=2 (out of range)
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].scriptSig = MakeAuxCoinbaseScript(childHash, /*nSize=*/2, /*nNonce=*/0);
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;

    CAuxPow aux;
    aux.coinbaseTx = coinbase;
    aux.vMerkleBranch.clear();
    aux.nIndex = 0;
    aux.vChainMerkleBranch.resize(1, uint256()); // log2(2)=1 entries
    aux.nChainIndex = 2; // out of range [0,2)
    aux.parentBlock.nVersion = 1;
    aux.parentBlock.hashMerkleRoot = CTransaction(coinbase).GetHash();
    aux.parentBlock.nBits = 0x207fffff;
    // Set hashBlock so Check reaches the chain-index checks instead of failing at the
    // hashBlock guard. nChainIndex=2 (with nNonce=0, nSize=2) is rejected by the
    // chain-index/nonce-consistency guard (2 != 0 % 2); the bare out-of-range bound is
    // shadowed by that guard and by the power-of-2/size-cap checks.
    aux.hashBlock = aux.parentBlock.GetHash();

    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

// ---- Tests for CheckAuxProofOfWork ----

BOOST_AUTO_TEST_CASE(check_auxpow_no_auxpow_bit)
{
    // Regular block (no BLOCK_VERSION_AUXPOW): CheckAuxProofOfWork falls back to
    // normal PoW check via CheckProofOfWork(hash, nBits).
    // Use regtest params (powLimit = 7fff...ffff) so solving PoW is trivial.
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();

    CBlock block;
    block.nVersion = 4;  // no BLOCK_VERSION_AUXPOW
    block.nBits = 0x207fffff;  // regtest easy target

    // Solve PoW so the block hash satisfies the target
    arith_uint256 target;
    target.SetCompact(block.nBits);
    block.nNonce = 0;
    while (UintToArith256(block.GetHash()) > target) {
        ++block.nNonce;
    }
    BOOST_CHECK(CheckAuxProofOfWork(block, params));  // solved → must pass

    // Use an impossible nBits so no nNonce can satisfy it
    block.nBits = 0x03000001;  // requires ~192 leading zero bits
    BOOST_CHECK(!CheckAuxProofOfWork(block, params));  // impossible target → must fail
}

BOOST_AUTO_TEST_CASE(check_auxpow_missing_data)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();

    CBlock block;
    block.nVersion = 4 | BLOCK_VERSION_AUXPOW | (params.nAuxpowChainId << 16);
    block.nBits = 0x207fffff;
    block.auxpow = nullptr; // bit set but no data → must fail
    BOOST_CHECK(!CheckAuxProofOfWork(block, params));
}

// ---- AUXPOW-UNIT-001: hashBlock must match parentBlock.GetHash() ----
BOOST_AUTO_TEST_CASE(auxpow_hashblock_mismatch)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");
    CAuxPow aux = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    // Corrupt hashBlock so it no longer matches parentBlock.GetHash()
    *aux.hashBlock.begin() ^= 0x01;
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

// ---- AUXPOW-UNIT-002: coinbase merkle root must match parentBlock.hashMerkleRoot ----
BOOST_AUTO_TEST_CASE(auxpow_coinbase_merkle_mismatch)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");
    CAuxPow aux = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    // Corrupt parent block's merkle root and re-sync hashBlock
    aux.parentBlock.hashMerkleRoot = uint256{"cafecafecafecafecafecafecafecafecafecafecafecafecafecafecafecafe"};
    aux.hashBlock = aux.parentBlock.GetHash();
    // Check must fail: coinbase hash no longer matches parentBlock.hashMerkleRoot
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

// ---- AUXPOW-UNIT-003: coinbase truncated after magic header (< 40 bytes remaining) ----
BOOST_AUTO_TEST_CASE(auxpow_coinbase_truncated_after_magic)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");
    CAuxPow aux = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    // Build a scriptSig with magic header followed by only 20 bytes (need ≥ 40)
    std::vector<unsigned char> raw = {0xfa, 0xbe, 0x6d, 0x6d};
    raw.resize(raw.size() + 20, 0x00);
    aux.coinbaseTx.vin[0].scriptSig = CScript(raw.begin(), raw.end());
    // Re-sync parentBlock.hashMerkleRoot and hashBlock
    const uint256 cbHash = CTransaction(aux.coinbaseTx).GetHash();
    aux.parentBlock.hashMerkleRoot = cbHash;
    aux.hashBlock = aux.parentBlock.GetHash();
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

// ---- AUXPOW-UNIT-004: chain merkle root in coinbase must match computed expectedRoot ----
BOOST_AUTO_TEST_CASE(auxpow_chain_merkle_root_mismatch)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");
    uint256 wrongHash;
    wrongHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000dead");
    // Coinbase commits to wrongHash; Check validates against childHash → mismatch
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].scriptSig = MakeAuxCoinbaseScript(wrongHash, /*nSize=*/1, /*nNonce=*/0);
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    CAuxPow aux;
    aux.coinbaseTx = coinbase;
    aux.vMerkleBranch.clear();
    aux.nIndex = 0;
    aux.vChainMerkleBranch.clear();
    aux.nChainIndex = 0;
    aux.parentBlock.nVersion = 1;
    aux.parentBlock.hashMerkleRoot = CTransaction(coinbase).GetHash();
    aux.parentBlock.nBits = 0x207fffff;
    aux.hashBlock = aux.parentBlock.GetHash();
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));
}

// ---- AUXPOW-UNIT-005: valid AuxPoW with chain tree size=2 (non-trivial chain branch) ----
BOOST_AUTO_TEST_CASE(auxpow_valid_size2)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    const int nChainId = params.nAuxpowChainId;

    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");
    uint256 siblingHash;
    siblingHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000ef01");

    // With nChainIndex=0 and nNonce=0 (0 % 2 == 0 ✓), the chain merkle root is:
    //   CheckMerkleBranch(childHash, {siblingHash}, 0) = Hash(childHash, siblingHash)
    const uint256 chainRoot = CAuxPow::CheckMerkleBranch(childHash, {siblingHash}, 0);

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    // Coinbase encodes the computed chain root (not childHash directly)
    coinbase.vin[0].scriptSig = MakeAuxCoinbaseScript(chainRoot, /*nSize=*/2, /*nNonce=*/0);
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;

    CAuxPow aux;
    aux.coinbaseTx = coinbase;
    aux.vMerkleBranch.clear();
    aux.nIndex = 0;
    aux.vChainMerkleBranch = {siblingHash}; // log2(2) = 1 entry
    aux.nChainIndex = 0;
    aux.parentBlock.nVersion = 1; // no chain ID in bits [31:16]
    aux.parentBlock.hashMerkleRoot = CTransaction(coinbase).GetHash();
    aux.parentBlock.nBits = 0x207fffff;
    aux.hashBlock = aux.parentBlock.GetHash();

    BOOST_CHECK(aux.Check(childHash, nChainId, params));
}

// ---- AUXPOW-UNIT-006: branch at exactly 30 entries (boundary — must not be rejected) ----
BOOST_AUTO_TEST_CASE(auxpow_merkle_branch_at_limit)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    uint256 childHash;
    childHash.SetHexDeprecated("000000000000000000000000000000000000000000000000000000000000abcd");
    CAuxPow aux = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    // Assign exactly 30 entries — must not trigger the "> 30" branch-length guard.
    // Check still returns false (merkle root won't match) but for a different reason.
    aux.vMerkleBranch.assign(30, uint256());
    BOOST_CHECK(!aux.Check(childHash, params.nAuxpowChainId, params));

    // 31 entries should trigger the explicit "branch too long" rejection.
    CAuxPow aux2 = MakeValidAuxPow(childHash, params.nAuxpowChainId);
    aux2.vMerkleBranch.assign(31, uint256());
    BOOST_CHECK(!aux2.Check(childHash, params.nAuxpowChainId, params));
}

// ---- AUXPOW-UNIT-007: LoadBlockIndexGuts activation-height guard ----
// txdb.cpp skips CheckProofOfWork for AuxPoW blocks only when BOTH:
//   (a) BLOCK_VERSION_AUXPOW bit is set in nVersion, AND
//   (b) IsAuxpowActive(nHeight) is true (i.e., nHeight >= nAuxpowStartHeight)
// A pre-activation block with the bit set must NOT be exempt from PoW checking;
// a post-activation AuxPoW block must be exempt.  This test verifies the guard
// logic directly (without exercising the LevelDB path).
BOOST_AUTO_TEST_CASE(txdb_auxpow_activation_guard)
{
    const auto mainParams  = CreateChainParams(*m_node.args, ChainType::MAIN);
    const auto regtestParams = CreateChainParams(*m_node.args, ChainType::REGTEST);

    for (const auto* chainParams : {mainParams.get(), regtestParams.get()}) {
        const Consensus::Params& params = chainParams->GetConsensus();
        const int32_t auxpowVersion = 4 | BLOCK_VERSION_AUXPOW;
        const int start = params.nAuxpowStartHeight;

        // Case 1: bit set, height below activation → isAuxpow=false → PoW must be checked
        if (start > 0) {
            bool isAuxpow = (auxpowVersion & BLOCK_VERSION_AUXPOW) &&
                            params.IsAuxpowActive(start - 1);
            BOOST_CHECK(!isAuxpow);
        }

        // Case 2: bit set, height at activation → isAuxpow=true → PoW check skipped
        if (start < std::numeric_limits<int>::max()) {
            bool isAuxpow = (auxpowVersion & BLOCK_VERSION_AUXPOW) &&
                            params.IsAuxpowActive(start);
            BOOST_CHECK(isAuxpow);
        }

        // Case 3: bit set, height well past activation → isAuxpow=true
        if (start < std::numeric_limits<int>::max() - 100) {
            bool isAuxpow = (auxpowVersion & BLOCK_VERSION_AUXPOW) &&
                            params.IsAuxpowActive(start + 100);
            BOOST_CHECK(isAuxpow);
        }

        // Case 4: bit NOT set, any height → isAuxpow=false → PoW always checked
        {
            const int32_t normalVersion = 4; // no BLOCK_VERSION_AUXPOW
            bool isAuxpow = (normalVersion & BLOCK_VERSION_AUXPOW) &&
                            params.IsAuxpowActive(start + 100);
            BOOST_CHECK(!isAuxpow);
        }
    }
}

// AUXPOW-UNIT-009: CheckMerkleBranch internal guard against negative indices
// other than the -1 sentinel. Any other negative nIndex would right-shift to
// -1 and loop forever; the internal throw must fire before the loop.
BOOST_AUTO_TEST_CASE(auxpow_merkle_branch_negative_index_guard)
{
    const uint256 root = uint256::FromUserHex("0xdeadbeef").value();
    const std::vector<uint256> branch = {uint256::FromUserHex("0xabcd1234").value()};

    // -1 is the accepted sentinel: returns the zero hash, no throw
    BOOST_CHECK(CAuxPow::CheckMerkleBranch(root, branch, -1).IsNull());

    // Any other negative nIndex must throw, not loop
    BOOST_CHECK_THROW(CAuxPow::CheckMerkleBranch(root, branch, -2), std::invalid_argument);
    BOOST_CHECK_THROW(CAuxPow::CheckMerkleBranch(root, branch, INT_MIN), std::invalid_argument);
}

// AUXPOW-UNIT-008: Serializing a CBlock with the AUXPOW version bit set but
// a null auxpow pointer must throw rather than dereference null.
// Trigger: CBlock(header) copies the header's nVersion (which may carry the
// AUXPOW bit) but leaves auxpow == nullptr after SetNull().
BOOST_AUTO_TEST_CASE(auxpow_null_ptr_serialize_guard)
{
    CBlockHeader header;
    header.nVersion = BLOCK_VERSION_AUXPOW | 4; // AUXPOW bit set, no auxpow data
    header.nBits    = 0x207fffff;
    header.nTime    = 1600000000;

    // CBlock(header) copies nVersion from the header; auxpow stays null.
    CBlock block(header);
    BOOST_REQUIRE(!block.auxpow);

    // v29: auxpow only (de)serializes on a params-carrying stream (StreamCarriesTxParams).
    // Wrap the block in TX_WITH_WITNESS so the AUXPOW-bit-set/null-pointer guard fires.
    DataStream ss;
    BOOST_CHECK_THROW(ss << TX_WITH_WITNESS(block), std::ios_base::failure);
}

// AUXPOW-UNIT-010: header-layer PoW gate. HasValidProofOfWork (the HEADERS-message
// anti-DoS check) must verify the auxpow proof for auxpow-flagged headers instead of
// trusting the bit. An auxpow header is accepted ONLY when its parent block actually
// satisfies the aux difficulty target AND the merged-mining commitment is valid; a
// missing or under-worked proof must be rejected (which drives Misbehaving(peer,100)).
BOOST_AUTO_TEST_CASE(auxpow_header_layer_pow_gate)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    const int nChainId = params.nAuxpowChainId;

    // An auxpow-flagged child header. Its own hash need not meet nBits — the PoW is
    // carried by the parent block referenced in the auxpow.
    CBlockHeader hdr;
    hdr.nVersion       = 4 | BLOCK_VERSION_AUXPOW;
    hdr.hashPrevBlock.SetNull();
    hdr.hashMerkleRoot.SetNull();
    hdr.nTime          = 1600000000;
    hdr.nBits          = 0x207fffff; // regtest powLimit; the parent must beat this
    hdr.nNonce         = 0;
    const uint256 childHash = hdr.GetHash();

    // (1) AUXPOW bit set but NO proof attached -> rejected at the header layer.
    BOOST_REQUIRE(!hdr.auxpow);
    BOOST_CHECK(!CheckAuxProofOfWork(hdr, params));
    BOOST_CHECK(!HasValidProofOfWork({hdr}, params));

    // (2) Valid proof whose parent block genuinely satisfies the aux target -> accepted.
    auto aux = std::make_shared<CAuxPow>(MakeValidAuxPow(childHash, nChainId));
    int guard = 0;
    while (!CheckProofOfWork(aux->parentBlock.GetHash(), hdr.nBits, params)) {
        aux->parentBlock.nNonce++;
        BOOST_REQUIRE(++guard < 1000000);
    }
    aux->hashBlock = aux->parentBlock.GetHash();
    hdr.auxpow = aux;
    BOOST_CHECK(CheckAuxProofOfWork(hdr, params));
    BOOST_CHECK(HasValidProofOfWork({hdr}, params));

    // (3) Well-formed commitment but the parent does NOT satisfy PoW -> rejected.
    auto aux_bad = std::make_shared<CAuxPow>(*aux);
    guard = 0;
    while (CheckProofOfWork(aux_bad->parentBlock.GetHash(), hdr.nBits, params)) {
        aux_bad->parentBlock.nNonce++;
        BOOST_REQUIRE(++guard < 1000000);
    }
    aux_bad->hashBlock = aux_bad->parentBlock.GetHash();
    CBlockHeader hdr_bad = hdr;
    hdr_bad.auxpow = aux_bad;
    BOOST_CHECK(!CheckAuxProofOfWork(hdr_bad, params));
    BOOST_CHECK(!HasValidProofOfWork({hdr_bad}, params));

    // (4) A batch containing one good and one bad auxpow header fails as a whole
    // (all_of semantics) -> the peer feeding it is banned.
    BOOST_CHECK(!HasValidProofOfWork({hdr, hdr_bad}, params));
}

// An AuxPoW proof larger than MAX_AUXPOW_SERIALIZED_SIZE is rejected even when its
// commitment and parent PoW are otherwise valid. This bounds the `headers`-message size.
BOOST_AUTO_TEST_CASE(check_auxpow_oversized)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const Consensus::Params& params = chainParams->GetConsensus();
    const int nChainId = params.nAuxpowChainId;

    CBlockHeader hdr;
    hdr.nVersion = 4 | BLOCK_VERSION_AUXPOW;
    hdr.nBits    = 0x207fffff; // regtest powLimit; the parent must beat this
    const uint256 childHash = hdr.GetHash();

    // A normal-sized valid proof is within the bound and passes.
    auto aux = std::make_shared<CAuxPow>(MakeValidAuxPow(childHash, nChainId));
    int guard = 0;
    while (!CheckProofOfWork(aux->parentBlock.GetHash(), hdr.nBits, params)) {
        aux->parentBlock.nNonce++;
        BOOST_REQUIRE(++guard < 1000000);
    }
    aux->hashBlock = aux->parentBlock.GetHash();
    hdr.auxpow = aux;
    BOOST_REQUIRE(::GetSerializeSize(TX_WITH_WITNESS(*hdr.auxpow)) <= MAX_AUXPOW_SERIALIZED_SIZE);
    BOOST_CHECK(CheckAuxProofOfWork(hdr, params));

    // Inflate the coinbase past the cap (a large OP_RETURN output), re-derive the parent's
    // single-tx merkle root, and re-solve the parent PoW so the ONLY invalid property is
    // the serialized size. It must now be rejected.
    auto big = std::make_shared<CAuxPow>(MakeValidAuxPow(childHash, nChainId));
    std::vector<unsigned char> pad(MAX_AUXPOW_SERIALIZED_SIZE, 0x00);
    big->coinbaseTx.vout.push_back(CTxOut(0, CScript() << OP_RETURN << pad));
    big->parentBlock.hashMerkleRoot = CTransaction(big->coinbaseTx).GetHash();
    guard = 0;
    while (!CheckProofOfWork(big->parentBlock.GetHash(), hdr.nBits, params)) {
        big->parentBlock.nNonce++;
        BOOST_REQUIRE(++guard < 1000000);
    }
    big->hashBlock = big->parentBlock.GetHash();
    CBlockHeader hdr_big = hdr;
    hdr_big.auxpow = big;
    BOOST_REQUIRE(::GetSerializeSize(TX_WITH_WITNESS(*hdr_big.auxpow)) > MAX_AUXPOW_SERIALIZED_SIZE);
    BOOST_CHECK(!CheckAuxProofOfWork(hdr_big, params));
}

BOOST_AUTO_TEST_SUITE_END()
