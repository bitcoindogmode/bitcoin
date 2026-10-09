Optional Ord full-node setup
============================

The first-run data-directory dialog now offers an opt-in option to install and
synchronize Ord. Enabling it configures an unpruned node with `txindex`, RPC
server, and REST support, directs the user to the pinned official Ord release,
verifies the selected archive's checksum and version, and builds the Ord index
after Bitcoin synchronization. Ord remains a command-line tool in this release.
See `doc/ord.md` for storage, security, and terminal-use details.
