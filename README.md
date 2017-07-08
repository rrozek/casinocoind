![Casinocoin](/images/casinocoin.png)

# What is Casinocoin?
Casinocoin is an open source, peer-to-peer Internet currency specifically designed for online casino gaming. It is a network of computers which use the [Casinocoin consensus algorithm](https://www.youtube.com/watch?v=pj1QVb1vlC0) to atomically settle and record
transactions on a secure distributed database, the Casinocoin Consensus Ledger (CCL).
Because of its distributed nature, the CCL offers transaction immutability
without a central operator. The CCL contains a built-in currency exchange and its
path-finding algorithm finds competitive exchange rates across order books and currency pairs.

### Key Features
- **Distributed**
  - Direct account-to-account settlement with no central operator
  - Decentralized global market for competitive CSC exchange
- **Secure**
  - Transactions are cryptographically signed using ECDSA or Ed25519
  - Multi-signing capabilities
  - Master nodes are verified and certified by the Casinocoin Foundation
- **Scalable**
  - Capacity to process the world’s online casino transactions volume
  - Easy access to liquidity through a competitive CSC exchange

# casinocoind - Casinocoin server
`casinocoind` is the reference server implementation of the Casinocoin
protocol. To learn more about how to build and run a `casinocoind`
server, visit https://casinocoin.org/docs/

### License
`casinocoind` is open source and permissively licensed under the
ISC license. See the LICENSE file for more details.

#### Repository Contents

| Folder  | Contents |
|---------|----------|
| ./bin   | Scripts and data files for Ripple integrators. |
| ./build | Intermediate and final build outputs.          |
| ./Builds| Platform or IDE-specific project files.        |
| ./doc   | Documentation and example configuration files. |
| ./src   | Source code.                                   |

Some of the directories under `src` are external repositories inlined via
git-subtree. See the corresponding README for more details.

## For more information:

* [Casinocoin Website](http://www.casinocoin.org)
* [Casinocoin Developer Center](http://dev.casinocoin.org)

- - -

Copyright © 2017, Casinocoin Foundation. All rights reserved.

Portions of this document, including but not limited to the Casinocoin logo,
images and image templates are the property of the Casinocoin Foundation
and cannot be copied or used without permission.
