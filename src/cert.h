#ifndef CERT_H
#define CERT_H

#include "rpcprotocol.h"
#include "leveldbwrapper.h"
#include "script.h"
#include "db.h"

class CTransaction;
class CTxOut;
class CValidationState;
class CCoinsViewCache;
class COutPoint;
class CCoins;
class CScript;
class CWalletTx;
class CDiskTxPos;
class CReserveKey;
class CBlockIndex;

bool CheckCertInputs(CBlockIndex *pindex, const CTransaction &tx, CValidationState &state, CCoinsViewCache &inputs, bool fBlock, bool fMiner, bool fJustCheck);
bool IsCertMine(const CTransaction& tx);
bool IsCertMine(const CTransaction& tx, const CTxOut& txout);
std::string SendCertMoneyWithInputTx(std::vector<std::pair<CScript, CAmount> >& vecSend, CAmount nValue, CAmount nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew, 
    bool fAskFee, const std::string& txData = "");
std::string SendCertMoneyWithInputTx(CScript scriptPubKey, CAmount nValue, CAmount nNetFee, CWalletTx& wtxIn,
                                     CWalletTx& wtxNew, bool fAskFee, const std::string& txData = "");
bool CreateCertTransactionWithInputTx(const std::vector<std::pair<CScript, CAmount> >& vecSend, CWalletTx& wtxIn,
                                      int nTxOut, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRet, const std::string& txData);
bool DecodeCertTx(const CTransaction& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeCertTx(const CCoins& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, int nHeight);
bool DecodeCertTxInputs(const CTransaction& tx, int& op, int& nOut, std::vector<std::vector<unsigned char> >& vvch, CCoinsViewCache &inputs);
bool DecodeCertScript(const CScript& script, int& op, std::vector<std::vector<unsigned char> > &vvch);
bool IsCertOp(int op);
int IndexOfCertOutput(const CTransaction& tx);
bool GetValueOfCertTxHash(const uint256 &txHash, std::vector<unsigned char>& vchValue, uint256& hash, int& nHeight);
int GetTxHashHeight(const uint256 txHash);
int GetTxPosHeight(const CDiskTxPos& txPos);
int GetTxPosHeight2(const CDiskTxPos& txPos, int nHeight);
int GetCertDisplayExpirationDepth();
CAmount GetCertNetworkFee(opcodetype seed, unsigned int nHeight);
CAmount GetCertNetFee(const CTransaction& tx);
bool InsertCertFee(CBlockIndex *pindex, uint256 hash, uint64_t nValue);
bool ExtractCertAddress(const CScript& script, std::string& address);
bool EncryptMessage(const std::vector<unsigned char> &vchPublicKey, const std::vector<unsigned char> &vchMessage, std::string &strCipherText);
bool DecryptMessage(const std::vector<unsigned char> &vchPublicKey, const std::vector<unsigned char> &vchCipherText, std::string &strMessage);
std::string certFromOp(int op);


class CDarkSilkAddress;

class CCert {
public:
    std::vector<unsigned char> vchRand;
    std::vector<unsigned char> vchPubKey;
    std::vector<unsigned char> vchTitle;
    std::vector<unsigned char> vchData;
    uint256 txHash;
    uint64_t nHeight;
    bool bPrivate;
    std::vector<unsigned char> vchOfferLink;
    CCert() {
        SetNull();
    }
    CCert(const CTransaction &tx) {
        SetNull();
        UnserializeFromTx(tx);
    }
    IMPLEMENT_SERIALIZE (
        READWRITE(vchRand);
        READWRITE(vchTitle);
        READWRITE(vchData);
        READWRITE(txHash);
        READWRITE(nHeight);
        READWRITE(vchPubKey);
        READWRITE(bPrivate);
        READWRITE(vchOfferLink);
        
        
    )

    friend bool operator==(const CCert &a, const CCert &b) {
        return (
        a.vchRand == b.vchRand
        && a.vchTitle == b.vchTitle
        && a.vchData == b.vchData
        && a.txHash == b.txHash
        && a.nHeight == b.nHeight
        && a.vchPubKey == b.vchPubKey
        && a.bPrivate == b.bPrivate
        && a.vchOfferLink == b.vchOfferLink
        );
    }

    CCert operator=(const CCert &b) {
        vchRand = b.vchRand;
        vchTitle = b.vchTitle;
        vchData = b.vchData;
        txHash = b.txHash;
        nHeight = b.nHeight;
        vchPubKey = b.vchPubKey;
        bPrivate = b.bPrivate;
        vchOfferLink = b.vchOfferLink;
        return *this;
    }

    friend bool operator!=(const CCert &a, const CCert &b) {
        return !(a == b);
    }

    void SetNull() { nHeight = 0; txHash = 0;  vchRand.clear(); vchPubKey.clear(); bPrivate = false;vchOfferLink.clear();}
    bool IsNull() const { return (txHash == 0 &&  nHeight == 0 && vchRand.size() == 0); }
    bool UnserializeFromTx(const CTransaction &tx);
    std::string SerializeToString();
};


class CCertDB : public CLevelDBWrapper {
public:

    class CBlockIndex;

    CCertDB(size_t nCacheSize, bool fMemory, bool fWipe) : CLevelDBWrapper(GetDataDir() / "certificates", nCacheSize, fMemory, fWipe) {}

    bool WriteCert(const std::vector<unsigned char>& name, std::vector<CCert>& vtxPos) {
        return Write(make_pair(std::string("certi"), name), vtxPos);
    }

    bool EraseCert(const std::vector<unsigned char>& name) {
        return Erase(make_pair(std::string("certi"), name));
    }

    bool ReadCert(const std::vector<unsigned char>& name, std::vector<CCert>& vtxPos) {
        return Read(make_pair(std::string("certi"), name), vtxPos);
    }

    bool ExistsCert(const std::vector<unsigned char>& name) {
        return Exists(make_pair(std::string("certi"), name));
    }

    bool ScanCerts(
            const std::vector<unsigned char>& vchName,
            unsigned int nMax,
            std::vector<std::pair<std::vector<unsigned char>, CCert> >& certScan);

    bool ReconstructCertIndex(CBlockIndex *pindexRescan);
};

bool GetTxOfCert(CCertDB& dbCert, const std::vector<unsigned char> &vchCert, CTransaction& tx);

#endif // CERT_H
