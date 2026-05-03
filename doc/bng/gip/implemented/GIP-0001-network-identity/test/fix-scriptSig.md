
### What **scriptSig** is (in precise Bitcoin terms)

In Bitcoin, **`scriptSig`** is the **unlocking script** attached to each transaction **input**.
It provides the data needed to satisfy (unlock) the **locking script** (`scriptPubKey`) of the output being spent.

Formally:

```
scriptSig + scriptPubKey  →  executed together by the script engine
```

If execution ends with `true`, the input is valid.

---

### Why the **genesis coinbase** is special

The **genesis block’s coinbase transaction** is **not a normal spend**:

* It does **not** spend a previous UTXO
* Its `scriptSig` is **arbitrary data**
* It is historically used to embed a **timestamp / message**

Example from Bitcoin mainnet:

```
"The Times 03/Jan/2009 Chancellor on brink of second bailout for banks"
```

This text lives **inside the coinbase `scriptSig`**, not the output.

---

### The consensus rule you’re hitting

In Bitcoin Core, `CheckTransaction()` enforces this rule:

```cpp
// Coinbase scriptSig must be between 2 and 100 bytes
if (tx.IsCoinBase())
    if (tx.vin[0].scriptSig.size() < 2 ||
        tx.vin[0].scriptSig.size() > 100)
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
```

So:

| Condition     | Result    |
| ------------- | --------- |
| `< 2 bytes`   | ❌ invalid |
| `> 100 bytes` | ❌ invalid |
| `2–100 bytes` | ✅ valid   |

This applies **even to genesis**.

---

### Why your **2026 timestamp** can break it

The problem is **not the date itself**, but **how it’s encoded**.

Genesis `scriptSig` is usually built like this:

```
[push difficulty bits] [push timestamp message]
```

In C++ terms (simplified):

```cpp
scriptSig << 486604799 << CScriptNum(4)
          << std::vector<unsigned char>(pszTimestamp.begin(), pszTimestamp.end());
```

If your **GIP-0001 message** is long (e.g. verbose manifesto text, URL, UUID, etc.), then:

* `pszTimestamp.size()` grows
* Total `scriptSig.size()` exceeds **100 bytes**
* Genesis fails **before chainparams even load**

⚠️ The block timestamp (`nTime = 2026-01-23`) is irrelevant here —
only the **embedded coinbase message length** matters.

---

### Why this surfaced after changing `chainparams.cpp`

When you fork Bitcoin Core:

* Genesis block is **hard-coded**
* Any modification to:

  * timestamp text
  * difficulty bits
  * coinbase construction
* can silently violate consensus rules

Bitcoin Core **does not special-case genesis** for script validation.

---

### How to fix it (clean + consensus-safe)

#### ✅ 1. Keep the coinbase message short

**Target:** ≤ 80 bytes to stay safe

Example:

```
"BNG 23/Jan/2026 GIP-0001"
```

Instead of:

```
"Bitcoin-NG Genesis Block implementing GIP-0001 network identity changes..."
```

---

#### ✅ 2. Verify length explicitly

Add a quick check while debugging:

```cpp
assert(genesis.vtx[0]->vin[0].scriptSig.size() <= 100);
```

Or log it:

```cpp
LogPrintf("Genesis scriptSig size = %u\n",
          genesis.vtx[0]->vin[0].scriptSig.size());
```

---

#### ❌ 3. Do NOT relax the rule

Do **not** change `CheckTransaction()` to allow longer coinbase scripts:

* You will fork **consensus**
* You will diverge from every Bitcoin-derived tool
* Wallets and explorers may reject your chain

---

### Mental model (useful going forward)

* `scriptSig` is **data + logic**, not just “a signature”
* Genesis coinbase is **arbitrary data**, but still **bounded**
* If consensus rules feel arbitrary → they probably encode a **DoS protection**

---

### TL;DR

* `scriptSig` = unlocking script attached to each transaction input
* Genesis coinbase uses it to embed a **message**
* Bitcoin enforces **2–100 bytes**, even for genesis
* Your GIP-0001 timestamp text is almost certainly **too long**
* Shorten the message — don’t touch consensus rules

If you want, next we can:

* sanity-check your **exact genesis construction**
* review a **safe BNG genesis template**
* or discuss **embedding versioning without using scriptSig** (cleaner long-term)
