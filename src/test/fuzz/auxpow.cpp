// Copyright (c) 2026 The ElevenSeventyFive Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <pow.h>
#include <primitives/block.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <uint256.h>
#include <util/chaintype.h>

#include <cassert>
#include <cstdint>
#include <optional>

void initialize_auxpow()
{
    SelectParams(ChainType::REGTEST);
}

FUZZ_TARGET(auxpow, .init = initialize_auxpow)
{
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());

    // Deserialize a CAuxPow from fuzz input — exercises the bounded-read
    // Unserialize path (vMerkleBranch/vChainMerkleBranch length checks).
    // v29: CAuxPow embeds a coinbase tx, so its (de)serialization needs TransactionSerParams.
    const std::optional<CAuxPow> maybe_auxpow = ConsumeDeserializable<CAuxPow>(fuzzed_data_provider, TX_WITH_WITNESS);
    if (!maybe_auxpow) return;
    const CAuxPow& auxpow = *maybe_auxpow;

    const Consensus::Params& params = Params().GetConsensus();

    // Check() must never throw — it returns bool.
    // Fuzz the child hash and chain ID independently of the deserialized data.
    const std::optional<uint256> maybe_child_hash = ConsumeDeserializable<uint256>(fuzzed_data_provider);
    const uint256 child_hash = maybe_child_hash.value_or(auxpow.hashBlock);
    const int chain_id = fuzzed_data_provider.ConsumeIntegralInRange<int>(0, 65535);
    (void)auxpow.Check(child_hash, chain_id, params);

    // Also call Check with the correct chain ID to probe the happy path.
    (void)auxpow.Check(child_hash, params.nAuxpowChainId, params);

    // CheckMerkleBranch must not crash for any branch/index combination.
    {
        const std::optional<uint256> maybe_root = ConsumeDeserializable<uint256>(fuzzed_data_provider);
        if (maybe_root) {
            const int branch_index = fuzzed_data_provider.ConsumeIntegralInRange<int>(0, 255);
            (void)CAuxPow::CheckMerkleBranch(*maybe_root, auxpow.vMerkleBranch, branch_index);
            (void)CAuxPow::CheckMerkleBranch(*maybe_root, auxpow.vChainMerkleBranch, branch_index);
        }
    }

    // Re-serialize and deserialize to confirm round-trip stability.
    {
        DataStream ss;
        ss << TX_WITH_WITNESS(auxpow);
        CAuxPow auxpow2;
        try {
            ss >> TX_WITH_WITNESS(auxpow2);
        } catch (const std::ios_base::failure&) {
            return;
        }
        // After a clean round-trip, Check() results must be identical.
        const bool r1 = auxpow.Check(child_hash, params.nAuxpowChainId, params);
        const bool r2 = auxpow2.Check(child_hash, params.nAuxpowChainId, params);
        assert(r1 == r2);
    }
}
