# Experimental Ord inscription interface

DogMode provides an experimental **Inscribe** page when Ord was enabled during
initial setup and the pinned Ord executable is installed. The page accepts one
local image or data file through drag and drop or a file picker, previews image
formats supported by Qt, and invokes the pinned Ord command-line interface.

Before using the page, create, back up, and fund the dedicated Bitcoin Core
wallet named `ord`. This first interface intentionally does not create a wallet
or display recovery words. The wallet selected in DogMode's toolbar is not used
to fund inscriptions.

The page supports:

- an explicit fee rate in sats/vB;
- an optional destination address;
- Ord's optional Brotli content compression; and
- confirmation immediately before transaction creation and broadcast.

The interface runs the equivalent of:

```sh
ord \
  --chain <network> \
  --bitcoin-data-dir <bitcoin-data-directory> \
  --cookie-file <network-cookie> \
  --data-dir <ord-data-directory> \
  wallet --name ord inscribe \
  --fee-rate <sats-per-vbyte> \
  --file <selected-file>
```

The destination and compression arguments are appended only when selected.
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
