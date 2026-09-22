#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that empty $DOG Mode slots do not starve feelers or network-specific connections.

The automatic connection loop chooses one connection type per pass. On a network with few
$DOG Mode nodes the "dog" slots can stay empty for hours, so the "dog" choice must not take the
passes that are due to a feeler or to an extra network-specific connection.

With every full-relay and block-relay slot filled, no address advertising NODE_DOG_MODE in
addrman, and the clock moved past every connection timer, the node must still open a feeler and
a network-specific connection. Once it learns an address advertising NODE_DOG_MODE, it must then
fill a "dog" slot on its own.

Automatic connections go through a SOCKS5 proxy, so the test never reaches the real network:
each attempt is read from the debug log, and only the address advertising NODE_DOG_MODE is
redirected, to a Python peer.
"""
import time

from test_framework.messages import (
    CAddress,
    CBlockHeader,
    NODE_DOG_MODE,
    from_hex,
    msg_addrv2,
    msg_headers,
)
from test_framework.p2p import (
    P2PInterface,
    P2P_SERVICES,
    start_p2p_listener,
)
from test_framework.socks5 import start_socks5_server
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

# See net.h
MAX_OUTBOUND_FULL_RELAY_CONNECTIONS = 8
MAX_BLOCK_RELAY_ONLY_CONNECTIONS = 2

# Routable addresses that do not advertise NODE_DOG_MODE. The proxy serves no connection to them.
PLAIN_ADDRS = ["1.2.3.4", "2.3.4.5", "3.4.5.6", "4.5.6.7"]
ONION_ADDR = "pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion"
# The one address advertising NODE_DOG_MODE, learned from a peer's addrv2 message.
DOG_ADDR = "5.6.7.8"
REGTEST_PORT = 18444

# Each step stays under the 30 minute stale tip threshold (3 regtest block intervals), so the
# node never seeks an extra outbound peer, which would take priority over every choice tested
# here. Five steps put the clock more than 2 hours past start: every exponential connection
# timer (at most a 5 minute mean) is due.
CLOCK_STEP = 25 * 60
CLOCK_STEPS = 5


class DogModeFeelersTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        # The connection loop under test runs only with automatic connections on.
        self.disable_autoconnect = False

    def setup_nodes(self):
        self.dog_listener_addr = None

        def destinations_factory(requested_to_addr, requested_to_port):
            if requested_to_addr == "127.0.0.1":
                # Connections the test itself opens with addconnection.
                return {"actual_to_addr": requested_to_addr, "actual_to_port": requested_to_port}
            if requested_to_addr == DOG_ADDR and self.dog_listener_addr is not None:
                return {"actual_to_addr": self.dog_listener_addr[0], "actual_to_port": self.dog_listener_addr[1]}
            # Refused. The attempt is still in the debug log.
            return None

        self.socks5_server = start_socks5_server(destinations_factory)
        self.extra_args = [[
            f"-proxy={self.socks5_server.conf.addr[0]}:{self.socks5_server.conf.addr[1]}",
            "-v2transport=0",
        ]]
        super().setup_nodes()

    def announce_tip(self, node, peers):
        """Have every outbound peer announce our tip, so none is evicted as behind while the clock moves."""
        header = from_hex(CBlockHeader(), node.getblockheader(node.getbestblockhash(), False))
        for peer in peers:
            peer.send_and_ping(msg_headers([header]))

    def run_test(self):
        node = self.nodes[0]
        self.mocktime = int(time.time())
        node.setmocktime(self.mocktime)

        self.log.info("Fill every full-relay and block-relay slot, so the loop reaches the dog, feeler and network-specific choices")
        peers = []
        for i in range(MAX_OUTBOUND_FULL_RELAY_CONNECTIONS):
            peers.append(node.add_outbound_p2p_connection(P2PInterface(), p2p_idx=i, connection_type="outbound-full-relay"))
        for i in range(MAX_OUTBOUND_FULL_RELAY_CONNECTIONS, MAX_OUTBOUND_FULL_RELAY_CONNECTIONS + MAX_BLOCK_RELAY_ONLY_CONNECTIONS):
            peers.append(node.add_outbound_p2p_connection(P2PInterface(), p2p_idx=i, connection_type="block-relay-only"))
        self.generate(node, 1, sync_fun=self.no_op)
        self.announce_tip(node, peers)

        self.log.info("Put addresses without NODE_DOG_MODE in addrman, on IPv4 and on onion")
        for addr in PLAIN_ADDRS + [ONION_ADDR]:
            assert_equal(node.addpeeraddress(address=addr, port=REGTEST_PORT)["success"], True)

        self.log.info("With every dog slot empty and nothing to fill them, a feeler and a network-specific connection are still made")
        with node.assert_debug_log(expected_msgs=["connection (feeler) to", "Making network specific connection to"],
                                   unexpected_msgs=["connection (dog) to", "Potential stale tip detected"], timeout=60):
            for _ in range(CLOCK_STEPS):
                self.mocktime += CLOCK_STEP
                node.setmocktime(self.mocktime)
                self.generate(node, 1, sync_fun=self.no_op)
                self.announce_tip(node, peers)
        # The slots stayed full throughout. (A count from getpeerinfo would race the refused attempts:
        # the test proxy reports success before it closes a connection it will not serve.)
        assert all(peer.is_connected for peer in peers)

        self.log.info("Once an address advertising NODE_DOG_MODE is learned, a dog slot fills on its own")
        dog_peer = P2PInterface()
        dog_peer.peer_connect_helper(dstaddr="0.0.0.0", dstport=0, net=self.chain, timeout_factor=self.options.timeout_factor)
        dog_peer.peer_connect_send_version(services=P2P_SERVICES | NODE_DOG_MODE)
        self.dog_listener_addr = start_p2p_listener(self.network_thread, dog_peer)

        addr = CAddress()
        addr.time = self.mocktime
        addr.nServices = P2P_SERVICES | NODE_DOG_MODE
        addr.ip = DOG_ADDR
        addr.port = REGTEST_PORT
        msg = msg_addrv2()
        msg.addrs = [addr]
        with node.assert_debug_log(expected_msgs=[f"connection (dog) to {DOG_ADDR}:{REGTEST_PORT}"], timeout=60):
            node.add_p2p_connection(P2PInterface()).send_and_ping(msg)
            self.wait_until(lambda: any(p["connection_type"] == "dog" and p["addr"] == f"{DOG_ADDR}:{REGTEST_PORT}"
                                        for p in node.getpeerinfo()))
        dog_peer.wait_for_verack()


if __name__ == '__main__':
    DogModeFeelersTest(__file__).main()
