## Q: How does one usually perform basic regression testing locally with bitcoin core repo? What I did was to run `make check` - is that the general approach? What else can one do?

Short answer: `make check` is *one* important piece, but not the whole story. For “typical” local regression testing with Bitcoin Core, people generally use a stack of test layers:

1. **Unit + Integration tests**
2. **Functional (RPC/black-box) test suite**
3. **Fuzz + sanitizers (when you’re touching tricky logic)**
4. **Targeted custom tests for your changes**

Let’s go through them in a practical, engineer-to-engineer way.

---

## 1. `make check` — what it actually covers

You already did:

```bash
make check
```

In the Bitcoin Core tree, that typically runs:

* C++ **unit tests** (`src/test/test_bitcoin`)
* Some **wallet tests**, **script tests**, and other lower-level checks wired into the test harness
* On some configs, it may also trigger Python functional test stubs that are wired in via `check-functional` targets (depending on branch/Autotools vs CMake/etc).

This is absolutely part of the “default” workflow after non-trivial changes. But it won’t give you end-to-end coverage of node behavior.

**Rule of thumb:** after any consensus, mempool, or wallet logic change, `make check` is necessary but not sufficient.

---

## 2. Functional tests (Python, end-to-end via RPC)

These are the real workhorse for regression testing.

From the repo root, after building:

```bash
# Build (one-time, you probably already did this)
./autogen.sh
./configure --enable-debug
make -j$(nproc)

# Run all functional tests
test/functional/test_runner.py
```

Or for faster feedback, run only the tests related to your changes:

```bash
# Example: mempool / policy related tests
test/functional/test_runner.py mempool_limit.py p2p_tx_download.py

# Example: wallet-related
test/functional/test_runner.py wallet_basic.py wallet_hd.py

# You can pass multiple tests:
test/functional/test_runner.py feature_block.py feature_fee_estimation.py
```

This spins up small regtest networks, drives them via RPC, and asserts on chain / mempool / wallet behavior. This is where most regressions show up in practice.

**Typical local workflow after a change:**

1. `make check`
2. Run the most relevant `test/functional/*.py` tests.
3. If the change is intrusive or cross-cutting: run the full functional suite (or at least a large subset overnight / while you do something else).

---

## 3. Sanity + lint checks

If you’re doing something you might later upstream or compare with upstream, the usual stack is:

```bash
# Basic formatting / linting
contrib/devtools/lint-all.sh

# Or specific linters (depending on your tree and available tools)
contrib/devtools/clang-format-diff.py
contrib/devtools/symbol-check.py
contrib/devtools/bitcoin-util-test.py
```

These aren’t “regression tests” in the behavioral sense, but they *do* catch issues you’ll otherwise trip over later (bad symbols, broken util tools, etc.).

---

## 4. Fuzz tests (for tricky logic)

Bitcoin Core has a fuzzing harness under `src/test/fuzz/`. For non-trivial consensus/mempool/script changes, it’s reasonable to at least run the existing fuzz targets touching your area:

Build the fuzz binary (with appropriate sanitizer config):

```bash
./autogen.sh
./configure --enable-fuzz --with-sanitizers=address,undefined --disable-shared
make -j$(nproc)
```

Then something like:

```bash
FUZZ=tx_deserialize src/test/fuzz/fuzz
```

You can run various fuzz targets; for quick local regression checks, you don’t need to run them for hours – even short runs can catch obvious UB / ASan/UBSan issues.

---

## 5. Sanitizers on the full node / tests

When you suspect memory/UB landmines, build with sanitizers and re-run the usual tests:

```bash
./configure --enable-debug --with-sanitizers=address,undefined
make -j$(nproc)

# Then:
make check
test/functional/test_runner.py <targeted-tests>
```

This is heavier but really good at exposing subtle regressions.

---

## 6. Targeted manual regression checks

For changes touching the integration with your CTAM fork (mining, PoW tweaks, RPC extensions), a common pattern:

1. Spin up a **regtest** node with your options:

   ```bash
   bitcoind -regtest -printtoconsole -debug=1 -fallbackfee=0.0001
   ```
2. Use `bitcoin-cli` or a Python harness to:

   * Generate blocks (`generatetoaddress`)
   * Send transactions
   * Restart nodes, test reorgs, etc.

Once you have a bug fixed, **codify it as a new functional test** in `test/functional/` so you don’t reintroduce it later.

---

## Putting it together: a realistic “standard” loop

For a typical local change:

1. **Build (debug):**

   ```bash
   ./autogen.sh
   ./configure --enable-debug
   make -j$(nproc)
   ```

2. **Quick tests:**

   ```bash
   make check
   ```

3. **Targeted functional tests** related to what you touched:

   ```bash
   test/functional/test_runner.py <2–5 relevant tests>
   ```

4. If the change is wide-impact (consensus, block assembly, mining, script, or cross-module):

   * Run more of the functional suite.
   * Optionally re-build with sanitizers and rerun.

5. **For CTAM / PoW experimentation specifically:**

   * Add or modify a functional test that drives `/mine/start` / `/mine/submit` RPCs and verifies:

     * Node still reaches consensus on regtest.
     * Blocks are accepted / rejected as expected.
     * Reorg handling is sane.

