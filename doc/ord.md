# Optional Ord integration

DogMode can install and initialize the [`ord`](https://github.com/ordinals/ord)
indexer as an explicit option during first-run setup. This integration does not
enable Ord by default. An experimental inscription page is available after Ord
is installed.

The follow-up experimental inscription interface is documented in
[`ord-inscriptions.md`](ord-inscriptions.md).

## What the option changes

Selecting **Download and synchronize Ord** disables pruning and enables the
Bitcoin Core options required by Ord: `txindex`, `server`, and `rest`. These
values are saved in `settings.json`. An explicit conflicting command-line value,
such as `-prune=1` or `-txindex=0`, causes startup to stop with an explanation
instead of silently overriding the command line.

After Bitcoin Core and its transaction index are synchronized, DogMode:

1. Opens the pinned Ord 0.29.0 release in the system browser when requested and
   asks you to select the downloaded archive.
2. Verifies the selected archive against a checksum compiled into DogMode.
3. Extracts it with the platform `tar` program, checks `ord --version`, and
   installs it atomically.
4. Runs `ord --index-runes --index-sats index update` in the background with
   cookie authentication. These indexes keep the dedicated Ord wallet from
   treating rune-bearing or rare-sat outputs as ordinary cardinal funds.

The Ord executable and index are stored below the network-specific Bitcoin data
directory:

```
ord/bin/0.29.0/ord
ord/data-runes-sats-v1/
```

On Windows the executable is named `ord.exe`. Testnet, testnet4, signet, and
regtest each use their own Bitcoin network data directory and Ord index.

## Terminal use

The executable is deliberately not added to `PATH`. Invoke it by its full path.
For a custom Bitcoin data directory, also pass the matching Bitcoin data and
cookie paths. For example on mainnet:

```sh
<bitcoin-data-dir>/ord/bin/0.29.0/ord \
  --bitcoin-data-dir <bitcoin-data-dir> \
  --cookie-file <bitcoin-data-dir>/.cookie \
  --data-dir <bitcoin-data-dir>/ord/data-runes-sats-v1 \
  --index-runes \
  --index-sats \
  index info
```

Ord is third-party experimental software and is distributed separately under
its own license. DogMode itself does not make network requests to install Ord;
the system browser performs the download. The rune and sat indexes require
significant additional synchronization time and disk space, especially on
mainnet. Deleting the `ord/` directory removes the installed executable and
index but does not change DogMode's saved full-node settings.

Ord 0.29.0 cannot add rune and sat tracking to an index that was originally
created without those options. DogMode therefore uses the versioned
`ord/data-runes-sats-v1` directory and leaves a legacy `ord/data` directory
untouched. Upgrading from an earlier build requires a new synchronization and
temporarily needs disk space for both indexes. After the new index is fully
synchronized and the dedicated Ord wallet is verified, the unused legacy
`ord/data` directory may be removed manually.

## Updating the pinned release

Update the version, URLs, and all platform checksums together in
`src/qt/ordinstaller.cpp`. The extended CI job independently downloads the
Linux archive, verifies its pinned checksum, and runs
`test/functional/feature_ord_index.py` against a regtest node.
