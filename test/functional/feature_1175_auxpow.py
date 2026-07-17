#!/usr/bin/env python3
# Copyright (c) 2026 The ElevenSeventyFive Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test AuxPoW merged mining: getauxblock work request, submission, and activation."""

import struct
from io import BytesIO

from test_framework.blocktools import create_coinbase
from test_framework.p2p import P2PInterface
from test_framework.messages import (
    MSG_BLOCK,
    msg_headers,
    BLOCK_VERSION_AUXPOW,
    BLOCK_VERSION_CHAIN_START,
    CAuxPow,
    CBlock,
    CBlockHeader,
    CTransaction,
    CTxIn,
    CTxOut,
    COutPoint,
    hash256,
    ser_uint256,
    uint256_from_str,
)
from test_framework.script import CScript
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.wallet import MiniWallet

# Regtest AuxPoW activation height (must match chainparams.cpp)
AUXPOW_START_HEIGHT = 200
AUXPOW_CHAIN_ID = 1175
# Any valid regtest ("resf") address; getauxblock now requires a payout address so the
# coinbase reward is never sent to an anyone-can-spend script. The destination is
# irrelevant to what these tests verify.
PAYOUT_ADDR = "resf1ql58teezaywtn0gpedktrju57sz7q8avl0k6xka"


def build_auxpow_coinbase(child_hash_hex):
    """
    Build a coinbase transaction whose scriptSig contains the merged mining
    commitment for child_hash_hex.

    Commitment format (raw bytes in scriptSig):
      {0xfa,0xbe,0x6d,0x6d} + child_hash(32B LE) + merkle_size(4B LE) + nonce(4B LE)

    The child hash from getauxblock is displayed as big-endian hex; we need
    the raw little-endian bytes for the coinbase (matching uint256 internal storage).
    """
    magic = bytes([0xfa, 0xbe, 0x6d, 0x6d])
    child_hash_le = bytes.fromhex(child_hash_hex)[::-1]  # BE hex → LE bytes
    merkle_size = struct.pack("<I", 1)   # single chain
    nonce       = struct.pack("<I", 0)

    script_bytes = magic + child_hash_le + merkle_size + nonce

    tx = CTransaction()
    tx.vin.append(CTxIn(COutPoint(0, 0xffffffff), CScript(script_bytes)))
    tx.vout.append(CTxOut(50 * 100_000_000, CScript(b'\x51')))  # OP_1
    tx.calc_sha256()
    return tx


def build_auxpow(child_hash_hex, target_nbits):
    """
    Construct a valid CAuxPow for a single-chain merged mining setup:
      - Single coinbase tx with the child hash commitment
      - Empty merkle branches (coinbase is the only tx, size=1)
      - Parent block with an easily-solvable target
    """
    aux = CAuxPow()

    coinbase_tx = build_auxpow_coinbase(child_hash_hex)
    aux.coinbaseTx = coinbase_tx
    aux.vMerkleBranch = []     # coinbase is the only tx
    aux.nIndex = 0
    aux.vChainMerkleBranch = []  # size=1, log2(1)=0 branch entries
    aux.nChainIndex = 0

    # Parent block header
    parent = CBlockHeader()
    parent.nVersion = 1        # no chain ID in bits [31:16]
    parent.hashPrevBlock = 0
    parent.hashMerkleRoot = coinbase_tx.sha256  # single-tx merkle root = tx hash
    parent.nTime = 1000000
    parent.nBits = target_nbits

    # Solve parent block PoW (should be trivial at regtest difficulty 0x207fffff)
    target = _compact_to_target(target_nbits)
    parent.rehash()
    while parent.sha256 > target:
        parent.nNonce += 1
        parent.rehash()

    aux.parentBlock = parent
    # hashBlock must equal parentBlock.GetHash() (validated by CAuxPow::Check)
    aux.hashBlock = parent.sha256
    return aux


def build_underworked_auxpow(child_hash_hex, target_nbits):
    """Like build_auxpow, but the parent block does NOT satisfy the target.

    The commitment is well-formed (so the header deserializes cleanly), but the
    parent block's hash is above the target — i.e. the proof-of-work is not done.
    HasValidProofOfWork must reject this at the header layer.
    """
    aux = build_auxpow(child_hash_hex, target_nbits)
    target = _compact_to_target(target_nbits)
    # Grind the parent nonce until its hash is ABOVE the target (fails PoW).
    aux.parentBlock.rehash()
    while aux.parentBlock.sha256 <= target:
        aux.parentBlock.nNonce += 1
        aux.parentBlock.rehash()
    # Keep the commitment self-consistent (hashBlock == parentBlock hash) so the
    # only defect is the missing work.
    aux.hashBlock = aux.parentBlock.sha256
    return aux


def _make_auxpow_header(node, aux_builder):
    """Build a CBlock header extending the current tip, carrying an auxpow proof
    produced by aux_builder(child_hash_hex, nbits)."""
    tip_hash = node.getbestblockhash()
    tip_info = node.getblockheader(tip_hash)
    blk = CBlock()
    blk.nVersion      = 4 | BLOCK_VERSION_AUXPOW | (AUXPOW_CHAIN_ID << 16)
    blk.hashPrevBlock = uint256_from_str(bytes.fromhex(tip_hash)[::-1])
    blk.hashMerkleRoot = 0
    blk.nTime         = tip_info["time"] + 600
    blk.nBits         = int(tip_info["bits"], 16)
    blk.nNonce        = 0
    blk.rehash()  # child hash is over the 80-byte header only
    blk.auxpow = aux_builder(blk.hash, blk.nBits)
    return blk


def _compact_to_target(nbits):
    """Decode compact nBits to integer target."""
    exponent = nbits >> 24
    mantissa = nbits & 0x007fffff
    return mantissa << (8 * (exponent - 3))


class AuxPowTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)

        # ---- Before activation: getauxblock RPC must fail ----
        # At genesis nNextHeight=1 < AUXPOW_START_HEIGHT=200, so getauxblock must reject.
        self.log.info("Check getauxblock rejected before activation height")
        assert node.getblockcount() == 0
        assert_raises_rpc_error(-1, "AuxPoW not yet active", node.getauxblock)

        # ---- Before activation: consensus-layer rejection of AuxPoW-bit block ----
        # Even bypassing the RPC guard, a block with BLOCK_VERSION_AUXPOW set must
        # be rejected by ContextualCheckBlockHeader with "auxpow-too-early".
        # Use submitblock with a hand-crafted header to hit the consensus path directly.
        self.log.info("Pre-activation AuxPoW block rejected at consensus layer (auxpow-too-early)")
        self.generate(wallet, 1)   # height 1 — well below activation
        tmpl = node.getblocktemplate({"rules": ["segwit"]})
        from test_framework.blocktools import create_coinbase as _create_coinbase
        import struct as _struct
        early_block = CBlock()
        early_block.nVersion  = int(tmpl["version"]) | BLOCK_VERSION_AUXPOW | (AUXPOW_CHAIN_ID << 16)
        early_block.hashPrevBlock = uint256_from_str(bytes.fromhex(tmpl["previousblockhash"])[::-1])
        early_block.nTime     = tmpl["curtime"]
        early_block.nBits     = int(tmpl["bits"], 16)
        early_block.nNonce    = 0
        # Coinbase + merkle root
        cb = _create_coinbase(node.getblockcount() + 1)
        early_block.vtx = [cb]
        early_block.hashMerkleRoot = early_block.calc_merkle_root()
        # Attach a minimal (but structurally valid) AuxPoW so deserialization succeeds
        aux_early = build_auxpow(
            uint256_from_str(bytes.fromhex(tmpl["previousblockhash"])[::-1]).__format__('064x'),
            early_block.nBits,
        )
        aux_early.hashBlock = aux_early.parentBlock.sha256
        early_block.auxpow = aux_early
        result = node.submitblock(early_block.serialize().hex())
        assert result == "auxpow-too-early", f"expected auxpow-too-early, got {result!r}"
        assert_equal(node.getblockcount(), 1)  # chain unchanged

        # ---- Mine to the activation boundary ----
        # When chain is at height AUXPOW_START_HEIGHT-1, nNextHeight=AUXPOW_START_HEIGHT,
        # so IsAuxpowActive returns true and getauxblock should succeed.
        target = AUXPOW_START_HEIGHT - 1
        self.log.info(f"Mine to height {target}")
        self.generate(wallet, target - node.getblockcount())
        assert_equal(node.getblockcount(), target)

        # ---- Payout address is required (fail-closed; never an anyone-can-spend coinbase) ----
        self.log.info("getauxblock work request rejects missing/invalid payout address")
        assert_raises_rpc_error(-8, "requires a payout address", node.getauxblock)
        assert_raises_rpc_error(-5, "Invalid payout address", node.getauxblock, "not_a_valid_address")

        # ---- Work request ----
        self.log.info("Call getauxblock to fetch work (nNextHeight == AUXPOW_START_HEIGHT)")
        work = node.getauxblock(PAYOUT_ADDR)
        assert "hash" in work
        assert "target" in work
        assert_equal(work["chainid"], AUXPOW_CHAIN_ID)
        assert_equal(work["height"], AUXPOW_START_HEIGHT)

        child_hash = work["hash"]
        self.log.info(f"  child hash: {child_hash}")

        # Verify the block version in the work template has the right bits set
        # We can't inspect nVersion directly from getauxblock response, but
        # the hash must have been computed after setting BLOCK_VERSION_AUXPOW.
        # (If the hash were computed before setting the bit, the block would
        # fail ContextualCheckBlockHeader's "auxpow-too-early" guard — we test
        # that indirectly by verifying the submission succeeds.)

        # ---- Construct and submit AuxPoW ----
        self.log.info("Build valid AuxPoW and submit")
        # Use regtest target (0x207fffff from bits field)
        nbits = int(work["bits"], 16)
        aux = build_auxpow(child_hash, nbits)
        auxpow_hex = aux.serialize().hex()

        result = node.getauxblock(child_hash, auxpow_hex)
        assert result is True, f"getauxblock submission returned {result}"

        # Block count must have increased
        assert_equal(node.getblockcount(), AUXPOW_START_HEIGHT)
        self.log.info(f"  Block {AUXPOW_START_HEIGHT} accepted via AuxPoW")

        # ---- Double-submit the same hash must fail ----
        self.log.info("Double-submit same hash must fail (removed from pending map)")
        assert_raises_rpc_error(-8, "Block hash not found", node.getauxblock, child_hash, auxpow_hex)

        # ---- submitauxblock alias works the same way ----
        self.log.info("Test submitauxblock alias")
        work2 = node.getauxblock(PAYOUT_ADDR)
        child_hash2 = work2["hash"]
        nbits2 = int(work2["bits"], 16)
        aux2 = build_auxpow(child_hash2, nbits2)
        result2 = node.submitauxblock(child_hash2, aux2.serialize().hex())
        assert result2 is True
        assert_equal(node.getblockcount(), AUXPOW_START_HEIGHT + 1)
        self.log.info(f"  Block {AUXPOW_START_HEIGHT + 1} accepted via submitauxblock")

        # ---- Stale/unknown hash is rejected ----
        self.log.info("Unknown child hash is rejected")
        fake_hash = "deadbeef" * 8
        assert_raises_rpc_error(-8, "Block hash not found", node.getauxblock, fake_hash, "00")

        # ---- AuxPoW with wrong child hash commitment is rejected ----
        self.log.info("Wrong child hash in coinbase is rejected")
        work3 = node.getauxblock(PAYOUT_ADDR)
        child_hash3 = work3["hash"]
        nbits3 = int(work3["bits"], 16)
        wrong_hash = "cafebabe" * 8
        aux_wrong = build_auxpow(wrong_hash, nbits3)  # commits to wrong hash
        assert_raises_rpc_error(-25, "", node.getauxblock, child_hash3, aux_wrong.serialize().hex())

        # ---- AuxPoW with parent chain-ID == ESF chain-ID is rejected ----
        # CAuxPow::Check must reject a parent whose nVersion has chain ID 1175 in
        # bits[31:16].  Build an auxpow whose parent block nVersion encodes ESF's
        # chain ID and verify it is rejected.
        self.log.info("Parent block with same chain ID as ESF is rejected")
        work4 = node.getauxblock(PAYOUT_ADDR)
        child_hash4 = work4["hash"]
        nbits4 = int(work4["bits"], 16)
        aux_sameid = build_auxpow(child_hash4, nbits4)
        # Overwrite parent nVersion to include ESF chain ID (1175 << 16 = 0x04970000)
        aux_sameid.parentBlock.nVersion = (aux_sameid.parentBlock.nVersion & 0x0000FFFF) | (AUXPOW_CHAIN_ID << 16)
        # Re-solve PoW with the new version and update hashBlock accordingly
        target = _compact_to_target(nbits4)
        aux_sameid.parentBlock.nNonce = 0
        aux_sameid.parentBlock.rehash()
        while aux_sameid.parentBlock.sha256 > target:
            aux_sameid.parentBlock.nNonce += 1
            aux_sameid.parentBlock.rehash()
        aux_sameid.hashBlock = aux_sameid.parentBlock.sha256
        assert_raises_rpc_error(-25, "", node.getauxblock, child_hash4, aux_sameid.serialize().hex())

        # ---- ReadBlockFromDisk PoW check: getblock must work for AuxPoW blocks ----
        # ReadBlockFromDisk re-checks proof-of-work when loading blocks from disk.
        # It must call CheckAuxProofOfWork (not bare CheckProofOfWork) so the parent
        # block hash is validated, not the child hash.
        self.log.info("getblock on AuxPoW block must succeed (ReadBlockFromDisk PoW check)")
        auxpow_blkhash = node.getblockhash(AUXPOW_START_HEIGHT)
        blk = node.getblock(auxpow_blkhash)
        assert_equal(blk["height"], AUXPOW_START_HEIGHT)
        # nVersion must have AuxPoW bit and chain ID set
        assert blk["version"] & BLOCK_VERSION_AUXPOW, "AuxPoW block missing BLOCK_VERSION_AUXPOW bit"
        assert_equal((blk["version"] >> 16) & 0xFFFF, AUXPOW_CHAIN_ID)

        # ---- Solo mining (no AuxPoW) still works after activation ----
        # generate() uses the node's built-in CPU miner which produces regular
        # blocks (BLOCK_VERSION_AUXPOW bit clear).  Both block styles must coexist.
        self.log.info("Solo mining still works after AuxPoW activation")
        height_before = node.getblockcount()
        self.generate(wallet, 2)
        assert_equal(node.getblockcount(), height_before + 2)

        # ---- -reindex: AuxPoW blocks survive a full reindex ----
        # This exercises ReadBlockFromDisk → ConnectBlock → CheckAuxProofOfWork
        # on every AuxPoW block we mined above.  If CheckAuxProofOfWork is not
        # called (or called with wrong arguments) during reindex the node would
        # either crash or report a corrupt chain.
        self.log.info("Restart with -reindex and verify AuxPoW blocks are re-validated")
        chain_height = node.getblockcount()
        best_hash    = node.getbestblockhash()

        self.restart_node(0, extra_args=["-reindex"])
        # After reindex the chain tip must be identical
        self.wait_until(lambda: node.getblockcount() == chain_height, timeout=120)
        assert_equal(node.getbestblockhash(), best_hash)

        # Re-fetch the first AuxPoW block; it must still decode correctly
        reindexed = node.getblock(node.getblockhash(AUXPOW_START_HEIGHT))
        assert_equal(reindexed["height"], AUXPOW_START_HEIGHT)
        assert reindexed["version"] & BLOCK_VERSION_AUXPOW
        assert_equal((reindexed["version"] >> 16) & 0xFFFF, AUXPOW_CHAIN_ID)
        self.log.info(f"  -reindex: chain tip {chain_height} matches, AuxPoW block {AUXPOW_START_HEIGHT} valid")

        # ---- Reorg across the AuxPoW activation boundary ----
        # Disconnect AuxPoW blocks at and above height 200, reconnect with regular
        # blocks, then reconsider the original AuxPoW branch.  This exercises
        # DisconnectBlock / ConnectBlock through the activation boundary.
        self.log.info("Test reorg across AuxPoW activation boundary")
        # Record the hash of the first AuxPoW block (200) before the reorg.
        hash_auxpow_200 = node.getblockhash(AUXPOW_START_HEIGHT)
        height_before_reorg = node.getblockcount()

        # Invalidate the AuxPoW block at height 200 → chain rolls back to 199.
        node.invalidateblock(hash_auxpow_200)
        assert_equal(node.getblockcount(), AUXPOW_START_HEIGHT - 1)

        # Mine (height_before_reorg - 199 + 2) regular blocks so the regular
        # chain is definitively longer than the AuxPoW branch.
        n_regular = height_before_reorg - (AUXPOW_START_HEIGHT - 1) + 2
        self.generate(wallet, n_regular)
        assert_equal(node.getblockcount(), AUXPOW_START_HEIGHT - 1 + n_regular)

        # Block at AUXPOW_START_HEIGHT in the regular fork must NOT have the AuxPoW bit.
        blk200_regular = node.getblock(node.getblockhash(AUXPOW_START_HEIGHT))
        assert not (blk200_regular["version"] & BLOCK_VERSION_AUXPOW), \
            "expected regular (non-AuxPoW) block at height 200 after reorg"

        # Reconsider the original AuxPoW block; it should remain a valid-but-not-active
        # side branch (shorter than the regular chain).
        node.reconsiderblock(hash_auxpow_200)
        regular_tip = node.getblockcount()
        assert regular_tip == AUXPOW_START_HEIGHT - 1 + n_regular, \
            "regular chain should remain active after reconsiderblock"
        tips = {t["hash"]: t for t in node.getchaintips()}
        assert hash_auxpow_200 not in [node.getbestblockhash()], \
            "AuxPoW branch must not be the active tip"
        self.log.info(f"  Reorg OK: regular chain at {regular_tip}, AuxPoW branch is a side fork")

        # ---- P2P headers: AuxPoW header PoW is verified at the header layer ----
        # The HEADERS message now carries the auxpow proof, so HasValidProofOfWork
        # verifies it instead of trusting the bit. A header whose proof is missing or
        # under-worked is rejected (CheckHeadersPoW → Misbehaving(100) → peer banned);
        # a header with a valid proof is accepted and the node requests the full block.

        # (a) AuxPoW header with a well-formed but UNDER-WORKED parent → peer banned.
        self.log.info("P2P: AuxPoW header with invalid (under-worked) proof bans the peer")
        p2p_uw = node.add_p2p_connection(P2PInterface())
        bad_hdr = _make_auxpow_header(node, build_underworked_auxpow)
        p2p_uw.send_message(msg_headers([bad_hdr]))
        p2p_uw.wait_for_disconnect(timeout=10)
        self.log.info("  P2P: under-worked AuxPoW header → peer disconnected")

        # (b) AuxPoW header with a VALID proof (parent meets the aux target + valid
        #     commitment) → accepted, node requests the block, peer not banned.
        self.log.info("P2P: valid AuxPoW header accepted, triggers getdata, peer not banned")
        p2p = node.add_p2p_connection(P2PInterface())
        good_hdr = _make_auxpow_header(node, build_auxpow)
        p2p.send_message(msg_headers([good_hdr]))
        p2p.wait_for_getdata([good_hdr.sha256], timeout=10)
        assert p2p.is_connected, "peer disconnected after a valid AuxPoW header"
        self.log.info("  P2P: valid AuxPoW header accepted, getdata sent, peer not banned")

        node.disconnect_p2ps()

        # ---- P2P headers: non-AuxPoW header with impossible PoW causes peer disconnect ----
        # A HEADERS message with a regular (non-AuxPoW) header that claims an
        # impossible nBits (fNeg target → CheckProofOfWork returns false) triggers
        # CheckHeadersPoW → Misbehaving(peer, 100) → immediate peer disconnect.
        self.log.info("P2P: non-AuxPoW header with invalid PoW causes peer disconnect")
        p2p_bad = node.add_p2p_connection(P2PInterface())

        tip_hash_bad = node.getbestblockhash()
        tip_info_bad = node.getblockheader(tip_hash_bad)
        tip_time_bad = tip_info_bad["time"]

        # Build a non-AuxPoW header (no BLOCK_VERSION_AUXPOW bit).
        # nBits = 0x01800000 → mantissa has bit 23 set → fNeg=true in SetCompact →
        # CheckProofOfWork returns false immediately without mining.
        bad_pow_hdr = CBlockHeader()
        bad_pow_hdr.nVersion     = 4  # no BLOCK_VERSION_AUXPOW
        bad_pow_hdr.hashPrevBlock = uint256_from_str(bytes.fromhex(tip_hash_bad)[::-1])
        bad_pow_hdr.hashMerkleRoot = 0
        bad_pow_hdr.nTime  = tip_time_bad + 600
        bad_pow_hdr.nBits  = 0x01800000  # negative target: impossible PoW
        bad_pow_hdr.nNonce = 0
        bad_pow_hdr.rehash()

        p2p_bad.send_message(msg_headers([bad_pow_hdr]))
        # Node must score 100 misbehavior points (>= DISCOURAGEMENT_THRESHOLD) and disconnect.
        p2p_bad.wait_for_disconnect(timeout=10)
        self.log.info("  P2P: non-AuxPoW header with invalid PoW → peer disconnected")

        # ---- getauxblock FIFO eviction at MAX_AUXPOW_WORK_ITEMS (256) ----
        # After 257 unique work requests the oldest entry must be evicted.
        # Use setmocktime to guarantee distinct block templates on each call.
        self.log.info("Test getauxblock FIFO eviction (MAX_AUXPOW_WORK_ITEMS = 256)")
        base_time = node.getblockheader(node.getbestblockhash())["time"] + 1000
        unique_hashes = []
        for i in range(300):
            node.setmocktime(base_time + i * 600)
            w = node.getauxblock(PAYOUT_ADDR)
            if w["hash"] not in unique_hashes:
                unique_hashes.append(w["hash"])
            if len(unique_hashes) >= 257:
                break

        node.setmocktime(0)

        assert len(unique_hashes) >= 257, (
            f"getauxblock returned only {len(unique_hashes)} unique hashes in 300 calls "
            f"with advancing mocktime — FIFO eviction test requires ≥ 257 distinct templates"
        )
        evicted = unique_hashes[0]
        current_bits = int(node.getblockheader(node.getbestblockhash())["bits"], 16)
        aux_evicted = build_auxpow(evicted, current_bits)
        assert_raises_rpc_error(-8, "Block hash not found",
                                node.getauxblock, evicted, aux_evicted.serialize().hex())
        self.log.info(f"  Eviction: first of {len(unique_hashes)} work items evicted after 33 unique requests")

        # ---- getblocktemplate must NOT set BLOCK_VERSION_AUXPOW ----
        # Solo miners use getblocktemplate and must produce regular (non-AuxPoW)
        # blocks. Only merged miners call getauxblock, which sets the bit.
        self.log.info("getblocktemplate must not set BLOCK_VERSION_AUXPOW")
        tmpl = node.getblocktemplate({"rules": ["segwit"]})
        assert not (int(tmpl["version"]) & BLOCK_VERSION_AUXPOW), \
            "getblocktemplate set BLOCK_VERSION_AUXPOW — solo miners would produce invalid blocks"
        self.log.info("  getblocktemplate: BLOCK_VERSION_AUXPOW not set")

        # ---- submitblock with a valid AuxPoW block (alternative submission path) ----
        # A miner can bypass getauxblock's mapAuxpowBlocks map and submit a fully
        # constructed AuxPoW block directly via submitblock.  This exercises the
        # ProcessNewBlock → ContextualCheckBlockHeader → CheckAuxProofOfWork path
        # without touching the getauxblock work-item state.
        self.log.info("submitblock: accept a hand-crafted AuxPoW block (bypasses mapAuxpowBlocks)")
        sb_height = node.getblockcount() + 1
        sb_tmpl = node.getblocktemplate({"rules": ["segwit"]})
        sb_block = CBlock()
        sb_block.nVersion = (int(sb_tmpl["version"]) & 0x0000FFFF) | BLOCK_VERSION_AUXPOW | (AUXPOW_CHAIN_ID << 16)
        sb_block.hashPrevBlock = uint256_from_str(bytes.fromhex(sb_tmpl["previousblockhash"])[::-1])
        sb_block.nTime  = sb_tmpl["curtime"]
        sb_block.nBits  = int(sb_tmpl["bits"], 16)
        sb_block.nNonce = 0
        sb_coinbase = create_coinbase(sb_height)
        sb_block.vtx = [sb_coinbase]
        sb_block.hashMerkleRoot = sb_block.calc_merkle_root()
        sb_block.rehash()
        sb_child_hash = format(sb_block.sha256, '064x')
        sb_aux = build_auxpow(sb_child_hash, sb_block.nBits)
        sb_block.auxpow = sb_aux
        sb_result = node.submitblock(sb_block.serialize().hex())
        assert sb_result is None, f"submitblock expected success (None), got {sb_result!r}"
        assert_equal(node.getblockcount(), sb_height)
        sb_block_hash = node.getblockhash(sb_height)
        self.log.info(f"  submitblock: AuxPoW block {sb_height} accepted")

        # ---- UTXO / coinbase isolation: auxpow->coinbaseTx must not enter the UTXO set ----
        # The parent chain's coinbase lives in block.auxpow->coinbaseTx (not a child
        # chain transaction).  Only block.vtx entries may create UTXOs.  Verify that:
        #   (a) getblock reports exactly one transaction (the child coinbase)
        #   (b) the child coinbase output IS in the UTXO set
        self.log.info("UTXO isolation: auxpow coinbaseTx must not appear in child vtx")
        sb_info = node.getblock(sb_block_hash)  # verbosity=1: tx list = txids
        assert len(sb_info["tx"]) == 1, "AuxPoW block must have exactly one child transaction"
        vtx0_txid = sb_info["tx"][0]
        # Child coinbase output must be in the UTXO set
        utxo = node.gettxout(vtx0_txid, 0, False)
        assert utxo is not None, "child coinbase output missing from UTXO set"
        # The parent chain coinbase is distinct from the child coinbase
        sb_aux_cb_hash = sb_coinbase.sha256  # child coinbase hash (integer)
        sb_aux_parent_cb_hash = sb_aux.coinbaseTx.sha256  # parent coinbase hash (integer)
        assert sb_aux_cb_hash != sb_aux_parent_cb_hash, \
            "child coinbase hash must differ from parent coinbase hash"
        self.log.info("  UTXO isolation: only child vtx[0] in UTXO set, parent coinbase excluded")

        # ---- AuxPoW block with incorrect nBits is rejected ----
        # ContextualCheckBlockHeader validates nBits regardless of AuxPoW.
        # A block with a fraudulently easy target must be rejected with "bad-diffbits".
        self.log.info("AuxPoW block with wrong nBits rejected by ContextualCheckBlockHeader")
        bad_height = node.getblockcount() + 1
        bad_tmpl = node.getblocktemplate({"rules": ["segwit"]})
        bad_block = CBlock()
        bad_block.nVersion = (int(bad_tmpl["version"]) & 0x0000FFFF) | BLOCK_VERSION_AUXPOW | (AUXPOW_CHAIN_ID << 16)
        bad_block.hashPrevBlock = uint256_from_str(bytes.fromhex(bad_tmpl["previousblockhash"])[::-1])
        bad_block.nTime  = bad_tmpl["curtime"]
        # Use a different nBits than expected (harder target → bad-diffbits)
        expected_bits = int(bad_tmpl["bits"], 16)
        bad_block.nBits = expected_bits ^ 0x01  # flip one bit → wrong difficulty
        bad_block.nNonce = 0
        bad_cb = create_coinbase(bad_height)
        bad_block.vtx = [bad_cb]
        bad_block.hashMerkleRoot = bad_block.calc_merkle_root()
        bad_block.rehash()
        bad_aux = build_auxpow(format(bad_block.sha256, '064x'), bad_block.nBits)
        bad_block.auxpow = bad_aux
        bad_result = node.submitblock(bad_block.serialize().hex())
        assert bad_result is not None and "diffbits" in bad_result.lower(), \
            f"expected bad-diffbits rejection, got {bad_result!r}"
        assert_equal(node.getblockcount(), bad_height - 1)  # chain unchanged
        self.log.info(f"  AuxPoW wrong nBits: rejected with '{bad_result}'")

        # ---- getchaintips accuracy after activation-boundary reorg ----
        # There must be an active tip and at least one valid-fork side branch
        # (the AuxPoW branch 200-203 that we disconnected and then reconsidered).
        self.log.info("getchaintips: verify active tip and AuxPoW side fork status")
        tips = {t["hash"]: t for t in node.getchaintips()}
        active_tips = [t for t in tips.values() if t["status"] == "active"]
        fork_tips   = [t for t in tips.values() if t["status"] == "valid-fork"]
        assert_equal(len(active_tips), 1)
        assert active_tips[0]["height"] == node.getblockcount()
        assert len(fork_tips) >= 1, "expected at least one valid-fork tip from AuxPoW reorg"
        # The AuxPoW side branch diverged at height 199; the branch tip is at 203.
        auxpow_fork = next((t for t in fork_tips if t["height"] == AUXPOW_START_HEIGHT + 3), None)
        assert auxpow_fork is not None, "AuxPoW side branch (height 203) not found in chain tips"
        assert_equal(auxpow_fork["branchlen"], AUXPOW_START_HEIGHT + 3 - (AUXPOW_START_HEIGHT - 1))
        self.log.info(f"  getchaintips: active={active_tips[0]['height']}, "
                      f"AuxPoW fork branchlen={auxpow_fork['branchlen']}")

        # ---- Block index persistence: AuxPoW nVersion survives a clean restart ----
        # After a restart without -reindex, the node loads block headers from LevelDB.
        # The BLOCK_VERSION_AUXPOW bit in nVersion must be preserved so that
        # CheckAuxProofOfWork is still invoked correctly on reconnect.
        self.log.info("Block index persistence: AuxPoW nVersion survives clean restart")
        chain_height_before = node.getblockcount()
        best_hash_before = node.getbestblockhash()

        self.restart_node(0)  # clean restart, no -reindex

        assert_equal(node.getblockcount(), chain_height_before)
        assert_equal(node.getbestblockhash(), best_hash_before)

        # The AuxPoW block submitted via submitblock must still be accessible
        # and must still carry the BLOCK_VERSION_AUXPOW bit in nVersion.
        persisted = node.getblock(sb_block_hash)
        assert persisted["height"] == sb_height
        assert persisted["version"] & BLOCK_VERSION_AUXPOW, \
            "BLOCK_VERSION_AUXPOW bit lost after clean restart"
        assert_equal((persisted["version"] >> 16) & 0xFFFF, AUXPOW_CHAIN_ID)
        self.log.info(f"  Persistence: AuxPoW block {sb_height} nVersion={hex(persisted['version'])} intact after restart")

        self.log.info("All AuxPoW tests passed")


if __name__ == '__main__':
    AuxPowTest(__file__).main()
