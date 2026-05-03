// Copyright (c) 2026-present The Bitcoin-NG developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TEST_UTIL_LEGACY_SIGNATURE_CHECKER_H
#define BITCOIN_TEST_UTIL_LEGACY_SIGNATURE_CHECKER_H

#include <key.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <serialize.h>

namespace test::legacy {

inline bool SetScriptError(ScriptError* ret, ScriptError serror)
{
    if (ret) *ret = serror;
    return false;
}

inline uint256 SignatureHashOld(CScript scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType)
{
    if (nIn >= txTo.vin.size()) return uint256::ONE;
    CMutableTransaction txTmp(txTo);

    FindAndDelete(scriptCode, CScript(OP_CODESEPARATOR));

    for (unsigned int i = 0; i < txTmp.vin.size(); ++i) {
        txTmp.vin[i].scriptSig = CScript();
    }
    txTmp.vin[nIn].scriptSig = scriptCode;

    if ((nHashType & 0x1f) == SIGHASH_NONE) {
        txTmp.vout.clear();
        for (unsigned int i = 0; i < txTmp.vin.size(); ++i) {
            if (i != nIn) txTmp.vin[i].nSequence = 0;
        }
    } else if ((nHashType & 0x1f) == SIGHASH_SINGLE) {
        const unsigned int nOut = nIn;
        if (nOut >= txTmp.vout.size()) return uint256::ONE;
        txTmp.vout.resize(nOut + 1);
        for (unsigned int i = 0; i < nOut; ++i) {
            txTmp.vout[i].SetNull();
        }
        for (unsigned int i = 0; i < txTmp.vin.size(); ++i) {
            if (i != nIn) txTmp.vin[i].nSequence = 0;
        }
    }

    if (nHashType & SIGHASH_ANYONECANPAY) {
        txTmp.vin[0] = txTmp.vin[nIn];
        txTmp.vin.resize(1);
    }

    HashWriter ss{};
    ss << TX_NO_WITNESS(txTmp) << nHashType;
    return ss.GetHash();
}

template <class T>
inline uint256 GetPrevoutsSHA256Old(const T& txTo)
{
    HashWriter ss{};
    for (const auto& txin : txTo.vin) ss << txin.prevout;
    return ss.GetSHA256();
}

template <class T>
inline uint256 GetSequencesSHA256Old(const T& txTo)
{
    HashWriter ss{};
    for (const auto& txin : txTo.vin) ss << txin.nSequence;
    return ss.GetSHA256();
}

template <class T>
inline uint256 GetOutputsSHA256Old(const T& txTo)
{
    HashWriter ss{};
    for (const auto& txout : txTo.vout) ss << txout;
    return ss.GetSHA256();
}

template <class T>
inline uint256 SignatureHashWitnessV0Old(const CScript& scriptCode, const T& txTo, unsigned int nIn, int32_t nHashType, const CAmount& amount)
{
    assert(nIn < txTo.vin.size());

    uint256 hashPrevouts;
    uint256 hashSequence;
    uint256 hashOutputs;

    if (!(nHashType & SIGHASH_ANYONECANPAY)) {
        hashPrevouts = SHA256Uint256(GetPrevoutsSHA256Old(txTo));
    }

    if (!(nHashType & SIGHASH_ANYONECANPAY) && (nHashType & 0x1f) != SIGHASH_SINGLE && (nHashType & 0x1f) != SIGHASH_NONE) {
        hashSequence = SHA256Uint256(GetSequencesSHA256Old(txTo));
    }

    if ((nHashType & 0x1f) != SIGHASH_SINGLE && (nHashType & 0x1f) != SIGHASH_NONE) {
        hashOutputs = SHA256Uint256(GetOutputsSHA256Old(txTo));
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

template <typename T>
inline bool SignatureHashSchnorrOld(uint256& hash_out, ScriptExecutionData& execdata, const T& tx_to, uint32_t in_pos, uint8_t hash_type, SigVersion sigversion, const PrecomputedTransactionData& cache)
{
    uint8_t ext_flag;
    uint8_t key_version{0};
    switch (sigversion) {
    case SigVersion::TAPROOT:
        ext_flag = 0;
        break;
    case SigVersion::TAPSCRIPT:
        ext_flag = 1;
        break;
    default:
        assert(false);
    }
    assert(in_pos < tx_to.vin.size());
    if (!(cache.m_bip341_taproot_ready && cache.m_spent_outputs_ready)) return false;

    HashWriter ss{HASHER_TAPSIGHASH};
    static constexpr uint8_t EPOCH = 0;
    ss << EPOCH;

    const uint8_t output_type = hash_type == SIGHASH_DEFAULT ? SIGHASH_ALL : (hash_type & SIGHASH_OUTPUT_MASK);
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
    if (output_type != SIGHASH_NONE && output_type != SIGHASH_SINGLE) {
        ss << cache.m_outputs_single_hash;
    }

    const uint8_t spend_type = (ext_flag << 1) + (execdata.m_annex_present ? 1 : 0);
    ss << spend_type;

    if (input_type == SIGHASH_ANYONECANPAY) {
        ss << tx_to.vin[in_pos].prevout;
        ss << cache.m_spent_outputs[in_pos];
        ss << tx_to.vin[in_pos].nSequence;
    } else {
        ss << in_pos;
    }

    if (execdata.m_annex_present) {
        ss << execdata.m_annex_hash;
    }

    if (output_type == SIGHASH_SINGLE) {
        if (in_pos >= tx_to.vout.size()) return false;
        HashWriter sha_single_output{};
        sha_single_output << tx_to.vout[in_pos];
        ss << sha_single_output.GetSHA256();
    }

    if (sigversion == SigVersion::TAPSCRIPT) {
        ss << execdata.m_tapleaf_hash;
        ss << key_version;
        ss << execdata.m_codeseparator_pos;
    }

    hash_out = ss.GetHash();
    return true;
}

class LegacyVectorSignatureChecker : public BaseSignatureChecker
{
public:
    LegacyVectorSignatureChecker(const CTransaction& tx_to, unsigned int n_in, CAmount amount_in, const PrecomputedTransactionData& txdata_in)
        : m_tx_to(tx_to), m_n_in(n_in), m_amount(amount_in), m_txdata(txdata_in) {}

    bool CheckECDSASignature(const std::vector<unsigned char>& vchSigIn, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode, SigVersion sigversion) const override
    {
        CPubKey pubkey(vchPubKey);
        if (!pubkey.IsValid()) return false;

        std::vector<unsigned char> vchSig(vchSigIn);
        if (vchSig.empty()) return false;
        const int nHashType = vchSig.back();
        vchSig.pop_back();

        uint256 sighash;
        switch (sigversion) {
        case SigVersion::BASE:
            sighash = SignatureHashOld(scriptCode, m_tx_to, m_n_in, nHashType);
            break;
        case SigVersion::WITNESS_V0:
            if (m_amount < 0) return false;
            sighash = SignatureHashWitnessV0Old(scriptCode, m_tx_to, m_n_in, nHashType, m_amount);
            break;
        case SigVersion::TAPROOT:
        case SigVersion::TAPSCRIPT:
            return false;
        }

        return pubkey.Verify(sighash, vchSig);
    }

    bool CheckSchnorrSignature(std::span<const unsigned char> sig, std::span<const unsigned char> pubkey_in, SigVersion sigversion, ScriptExecutionData& execdata, ScriptError* serror = nullptr) const override
    {
        assert(sigversion == SigVersion::TAPROOT || sigversion == SigVersion::TAPSCRIPT);
        if (pubkey_in.size() != 32) return SetScriptError(serror, SCRIPT_ERR_PUBKEYTYPE);
        if (sig.size() != 64 && sig.size() != 65) return SetScriptError(serror, SCRIPT_ERR_SCHNORR_SIG_SIZE);

        XOnlyPubKey pubkey{pubkey_in};
        uint8_t hashtype = SIGHASH_DEFAULT;
        if (sig.size() == 65) {
            hashtype = SpanPopBack(sig);
            if (hashtype == SIGHASH_DEFAULT) return SetScriptError(serror, SCRIPT_ERR_SCHNORR_SIG_HASHTYPE);
        }

        uint256 sighash;
        if (!SignatureHashSchnorrOld(sighash, execdata, m_tx_to, m_n_in, hashtype, sigversion, m_txdata)) {
            return SetScriptError(serror, SCRIPT_ERR_SCHNORR_SIG_HASHTYPE);
        }
        if (!pubkey.VerifySchnorr(sighash, sig)) return SetScriptError(serror, SCRIPT_ERR_SCHNORR_SIG);
        return true;
    }

    bool CheckLockTime(const CScriptNum& nLockTime) const override
    {
        if (!((m_tx_to.nLockTime < LOCKTIME_THRESHOLD && nLockTime < LOCKTIME_THRESHOLD) ||
              (m_tx_to.nLockTime >= LOCKTIME_THRESHOLD && nLockTime >= LOCKTIME_THRESHOLD))) {
            return false;
        }
        if (nLockTime > static_cast<int64_t>(m_tx_to.nLockTime)) return false;
        if (CTxIn::SEQUENCE_FINAL == m_tx_to.vin[m_n_in].nSequence) return false;
        return true;
    }

    bool CheckSequence(const CScriptNum& nSequence) const override
    {
        const int64_t tx_to_sequence = static_cast<int64_t>(m_tx_to.vin[m_n_in].nSequence);
        if (m_tx_to.version < 2) return false;
        if (tx_to_sequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) return false;

        const uint32_t nLockTimeMask = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | CTxIn::SEQUENCE_LOCKTIME_MASK;
        const int64_t tx_to_sequence_masked = tx_to_sequence & nLockTimeMask;
        const CScriptNum nSequenceMasked = nSequence & nLockTimeMask;

        if (!((tx_to_sequence_masked < CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG && nSequenceMasked < CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) ||
              (tx_to_sequence_masked >= CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG && nSequenceMasked >= CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG))) {
            return false;
        }
        if (nSequenceMasked > tx_to_sequence_masked) return false;
        return true;
    }

private:
    const CTransaction& m_tx_to;
    const unsigned int m_n_in;
    const CAmount m_amount;
    const PrecomputedTransactionData& m_txdata;
};

} // namespace test::legacy

#endif // BITCOIN_TEST_UTIL_LEGACY_SIGNATURE_CHECKER_H
