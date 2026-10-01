#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Exercise a pinned external ord binary against DogMode on regtest."""

import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
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
        for name in ("NO_PROXY", "no_proxy"):
            entries = [entry for entry in os.environ.get(name, "").split(",") if entry]
            for host in ("127.0.0.1", "localhost"):
                if host not in entries:
                    entries.append(host)
            os.environ[name] = ",".join(entries)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.ord = os.getenv("ORD")
        if not self.ord or not Path(self.ord).is_file():
            raise SkipTest("Set ORD to a pinned ord executable to run this extended test")

    def ord_command(self, *arguments, input_text=None, data_dir=None, index_assets=True, check=True):
        node = self.nodes[0]
        rpc_port = urllib.parse.urlparse(node.url).port
        command = [
            self.ord,
            "--chain", "regtest",
            "--bitcoin-data-dir", str(node.datadir_path),
            "--bitcoin-rpc-url", f"127.0.0.1:{rpc_port}",
            "--cookie-file", str(node.chain_path / ".cookie"),
            "--data-dir", str(data_dir or self.ord_data_dir),
        ]
        if index_assets:
            command.extend(["--index-runes", "--index-sats"])
        command.extend(arguments)
        return subprocess.run(command, check=check, capture_output=True, text=True, input=input_text, timeout=120)

    def assert_ord_at_tip(self):
        output = self.ord_command("--format", "json", "index", "info")
        info = json.loads(output.stdout)
        # Ord counts the genesis block, while getblockcount reports its height.
        assert_equal(info["blocks_indexed"], self.nodes[0].getblockcount() + 1)

    def update_ord(self):
        self.ord_command("index", "update")
        self.assert_ord_at_tip()

    def start_ord_server(self):
        node = self.nodes[0]
        rpc_port = urllib.parse.urlparse(node.url).port
        command = [
            self.ord,
            "--chain", "regtest",
            "--bitcoin-data-dir", str(node.datadir_path),
            "--bitcoin-rpc-url", f"127.0.0.1:{rpc_port}",
            "--cookie-file", str(node.chain_path / ".cookie"),
            "--data-dir", str(self.ord_data_dir),
            "--index-runes",
            "--index-sats",
            "server", "--address", "127.0.0.1", "--http-port", "0",
        ]
        server_log = tempfile.TemporaryFile(mode="w+")
        server = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=server_log, text=True)

        deadline = time.monotonic() + 10
        port = None
        log_position = 0
        startup_output = ""
        while time.monotonic() < deadline:
            if server.poll() is not None:
                server_log.close()
                raise AssertionError(f"Ord server exited with code {server.returncode}")
            server_log.seek(log_position)
            startup_output = (startup_output + server_log.read())[-4096:]
            log_position = server_log.tell()
            match = re.search(r"Listening on http://127\.0\.0\.1:([0-9]{1,5})", startup_output)
            if match:
                port = int(match.group(1))
            if port:
                break
            time.sleep(0.1)
        if not port or port > 65535:
            server.terminate()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=10)
            server_log.close()
            raise AssertionError("Ord server did not report a valid kernel-assigned loopback port")

        server_url = f"http://127.0.0.1:{port}"
        self.log.info(f"Ord server selected {server_url}")
        self.wait_for_server_tip(server_url)
        return server, server_log, server_url

    def wallet_command(self, server_url, *arguments, input_text=None):
        return self.ord_command("wallet", "--server-url", server_url, *arguments, input_text=input_text)

    def wait_for_server_tip(self, server_url):
        # Ord reports a block count including genesis, while bitcoind reports height.
        expected = self.nodes[0].getblockcount() + 1
        last_response = None
        last_error = None

        def synced():
            nonlocal last_response, last_error
            try:
                with urllib.request.urlopen(f"{server_url}/blockcount", timeout=1) as response:
                    last_response = response.read()
                    last_error = None
                    return json.loads(last_response) == expected
            except (OSError, ValueError) as error:
                last_error = repr(error)
                return False

        try:
            self.wait_until(synced, timeout=30)
        except AssertionError as error:
            raise AssertionError(f"{error}; last response={last_response!r}; last error={last_error}") from error

    def run_test(self):
        node = self.nodes[0]
        self.ord_data_dir = Path(self.options.tmpdir) / "ord data with spaces"
        self.ord_data_dir.mkdir()

        self.wait_until(lambda: node.getindexinfo("txindex")["txindex"]["synced"])

        self.log.info("Index the initial regtest chain")
        self.generate(node, 101)

        self.log.info("Verify an existing inscription-only index cannot be retrofitted with rune and sat indexes")
        legacy_data_dir = Path(self.options.tmpdir) / "legacy ord data"
        legacy_data_dir.mkdir()
        self.ord_command("index", "update", data_dir=legacy_data_dir, index_assets=False)
        self.ord_command("index", "update", data_dir=legacy_data_dir)
        legacy_find = self.ord_command("find", "0", data_dir=legacy_data_dir, check=False)
        assert_equal(legacy_find.returncode, 1)
        assert "requires index created with `--index-sats` flag" in legacy_find.stderr

        self.update_ord()
        self.ord_command("find", "0")
        self.ord_command("runes")

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
        server, server_log, server_url = self.start_ord_server()
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
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=10)
            server_log.close()


if __name__ == "__main__":
    OrdIndexTest(__file__).main()
