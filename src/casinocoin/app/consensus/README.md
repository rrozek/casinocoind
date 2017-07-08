# CCL Consensus 

This directory holds the types and classes needed
to connect the generic consensus algorithm to the
casinocoind-specific instance of consensus.

  * `CCLCxTx` adapts a `SHAMapItem` transaction.
  * `CCLCxTxSet` adapts a `SHAMap` to represent a set of transactions.
  * `CCLCxLedger` adapts a `Ledger`.
  * `CCLConsensus` is implements the requirements of the generic 
    `Consensus` class by connecting to the rest of the `casinocoind`
    application. 

