#!/usr/bin/env python3
# Copyright (c) 2026 The 1175 (ESF) developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""1175-native replacement for p2p_dos_header_tree.py.

Upstream's p2p_dos_header_tree feeds Bitcoin testnet3's early chain to exercise
checkpoint-based header rejection. 1175 has its own genesis on every network and
only a genesis checkpoint on regtest, so that precomputed data does not apply.

Instead, this test exercises the header-tree-bloat DoS protection as it exists after
1175 decoupled the two header-count bounds:

  * MAX_HEADERS_RESULTS (800) is what the node SENDS per getheaders batch, and the
    "full batch" continuation threshold. It was lowered from Bitcoin's 2000 (the M3
    auxpow-size cap) so a full batch of capped-auxpow headers stays under the 4 MB
    P2P message limit.
  * MAX_HEADERS_RESULTS_ACCEPT (2000) is the largest headers message the node will
    ACCEPT before penalising the sender. It stays at Bitcoin's default so the node
    remains wire-compatible with legacy peers that still send 2000-header batches
    during IBD.

Verifies:
  * a legacy-sized headers message (MAX_HEADERS_RESULTS_ACCEPT = 2000, i.e. larger
    than our own 800-header send cap) is accepted and its headers enter the tree —
    this is the IBD wire-compat the decoupling exists to preserve,
  * a headers message of MAX_HEADERS_RESULTS_ACCEPT + 1 is rejected, the sender is
    disconnected, and none of the oversized message's headers enter the block tree,
    so the per-message count check still bounds header-tree growth.
"""

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import CBlockHeader, MAX_HEADERS_RESULTS, MAX_HEADERS_RESULTS_ACCEPT
from test_framework.p2p import P2PInterface, msg_headers
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class Header1175DosTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        # minimumchainwork=0 so the node does not ignore these low-work regtest chains
        # for an unrelated reason; we want to isolate the per-message size limit.
        self.extra_args = [["-minimumchainwork=0x0"]]

    def build_headers(self, tip_hash, tip_height, count, t_base):
        """Build `count` valid regtest block headers extending from tip_hash."""
        headers = []
        prev = int(tip_hash, 16)
        for i in range(count):
            block = create_block(prev, create_coinbase(tip_height + i + 1), t_base + i + 1)
            block.solve()
            headers.append(CBlockHeader(block))
            prev = block.sha256
        return headers

    def run_test(self):
        node = self.nodes[0]
        genesis = node.getbestblockhash()
        gtime = node.getblockheader(genesis)['time']
        node.setmocktime(gtime)
        # Sanity: the accept limit is strictly larger than the send cap, so a legacy
        # 2000-header batch exercises the decoupled path (bigger than we would send).
        assert MAX_HEADERS_RESULTS_ACCEPT > MAX_HEADERS_RESULTS

        self.log.info(f"A legacy-sized headers message ({MAX_HEADERS_RESULTS_ACCEPT}, larger than our {MAX_HEADERS_RESULTS}-header send cap) is accepted")
        legacy_peer = node.add_p2p_connection(P2PInterface())
        legacy = self.build_headers(genesis, 0, MAX_HEADERS_RESULTS_ACCEPT, gtime)
        legacy_peer.send_and_ping(msg_headers(legacy))
        assert legacy_peer.is_connected, "peer disconnected for a headers message within MAX_HEADERS_RESULTS_ACCEPT"
        # The WHOLE batch must have entered the header tree (this is the v25.1 IBD compat):
        # the header tree must reach the full 2000, not be truncated at the 800 send cap.
        assert_equal(max(t['height'] for t in node.getchaintips()), MAX_HEADERS_RESULTS_ACCEPT)

        self.log.info(f"A headers message over the accept limit ({MAX_HEADERS_RESULTS_ACCEPT + 1}) is rejected and disconnects the peer")
        # Use a distinct timestamp base so this is a different chain from the accepted one.
        oversized = self.build_headers(genesis, 0, MAX_HEADERS_RESULTS_ACCEPT + 1, gtime + 1_000_000)
        oversized_tip = oversized[-1].rehash()

        tips_before = node.getchaintips()
        over_limit_peer = node.add_p2p_connection(P2PInterface())
        with node.assert_debug_log([f"headers message size = {MAX_HEADERS_RESULTS_ACCEPT + 1}"]):
            over_limit_peer.send_message(msg_headers(oversized))
            over_limit_peer.wait_for_disconnect()

        # Anti-bloat: none of the oversized message's headers were stored. The message is
        # rejected on the count check before any header is deserialized, so its tip must not
        # appear in the block tree, the tree must not have grown a new branch, and its height
        # must not have advanced past the previously accepted chain.
        tips_after = node.getchaintips()
        assert oversized_tip not in [t['hash'] for t in tips_after], "oversized-message headers leaked into the block tree"
        assert_equal(len(tips_after), len(tips_before))
        assert_equal(max(t['height'] for t in tips_after), MAX_HEADERS_RESULTS_ACCEPT)
        self.log.info("Oversized headers message rejected without bloating the block tree")


if __name__ == '__main__':
    Header1175DosTest(__file__).main()
