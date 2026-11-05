# Consensus and network changes (Phase 1)

Base: Bitcoin Core v32.1 (`f0de87f`), branch `octet` in `vendor/bitcoin`.
Patches: `patches/*.patch` (generated with `git format-patch f0de87f..octet`).

**Status:** builds and runs on regtest (see ROADMAP.md for what was actually run and what still fails).
The unit tests named below were written but the full suite has not yet passed.
Every consensus change lists what could break and how it is tested.

## 1. Block target: `nPowTargetSpacing = 10`

All networks. Block interval is a single constant, so it can be re-tuned after testnet
measurements (README discusses the stale-rate trade-off).

- **What could break:** anything that silently assumed 600 s. Reviewed uses of
  `nPowTargetSpacing` (net_processing stale-tip and download timeouts, `getchaintxstats`,
  `GetBlockProofEquivalentTime`): they scale with the parameter. Known exceptions are in
  "Not changed / open" below.
- **Test:** unit tests that hardcode 600 s; functional tests; the 200-block regtest run.

## 2. Difficulty: LWMA-1 every block (`src/pow.cpp`)

Replaces the 2016-block retarget (5.6 hours at 10 s blocks, far too slow).

```
next_target = avg(target_i, i=1..N) * sum(i * ST_i) / (T * N * (N+1) / 2)
```

- `i = 1` is the oldest block in the window, `i = N` the newest, so recent blocks weigh more.
- Solvetimes: timestamps inside the window are forced strictly increasing
  (`ts_i = max(ts_i, ts_{i-1} + 1)`), then `ST_i = min(6T, ts_i - ts_{i-1})`.
  This bounds what a miner gains by manipulating timestamps.
- Window `N`: 90 on main/test/testnet4/signet (15 min of blocks at 10 s), 60 on regtest.
  **Draft value**, to be tuned on a multi-node testnet. Larger `N` is smoother but reacts
  more slowly to hashrate swings, which matters for a low-hashrate new chain.
- Until the chain has `N` blocks past genesis, difficulty stays at `powLimit`.
- Arithmetic: when `powLimit` is well below 2^256 (main, ~2^224) targets are summed
  undivided, so a constant-difficulty chain reproduces its `nBits` exactly. When it is not
  (regtest, ~2^255) targets are averaged first and the result saturates at `powLimit`;
  no 256-bit overflow in either path.
- `fPowAllowMinDifficultyBlocks` is no longer read by `GetNextWorkRequired`. Bitcoin's
  testnet "20 minute" min-difficulty rule is dropped, because with 10 s blocks it would
  hand out minimum-difficulty blocks after 20 s. LWMA recovers from a hashrate loss
  within about `N` blocks on its own.
- `PermittedDifficultyTransition` (headers-sync anti-DoS heuristic that bounded the step
  size at retarget boundaries) now always returns true. Header sync keeps only the previous
  header, not the LWMA window, and a one-step bound is not derivable from the previous
  `nBits` alone. The exact value is still enforced per block in
  `ContextualCheckBlockHeader`. **Cost:** a peer can feed low-work headers with arbitrary
  `nBits` during pre-sync; each header still needs valid PoW for its claimed `nBits`.
- Regtest keeps fixed minimum difficulty (`fPowNoRetargeting = true`), otherwise
  quickly mined test chains would raise difficulty until blocks take ~10 s each. Start with
  `-test=lwma` to enable LWMA on regtest.
- **What could break:** off-by-one in the window, overflow, and clock-manipulation
  attacks. **Test:** unit tests in `src/test/pow_tests.cpp` (steady state exact, 2x/0.5x
  hashrate, clamp at 6T, identical timestamps, recency weighting, powLimit saturation,
  regtest path). The regtest run with `-test=lwma` shows difficulty moving. No review by
  a second person yet, and no adversarial simulation.

## 3. Values that assumed 10-minute blocks

| Constant | Bitcoin | Octet (Phase 1) | Reasoning |
|---|---|---|---|
| `nPowTargetTimespan` | 14 days | 2016 blocks (20160 s) main/test; 144 blocks regtest | No longer drives retargeting. Kept only because `DifficultyAdjustmentInterval()` is used as the versionbits period, BIP94 period and `getnetworkhashps -1` window. Same block counts as Bitcoin keeps versionbits constants (2016/144) and tests valid. |
| Soft-fork heights (BIP34/65/66/CSV/segwit) | Historical Bitcoin heights | 1 / 1 / 1 / 1 / 0 on main and testnet (regtest/testnet4/signet already were) | A new chain has no history to protect; Bitcoin heights (e.g. segwit at 481824) refer to Bitcoin blocks. Block-hash script exceptions removed. |
| BIP9 deployments | 2016-block periods | unchanged (`TESTDUMMY` only) | No live deployments. A future deployment should use a period of about a day (8640 blocks). |
| `nSubsidyHalvingInterval` | 210000 | 12,500,000 main/test/testnet4/signet; 150 regtest | See "Supply schedule" below. |
| `COINBASE_MATURITY` | 100 | **unchanged (100 blocks = 16.7 min), OPEN DECISION** | See below. |

### Supply schedule (decided: 500M cap)

- Initial subsidy 20 OCT (`nInitialSubsidy`, new consensus parameter), halving every
  12,500,000 blocks (about 3.96 years at 10 s), so 2 x 20 x 12.5M = 500,000,000 OCT.
- Because the subsidy is an integer number of satoshis that rounds down at each halving,
  actual total issuance is **499,999,998.375 OCT** (computed offline; the unit test
  `subsidy_limit_test` asserts 49999999837500000 satoshis, not yet run). The subsidy
  reaches zero after 31 halvings, roughly 123 years.
- `MAX_MONEY` is raised from 21M to 500M OCT (5e16 satoshis, far below the int64 limit).
- Half of all coins are issued in the first ~4 years, as in Bitcoin, at 172,800 OCT per day
  at launch (20 OCT x 8640 blocks).
- No premine and no dev fee: the genesis output is `OP_RETURN` and the whole subsidy goes
  to the miner.
- Regtest keeps 50 OCT with 150-block halvings so existing tests stay meaningful.
- **What could break:** anything assuming 21M or 50 coins: `compress_tests` (updated to
  `0x1dcd6500` for 500M), functional framework `MAX_MONEY` (updated), tests asserting
  21M-based amounts. **Test:** `validation_tests` (halving boundaries, total), not yet run.
- Tail emission (permanent small reward) was considered and not adopted; revisit with
  testnet data if the post-subsidy security budget looks thin.

### Open decision: coinbase maturity

100 blocks at 10 s is about 17 minutes. With a high orphan rate on 10 s blocks, reorgs of
several dozen blocks are plausible, so Bitcoin's ~16.7 hours (~6000 blocks) is the
comparable safety margin. `COINBASE_MATURITY` is a global constant that hundreds of tests
and the wallet assume equals 100, and regtest would need 6000+ blocks to spend a coinbase.
Left at 100 for Phase 1. Recommended: decide after measuring orphan rates on the testnet,
likely via a per-chain parameter so regtest can stay at 100.

## 4. Identity (not consensus, but prevents mixing with Bitcoin)

| Item | Value |
|---|---|
| Binaries | `octetd`, `octet-cli`, `octet-qt` (CMake `OUTPUT_NAME`; target names unchanged). `bitcoin-tx`, `bitcoin-util`, `bitcoin-wallet` not renamed. |
| Conf file | `octet.conf` |
| Data dir | `%LOCALAPPDATA%\Octet`, `~/.octet`, `~/Library/Application Support/Octet` |
| User agent | `/Octet:32.1.0/` |
| Ports p2p / rpc | main 25333/25332, testnet 25433/25432, testnet4 25533/25532, signet 25633/25632, regtest 25744/25743 |
| Magic | main `e38fb16d`, testnet `d5a2913c`, testnet4 `c7b4e91a`, regtest `f1d68b4e` |
| Base58 P2PKH/P2SH/WIF | main 115/117/247 (`o...`, `p...`), test networks 68/70/207 |
| Bech32 HRP | `oct`, `toct` (testnet, testnet4, signet), `octrt` (regtest) |
| BIP32 xpub/xprv versions | **unchanged** (Bitcoin's `xpub`/`tpub`). Changing them invalidates the checksummed key strings in many unit/functional test vectors. Extended keys can therefore be confused with Bitcoin ones; addresses cannot. Do before any public use. |

## 5. Genesis

- Regtest: new block. Message `05/Nov/2026 One CPU, one vote. Octet regtest genesis`,
  `nTime = 1793836800` (2026-11-05 00:00:00 UTC), `nBits = 0x207fffff`, `nNonce = 0`
  (the first nonce already met the target), output script `OP_RETURN`.
  Hash `6ee6e272ca2a52d0b086c686ab1c68e0cd1a5e59226a5db2ddc0ee7b7dd83abc`,
  merkle root `aea56117ce8b82dbb92c2cd66dea2fe60ff52b7f5783450464d8ab7f6f98745c`.
  Computed offline in Python; the C++ asserts in `CRegTestParams` re-derive both at
  startup and abort on mismatch.
- Main, testnet, testnet4, signet still use **Bitcoin's genesis blocks**. They are
  therefore not usable Octet networks. This is intentional for Phase 1 and must be
  replaced (and re-mined when Phase 2 changes the PoW hash).
- Remaining Bitcoin-chain data not yet cleaned: `m_assumeutxo_data` and `chainTxData` for
  main/testnet/testnet4/signet and the regtest assumeutxo hashes used by tests.

## 6. Not changed / open

- `MAX_FUTURE_BLOCK_TIME` (2 h). With 10 s blocks a 2 h future limit is generous for
  timestamp games; LWMA's 6T clamp bounds the damage but a smaller value (and clock-skew
  measurements) should be considered.
- `MIN_BLOCKS_TO_KEEP = 288` (48 min of blocks now). Pruned nodes keep far less history in
  wall-clock terms, and `net_processing.cpp` computes `MIN_BLOCKS_TO_KEEP - 3600/spacing`,
  which is **negative** at 10 s blocks (288 - 360), so pruned nodes will not announce
  blocks via `getblocks`. Needs a decision together with reorg depth.
- Signet's default magic is derived from the default (Bitcoin) signet challenge.
- `src/test/fuzz/pow.cpp` was edited but not compiled.
- BIP94 (timewarp) checks use `DifficultyAdjustmentInterval()`; LWMA makes them moot.
