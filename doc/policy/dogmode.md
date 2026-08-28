# Bitcoin DOG Mode relay policy

DOG Mode is Bitcoin Core v31.1 with a small, explicit relay-policy overlay.
Consensus rules are unchanged. A miner still has to include any transaction this
node forwards.

This document is the policy spec. If a default is not listed here, it is Core's.

## What we change

| Knob | Core v31.1 | DOG Mode | Notes |
| --- | --- | --- | --- |
| `MAX_STANDARD_TX_WEIGHT` | 400,000 WU | 3,900,000 WU | Near a full block. |
| `MAX_PACKAGE_WEIGHT` | 404,000 WU | 3,904,000 WU | Same 4,000 WU headroom Core uses, so a max-size tx can still sit in a package with a tiny child. |
| `DEFAULT_CLUSTER_SIZE_LIMIT_KVB` | 101 | 976 | Forced by the package/cluster `static_assert` chain. |
| `-dustrelayfee` default | 3000 sat/kvB | 0 sat/kvB | 1-sat floor on standard output types. The flag still works; set `3000` to restore Core. |
| P2P user-agent | `/Satoshi:31.1.0/` | `/DOGMode:31.1.0/` | So peers and crawlers can tell the clients apart. |

## What we deliberately do not change

- Consensus, block weight, or subsidy.
- `-minrelaytxfee` / incremental relay fee.
- RBF policy.
- Wallet defaults other than inheriting the new standardness ceiling.
- **Datacarrier / OP_RETURN.** `MAX_OP_RETURN_RELAY` stays 100,000 vB. It is *not* derived from `MAX_STANDARD_TX_WEIGHT`. Raising the tx-weight constant without pinning this would silently lift OP_RETURN to 975,000 vB.

## Operator-visible side effects

`CTxMemPool::Flatten()` requires `-maxmempool >= cluster_size_vbytes * 40`. With a 976 kvB cluster that is **40 MB**. A node given a smaller `-maxmempool` will not start.

`-blocksonly` used to imply `-maxmempool=5`. That default is raised to the same 40 MB floor.

Tests or configs that need a tiny mempool (for example `fill_mempool` at 5 MB) must also pin `-limitclustersize=101`.

## What this release does not include

Preferential peering (`NODE_DOG_RELAY`) is a separate change. Shipping the policy defaults without it means large or dusty transactions still die at the first Core peer. That work belongs in a follow-up once the service-bit question in [PR #2](https://github.com/bitcoindogmode/bitcoin/pull/2) is decided.

## UTXO cost of 1-sat outputs

A 1-sat output is cheap for the creator and permanent for every full node. Measured on regtest against v31.1 (see https://github.com/tialkan/bitcoin-dust-utxo-lab): about 43 bytes of chainstate per output after LevelDB settles. Spending a P2TR key-path input is ~57.5 vB, so a 1-sat output is not economically spendable at the 1 sat/vB relay floor. Treat these as markers, not recovered value.
