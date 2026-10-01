# Experimental Ord inscription interface

DogMode provides an experimental **Inscribe** page when Ord was enabled during
initial setup and the pinned Ord executable is installed. The page accepts one
local image or data file through drag and drop or a file picker, previews image
formats supported by Qt, and invokes the pinned Ord command-line interface.

The page detects the dedicated Bitcoin Core wallet named `ord`. It can create a
new wallet or restore one from BIP39 recovery words. New recovery words are
shown once in a non-dismissible dialog, and the user must confirm that they
were recorded before continuing. Restore words are passed to Ord over standard
input instead of process arguments. An optional BIP39 passphrase is supported;
Ord 0.29.0 accepts that passphrase only as a command-line option.

Once the wallet is available, the page displays its spendable cardinal balance,
total balance, and a funding address. The wallet selected in DogMode's toolbar
is not used to fund inscriptions.

After the initial one-shot index build, DogMode runs Ord's HTTP server on an
ephemeral loopback-only port. Wallet commands use that private endpoint, which
keeps the Ord index synchronized while DogMode is running without exposing the
server to the local network.

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
  wallet --server-url <loopback-ord-server> --name ord inscribe \
  --fee-rate <sats-per-vbyte> \
  --file <selected-file>
```

The destination and compression arguments are appended only when selected. A
preview hashes the complete selected file and records its fee, destination, and
compression settings. Changing any input—or changing the file contents—requires
a new preview before broadcast. The preview reports Ord's mining-fee estimate,
the default 10,000-sat postage, and the wallet's available cardinal balance.
DogMode does not invoke a shell, and Ord process output is bounded to 1 MiB.

## Important safety notes

Creating an inscription broadcasts Bitcoin transactions, pays mining fees, and
cannot be undone. Verify the selected file, its size, fee rate, destination, and
the backup for the `ord` wallet before confirming. Ord may reject content that
would produce non-standard transactions. DogMode does not enable Ord's
`--no-limit`, reinscription, parent, delegate, metadata, or rare-sat options in
this initial interface.

The compression checkbox enables Ord's lossless Brotli compression. It does not
resize images or change image quality. Image transcoding should be implemented
and reviewed separately so that the exact bytes being inscribed are always
clear to the user.
