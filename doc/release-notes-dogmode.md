# Bitcoin DOG Mode 31.1 overlay

Based on Bitcoin Core 31.1. Consensus-compatible. Relay policy only.

## Policy defaults

- Standard transaction weight ceiling: **3,900,000 WU** (Core: 400,000).
- Package weight ceiling: **3,904,000 WU** (Core: 404,000).
- Default cluster size: **976 kvB** (Core: 101).
- Default `-dustrelayfee`: **0** (Core: 3000 sat/kvB), i.e. a 1-sat dust floor.
- Default `-datacarriersize`: **100,000 vB**, unchanged. This is pinned on purpose so the weight change does not silently raise OP_RETURN.
- P2P user-agent: `/DOGMode:31.1.0/`.

## Migration

- `-maxmempool` must be at least **40 MB**, or the node will refuse to start (`-maxmempool must be at least 40 MB`).
- `-blocksonly` now implies `-maxmempool=40` rather than `5`.
- To keep a 5 MB mempool for testing, also set `-limitclustersize=101`.
- To restore Core's dust floor: `-dustrelayfee=0.00003000`.

## Not in this tag

Preferential peering is not included. Without it, transactions that only this policy accepts still need a path to a miner (direct submission, or a later peering patch).
