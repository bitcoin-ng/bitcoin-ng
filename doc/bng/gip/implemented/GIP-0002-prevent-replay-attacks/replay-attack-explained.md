A replay attack is when a valid transaction you signed on **Chain A** is also valid on **Chain B**, so someone can “replay” it and make you accidentally pay on both chains.

A **forkid** protects a hardforked coin by making signatures (the authorization on the transaction) **chain-specific**. After the fork, a signature created for the new coin’s rules **will not verify on Bitcoin**, and a Bitcoin signature **will not verify on the fork**—so the same raw transaction can’t be accepted on both.

Below is a slow, concrete Alice/Bob story.

---

## Setup

* **Before fork (Day 0):** There’s one chain. Alice has 1 BTC in a UTXO.
* **Fork moment (Day 1):** The chain splits into:

  * **Bitcoin (BTC chain)**
  * **NewCoin (NC chain)**

Because both chains share history up to Day 1, Alice now effectively has:

* **1 BTC** on BTC chain
* **1 NC** on NC chain
  (same UTXO history, duplicated after the split)

---

## What goes wrong without replay protection

### Step 1 — Alice pays Bob on the NewCoin chain

Alice wants to buy something from Bob using **NC**.

She creates a transaction:

* Input: her old UTXO (from pre-fork history)
* Output: Bob’s address
* Signs it with her key (normal ECDSA/Schnorr, but **no fork-specific domain separation**)

She broadcasts this transaction on **NC**.

Bob sees it confirmed on NC. Great.

### Step 2 — Mallory replays it on Bitcoin

Mallory (any observer) copies the exact raw transaction bytes from NC and broadcasts them on **BTC**.

If the transaction format + signature rules are the same, then on BTC:

* The referenced input UTXO also exists (because history was shared)
* The signature validates (because it’s the same message/signature scheme)
* Miners accept it

Result:

* Alice accidentally paid Bob on BTC too.

Bob might even be honest and surprised — but the damage is real: **Alice spent on both chains**.

---

## How forkid stops this

The fork introduces a **ForkID** into what gets signed, so the signature commits to:

* the transaction data **and**
* a **chain identifier / signing mode**

This is usually done by changing the signature hash algorithm (“what message is being signed”).

### Intuition

Think of the signature as signing not just:

> “Pay Bob 1 coin”

but:

> “Pay Bob 1 coin **on NewCoin (ForkID=XYZ)**”

So if you try to verify that signature on Bitcoin, Bitcoin is effectively checking:

> “Did Alice sign ‘Pay Bob 1 coin **on Bitcoin (ForkID=BTC)**’ ?”

And the answer is **no**, because the bytes being verified are different.

---

## Slow illustration with Alice & Bob (with forkid)

### Step 1 — Alice pays Bob on NewCoin

Alice constructs the same spend, but when she signs, NewCoin’s rules compute the signature hash like:

* `msg = H( tx_data || forkid = 0x1234 )`

She produces signature `sig_NC` over that message.

She broadcasts transaction `(tx_data, sig_NC)` on **NC**.

NC nodes validate by recomputing:

* `H( tx_data || 0x1234 )`
* check signature ✅
  Transaction confirms.

### Step 2 — Mallory tries to replay on Bitcoin

Mallory takes the same bytes `(tx_data, sig_NC)` and broadcasts on **BTC**.

But BTC nodes verify using Bitcoin’s signature hash rule:

* `msg = H( tx_data || forkid = BTC_default )`
  (or simply no forkid / different constant / different sighash type—either way **different bytes**)

Now BTC checks whether `sig_NC` is a valid signature for:

* `H( tx_data || BTC_id )`

It is not.

So BTC nodes reject it with “invalid signature”.

Result:

* Alice’s payment happened **only on NC**
* Replay fails **at validation**, not by “policy” or “miners being nice”

---

## Why this is robust

* It does **not** rely on wallets “being careful”
* It does **not** rely on different addresses
* It is enforced by consensus: blocks containing replayed transactions become invalid on the other chain

---

## One subtlety (important in practice)

ForkID protects transactions that use the fork’s new signature hashing mode.

But you still need to ensure:

* the fork actually *requires* that mode for typical spends (so users can’t accidentally create “old-style” signatures that might still replay), or
* wallets always sign using the forkid mode.

Bitcoin Cash’s approach (historically) was to introduce a new sighash with forkid and have post-fork wallets use it, which provides strong replay protection when used.

