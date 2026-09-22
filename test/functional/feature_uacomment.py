#!/usr/bin/env python3
# Copyright (c) 2017-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the -uacomment option, and the $DOG Mode component of the user agent."""

import re

from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import ErrorMatch
from test_framework.util import assert_equal


class UacommentTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        self.log.info("test multiple -uacomment, on the Bitcoin Core component, followed by the $DOG Mode one")
        test_uacomment = self.nodes[0].getnetworkinfo()["subversion"]
        assert re.fullmatch(r"/Satoshi:\d+\.\d+\.\d+\(testnode0\)/DOGMode:[^/:()]+/", test_uacomment), test_uacomment

        self.restart_node(0, ["-uacomment=foo"])
        foo_uacomment = self.nodes[0].getnetworkinfo()["subversion"]
        assert re.fullmatch(r"/Satoshi:\d+\.\d+\.\d+\(testnode0; foo\)/DOGMode:[^/:()]+/", foo_uacomment), foo_uacomment

        self.log.info("test peers receive the same user agent")
        peer = self.nodes[0].add_p2p_connection(P2PInterface())
        assert_equal(peer.last_message["version"].strSubVer, foo_uacomment)

        self.log.info("test -uacomment max length")
        self.stop_node(0)
        expected = r"Error: Total length of network version string \([0-9]+\) exceeds maximum length \(256\). Reduce the number or size of uacomments."
        self.nodes[0].assert_start_raises_init_error(["-uacomment=" + 'a' * 256], expected, match=ErrorMatch.FULL_REGEX)

        self.log.info("test -uacomment unsafe characters")
        for unsafe_char in ['/', ':', '(', ')', '₿', '🏃']:
            expected = r"Error: User Agent comment \(" + re.escape(unsafe_char) + r"\) contains unsafe characters."
            self.nodes[0].assert_start_raises_init_error(["-uacomment=" + unsafe_char], expected, match=ErrorMatch.FULL_REGEX)


if __name__ == '__main__':
    UacommentTest(__file__).main()
