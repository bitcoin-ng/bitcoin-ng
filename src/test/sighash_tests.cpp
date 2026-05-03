// Copyright (c) 2013-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <common/system.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <hash.h>
#include <key.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/signingprovider.h>
#include <serialize.h>
#include <streams.h>
#include <test/data/sighash.json.h>
#include <test/util/json.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <iostream>

#include <boost/test/unit_test.hpp>

#include <univalue.h>

// Old script.cpp SignatureHash function
uint256 static SignatureHashOld(CScript scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType)
{
    if (nIn >= txTo.vin.size())
    {
        return uint256::ONE;
    }
    CMutableTransaction txTmp(txTo);

    // In case concatenating two scripts ends up with two codeseparators,
    // or an extra one at the end, this prevents all those possible incompatibilities.
    FindAndDelete(scriptCode, CScript(OP_CODESEPARATOR));

    // Blank out other inputs' signatures
    for (unsigned int i = 0; i < txTmp.vin.size(); i++)
        txTmp.vin[i].scriptSig = CScript();
    txTmp.vin[nIn].scriptSig = scriptCode;

    // Blank out some of the outputs
    if ((nHashType & 0x1f) == SIGHASH_NONE)
    {
        // Wildcard payee
        txTmp.vout.clear();

        // Let the others update at will
        for (unsigned int i = 0; i < txTmp.vin.size(); i++)
            if (i != nIn)
                txTmp.vin[i].nSequence = 0;
    }
    else if ((nHashType & 0x1f) == SIGHASH_SINGLE)
    {
        // Only lock-in the txout payee at same index as txin
        unsigned int nOut = nIn;
        if (nOut >= txTmp.vout.size())
        {
            return uint256::ONE;
        }
        txTmp.vout.resize(nOut+1);
        for (unsigned int i = 0; i < nOut; i++)
            txTmp.vout[i].SetNull();

        // Let the others update at will
        for (unsigned int i = 0; i < txTmp.vin.size(); i++)
            if (i != nIn)
                txTmp.vin[i].nSequence = 0;
    }

    // Blank out other inputs completely, not recommended for open transactions
    if (nHashType & SIGHASH_ANYONECANPAY)
    {
        txTmp.vin[0] = txTmp.vin[nIn];
        txTmp.vin.resize(1);
    }

    // Serialize and hash
    HashWriter ss{};
    ss << TX_NO_WITNESS(txTmp) << nHashType;
    return ss.GetHash();
}

template <class T>
uint256 GetPrevoutsSHA256Test(const T& txTo)
{
    HashWriter ss{};
    for (const auto& txin : txTo.vin) ss << txin.prevout;
    return ss.GetSHA256();
}

template <class T>
uint256 GetSequencesSHA256Test(const T& txTo)
{
    HashWriter ss{};
    for (const auto& txin : txTo.vin) ss << txin.nSequence;
    return ss.GetSHA256();
}

template <class T>
uint256 GetOutputsSHA256Test(const T& txTo)
{
    HashWriter ss{};
    for (const auto& txout : txTo.vout) ss << txout;
    return ss.GetSHA256();
}

template <class T>
uint256 SignatureHashWitnessV0Old(const CScript& scriptCode, const T& txTo, unsigned int nIn, int32_t nHashType, const CAmount& amount)
{
    assert(nIn < txTo.vin.size());

    uint256 hashPrevouts;
    uint256 hashSequence;
    uint256 hashOutputs;

    if (!(nHashType & SIGHASH_ANYONECANPAY)) {
        hashPrevouts = SHA256Uint256(GetPrevoutsSHA256Test(txTo));
    }

    if (!(nHashType & SIGHASH_ANYONECANPAY) && (nHashType & 0x1f) != SIGHASH_SINGLE && (nHashType & 0x1f) != SIGHASH_NONE) {
        hashSequence = SHA256Uint256(GetSequencesSHA256Test(txTo));
    }

    if ((nHashType & 0x1f) != SIGHASH_SINGLE && (nHashType & 0x1f) != SIGHASH_NONE) {
        hashOutputs = SHA256Uint256(GetOutputsSHA256Test(txTo));
    } else if ((nHashType & 0x1f) == SIGHASH_SINGLE && nIn < txTo.vout.size()) {
        HashWriter ss{};
        ss << txTo.vout[nIn];
        hashOutputs = ss.GetHash();
    }

    HashWriter ss{};
    ss << txTo.version;
    ss << hashPrevouts;
    ss << hashSequence;
    ss << txTo.vin[nIn].prevout;
    ss << scriptCode;
    ss << amount;
    ss << txTo.vin[nIn].nSequence;
    ss << hashOutputs;
    ss << txTo.nLockTime;
    ss << nHashType;
    return ss.GetHash();
}

template<typename T>
bool SignatureHashSchnorrOld(uint256& hash_out, ScriptExecutionData& execdata, const T& tx_to, uint32_t in_pos, uint8_t hash_type, SigVersion sigversion, const PrecomputedTransactionData& cache)
{
    uint8_t ext_flag, key_version;
    switch (sigversion) {
    case SigVersion::TAPROOT:
        ext_flag = 0;
        break;
    case SigVersion::TAPSCRIPT:
        ext_flag = 1;
        key_version = 0;
        break;
    default:
        assert(false);
    }
    assert(in_pos < tx_to.vin.size());
    if (!(cache.m_bip341_taproot_ready && cache.m_spent_outputs_ready)) return false;

    HashWriter ss{HASHER_TAPSIGHASH};

    static constexpr uint8_t EPOCH = 0;
    ss << EPOCH;

    const uint8_t output_type = (hash_type == SIGHASH_DEFAULT) ? SIGHASH_ALL : (hash_type & SIGHASH_OUTPUT_MASK);
    const uint8_t input_type = hash_type & SIGHASH_INPUT_MASK;
    if (!(hash_type <= 0x03 || (hash_type >= 0x81 && hash_type <= 0x83))) return false;
    ss << hash_type;

    ss << tx_to.version;
    ss << tx_to.nLockTime;
    if (input_type != SIGHASH_ANYONECANPAY) {
        ss << cache.m_prevouts_single_hash;
        ss << cache.m_spent_amounts_single_hash;
        ss << cache.m_spent_scripts_single_hash;
        ss << cache.m_sequences_single_hash;
    }
    if (output_type == SIGHASH_ALL) {
        ss << cache.m_outputs_single_hash;
    }

    assert(execdata.m_annex_init);
    const bool have_annex = execdata.m_annex_present;
    const uint8_t spend_type = (ext_flag << 1) + (have_annex ? 1 : 0);
    ss << spend_type;
    if (input_type == SIGHASH_ANYONECANPAY) {
        ss << tx_to.vin[in_pos].prevout;
        ss << cache.m_spent_outputs[in_pos];
        ss << tx_to.vin[in_pos].nSequence;
    } else {
        ss << in_pos;
    }
    if (have_annex) {
        ss << execdata.m_annex_hash;
    }

    if (output_type == SIGHASH_SINGLE) {
        if (in_pos >= tx_to.vout.size()) return false;
        if (!execdata.m_output_hash) {
            HashWriter sha_single_output{};
            sha_single_output << tx_to.vout[in_pos];
            execdata.m_output_hash = sha_single_output.GetSHA256();
        }
        ss << execdata.m_output_hash.value();
    }

    if (sigversion == SigVersion::TAPSCRIPT) {
        assert(execdata.m_tapleaf_hash_init);
        ss << execdata.m_tapleaf_hash;
        ss << key_version;
        assert(execdata.m_codeseparator_pos_init);
        ss << execdata.m_codeseparator_pos;
    }

    hash_out = ss.GetSHA256();
    return true;
}

struct SigHashTest : BasicTestingSetup {
void RandomScript(CScript &script) {
    static const opcodetype oplist[] = {OP_FALSE, OP_1, OP_2, OP_3, OP_CHECKSIG, OP_IF, OP_VERIF, OP_RETURN, OP_CODESEPARATOR};
    script = CScript();
    int ops = (m_rng.randrange(10));
    for (int i=0; i<ops; i++)
        script << oplist[m_rng.randrange(std::size(oplist))];
}

void RandomTransaction(CMutableTransaction& tx, bool fSingle)
{
    tx.version = m_rng.rand32();
    tx.vin.clear();
    tx.vout.clear();
    tx.nLockTime = (m_rng.randbool()) ? m_rng.rand32() : 0;
    int ins = (m_rng.randbits(2)) + 1;
    int outs = fSingle ? ins : (m_rng.randbits(2)) + 1;
    for (int in = 0; in < ins; in++) {
        tx.vin.emplace_back();
        CTxIn &txin = tx.vin.back();
        txin.prevout.hash = Txid::FromUint256(m_rng.rand256());
        txin.prevout.n = m_rng.randbits(2);
        RandomScript(txin.scriptSig);
        txin.nSequence = (m_rng.randbool()) ? m_rng.rand32() : std::numeric_limits<uint32_t>::max();
    }
    for (int out = 0; out < outs; out++) {
        tx.vout.emplace_back();
        CTxOut &txout = tx.vout.back();
        txout.nValue = RandMoney(m_rng);
        RandomScript(txout.scriptPubKey);
    }
}
}; // struct SigHashTest

BOOST_FIXTURE_TEST_SUITE(sighash_tests, SigHashTest)

BOOST_AUTO_TEST_CASE(sighash_test)
{
    #if defined(PRINT_SIGHASH_JSON)
    std::cout << "[\n";
    std::cout << "\t[\"raw_transaction, script, input_index, hashType, signature_hash (result)\"],\n";
    int nRandomTests = 500;
    #else
    int nRandomTests = 50000;
    #endif
    for (int i=0; i<nRandomTests; i++) {
        int nHashType{int(m_rng.rand32())};
        CMutableTransaction txTo;
        RandomTransaction(txTo, (nHashType & 0x1f) == SIGHASH_SINGLE);
        CScript scriptCode;
        RandomScript(scriptCode);
        int nIn = m_rng.randrange(txTo.vin.size());

        uint256 sh, sho;
        sho = SignatureHashOld(scriptCode, CTransaction(txTo), nIn, nHashType);
        sh = SignatureHash(scriptCode, txTo, nIn, nHashType, 0, SigVersion::BASE);
        #if defined(PRINT_SIGHASH_JSON)
        DataStream ss;
        ss << TX_WITH_WITNESS(txTo);

        std::cout << "\t[\"" ;
        std::cout << HexStr(ss) << "\", \"";
        std::cout << HexStr(scriptCode) << "\", ";
        std::cout << nIn << ", ";
        std::cout << nHashType << ", \"";
        std::cout << sh.GetHex() << "\"]";
        if (i+1 != nRandomTests) {
          std::cout << ",";
        }
        std::cout << "\n";
        #endif
        if (static_cast<uint32_t>(nHashType) == ForkedSighashType(static_cast<uint8_t>(nHashType))) {
            BOOST_CHECK_EQUAL(sh, sho);
        } else {
            BOOST_CHECK_NE(sh, sho);
        }
    }
    #if defined(PRINT_SIGHASH_JSON)
    std::cout << "]\n";
    #endif
}

// Goal: check that SignatureHash generates correct hash
BOOST_AUTO_TEST_CASE(sighash_from_data)
{
    UniValue tests = read_json(json_tests::sighash);

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 1) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        if (test.size() == 1) continue; // comment

        std::string raw_tx, raw_script, sigHashHex;
        int nIn, nHashType;
        uint256 sh;
        CTransactionRef tx;
        CScript scriptCode = CScript();

        try {
          // deserialize test data
          raw_tx = test[0].get_str();
          raw_script = test[1].get_str();
          nIn = test[2].getInt<int>();
          nHashType = test[3].getInt<int>();
          sigHashHex = test[4].get_str();

          DataStream stream(ParseHex(raw_tx));
          stream >> TX_WITH_WITNESS(tx);

          TxValidationState state;
          BOOST_CHECK_MESSAGE(CheckTransaction(*tx, state), strTest);
          BOOST_CHECK(state.IsValid());

          std::vector<unsigned char> raw = ParseHex(raw_script);
          scriptCode.insert(scriptCode.end(), raw.begin(), raw.end());
        } catch (...) {
          BOOST_ERROR("Bad test, couldn't deserialize data: " << strTest);
          continue;
        }

        sh = SignatureHash(scriptCode, *tx, nIn, nHashType, 0, SigVersion::BASE);
        BOOST_CHECK_MESSAGE(sh.GetHex() == sigHashHex, strTest);
    }
}

BOOST_AUTO_TEST_CASE(sighash_caching)
{
    // Get a script, transaction and parameters as inputs to the sighash function.
    CScript scriptcode;
    RandomScript(scriptcode);
    CScript diff_scriptcode{scriptcode};
    diff_scriptcode << OP_1;
    CMutableTransaction tx;
    RandomTransaction(tx, /*fSingle=*/false);
    const auto in_index{static_cast<uint32_t>(m_rng.randrange(tx.vin.size()))};
    const auto amount{m_rng.rand<CAmount>()};

    // Exercise the sighash function under both legacy and segwit v0.
    for (const auto sigversion: {SigVersion::BASE, SigVersion::WITNESS_V0}) {
        // For each, run it against all the 6 standard hash types and a few additional random ones.
        std::vector<int32_t> hash_types{{SIGHASH_ALL, SIGHASH_SINGLE, SIGHASH_NONE, SIGHASH_ALL | SIGHASH_ANYONECANPAY,
                                          SIGHASH_SINGLE | SIGHASH_ANYONECANPAY, SIGHASH_NONE | SIGHASH_ANYONECANPAY,
                                          SIGHASH_ANYONECANPAY, 0, std::numeric_limits<int32_t>::max()}};
        for (int i{0}; i < 10; ++i) {
            hash_types.push_back(i % 2 == 0 ? m_rng.rand<int8_t>() : m_rng.rand<int32_t>());
        }

        // Reuse the same cache across script types. This must not cause any issue as the cached value for one hash type must never
        // be confused for another (instantiating the cache within the loop instead would prevent testing this).
        SigHashCache cache;
        for (const auto hash_type: hash_types) {
            const bool expect_one{sigversion == SigVersion::BASE && ((hash_type & 0x1f) == SIGHASH_SINGLE) && in_index >= tx.vout.size()};

            // The result of computing the sighash should be the same with or without cache.
            const auto sighash_with_cache{SignatureHash(scriptcode, tx, in_index, hash_type, amount, sigversion, nullptr, &cache)};
            const auto sighash_no_cache{SignatureHash(scriptcode, tx, in_index, hash_type, amount, sigversion, nullptr, nullptr)};
            BOOST_CHECK_EQUAL(sighash_with_cache, sighash_no_cache);

            // Calling the cached version again should return the same value again.
            BOOST_CHECK_EQUAL(sighash_with_cache, SignatureHash(scriptcode, tx, in_index, hash_type, amount, sigversion, nullptr, &cache));

            // While here we might as well also check that the result for legacy is the same as for the old SignatureHash() function.
            if (sigversion == SigVersion::BASE) {
                const auto old_sighash{SignatureHashOld(scriptcode, CTransaction(tx), in_index, hash_type)};
                if (expect_one || static_cast<uint32_t>(hash_type) == ForkedSighashType(static_cast<uint8_t>(hash_type))) {
                    BOOST_CHECK_EQUAL(sighash_with_cache, old_sighash);
                } else {
                    BOOST_CHECK_NE(sighash_with_cache, old_sighash);
                }
            }

            // Calling with a different scriptcode (for instance in case a CODESEP is encountered) will not return the cache value but
            // overwrite it. The sighash will always be different except in case of legacy SIGHASH_SINGLE bug.
            const auto sighash_with_cache2{SignatureHash(diff_scriptcode, tx, in_index, hash_type, amount, sigversion, nullptr, &cache)};
            const auto sighash_no_cache2{SignatureHash(diff_scriptcode, tx, in_index, hash_type, amount, sigversion, nullptr, nullptr)};
            BOOST_CHECK_EQUAL(sighash_with_cache2, sighash_no_cache2);
            if (!expect_one) {
                BOOST_CHECK_NE(sighash_with_cache, sighash_with_cache2);
            } else {
                BOOST_CHECK_EQUAL(sighash_with_cache, sighash_with_cache2);
                BOOST_CHECK_EQUAL(sighash_with_cache, uint256::ONE);
            }

            // Calling the cached version again should return the same value again.
            BOOST_CHECK_EQUAL(sighash_with_cache2, SignatureHash(diff_scriptcode, tx, in_index, hash_type, amount, sigversion, nullptr, &cache));

            // And if we store a different value for this scriptcode and hash type it will return that instead.
            {
                HashWriter h{};
                h << 42;
                cache.Store(hash_type, scriptcode, h);
                const auto stored_hash{h.GetHash()};
                BOOST_CHECK(cache.Load(hash_type, scriptcode, h));
                const auto loaded_hash{h.GetHash()};
                BOOST_CHECK_EQUAL(stored_hash, loaded_hash);
            }

            // And using this mutated cache with the sighash function will return the new value (except in the legacy SIGHASH_SINGLE bug
            // case in which it'll return 1).
            if (!expect_one) {
                BOOST_CHECK_NE(SignatureHash(scriptcode, tx, in_index, hash_type, amount, sigversion, nullptr, &cache), sighash_with_cache);
                HashWriter h{};
                BOOST_CHECK(cache.Load(hash_type, scriptcode, h));
                h << ForkedSighashType(static_cast<uint8_t>(hash_type));
                const auto new_hash{h.GetHash()};
                BOOST_CHECK_EQUAL(SignatureHash(scriptcode, tx, in_index, hash_type, amount, sigversion, nullptr, &cache), new_hash);
            } else {
                BOOST_CHECK_EQUAL(SignatureHash(scriptcode, tx, in_index, hash_type, amount, sigversion, nullptr, &cache), uint256::ONE);
            }

            // Wipe the cache and restore the correct cached value for this scriptcode and hash_type before starting the next iteration.
            HashWriter dummy{};
            cache.Store(hash_type, diff_scriptcode, dummy);
            (void)SignatureHash(scriptcode, tx, in_index, hash_type, amount, sigversion, nullptr, &cache);
            BOOST_CHECK(cache.Load(hash_type, scriptcode, dummy) || expect_one);
        }
    }
}

BOOST_AUTO_TEST_CASE(replay_protection_rejects_bitcoin_style_signatures)
{
    CKey key;
    key.MakeNewKey(/*fCompressed=*/true);
    const CPubKey pubkey = key.GetPubKey();

    {
        CMutableTransaction tx;
        tx.version = 2;
        tx.vin.resize(1);
        tx.vout.resize(1);
        tx.vin[0].prevout.hash = Txid::FromUint256(m_rng.rand256());
        tx.vin[0].prevout.n = 0;
        tx.vin[0].nSequence = std::numeric_limits<uint32_t>::max();
        tx.vout[0].nValue = 1000;
        tx.vout[0].scriptPubKey = CScript() << OP_TRUE;

        const CScript prevout_script = CScript() << std::vector<unsigned char>(pubkey.begin(), pubkey.end()) << OP_CHECKSIG;
        const uint256 old_hash = SignatureHashOld(prevout_script, CTransaction(tx), 0, SIGHASH_ALL);
        const uint256 bng_hash = SignatureHash(prevout_script, tx, 0, SIGHASH_ALL, 0, SigVersion::BASE);

        std::vector<unsigned char> old_sig;
        std::vector<unsigned char> bng_sig;
        BOOST_REQUIRE(key.Sign(old_hash, old_sig));
        BOOST_REQUIRE(key.Sign(bng_hash, bng_sig));
        old_sig.push_back(SIGHASH_ALL);
        bng_sig.push_back(SIGHASH_ALL);

        ScriptError err;
        tx.vin[0].scriptSig = CScript() << old_sig;
        BOOST_CHECK(!VerifyScript(tx.vin[0].scriptSig, prevout_script, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker{&tx, 0, 0, MissingDataBehavior::FAIL}, &err));
        tx.vin[0].scriptSig = CScript() << bng_sig;
        BOOST_CHECK(VerifyScript(tx.vin[0].scriptSig, prevout_script, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker{&tx, 0, 0, MissingDataBehavior::FAIL}, &err));
    }

    {
        CMutableTransaction tx;
        tx.version = 2;
        tx.vin.resize(1);
        tx.vout.resize(1);
        tx.vin[0].prevout.hash = Txid::FromUint256(m_rng.rand256());
        tx.vin[0].prevout.n = 1;
        tx.vin[0].nSequence = std::numeric_limits<uint32_t>::max();
        tx.vout[0].nValue = 2000;
        tx.vout[0].scriptPubKey = CScript() << OP_TRUE;

        const CAmount amount = 3000;
        const CScript witness_script = CScript() << std::vector<unsigned char>(pubkey.begin(), pubkey.end()) << OP_CHECKSIG;
        const CScript prevout_script = GetScriptForDestination(WitnessV0ScriptHash(witness_script));
        const uint256 old_hash = SignatureHashWitnessV0Old(witness_script, tx, 0, SIGHASH_ALL, amount);
        const uint256 bng_hash = SignatureHash(witness_script, tx, 0, SIGHASH_ALL, amount, SigVersion::WITNESS_V0);

        std::vector<unsigned char> old_sig;
        std::vector<unsigned char> bng_sig;
        BOOST_REQUIRE(key.Sign(old_hash, old_sig));
        BOOST_REQUIRE(key.Sign(bng_hash, bng_sig));
        old_sig.push_back(SIGHASH_ALL);
        bng_sig.push_back(SIGHASH_ALL);

        ScriptError err;
        tx.vin[0].scriptSig.clear();
        tx.vin[0].scriptWitness.stack = {old_sig, std::vector<unsigned char>(witness_script.begin(), witness_script.end())};
        BOOST_CHECK(!VerifyScript(tx.vin[0].scriptSig, prevout_script, &tx.vin[0].scriptWitness, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, MutableTransactionSignatureChecker{&tx, 0, amount, MissingDataBehavior::FAIL}, &err));
        tx.vin[0].scriptWitness.stack = {bng_sig, std::vector<unsigned char>(witness_script.begin(), witness_script.end())};
        BOOST_CHECK(VerifyScript(tx.vin[0].scriptSig, prevout_script, &tx.vin[0].scriptWitness, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, MutableTransactionSignatureChecker{&tx, 0, amount, MissingDataBehavior::FAIL}, &err));
    }

    {
        CMutableTransaction tx;
        tx.version = 2;
        tx.vin.resize(1);
        tx.vout.resize(1);
        tx.vin[0].prevout.hash = Txid::FromUint256(m_rng.rand256());
        tx.vin[0].prevout.n = 2;
        tx.vin[0].nSequence = std::numeric_limits<uint32_t>::max();
        tx.vout[0].nValue = 4000;
        tx.vout[0].scriptPubKey = CScript() << OP_TRUE;

        TaprootBuilder builder;
        const XOnlyPubKey internal_pubkey{pubkey};
        builder.Finalize(internal_pubkey);
        const CScript prevout_script = GetScriptForDestination(builder.GetOutput());
        const CAmount amount = 5000;
        const uint256 merkle_root;
        PrecomputedTransactionData txdata;
        txdata.Init(tx, {CTxOut(amount, prevout_script)}, true);

        ScriptExecutionData execdata;
        execdata.m_annex_init = true;
        execdata.m_annex_present = false;

        uint256 old_hash;
        uint256 bng_hash;
        BOOST_REQUIRE(SignatureHashSchnorrOld(old_hash, execdata, tx, 0, SIGHASH_ALL, SigVersion::TAPROOT, txdata));
        execdata.m_output_hash.reset();
        BOOST_REQUIRE(SignatureHashSchnorr(bng_hash, execdata, tx, 0, SIGHASH_ALL, SigVersion::TAPROOT, txdata, MissingDataBehavior::FAIL));

        std::vector<unsigned char> old_sig(64);
        std::vector<unsigned char> bng_sig(64);
        BOOST_REQUIRE(key.SignSchnorr(old_hash, old_sig, &merkle_root, {}));
        BOOST_REQUIRE(key.SignSchnorr(bng_hash, bng_sig, &merkle_root, {}));
        old_sig.push_back(SIGHASH_ALL);
        bng_sig.push_back(SIGHASH_ALL);

        ScriptError err;
        tx.vin[0].scriptWitness.stack = {old_sig};
        BOOST_CHECK(!VerifyScript(tx.vin[0].scriptSig, prevout_script, &tx.vin[0].scriptWitness, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT, MutableTransactionSignatureChecker{&tx, 0, amount, txdata, MissingDataBehavior::FAIL}, &err));
        tx.vin[0].scriptWitness.stack = {bng_sig};
        BOOST_CHECK(VerifyScript(tx.vin[0].scriptSig, prevout_script, &tx.vin[0].scriptWitness, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT, MutableTransactionSignatureChecker{&tx, 0, amount, txdata, MissingDataBehavior::FAIL}, &err));
    }
}

BOOST_AUTO_TEST_SUITE_END()
