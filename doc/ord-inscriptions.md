# Experimental Ord inscription interface

DogMode provides an experimental **Inscribe** page when Ord was enabled during
initial setup and the pinned Ord executable is installed. The page accepts one
local image or data file through drag and drop or a file picker, previews image
formats supported by Qt, and invokes the pinned Ord command-line interface.

The page detects the dedicated Bitcoin Core wallet named `ord`. It can create a
new wallet or restore one from BIP39 recovery words. New recovery words are
shown once in a non-dismissible dialog, and the user must confirm that they
were recorded before continuing. Restore words are passed to Ord over standard
input instead of process arguments. BIP39 passphrases are intentionally not
accepted because Ord 0.29.0 can only receive them through process arguments,
where other local processes may be able to read them.

Once the wallet is available, the page displays its spendable cardinal balance,
total balance, and a funding address. The wallet selected in DogMode's toolbar
is not used to fund inscriptions.

After the initial one-shot index build, DogMode runs Ord's HTTP server on a
kernel-assigned ephemeral loopback-only port. Ord binds port zero itself and
reports the selected port, avoiding a close-and-rebind race. Wallet commands
use that loopback endpoint, which keeps the Ord index synchronized while
DogMode is running without exposing the server to the local network. The Ord
HTTP server has no application-layer authentication: loopback limits it to
local processes but is not an authentication boundary. Transaction construction
and signing remain confined to the pinned Ord CLI and dedicated `ord` wallet.

The page supports:

- an explicit fee rate in sats/vB;
- an optional destination address;
- Ord's optional Brotli content compression; and
- an Ord `--dry-run` cost preview that must match the exact file and settings;
- confirmation immediately before transaction creation and broadcast.

The interface runs the equivalent of:

```sh
ord \
  --chain <network> \
  --bitcoin-data-dir <bitcoin-data-directory> \
  --cookie-file <network-cookie> \
  --data-dir <ord-data-directory> \
  --index-runes --index-sats \
  wallet --server-url <loopback-ord-server> --name ord inscribe \
  --fee-rate <sats-per-vbyte> \
  --file <selected-file>
```

The destination and compression arguments are appended only when selected. A
preview copies the complete selected file into a private, immutable snapshot
and records its hash, fee, destination, and compression settings in a one-shot
manager authorization. Changing any input—or changing the original file
contents—requires a new preview before broadcast. Immediately before broadcast,
DogMode repeats the dry-run against the same snapshot and requires the mining
fee estimate to match. Ord then constructs the live transaction plan in a
separate CLI invocation, so a concurrent change to wallet state can still make
the live command fail or produce a different final fee. Review the returned
transaction result after broadcast. The preview reports Ord's mining-fee estimate,
the default 10,000-sat postage, and the wallet's available cardinal balance.
DogMode does not invoke a shell, and Ord process output is bounded to 1 MiB.
Symbolic links, device files, files larger than 10 MiB, and oversized image
previews are rejected. Fee rates are limited to 0.1–1,000 sats/vB.

## Important safety notes

Creating an inscription broadcasts Bitcoin transactions, pays mining fees, and
cannot be undone. Verify the selected file, its size, fee rate, destination, and
the backup for the `ord` wallet before confirming. Ord may reject content that
would produce non-standard transactions. DogMode does not enable Ord's
`--no-limit`, reinscription, parent, delegate, metadata, or rare-sat options in
this initial interface.

The managed index enables rune and sat indexing so that rune-bearing outputs
and rare sats are not misclassified as cardinal funding inputs. These indexes
increase initial synchronization time and disk use, especially on mainnet.
Ord 0.29.0 cannot retrofit these tables into an index created without them, so
DogMode uses the separate versioned `ord/data-runes-sats-v1` directory and
leaves any legacy `ord/data` index untouched. Upgrades require a new index
synchronization and temporarily require space for both indexes.

The compression checkbox enables Ord's lossless Brotli compression. It does not
resize images or change image quality. Image transcoding should be implemented
and reviewed separately so that the exact bytes being inscribed are always
clear to the user.
