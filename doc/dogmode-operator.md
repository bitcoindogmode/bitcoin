# Running a Bitcoin DOG Mode node: the operator's runbook

*Written 2026-09-02 from the node we run ourselves, updated 2026-09-03 after the policy set
merged, and maintained by the Dog of Bitcoin Foundation since 2026-09-14. Every number in here
was measured on that node or read from the DOG Mode repository on the date given; nothing is a
guess with a confident voice. When the client cuts a release, the "today" sections below get
rewritten and the recipe stays.*

DOG Mode (`github.com/bitcoindogmode/bitcoin`) is Bitcoin Core with a different relay policy.
This document is for the person who wants to run one: what it is, what it is not, what you
need, how to build it so you know what you are running, how to run it safely, how to open it to
the network on purpose, how to update it without a re-sync, and how to tell when it is quietly
wrong. If you have run Bitcoin Core before, most of this is familiar and the DOG Mode parts are
sections 1, 7 and 8. If you have not, read it in order.

## 1. What DOG Mode is, and what it is not

**Policy, never consensus.** A Bitcoin node enforces two kinds of rules. Consensus rules decide
which blocks are valid; every node on the network must agree on them or the chain splits. Relay
policy decides which unconfirmed transactions a node is willing to accept into its mempool,
pass on to its peers, and (if it mines) put into a block. Policy is local. Two nodes with
different policies validate exactly the same chain; they just disagree about what to gossip.

DOG Mode changes policy only. A DOG Mode node accepts the same blocks as Bitcoin Core, rejects
the same invalid blocks, and follows the same chain. It cannot be forked off the network by its
settings, and it cannot fork anyone else. What it changes is what it relays.

**The three announced changes** (merged from pull request 3 as commit
`75032400914250c7ad857dc29043761680a66485`, read 2026-09-02):

| Policy | Bitcoin Core 31.1 | DOG Mode |
|---|---|---|
| Maximum standard transaction weight | 400,000 WU (100 kvB) | 3,900,000 WU (975 kvB), just under the 4,000,000 WU block limit |
| Dust threshold (smallest output relayed) | 294 to 546 sats depending on output type | 1 sat, for every output type |
| Peering | none | advertises service bit 14 (`NODE_DOG_MODE`) and opens up to 4 extra outbound connections to other DOG Mode nodes |

Two consequences ride along and are worth knowing before you run it, because the release notes
mention them only in passing:

- **The OP_RETURN default rises with the weight.** Core defines the data carrier limit as one
  quarter of the standard weight, so `-datacarriersize` defaults to 975,000 bytes on DOG Mode
  instead of Core's 100,000. If you would rather keep Core's number, set it explicitly
  (section 7). Runes need none of the extra room: a runestone is tens of bytes.
- **The four DOG peering slots are taken out of your inbound capacity.** At the default
  `-maxconnections=125` you serve 110 inbound peers instead of 114; at `-maxconnections=20`,
  5 instead of 9. Measured by an independent reviewer in the review threads. The merged
  implementation has no switch to turn the extra slots off.

**What DOG Mode does not do.** Relaying a transaction does not confirm it. A 975 kvB
transaction or a 1 sat output reaches a block only when a miner running a compatible policy
includes it. Until miners run DOG Mode, a DOG Mode node is a node that would relay those
transactions, and that is the honest description. Run it because you want that policy on the
network, not because it changes what your own transactions can do today.

## 2. Where the client stands today (2026-09-02)

Read from the repository, not from the announcement:

- **The default branch `31.1-dogmode` includes the policy changes.** Its tip is merge commit
  `75032400914250c7ad857dc29043761680a66485`, which merged pull request 3. A node built from
  that commit advertises the DOG bit and applies the policies described in section 1.
- **Pull request 3 was merged** at `75032400914250c7ad857dc29043761680a66485`. Its final
  policy commit moved the service bit from 28 to 14 on 2026-09-01 (28 collides with a bit
  another client already uses).
- **There are no DOG Mode releases, binaries, reproducible-build attestations, or DOG Mode
  security contact.** The website says "Relay policy is a choice" and nothing else. The
  repository's `SECURITY.md` is still Core's. Anyone offering you a DOG Mode binary today built
  it themselves; treat it accordingly.

So "running DOG Mode today" means building from source at a commit you chose and verified.
For the merged DOG Mode policy, use the merge commit above or a descendant you have reviewed.
That is what the rest of this document does.

## 3. What you need

The node we run: 16 cores, 16 GB of RAM, a 1.1 TB SSD, on a Linux virtual machine. That is
comfortable, not minimal.

| Resource | Minimum that works | What we recommend | Why |
|---|---|---|---|
| Disk | 800 GB SSD | 1 TB or more, SSD | Our data directory read 625 GB at block 822,412 on 2026-09-02: 567 GB of blocks, 9 GB of chainstate (the UTXO set) and 45 GB of transaction index, and it grows every block. A hard disk makes the initial sync take weeks. |
| RAM | 4 GB | 8 GB or more | Core's default database cache is 450 MB; the initial sync goes much faster with several gigabytes of cache (section 5), turned back down afterward. |
| CPU | 2 cores | 4 or more | Signature validation during the initial sync is the only heavy work. |
| Network | unmetered | unmetered | The initial sync downloads the whole chain once. A public node then uploads blocks to peers indefinitely; cap it with `-maxuploadtarget` if your line is metered. |

**Pruning.** If disk is the constraint, `prune=550` (megabytes) keeps a full validating node at
the size of the UTXO set plus 550 MB of blocks: the chainstate was 9 GB at block 822,412 and
grows, so budget 20 GB. A pruned node still validates everything and relays with DOG Mode policy, but it
cannot serve old blocks to peers (it advertises `NETWORK_LIMITED` instead of `NETWORK`) and it
cannot run an index such as `txindex` or an ordinals indexer. For "I want the DOG policy on the
network", pruned is fine. For "I want to serve the chain", it is not.

## 4. Build it so you can say what you are running

Do not run a binary you did not build or cannot verify. Until the project publishes
reproducible builds, the only way to know what your DOG Mode node is doing is to build a commit
whose hash you wrote down.

On Debian 13 (Ubuntu is the same packages):

```
sudo apt-get install build-essential cmake pkgconf python3 git libevent-dev libboost-dev
```

Fetch exactly one commit and check it is the one you meant. Replace the commit with the one you
chose: today, `75032400914250c7ad857dc29043761680a66485` is the merge commit that put the DOG
Mode policy on `31.1-dogmode`.

```
COMMIT=75032400914250c7ad857dc29043761680a66485   # the full hash you decided on; this is the PR #3 merge commit
git init bitcoin && cd bitcoin
git remote add origin https://github.com/bitcoindogmode/bitcoin
git fetch --depth 1 origin "$COMMIT"
git checkout FETCH_HEAD
[ "$(git rev-parse HEAD)" = "$COMMIT" ] || { echo "PROVENANCE MISMATCH"; exit 1; }
```

Build without a wallet or a GUI; a relay node needs neither, and every feature you do not build
is a feature that cannot be exploited:

```
cmake -B build -DENABLE_WALLET=OFF -DBUILD_GUI=OFF -DENABLE_IPC=OFF
cmake --build build -j"$(nproc)"
./build/bin/bitcoind --version
sudo cmake --install build
```

About 15 minutes on 16 cores; scale accordingly. Write the commit hash and the version string
into your own notes. From now on the question "what is this node running" has an answer.

## 5. Run it as its own user, with nothing exposed

The posture is the same one Core's own documentation recommends and it is not optional on a
public node: a dedicated unprivileged user, no wallet, RPC bound to localhost only, and a
systemd unit that keeps it that way.

```
sudo adduser --system --group --home /var/lib/bitcoind bitcoin
sudo install -d -o bitcoin -g bitcoin -m 710 /var/lib/bitcoind /etc/bitcoin
```

`/etc/bitcoin/bitcoin.conf`:

```
server=1
disablewallet=1
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
listen=1
dbcache=8192
# txindex=1        only if you run an indexer that needs it; 45 GB at block 822,412 and growing
# prune=550        only if disk is the constraint; incompatible with txindex
```

`dbcache=8192` is for the initial sync on a machine with 16 GB; use roughly half your RAM and
turn it down to Core's default (450) or 2000 once synced. RPC uses cookie authentication by
default (a file in the data directory readable by the `bitcoin` user); do not add `rpcuser` and
`rpcpassword` and never bind RPC to a network interface.

`/etc/systemd/system/bitcoind.service`:

```
[Unit]
Description=Bitcoin DOG Mode daemon
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/local/bin/bitcoind -conf=/etc/bitcoin/bitcoin.conf -datadir=/var/lib/bitcoind
User=bitcoin
Group=bitcoin
Type=exec
Restart=on-failure
TimeoutStopSec=600
PrivateTmp=true
ProtectSystem=full
NoNewPrivileges=true
PrivateDevices=true
MemoryDenyWriteExecute=true

[Install]
WantedBy=multi-user.target
```

```
sudo chown bitcoin:bitcoin /etc/bitcoin/bitcoin.conf && sudo chmod 640 /etc/bitcoin/bitcoin.conf
sudo systemctl daemon-reload && sudo systemctl enable --now bitcoind
```

`TimeoutStopSec=600` matters: a node flushing its cache on shutdown can take minutes, and a
unit that kills it at 90 seconds corrupts the database and costs you a re-sync.

## 6. The initial sync, with the port still closed

The first run downloads and validates the whole chain. It needs outbound peers only, so keep
port 8333 closed (no forward on your router or firewall) until the sync finishes. A node that
is still syncing is not useful to the network as a public service, and a public service you
have not finished setting up is a bad idea on general principle.

Watch it:

```
CLI="sudo -u bitcoin bitcoin-cli -conf=/etc/bitcoin/bitcoin.conf -datadir=/var/lib/bitcoind"
$CLI getblockchaininfo | grep -E '"blocks"|"headers"|"verificationprogress"|"initialblockdownload"'
$CLI getnetworkinfo | grep -E '"connections'
```

You are done when `initialblockdownload` is `false` and `blocks` equals `headers`.

**How long.** Our node, on 16 cores, 16 GB, an SSD mirror, `dbcache=8192`, 10 to 11 outbound
peers, and no inbound: after 22 hours it had validated block 821,808 of 965,166, about two
thirds of the verification work, and the last blocks are the heaviest. Budget days on good
hardware and a week or more on modest hardware. The measured total will be written here when
ours finishes. Everything else in this document is minutes; this is the one cost you pay once.

**Two sources, always.** When it says it is synced, compare `blocks` against a block explorer
you trust (mempool.space, or a friend's node). A node that agrees with itself has proven
nothing.

## 7. DOG Mode specifics: what to set, what to look for

This section applies to the pull request 3 merge commit
`75032400914250c7ad857dc29043761680a66485` and descendants that retain its policy changes.
It applies to the current `31.1-dogmode` branch.

**Prove the policy is live.**

```
$CLI getnetworkinfo | grep -A8 localservicesnames     # contains "DOG_MODE" (bit 14, 0x4000 in localservices)
$CLI getpeerinfo | grep '"connection_type"' | sort | uniq -c   # "dog" rows appear when a DOG peer is found
$CLI -netinfo 4                                        # the per-connection view, DOG connections in their own column
```

A node built from a commit before the pull request 3 merge shows `NETWORK`, `WITNESS`,
`NETWORK_LIMITED`, `P2P_V2` and no `DOG_MODE`; that is how you know you built the wrong commit.

**Expect empty DOG slots for a while.** The four DOG connections are attempted after the
node's ordinary outbound connections are filled, and only to peers that advertise bit 14. Today
almost nobody does. Reviewers measured that a node with few reachable peers may never fill a
DOG slot at all while still giving up the inbound capacity for them. That ordering is part of
the merged implementation; an empty DOG column is not a fault in your setup.

**Knobs you may want, all in `bitcoin.conf`:**

- `datacarriersize=100000` keeps Bitcoin Core's OP_RETURN default instead of inheriting
  975,000. Set it if you did not sign up for a near-megabyte data carrier; leave it out if you
  did. Either is a legitimate choice, and making it explicitly is the point.
- `dustrelayfee` can only lower the threshold further on DOG Mode (0 disables the dust check
  entirely). It cannot restore Core's 546; the merged implementation implements 1 sat as a cap,
  not a default.
  If you want Core's dust policy, run Core.
- `maxmempool` should be at least 40 (megabytes). The merged implementation lowers the
  enforced floor to about 5 MB so that old configurations still start, but at that size a DOG
  Mode mempool holds only three maximum-size transactions and its minimum fee lurches in a way
  the rest of the network's
  does not. Core's default of 300 is fine. `blocksonly=1` implies a 5 MB mempool; check that
  your build starts with it before relying on it.
- `maxconnections` and `maxuploadtarget` are Core's usual levers for a public node
  (`doc/reduce-traffic.md` in the repository). Remember the four DOG slots come out of the
  inbound side of whatever you set.

**Where the DOG bit comes from.** The service bit is advertised unconditionally by the merged
implementation; there is no `-nodogmode`. If you do not want to advertise it, do not run the
policy commit.

## 8. Open the node to the world, on purpose

A public node is a service you are choosing to run for other people. Do it deliberately, after
the sync, and only this much:

1. Forward TCP 8333 from your public address to the node (router port forward, cloud security
   group, or a firewall DNAT rule). Nothing else: not 8332 (RPC), not SSH from the world.
2. Tell the node its public address so it advertises it: `externalip=YOUR.PUBLIC.IP` in
   `bitcoin.conf`. Behind NAT without this, peers cannot find you.
3. Turn the sync cache down (`dbcache=2000` or the default) and restart.
4. Prove it from outside: `nc -zv YOUR.PUBLIC.IP 8333` from another network, or any of the
   public node-checking sites. Inbound peers appear in `getnetworkinfo` (`connections_in`) over
   the next hour, not the next minute; the network learns your address through gossip.

If you skip step 2, or the port is not really open, the node still works: it just serves nobody.
That is the failure mode of most "public" nodes and it is silent, so check.

## 9. Updating without a re-sync

The multi-day cost was the initial sync and it is paid once. An update is minutes. The rules
that keep it that way:

- **One pinned commit, always.** You know the hash of what is running (section 4). A moving
  branch is not a version.
- **No automatic updates of the client, ever.** Let your operating system patch itself; the
  Bitcoin client changes only when you decide it does.
- **Update on a reason, not on activity.** The triggers are a Bitcoin Core security advisory
  (`bitcoincore.org/en/security-advisories`), a DOG Mode release, or a feature you want. Upstream
  commit traffic is not a reason to touch a working node.

The procedure:

1. **Pick and pin.** Choose the new commit. Read what changed: the release notes for a version
   bump, the diff for a small fix. Write the new hash in your notes.
2. **Snapshot first.** A filesystem snapshot of the data directory (ZFS, LVM, btrfs, or your
   VM's snapshot) makes rollback one command instead of a re-sync. Not optional.
3. **Build the new commit alongside the running one** (section 4 into a fresh directory). The
   build does not touch the running daemon.
4. **Read the notes for the one expensive word: reindex.** A release that changes the database
   or index format says so loudly. If it forces a reindex, schedule it; the chain stays on disk
   and a reindex is hours, not days. If the format change is one-way, the snapshot from step 2
   is your rollback.
5. **Swap and restart.** `systemctl stop bitcoind` (wait for it; the flush can take minutes),
   `cmake --install`, `systemctl start bitcoind`. Catching up on the blocks missed during the
   stop takes minutes. Confirm the height against a second source.
6. **Roll back if wrong.** Reinstall the previous build (still on disk) and, if the data format
   moved, restore the snapshot.

## 10. Backups, and the honest scope of them

A node with no wallet holds nothing that cannot be re-downloaded. What a backup saves is
time: the days of initial sync. So back up the data directory only if a re-sync would hurt you,
and if you do, use a filesystem snapshot taken while the daemon is stopped or frozen, never a
live file copy (LevelDB does not survive being copied mid-write).

If you rely on a backup, restore it once into a throwaway machine and read its height before
you need it. We measured ours: 8,320 seconds end to end for a 367 GB snapshot under sync load,
of which almost all was the copy; the restored node answered its height 202 seconds after
boot. A backup that has never been restored is a rumor.

## 11. Knowing when it is quietly wrong

An outage is loud and cheap. The expensive failure is a node that looks green and is lying: a
stale tip, a height that stopped climbing, an index that fell behind, a "public" port that is
not really open. The standing answer is never to trust one source:

- **Height** against a block explorer or a second node, on a schedule. If yours is more than a
  few blocks behind for more than an hour, something is wrong.
- **Peers.** `connections_out` should sit near 10; `connections_in` should be non-zero on a
  public node once the address has propagated. Zero inbound for a day on a node you opened is
  a closed port.
- **Disk.** The chain grows every block. Alert before the disk is full, not after; a node that
  runs out of space stops cleanly but a full disk can take other services with it.
- **The log.** `debug.log` in the data directory. `grep -i error` once in a while is not
  monitoring, but it is better than nothing, and it is where a refused RPC method, a corrupted
  block file or a peer misbehaving shows up by name.

## 12. Things that actually hurt

Every one of these has been done by someone:

- RPC reachable from the network. Cookie auth or not, 8332 is localhost only.
- A wallet on a public relay node. Keep coins elsewhere; the node's job is truth, not custody.
- Running as root, or as the same user as your web server.
- Installing a binary someone posted. Until the project publishes reproducible builds with
  attestations, build it yourself (section 4).
- Automatic updates of the client.
- Killing the daemon with a short timeout. Let it flush.
- Calling a node "public" without checking the port from outside.

## 13. Reporting problems

For a bug in Bitcoin Core itself (the node crashes, validates wrongly, or leaks memory in code
that is unchanged from Core), use Core's process: `security@bitcoincore.org` for anything
security-sensitive, Core's issue tracker otherwise. For a problem in the DOG Mode changes, the
fork's own issue tracker on GitHub (read 2026-09-02 as enabled; it was not during the first
round of review, which is why two early measurement reports were filed as draft pull requests
1 and 2). Say which commit you built, what you set in `bitcoin.conf`, and what you observed;
a report without the commit hash cannot be reproduced.

There is no DOG Mode security contact yet. When there is, it belongs at the top of this
document.

---

*The node this runbook was written from ran the recipe above on `bbc08805` (pre-merge, plain Core
31.1 behaviour) through its initial sync. The merged tip
(`75032400914250c7ad857dc29043761680a66485`) was built on that node's box on 2026-09-03 with the
recipe in section 4: the unit suites and functional tests the policy change touched pass, and a copy
of the node's own chainstate booted on those binaries answered with `DOG_MODE` in
`localservicesnames` and needed no reindex. The node moved to it on 2026-09-05 and has advertised
`DOG_MODE` since. Independent builds of the merged tip, with thanks: krsnak on Apple Silicon
(`test_bitcoin`, 680 cases, no errors; #8) and J25dunn on Linux with the GUI (152 CTest entries
passing and DOG peering over v1 and v2 on regtest; the full report is on #3). Corrections welcome as
pull requests against this file.*
