// Copyright (c) 2015-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <test/util/random.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <boost/test/unit_test.hpp>

#include <functional>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

namespace {
/** Build a chain of n blocks, all with nBits, where solvetime(i) is the gap between block i-1 and i. */
std::vector<CBlockIndex> MakeChain(size_t n, uint32_t nbits, const std::function<int64_t(size_t)>& solvetime)
{
    std::vector<CBlockIndex> blocks(n);
    int64_t t{1'800'000'000};
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) t += solvetime(i);
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = t;
        blocks[i].nBits = nbits;
    }
    return blocks;
}

/** new target / old target */
double TargetRatio(uint32_t new_nbits, uint32_t old_nbits)
{
    arith_uint256 n, o;
    n.SetCompact(new_nbits);
    o.SetCompact(old_nbits);
    return n.getdouble() / o.getdouble();
}
} // namespace

BOOST_AUTO_TEST_CASE(lwma_params)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    BOOST_CHECK_EQUAL(consensus.nPowTargetSpacing, 10);
    BOOST_CHECK_EQUAL(consensus.lwma_window, 90);
}

BOOST_AUTO_TEST_CASE(lwma_before_full_window)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    const unsigned int limit{UintToArith256(consensus.powLimit).GetCompact()};
    auto chain = MakeChain(consensus.lwma_window, 0x1c05a3f4, [](size_t) { return 10; });
    // Tip height is window-1: not enough history yet
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&chain.back(), nullptr, consensus), limit);
}

BOOST_AUTO_TEST_CASE(lwma_steady_state_is_unchanged)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    auto chain = MakeChain(consensus.lwma_window + 1, 0x1c05a3f4, [](size_t) { return 10; });
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&chain.back(), nullptr, consensus), 0x1c05a3f4U);
}

BOOST_AUTO_TEST_CASE(lwma_responds_to_hashrate_changes)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    const uint32_t nbits{0x1c05a3f4};
    // Blocks twice as fast as target: target (and thus work per block inverse) halves.
    auto fast = MakeChain(consensus.lwma_window + 1, nbits, [](size_t) { return 5; });
    BOOST_CHECK_CLOSE(TargetRatio(GetNextWorkRequired(&fast.back(), nullptr, consensus), nbits), 0.5, 0.01);
    // Blocks twice as slow: target doubles.
    auto slow = MakeChain(consensus.lwma_window + 1, nbits, [](size_t) { return 20; });
    BOOST_CHECK_CLOSE(TargetRatio(GetNextWorkRequired(&slow.back(), nullptr, consensus), nbits), 2.0, 0.01);
}

BOOST_AUTO_TEST_CASE(lwma_clamps_solvetimes)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    const uint32_t nbits{0x1c05a3f4};
    // Huge gaps are clamped at 6T, so the target rises by at most a factor 6 per step.
    auto stalled = MakeChain(consensus.lwma_window + 1, nbits, [](size_t) { return 100'000; });
    BOOST_CHECK_CLOSE(TargetRatio(GetNextWorkRequired(&stalled.back(), nullptr, consensus), nbits), 6.0, 0.01);
    // Identical timestamps count as 1 second each (strictly increasing), so factor 1/T.
    auto same_time = MakeChain(consensus.lwma_window + 1, nbits, [](size_t) { return 0; });
    BOOST_CHECK_CLOSE(TargetRatio(GetNextWorkRequired(&same_time.back(), nullptr, consensus), nbits), 0.1, 0.01);
}

BOOST_AUTO_TEST_CASE(lwma_weights_recent_blocks_more)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    const uint32_t nbits{0x1c05a3f4};
    const size_t n{static_cast<size_t>(consensus.lwma_window + 1)};
    // Same set of solvetimes, in opposite order. Recent slowness must matter more.
    auto fast_then_slow = MakeChain(n, nbits, [&](size_t i) { return i <= n / 2 ? 5 : 15; });
    auto slow_then_fast = MakeChain(n, nbits, [&](size_t i) { return i <= n / 2 ? 15 : 5; });
    BOOST_CHECK(GetNextWorkRequired(&fast_then_slow.back(), nullptr, consensus) > GetNextWorkRequired(&slow_then_fast.back(), nullptr, consensus));
}

BOOST_AUTO_TEST_CASE(lwma_never_exceeds_pow_limit)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    const unsigned int limit{UintToArith256(consensus.powLimit).GetCompact()};
    auto slow = MakeChain(consensus.lwma_window + 1, limit, [](size_t) { return 100; });
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&slow.back(), nullptr, consensus), limit);
}

BOOST_AUTO_TEST_CASE(lwma_regtest)
{
    // Default regtest: no retargeting, nBits is carried forward.
    const auto fixed = CreateChainParams(*m_node.args, ChainType::REGTEST);
    auto chain = MakeChain(fixed->GetConsensus().lwma_window + 1, 0x207fffff, [](size_t) { return 1; });
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&chain.back(), nullptr, fixed->GetConsensus()), 0x207fffffU);

    // -test=lwma: powLimit is ~2^255, exercising the overflow-safe path.
    CChainParams::RegTestOptions opts;
    opts.lwma = true;
    const auto lwma = CChainParams::RegTest(opts);
    const auto& consensus = lwma->GetConsensus();
    BOOST_CHECK(!consensus.fPowNoRetargeting);
    const unsigned int limit{UintToArith256(consensus.powLimit).GetCompact()};
    // Slow blocks at the limit stay at the limit rather than overflowing.
    auto slow = MakeChain(consensus.lwma_window + 1, limit, [](size_t) { return 60; });
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&slow.back(), nullptr, consensus), limit);
    // Fast blocks at the limit make the target harder (smaller).
    auto fast = MakeChain(consensus.lwma_window + 1, limit, [](size_t) { return 1; });
    BOOST_CHECK_CLOSE(TargetRatio(GetNextWorkRequired(&fast.back(), nullptr, consensus), limit), 0.1, 1.0);
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
    BOOST_CHECK(!neg);
    BOOST_CHECK(pow_compact != 0);
    BOOST_CHECK(!over);
    BOOST_CHECK(UintToArith256(consensus.powLimit) >= pow_compact);

    // LWMA needs a window of at least two blocks, and the window must fit in 64-bit weights
    BOOST_CHECK_GE(consensus.lwma_window, 2);
    BOOST_CHECK_LE(consensus.lwma_window, 10000);
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

BOOST_AUTO_TEST_SUITE_END()
