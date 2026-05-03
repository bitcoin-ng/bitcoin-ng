// Copyright (c) 2026-present The Bitcoin-NG developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/sign.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <limits>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(bng_replay_protection_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(forkid_signatures_verify_on_bng)
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
        const uint256 bng_hash = SignatureHash(prevout_script, tx, 0, SIGHASH_ALL, 0, SigVersion::BASE);

        std::vector<unsigned char> bng_sig;
        BOOST_REQUIRE(key.Sign(bng_hash, bng_sig));
        bng_sig.push_back(SIGHASH_ALL);

        ScriptError err;
        tx.vin[0].scriptSig = CScript() << bng_sig;
        BOOST_CHECK(VerifyScript(tx.vin[0].scriptSig, prevout_script, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker{&tx, 0, 0, MissingDataBehavior::FAIL}, &err));
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
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
        const uint256 bng_hash = SignatureHash(witness_script, tx, 0, SIGHASH_ALL, amount, SigVersion::WITNESS_V0);

        std::vector<unsigned char> bng_sig;
        BOOST_REQUIRE(key.Sign(bng_hash, bng_sig));
        bng_sig.push_back(SIGHASH_ALL);

        ScriptError err;
        tx.vin[0].scriptSig.clear();
        tx.vin[0].scriptWitness.stack = {bng_sig, std::vector<unsigned char>(witness_script.begin(), witness_script.end())};
        BOOST_CHECK(VerifyScript(tx.vin[0].scriptSig, prevout_script, &tx.vin[0].scriptWitness, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, MutableTransactionSignatureChecker{&tx, 0, amount, MissingDataBehavior::FAIL}, &err));
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
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

        uint256 bng_hash;
        BOOST_REQUIRE(SignatureHashSchnorr(bng_hash, execdata, tx, 0, SIGHASH_ALL, SigVersion::TAPROOT, txdata, MissingDataBehavior::FAIL));

        std::vector<unsigned char> bng_sig(64);
        BOOST_REQUIRE(key.SignSchnorr(bng_hash, bng_sig, &merkle_root, {}));
        bng_sig.push_back(SIGHASH_ALL);

        ScriptError err;
        tx.vin[0].scriptWitness.stack = {bng_sig};
        BOOST_CHECK(VerifyScript(tx.vin[0].scriptSig, prevout_script, &tx.vin[0].scriptWitness, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT, MutableTransactionSignatureChecker{&tx, 0, amount, txdata, MissingDataBehavior::FAIL}, &err));
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
    }
}

BOOST_AUTO_TEST_SUITE_END()
