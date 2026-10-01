#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Exercise a pinned external ord binary against DogMode on regtest."""

import json
import os
from pathlib import Path
import socket
import subprocess
import urllib.parse
import urllib.request
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import assert_equal


class OrdIndexTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [["-txindex=1", "-server=1", "-rest=1"]]
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.ord = os.getenv("ORD")
        if not self.ord or not Path(self.ord).is_file():
            raise SkipTest("Set ORD to a pinned ord executable to run this extended test")

    def ord_command(self, *arguments, input_text=None):
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
        return subprocess.run(command, check=True, capture_output=True, text=True, input=input_text, timeout=120)

    def assert_ord_at_tip(self):
        output = self.ord_command("--format", "json", "index", "info")
        info = json.loads(output.stdout)
        # Ord counts the genesis block, while getblockcount reports its height.
        assert_equal(info["blocks_indexed"], self.nodes[0].getblockcount() + 1)

    def update_ord(self):
        self.ord_command("index", "update")
        self.assert_ord_at_tip()

    def start_ord_server(self):
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        node = self.nodes[0]
        rpc_port = urllib.parse.urlparse(node.url).port
        command = [
            self.ord,
            "--chain", "regtest",
            "--bitcoin-data-dir", str(node.datadir_path),
            "--bitcoin-rpc-url", f"127.0.0.1:{rpc_port}",
            "--cookie-file", str(node.chain_path / ".cookie"),
            "--data-dir", str(self.ord_data_dir),
            "server", "--address", "127.0.0.1", "--http-port", str(port),
        ]
        server = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        def listening():
            if server.poll() is not None:
                raise AssertionError(f"Ord server exited with code {server.returncode}")
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                    return True
            except OSError:
                return False

        self.wait_until(listening, timeout=10)
        return server, f"http://127.0.0.1:{port}"

    def wallet_command(self, server_url, *arguments, input_text=None):
        return self.ord_command("wallet", "--server-url", server_url, *arguments, input_text=input_text)

    def wait_for_server_tip(self, server_url):
        # Ord reports a block count including genesis, while bitcoind reports height.
        expected = self.nodes[0].getblockcount() + 1

        def synced():
            try:
                with urllib.request.urlopen(f"{server_url}/blockcount", timeout=1) as response:
                    return json.loads(response.read()) == expected
            except (OSError, ValueError):
                return False

        self.wait_until(synced, timeout=30)

    def run_test(self):
        node = self.nodes[0]
        self.ord_data_dir = Path(self.options.tmpdir) / "ord data with spaces"
        self.ord_data_dir.mkdir()

        self.wait_until(lambda: node.getindexinfo("txindex")["txindex"]["synced"])

        self.log.info("Index the initial regtest chain")
        self.generate(node, 101)
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

        self.log.info("Create, restore, fund, preview, and use an Ord wallet through the local server")
        server, server_url = self.start_ord_server()
        try:
            created = json.loads(self.wallet_command(server_url, "--name", "ord", "create").stdout)
            mnemonic = created["mnemonic"]
            self.wallet_command(
                server_url, "--name", "restored", "restore", "--from", "mnemonic", "--timestamp", "now",
                input_text=f"{mnemonic}\n",
            )
            restored_balance = json.loads(self.wallet_command(server_url, "--name", "restored", "balance").stdout)
            assert_equal(restored_balance["total"], 0)

            receive = json.loads(self.wallet_command(server_url, "--name", "ord", "receive").stdout)
            funding_address = receive["addresses"][0]
            funding_wallet = node.get_wallet_rpc(self.default_wallet_name)
            funding_wallet.sendtoaddress(funding_address, Decimal("0.001"))
            self.generate(node, 1)
            self.wait_for_server_tip(server_url)
            balance = json.loads(self.wallet_command(server_url, "--name", "ord", "balance").stdout)
            assert_equal(balance["cardinal"], 100_000)

            inscription_file = Path(self.options.tmpdir) / "dogmode inscription.txt"
            inscription_file.write_text("DogMode Ord integration test\n", encoding="utf-8")
            preview = json.loads(self.wallet_command(
                server_url, "--name", "ord", "inscribe", "--fee-rate", "1", "--file", str(inscription_file), "--compress", "--dry-run",
            ).stdout)
            assert preview["total_fees"] > 0
            assert_equal(preview["reveal_broadcast"], False)

            inscription = json.loads(self.wallet_command(
                server_url, "--name", "ord", "inscribe", "--fee-rate", "1", "--file", str(inscription_file), "--compress",
            ).stdout)
            assert_equal(inscription["reveal_broadcast"], True)
            assert inscription["total_fees"] > 0
        finally:
            server.terminate()
            server.wait(timeout=10)


if __name__ == "__main__":
    OrdIndexTest(__file__).main()
