# Roadmap

Each phase should work and be tested before the next starts.

## Phase 1: regtest fork

Status as of 2026-11-05. Source: branch `octet` in `vendor/bitcoin` (Bitcoin Core v32.1 plus the
commits in `patches/`). Details and open decisions: `docs/consensus-changes.md`.

Done and run (WSL Ubuntu 24.04, g++ 13.3, CMake + Ninja, RelWithDebInfo, wallet on, GUI/IPC off):
- [x] Builds cleanly: `octetd`, `octet-cli`, `test_bitcoin` (0 compiler errors, 0 warnings)
- [x] Renamed binaries, conf file, data dir, user agent (`/Octet:32.1.0/`), ports, magic, address prefixes, HRPs
- [x] New regtest genesis; `octetd -regtest` starts, so the C++ hash/merkle asserts passed
      (genesis `6ee6e272...3abc`, nonce 0)
- [x] 10 s target, LWMA-1 difficulty. With `-test=lwma`: mined 200 blocks, bits went `207fffff` ->
      `1f02d245` once the 60-block window filled (blocks mined much faster than 10 s). Then 80 slow
      blocks (mock clock, +60 s each) eased it back: `1f1aa153` -> `202aa80f`.
- [x] Sent a transaction (10 OCT to a second wallet): mempool -> confirmed in block 201, destination
      balance 10.0. Addresses are `octrt1...` (bech32) and `U...` (legacy), `validateaddress` OK.
- [x] 500M supply schedule in code (20 OCT, halving every 12.5M blocks, MAX_MONEY 500M). Wallet
      balances at height 200 matched regtest's 50 -> 25 OCT halving at 150.

Unit tests (ctest, 374 suites): first run **30 failed, 344 passed**. Most were one cause (fixture
clock set to 2020, before the new genesis: "time-too-new"), fixed and committed, **but not re-run
yet**. Other failures seen and not yet fixed: `bip324_tests` (test vectors use Bitcoin's magic),
`denialofservice_tests/stale_tip_peer_management` (timing assumptions), `net_peer_connection_tests`
(expects port 8333), `key_io_tests` (invalid vectors, fixed, not re-run), `miner_tests`,
`net_tests`, others not yet triaged. Not yet looked at: `script_assets_tests`.

Not done:
- [ ] Re-run unit tests after the fixture fix; triage and fix the rest
- [ ] Functional tests (`test/functional/test_runner.py`) not run at all. Many will fail on
      deliberately-invalid address vectors (e.g. `rpc_invalid_address_message.py`) and on
      BIP32/regtest assumeutxo hashes that were generated on Bitcoin's regtest chain
- [ ] `src/test/fuzz/pow.cpp` edited but never compiled
- [ ] Genesis for main/testnet/testnet4/signet (still Bitcoin's)
- [ ] BIP32 xpub/xprv version bytes (still Bitcoin's)
- [ ] Open decisions: coinbase maturity (100 blocks = 17 min), `MAX_FUTURE_BLOCK_TIME`,
      `MIN_BLOCKS_TO_KEEP` (value in net_processing goes negative at 10 s blocks)
- [ ] LWMA has had no second-person review and no adversarial simulation

## Phase 2: RandomX proof of work
- Add RandomX as a vendored dependency (see `vendor/RandomX`).
- Replace the block hash check in `CheckProofOfWork` with a RandomX hash.
- Add the RandomX key block / seed hash rotation (Monero uses a key block every 2048 blocks
  with a 64-block lag); decide the equivalent here.
- Light mode (256 MB) for validating nodes, fast mode (2 GB) for miners.
- Test vectors from the RandomX repo must pass.

## Phase 3: testnet
- Run 3+ nodes on separate machines. Measure real orphan rate and propagation.
- Re-tune block time from the data.
- Fuzz and review consensus changes; consider an outside review.

## Phase 4: compute layer (separate from the chain)
- Scheduler plus sandboxed node client (WASM or containers).
- Redundant execution to verify results; staking to punish bad results.
- Token spent to submit jobs; nodes paid per verified chunk.
- Expose as a "virtual cluster" API, not a literal single VM.

## Before any public launch
- Name/ticker/trademark check.
- Legal advice on token distribution (securities, money transmission, tax).
- Early-network security plan (checkpoints or similar), since low hashrate chains are
  cheap to 51%-attack.
