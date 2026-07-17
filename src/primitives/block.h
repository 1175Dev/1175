// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2011-2014 The Litecoin developers
// Copyright (c) 2014-2016 The Dogecoin Core developers
// Copyright (c) 2026 The ElevenSeventyFive Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include <consensus/params.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <uint256.h>
#include <util/time.h>

#include <memory>

// AuxPoW version flag: if set in nVersion, block carries AuxPoW data
static const int BLOCK_VERSION_AUXPOW = (1 << 8);
// Chain ID occupies bits [31:16] of nVersion
static const int BLOCK_VERSION_CHAIN_START = (1 << 16);

/**
 * The pure 80-byte block header. This is exactly what GetHash() hashes and what
 * proof-of-work commits to — it NEVER carries auxpow. The parent block embedded in
 * a CAuxPow is a CPureBlockHeader, which is what stops a header from recursively
 * nesting auxpow. CBlockHeader derives from this and adds the (optional) auxpow that
 * travels alongside the header on the wire/disk WITHOUT changing the block id.
 */
class CPureBlockHeader
{
public:
    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    CPureBlockHeader()
    {
        SetNull();
    }

    SERIALIZE_METHODS(CPureBlockHeader, obj) { READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot, obj.nTime, obj.nBits, obj.nNonce); }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    // Hashes ONLY the 6 pure header fields — the block id is independent of auxpow.
    uint256 GetHash() const;

    NodeSeconds Time() const
    {
        return NodeSeconds{std::chrono::seconds{nTime}};
    }

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }
};

/**
 * AuxPoW data attached to a block mined via merged mining.
 * The parent chain (BCH2) coinbase transaction commits to this block's hash.
 * Validation proves the parent block satisfies PoW and the commitment is correct.
 *
 * NOTE: parentBlock is a CPureBlockHeader (never a CBlockHeader) so an auxpow can
 * never recursively contain another auxpow.
 */
class CAuxPow
{
public:
    // Coinbase transaction from the parent chain block
    CMutableTransaction coinbaseTx;
    // Hash of the parent chain block
    uint256 hashBlock;
    // Merkle branch connecting coinbase tx to parent block's merkle root
    std::vector<uint256> vMerkleBranch;
    int nIndex{0};
    // Merkle branch for the chain in the aux chain merkle tree
    std::vector<uint256> vChainMerkleBranch;
    int nChainIndex{0};
    // Parent chain block header (pure 80 bytes, never itself auxpow)
    CPureBlockHeader parentBlock;

    CAuxPow() = default;

    // Serialization is split into explicit Serialize/Unserialize so that Unserialize
    // can read and validate the compact-size prefix BEFORE allocating the vector,
    // avoiding the 5MB transient heap spike from VectorFormatter's pre-allocation.
    // (SER_READ lambdas must compile for CSizeComputer which has no read() method,
    // so this bound check cannot live inside SERIALIZE_METHODS.)
    //
    // v29 note: ::Serialize(s, coinbaseTx) routes through CMutableTransaction, which
    // reads its witness policy from the stream via GetParams<TransactionSerParams>().
    // A CAuxPow is only ever serialized as part of a CBlock on a params-carrying
    // stream (the disk/net path), so the transaction params are always present there.
    template <typename Stream>
    void Serialize(Stream& s) const
    {
        ::Serialize(s, coinbaseTx);
        ::Serialize(s, hashBlock);
        ::Serialize(s, vMerkleBranch);
        ::Serialize(s, nIndex);
        ::Serialize(s, vChainMerkleBranch);
        ::Serialize(s, nChainIndex);
        ::Serialize(s, parentBlock);
    }

    template <typename Stream>
    void Unserialize(Stream& s)
    {
        ::Unserialize(s, coinbaseTx);
        ::Unserialize(s, hashBlock);
        {
            uint64_t cnt = ReadCompactSize(s);
            if (cnt > 30)
                throw std::ios_base::failure("CAuxPow: vMerkleBranch too long (>30)");
            vMerkleBranch.resize(static_cast<size_t>(cnt));
            for (auto& h : vMerkleBranch) ::Unserialize(s, h);
        }
        ::Unserialize(s, nIndex);
        {
            uint64_t cnt = ReadCompactSize(s);
            if (cnt > 30)
                throw std::ios_base::failure("CAuxPow: vChainMerkleBranch too long (>30)");
            vChainMerkleBranch.resize(static_cast<size_t>(cnt));
            for (auto& h : vChainMerkleBranch) ::Unserialize(s, h);
        }
        ::Unserialize(s, nChainIndex);
        ::Unserialize(s, parentBlock);
    }

    // Validate the AuxPoW against the child block hash and chain ID
    bool Check(const uint256& hashAuxBlock, int nChainId, const Consensus::Params& params) const;

    static uint256 CheckMerkleBranch(uint256 hash, const std::vector<uint256>& vMerkleBranch, int nIndex);
};

/** Consensus bound on the serialized size of an AuxPoW proof (as it appears on the wire /
 *  on disk via TX_WITH_WITNESS). A block whose auxpow exceeds this is invalid. The bound
 *  keeps each merge-mined header small enough that a full `headers` batch
 *  (MAX_HEADERS_RESULTS entries of up to 80 bytes + this) stays under
 *  MAX_PROTOCOL_MESSAGE_LENGTH; the two constants must be kept in step (see
 *  net_processing.h). A realistic proof is ~1-1.7 KB (80-byte parent header + a small
 *  coinbase + shallow, already-≤30-deep merkle branches), so 4 KiB leaves ample headroom
 *  and never rejects a real merged-mining parent. */
static constexpr size_t MAX_AUXPOW_SERIALIZED_SIZE = 4096;

/**
 * Whether a serialization stream carries the transaction parameters
 * (TransactionSerParams) needed to (de)serialize an embedded coinbase transaction.
 *
 * v29 threads witness policy through a ParamsStream; a plain, param-less stream
 * (e.g. the 80-byte HEADERS-message path, or a hex dump of a header) cannot carry a
 * transaction. On such a stream a CBlockHeader serializes its 6 pure fields ONLY and
 * the auxpow is intentionally omitted — byte-identical to a pristine 80-byte header.
 * The auxpow travels only on the params-carrying disk/net CBlock path. Callers that
 * need auxpow to survive a bare-header round trip must serialize on a params stream
 * (that is the integration layer's concern, not this data-structure port's).
 */
template <typename Stream>
concept StreamCarriesTxParams = requires(Stream& s) {
    s.template GetParams<TransactionSerParams>();
};

/**
 * A block header as carried on the wire (headers message) and in blocks: the pure
 * 80-byte header plus, when BLOCK_VERSION_AUXPOW is set in nVersion, the auxpow
 * proof. GetHash() is inherited from CPureBlockHeader, so it hashes only the pure
 * header — the auxpow rides alongside so headers-message peers can verify the PoW
 * of merge-mined blocks, but the block id is unchanged.
 */
class CBlockHeader : public CPureBlockHeader
{
public:
    // AuxPoW data (only present/serialized when nVersion & BLOCK_VERSION_AUXPOW)
    std::shared_ptr<CAuxPow> auxpow;

    CBlockHeader()
    {
        SetNull();
    }

    explicit CBlockHeader(const CPureBlockHeader& header) : CPureBlockHeader(header)
    {
        auxpow.reset();
    }

    void SetNull()
    {
        CPureBlockHeader::SetNull();
        auxpow.reset();
    }

    template <typename Stream>
    void Serialize(Stream& s) const
    {
        CPureBlockHeader::Serialize(s);
        if constexpr (StreamCarriesTxParams<Stream>) {
            if (this->nVersion & BLOCK_VERSION_AUXPOW) {
                if (!auxpow)
                    throw std::ios_base::failure("CBlockHeader: AUXPOW version bit set but auxpow is null");
                ::Serialize(s, *auxpow);
            }
        }
    }

    template <typename Stream>
    void Unserialize(Stream& s)
    {
        CPureBlockHeader::Unserialize(s);
        if constexpr (StreamCarriesTxParams<Stream>) {
            if (this->nVersion & BLOCK_VERSION_AUXPOW) {
                auxpow = std::make_shared<CAuxPow>();
                ::Unserialize(s, *auxpow);
            } else {
                auxpow.reset();
            }
        } else {
            auxpow.reset();
        }
    }
};

class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransactionRef> vtx;

    // Memory-only flags for caching expensive checks
    mutable bool fChecked;                            // CheckBlock()
    mutable bool m_checked_witness_commitment{false}; // CheckWitnessCommitment()
    mutable bool m_checked_merkle_root{false};        // CheckMerkleRoot()

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *(static_cast<CBlockHeader*>(this)) = header;
    }

    SERIALIZE_METHODS(CBlock, obj)
    {
        // CBlockHeader serializes the 6 pure fields plus the conditional auxpow blob
        // (auxpow only when this stream carries transaction params, i.e. disk/net).
        READWRITE(AsBase<CBlockHeader>(obj), obj.vtx);
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        fChecked = false;
        m_checked_witness_commitment = false;
        m_checked_merkle_root = false;
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion       = nVersion;
        block.hashPrevBlock  = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        // Carry the auxpow so a headers announcement can include the PoW proof.
        block.auxpow         = auxpow;
        return block;
    }

    std::string ToString() const;
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    /** Historically CBlockLocator's version field has been written to network
     * streams as the negotiated protocol version and to disk streams as the
     * client version, but the value has never been used.
     *
     * Hard-code to the highest protocol version ever written to a network stream.
     * SerParams can be used if the field requires any meaning in the future,
     **/
    static constexpr int DUMMY_VERSION = 70016;

    std::vector<uint256> vHave;

    CBlockLocator() = default;

    explicit CBlockLocator(std::vector<uint256>&& have) : vHave(std::move(have)) {}

    SERIALIZE_METHODS(CBlockLocator, obj)
    {
        int nVersion = DUMMY_VERSION;
        READWRITE(nVersion);
        READWRITE(obj.vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

#endif // BITCOIN_PRIMITIVES_BLOCK_H
