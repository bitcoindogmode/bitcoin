#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Exercise a pinned external ord binary against DogMode on regtest."""

import json
import os
from pathlib import Path
import subprocess
import urllib.parse

from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import assert_equal


class OrdIndexTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [["-txindex=1", "-server=1", "-rest=1"]]
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.ord = os.getenv("ORD")
        if not self.ord or not Path(self.ord).is_file():
            raise SkipTest("Set ORD to a pinned ord executable to run this extended test")

    def ord_command(self, *arguments):
        node = self.nodes[0]
        rpc_port = urllib.parse.urlparse(node.url).port
        command = [
            self.ord,
            "--chain", "regtest",
            "--bitcoin-data-dir", str(node.datadir_path),
            "--bitcoin-rpc-url", f"127.0.0.1:{rpc_port}",
            "--cookie-file", str(node.chain_path / ".cookie"),
            "--data-dir", str(self.ord_data_dir),
            *arguments,
        ]
        return subprocess.run(command, check=True, capture_output=True, text=True, timeout=120)

    def assert_ord_at_tip(self):
        output = self.ord_command("--format", "json", "index", "info")
        info = json.loads(output.stdout)
        # Ord counts the genesis block, while getblockcount reports its height.
        assert_equal(info["blocks_indexed"], self.nodes[0].getblockcount() + 1)

    def update_ord(self):
        self.ord_command("index", "update")
        self.assert_ord_at_tip()

    def run_test(self):
        node = self.nodes[0]
        self.ord_data_dir = Path(self.options.tmpdir) / "ord data with spaces"
        self.ord_data_dir.mkdir()

        self.wait_until(lambda: node.getindexinfo("txindex")["txindex"]["synced"])

        self.log.info("Index the initial regtest chain")
        self.generate(node, 12)
        self.update_ord()

        self.log.info("Resume after a new block and node restart")
        self.generate(node, 1)
        self.update_ord()
        self.restart_node(0, extra_args=self.extra_args[0])
        self.update_ord()

        self.log.info("Recover the Ord index after a one-block reorg")
        old_tip = node.getbestblockhash()
        old_tip_time = node.getblockheader(old_tip)["time"]
        node.invalidateblock(old_tip)
        # Ensure mining does not recreate the invalidated deterministic block.
        node.setmocktime(old_tip_time + 1)
        self.generate(node, 2)
        self.update_ord()


if __name__ == "__main__":
    OrdIndexTest(__file__).main()
