import struct
import time
import hashlib
import binascii

def sha256(data):
    return hashlib.sha256(data).digest()

def sha256d(data):
    return sha256(sha256(data))

def compact_to_target(bits):
    exponent = bits >> 24
    coefficient = bits & 0xffffff
    return coefficient * 2**(8 * (exponent - 3))

def generate_genesis(pszTimestamp, nTime, nNonce, nBits, nVersion, genesisReward):
    # const CScript genesisOutputScript = CScript() << "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f"_hex << OP_CHECKSIG;
    # OP_CHECKSIG is 0xac
    # Push 65 bytes (0x41) + key
    pubkey_hex = "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f"
    pubkey_bytes = binascii.unhexlify(pubkey_hex)
    script_pub_key = b'\x41' + pubkey_bytes + b'\xac'

    # scriptSig construction
    # CScript() << 486604799 << CScriptNum(4) << vector(timestamp)
    # 486604799 = 0x1d00ffff
    # Push 4 bytes (0x04) + 0xffff001d (LE)
    # Push 1 byte (0x01) + 0x04
    # Push len(timestamp) + timestamp
    
    script_sig = b'\x04\xff\xff\x00\x1d\x01\x04'
    if len(pszTimestamp) < 76:
        script_sig += bytes([len(pszTimestamp)]) + pszTimestamp.encode('ascii')
    else:
        # Handle larger (not needed here)
        raise ValueError("Timestamp too long")

    # Coinbase Transaction
    # Version (1) - 4 bytes LE
    # Vin count (1) - VarInt
    # Vin[0]:
    #   PrevOutHash (32 zero bytes)
    #   PrevOutIndex (0xffffffff)
    #   ScriptSigLen (VarInt)
    #   ScriptSig
    #   Sequence (0xffffffff)
    # Vout count (1) - VarInt
    # Vout[0]:
    #   Value (50 BTC) - 8 bytes LE
    #   ScriptPubKeyLen (VarInt)
    #   ScriptPubKey
    # LockTime (0) - 4 bytes LE

    tx_ver = struct.pack('<I', 1)
    
    vin_prev_hash = b'\x00' * 32
    vin_prev_idx = struct.pack('<I', 0xffffffff)
    vin_seq = struct.pack('<I', 0xffffffff)
    
    script_sig_len = bytes([len(script_sig)]) # Assuming < 253
    
    vout_value = struct.pack('<Q', genesisReward)
    script_pub_key_len = bytes([len(script_pub_key)]) # Assuming < 253
    
    tx_parts = [
        tx_ver,
        b'\x01', # vin count
        vin_prev_hash,
        vin_prev_idx,
        script_sig_len,
        script_sig,
        vin_seq,
        b'\x01', # vout count
        vout_value,
        script_pub_key_len,
        script_pub_key,
        struct.pack('<I', 0) # LockTime
    ]
    
    tx_bytes = b''.join(tx_parts)
    tx_hash = sha256d(tx_bytes)
    
    merkle_root = tx_hash # Single tx, so merkle root is the tx hash
    
    # Block Header
    # Version (4 bytes LE)
    # PrevBlock (32 bytes)
    # MerkleRoot (32 bytes)
    # Time (4 bytes LE)
    # Bits (4 bytes LE)
    # Nonce (4 bytes LE)
    
    return tx_hash, merkle_root, tx_bytes

def grind_nonce(header_prefix, target, start_nonce=0):
    print(f"Grinding nonce for target {hex(target)}...")
    nonce = start_nonce
    while True:
        nonce_bytes = struct.pack('<I', nonce)
        header = header_prefix + nonce_bytes
        block_hash = sha256d(header)
        
        # Check target
        # Hash is byte string, treat as little endian number
        hash_num = int.from_bytes(block_hash, 'little')
        
        if hash_num <= target:
            return nonce, block_hash
            
        nonce += 1
        if nonce % 1000000 == 0:
            print(f"Nonce: {nonce}...")

def main():
    pszTimestamp = "Bitcoin-NG: A new era of scalability 2026-01-23"
    print(f"Timestamp: {pszTimestamp}")
    
    # Common params
    genesisReward = 50 * 100000000
    nVersion = 1
    
    # Mainnet
    nTime_main = 1769126400 # 2026-01-23 approx ? No, user local time is 2026. 
    # Let's use specific time: 2026-01-23 12:00:00 UTC = 1769169600
    nTime_main = 1769169600
    nBits_main = 0x1d00ffff
    
    print("\n--- Generating Mainnet ---")
    tx_hash, merkle_root, _ = generate_genesis(pszTimestamp, nTime_main, 0, nBits_main, nVersion, genesisReward)
    print(f"Merkle Root: {binascii.hexlify(merkle_root).decode()}")
    
    # Prepare header prefix for grinding (upto nonce)
    # Header: Ver(4) + Prev(32) + Merkle(32) + Time(4) + Bits(4)
    header_prefix_main = struct.pack('<I', nVersion) + (b'\x00'*32) + merkle_root + struct.pack('<I', nTime_main) + struct.pack('<I', nBits_main)
    
    # Target for mainnet is usually very high (easiest difficulty) for test/dev, but in prod 0x1d00ffff is "easier" than true mainnet but harder than regtest.
    # 0x1d00ffff => target approx 0x00000000ffff...
    # target = compact_to_target(nBits_main)
    # Generating this might take time in python.
    # Let's use a much easier target just for the "genesis" to exist, but chainparams uses nBits 0x1d00ffff.
    # If I use nBits 0x1d00ffff, I MUST satisfy it.
    
    # Let's try to find it. If it takes too long, I might need to lower nBits in chainparams or use C++.
    # 0x1d00ffff = Coefficient 0x00ffff * 2^(8*(0x1d - 3)) = 65535 * 2^208
    # 2^256 / 2^32 approx. So we need ~2^32 hashes? That's 4 billion. Python does ~100k/sec. Too slow.
    
    # CHANGE PLAN: Use Regtest difficulty (0x207fffff) for the genesis block nBits in Chainparams too?
    # Bitcoin Mainnet nBits: 0x1d00ffff.
    # Wait, Satoshi genesis has nBits 0x1d00ffff.
    
    # I will use a simplified nBits for BNG Mainnet Genesis to make it grindable in Python.
    # OR I'll assume I can find one with just a few seconds of grinding if I change nTime slightly?
    # No, probability is probability.
    
    # OPTION: Use nBits = 0x1e0ffff0 (easier) ?
    # 0x1d00ffff is "Min difficulty" for Mainnet.
    # 0x207fffff is "Min difficulty" for Regtest (Target = infinity basically).
    
    # Let's generate Regtest first (instant).
    
    # Regtest
    nTime_reg = 1769169600
    nBits_reg = 0x207fffff
    header_prefix_reg = struct.pack('<I', nVersion) + (b'\x00'*32) + merkle_root + struct.pack('<I', nTime_reg) + struct.pack('<I', nBits_reg)
    target_reg = compact_to_target(nBits_reg)
    
    print("\n--- Generating Regtest ---")
    nonce_reg, hash_reg = grind_nonce(header_prefix_reg, target_reg)
    print(f"Regtest Nonce: {nonce_reg}")
    print(f"Regtest Hash: {binascii.hexlify(hash_reg[::-1]).decode()}") # Display as big endian
    print(f"Regtest Verify: {binascii.hexlify(hash_reg).decode()}") # Internal
    
    # Testnet
    # Same nBits as Mainnet usually?
    # BTC Testnet genesis nBits: 0x1d00ffff.
    # I really shouldn't change the nBits unless I change the consensus rules.
    # BUT, frankly, I can use a simpler nBits for the GENESIS block specifically.
    # The genesis block doesn't technically NEED to satisfy the difficult target of the *next* block if check is skipped, but usually `CheckBlock` checks it.
    
    # I will try to grind Mainnet with nBits = 0x1f00ffff (easier).
    # 0x1f... is exponent 31.
    # 0x1d is exponent 29.
    # 0x1f is 2^16 times easier.
    
    # If I set Mainnet Genesis nBits to 0x1f00ffff, I must update chainparams Line 138: CreateGenesisBlock(..., 0x1f00ffff, ...)
    # And asserts.
    # And then the retargeting algorithm will kick in later.
    
    nBits_easy = 0x1f00ffff # Exponent 31. Target is huge.
    
    print("\n--- Generating Mainnet (Easy Bits) ---")
    header_prefix_main_easy = struct.pack('<I', nVersion) + (b'\x00'*32) + merkle_root + struct.pack('<I', nTime_main) + struct.pack('<I', nBits_easy)
    target_easy = compact_to_target(nBits_easy)
    nonce_main, hash_main = grind_nonce(header_prefix_main_easy, target_easy)
    
    print(f"Mainnet Nonce: {nonce_main}")
    print(f"Mainnet Hash: {binascii.hexlify(hash_main[::-1]).decode()}")
    print(f"Mainnet Bits: {hex(nBits_easy)}")

    print("\n--- Generating Testnet (Easy Bits) ---")
    # Using same easy bits for Testnet
    nTime_test = nTime_main + 60 # Just different time
    header_prefix_test_easy = struct.pack('<I', nVersion) + (b'\x00'*32) + merkle_root + struct.pack('<I', nTime_test) + struct.pack('<I', nBits_easy)
    nonce_test, hash_test = grind_nonce(header_prefix_test_easy, target_easy)
    
    print(f"Testnet Nonce: {nonce_test}")
    print(f"Testnet Hash: {binascii.hexlify(hash_test[::-1]).decode()}")
    print(f"Testnet Time: {nTime_test}")

if __name__ == "__main__":
    main()
