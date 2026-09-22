$DOG Mode user agent
====================

P2P
---

- The user agent names $DOG Mode after Bitcoin Core's own component, for
  example `/Satoshi:31.1.0/DOGMode:20260902/`, the way Bitcoin Knots names
  itself. A peer, a DNS seed's crawler, `getpeerinfo` and `getnetworkinfo` can
  now tell a $DOG Mode node, and which release it runs, from plain Bitcoin
  Core. The Bitcoin Core component is unchanged, and `-uacomment` comments
  still attach to it.
