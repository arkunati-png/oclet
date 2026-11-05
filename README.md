# Octet(OCT)

A Bitcoin Core fork aiming at Satoshi's stated goals: peer-to-peer electronic cash and
"one-CPU-one-vote". Mining uses RandomX (CPU-friendly, ASIC-resistant) and blocks target
10 seconds. Later phases add a compute layer on top (see ROADMAP.md).

> Status: scaffolding only. Nothing here is a launched network. Testnet/regtest only until
> the consensus changes have been reviewed by people who know consensus code.

## Chain parameters (draft)

| Parameter | Value | Notes |
|---|---|---|
| Name / ticker | Octet / OCT | Check name and ticker conflicts before any public use |
| Base | Bitcoin Core (pin a release tag in `scripts/setup.ps1`) | MIT licensed |
| PoW | RandomX | Replaces SHA-256d; light verification, needs ~256 MB for fast mode / 2 GB mining dataset |
| Target block time | 10 seconds | See "10-second blocks" below |
| Difficulty adjustment | Per-block (LWMA-style) | Bitcoin's 2016-block retarget would take ~5.6 hours at 10s blocks, far too slow |
| Coinbase maturity | 100 blocks (~17 min) | Revisit |
| Premine / dev fee | None | Fair launch |
| Supply & reward schedule | 500M OCT cap; 20 OCT/block, halving every 12.5M blocks (~4 years) | Draft; total is 499,999,998.375 OCT after rounding |
| Network | Own genesis block, magic bytes, ports, address prefixes | |

## 10-second blocks: the trade-off

Block propagation across the internet takes roughly 1-3 seconds. The chance that two miners
find a block inside one propagation window is about `1 - exp(-t/T)`:

- 2 s propagation, 10 s blocks: ~18% stale (orphan) blocks
- 2 s propagation, 60 s blocks: ~3%

Stale blocks waste hash power and let a miner with fast connectivity gain an edge, which
undercuts "one CPU, one vote". The block time is a single constant so it can be tuned
after testnet measurements; 20-30 s is a common compromise. Compact block relay helps but
does not remove the effect.

## Layout

```
scripts/setup.ps1   clones Bitcoin Core and RandomX into vendor/
vendor/             upstream sources (git-ignored)
patches/            our changes to Bitcoin Core, as patch files
docs/               notes
ROADMAP.md          staged plan
```

## Getting started

```powershell
.\scripts\setup.ps1
```

Then follow ROADMAP.md phase 1.

To reproduce the Phase 1 fork on top of the pinned Bitcoin Core tag:

```powershell
cd vendor\bitcoin
git checkout -b octet
git am --3way ..\..\patches\*.patch
```

Build and test notes are in ROADMAP.md (built and run on WSL Ubuntu 24.04; regtest only).
