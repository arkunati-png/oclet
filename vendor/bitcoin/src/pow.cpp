// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/check.h>

#include <algorithm>
#include <vector>

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);

    if (params.fPowNoRetargeting) return pindexLast->nBits;

    return CalculateNextWorkRequiredLWMA(pindexLast, params);
}

/**
 * LWMA-1 (linearly weighted moving average, Zawy12) difficulty algorithm.
 *
 * For the last N blocks, with solvetime ST_i for block i (i = 1 is the oldest
 * and N the newest) the next target is
 *
 *     avg(target_i) * sum(i * ST_i) / (T * N * (N + 1) / 2)
 *
 * so recent blocks count more. Every solvetime is clamped to [1, 6T] after
 * forcing timestamps to be strictly increasing within the window, which bounds
 * the influence of a miner who plays with timestamps.
 */
unsigned int CalculateNextWorkRequiredLWMA(const CBlockIndex* pindexLast, const Consensus::Params& params)
{
    const int64_t T{params.nPowTargetSpacing};
    const int64_t N{params.lwma_window};
    const arith_uint256 bnPowLimit{UintToArith256(params.powLimit)};

    // Until a full window exists, stay at the proof-of-work limit.
    if (pindexLast->nHeight < N) return bnPowLimit.GetCompact();

    // window[0] is only used for its timestamp; window[1..N] are the N blocks averaged.
    std::vector<const CBlockIndex*> window(N + 1);
    const CBlockIndex* pindex{pindexLast};
    for (int64_t i = N; i >= 0; --i) {
        window[i] = Assert(pindex);
        pindex = pindex->pprev;
    }

    // Each solvetime is at most 6T, so the weighted sum is at most 6 * k.
    const int64_t k{N * (N + 1) / 2 * T};
    // If powLimit is far enough below 2^256 we can sum the targets undivided and
    // keep full precision (a constant-difficulty chain then yields exactly the
    // same nBits). Otherwise (regtest) average first and saturate at powLimit.
    const bool full_precision{bnPowLimit.bits() + arith_uint256{static_cast<uint64_t>(N)}.bits() + arith_uint256{static_cast<uint64_t>(6 * k)}.bits() <= 256};

    arith_uint256 sum_target;
    int64_t weighted_solvetimes{0};
    int64_t previous_timestamp{window[0]->GetBlockTime()};
    for (int64_t i = 1; i <= N; ++i) {
        const int64_t timestamp{window[i]->GetBlockTime()};
        const int64_t this_timestamp{timestamp > previous_timestamp ? timestamp : previous_timestamp + 1};
        const int64_t solvetime{std::min<int64_t>(6 * T, this_timestamp - previous_timestamp)};
        previous_timestamp = this_timestamp;
        weighted_solvetimes += solvetime * i;

        arith_uint256 target;
        target.SetCompact(window[i]->nBits);
        sum_target += full_precision ? target : target / static_cast<uint64_t>(N);
    }

    const arith_uint256 weight{static_cast<uint64_t>(weighted_solvetimes)};

    arith_uint256 next_target;
    if (full_precision) {
        next_target = sum_target * weight / (arith_uint256{static_cast<uint64_t>(k)} * static_cast<uint64_t>(N));
    } else {
        next_target = sum_target / static_cast<uint64_t>(k);
        if (next_target > bnPowLimit / weight) return bnPowLimit.GetCompact();
        next_target *= weight;
    }

    if (next_target > bnPowLimit) next_target = bnPowLimit;
    if (next_target == 0) next_target = 1;
    return next_target.GetCompact();
}

// With a per-block retarget there is no fixed adjustment boundary at which a
// bounded change can be checked, and header sync only keeps the previous
// header, not the LWMA window. Accept any transition here; the exact value is
// enforced by GetNextWorkRequired() in ContextualCheckBlockHeader().
bool PermittedDifficultyTransition(const Consensus::Params&, int64_t, uint32_t, uint32_t)
{
    return true;
}

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    if (EnableFuzzDeterminism()) return (hash.data()[31] & 0x80) == 0;
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
