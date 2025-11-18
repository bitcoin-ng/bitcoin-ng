To fork Bitcoin and specifically know which C++ (`.cpp`) files to change for customizations, focus primarily on the following technical components in the Bitcoin Core repository:

### Key Files to Modify

- **src/chainparams.cpp**: This file defines the blockchain’s parameters, such as the genesis block, network ID, initial message, block rewards, difficulty, and timestamp. You’ll need to modify this to set your new coin's genesis block, main parameters, and possibly network prefixes.[1]
- **src/chainparams.h**: This is the header file for chain parameters. Any parameter changes reflected in `chainparams.cpp` should be included or declared here.[1]
- **src/consensus/**: This folder contains files (`consensus.h`, `validation.cpp`, etc.) for block and transaction validation rules. Change these for different block sizes, transaction rules, or consensus updates.[1]
- **src/main.cpp** (in older versions) or **src/validation.cpp** (in newer versions): Handles the core validation, mining, and transaction process. Most forking operations will require changes here if you're adjusting consensus or behavior deeply.[1]
- **src/pow.cpp**: Code related to the proof-of-work algorithm; modify if you’re changing the mining algorithm or block finding process.[1]
- **src/net.cpp** and **src/protocol.cpp**: If you want to change networking or protocol messaging for nodes, these files are relevant.[2]
- **src/wallet/**: If you plan to customize or adjust the wallet implementation, changes go in this directory.[2]

### Key Steps and Where to Make Them

- Change the genesis block creation and its parameters in `chainparams.cpp` (timestamp, nonce, difficulty, initial script key).[1]
- Update network magic numbers (the values that distinguish your blockchain's network traffic) in `chainparams.cpp` and possibly in `protocol.cpp`.[2][1]
- Adjust reward systems, address formats, and prefixes in `chainparams.cpp`.[1]
- For new features or consensus, edit logic in the `consensus/` folder, and for core transaction/mining changes, use `validation.cpp`.[1]
- Update CMake and build scripts if you add new files or significantly restructure the codebase.[1]

### Extra Help and Live Example

There’s a recent video tutorial from August 2025 that shows a live walk-through on modifying the Bitcoin C++ codebase, building, testing in regtest mode, and where/how to add your own commands. The associated code example repo shows changes in real time, which can be very illustrative for practitioners.[3]

### Additional Tips

- Test modifications extensively in regtest/testnet before deploying live.
- Keep in mind that changing the genesis block (timestamp, nonce, script key) is one of the first steps for a true fork.[1]
- Carefully review all references to network parameters, especially if changing address formats or protocol versions.[2]

This covers the core technical guide for which `.cpp` files to change when forking Bitcoin in 2025, with examples directly linked to genesis block issues, network rules, and consensus alterations.[3][2][1]

[1](https://stackoverflow.com/questions/77184010/upgrading-from-bitcoin-0-14-3-to-bitcoin-0-25-i-have-a-problem-with-a-fork-and)
[2](http://bitcoinwiki.org/wiki/creating-forks)
[3](https://www.youtube.com/watch?v=C_8NjozwgL4)
[4](https://www.reddit.com/r/Bitcoin/comments/1o7sjhj/tftc_the_bitcoin_code_change_coming_in_2025_what/)
[5](https://99bitcoins.com/cryptocurrency/bitcoin/bitcoin-forks/)
[6](https://www.youtube.com/watch?v=XEoFOMb1MVo)
[7](https://en.bitcoin.it/wiki/Bitcoin_Core_0.11_(ch_1):_Overview)
[8](https://blog.mexc.com/wiki/how-to-fork-bitcoin/)
[9](https://github.com/bitcoin/bitcoin/commit/8c9479c6bbbc38b897dc97de9d04e4d5a5a36730)
[10](https://github.com/fletelli42/SimpleBlockchainImplementation)