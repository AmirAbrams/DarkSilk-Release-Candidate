// Copyright (c) 2014 Syscoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
//

#include "init.h"
#include "alias.h"
#include "txdb.h"
#include "util.h"
#include "script.h"
#include "main.h"
#include "coins.h"
#include "primitives/transaction.h"
#include "txmempool.h"
#include "base58.h"

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"

#include <boost/xpressive/xpressive_dynamic.hpp>

using namespace std;
using namespace json_spirit;

extern CAliasDB *paliasdb;



extern bool ExistsInMempool(std::vector<unsigned char> vchNameOrRand, opcodetype type);
template<typename T> void ConvertTo(Value& value, bool fAllowNull = false);

extern uint256 SignatureHash(CScript scriptCode, const CTransaction& txTo,
		unsigned int nIn, int nHashType);

bool GetValueOfAliasTxHash(const uint256 &txHash,
		vector<unsigned char>& vchValue, uint256& hash, int& nHeight);

extern Object JSONRPCError(int code, const string& message);

/** Global variable that points to the active block tree (protected by cs_main) */
extern CBlockTreeDB *pblocktree;

extern CCriticalSection cs_mapTransactions;
extern map<uint256, CTransaction> mapTransactions;

// forward decls
extern bool DecodeAliasScript(const CScript& script, int& op,
		vector<vector<unsigned char> > &vvch, CScript::const_iterator& pc);
extern bool Solver(const CKeyStore& keystore, const CScript& scriptPubKey,
		uint256 hash, int nHashType, CScript& scriptSigRet,
		txnouttype& whichTypeRet);
extern bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey,
		const CTransaction& txTo, unsigned int nIn, unsigned int flags,
		int nHashType);

extern int GetTxPosHeight(const CDiskTxPos& txPos);
extern int GetTxPosHeight2(const CDiskTxPos& txPos, int nHeight);
extern int GetTxHashHeight(const uint256 txHash);
extern int CheckTransactionAtRelativeDepth(CBlockIndex* pindexBlock,
		const CCoins *txindex, int maxDepth);
//extern Value sendtoaddress(const Array& params, bool fHelp);

CScript RemoveAliasScriptPrefix(const CScript& scriptIn);
int GetAliasExpirationDepth();
CAmount GetAliasNetFee(const CTransaction& tx);

// refund an offer accept by creating a transaction to send coins to offer accepter, and an offer accept back to the offer owner. 2 Step process in order to use the coins that were sent during initial accept.
string getCurrencyToDRKSLKFromAlias(const vector<unsigned char> &vchCurrency, CAmount &nFee, const unsigned int &nHeightToFind, vector<string>& rateList, int &precision)
{
	vector<unsigned char> vchName = vchFromString("DRKSLK_RATES");
	string currencyCodeToFind = stringFromVch(vchCurrency);
	// check for alias existence in DB
	vector<CAliasIndex> vtxPos;
	if (!paliasdb->ReadAlias(vchName, vtxPos))
	{
		if(fDebug)
			printf("getCurrencyToDRKSLKFromAlias() Could not find DRKSLK_RATES alias\n");
		return "1";
	}
	if (vtxPos.size() < 1)
	{
		if(fDebug)
			printf("getCurrencyToDRKSLKFromAlias() Could not find DRKSLK_RATES alias (vtxPos.size() == 0)\n");
		return "1";
	}
	CAliasIndex foundAlias;
	for(unsigned int i=0;i<vtxPos.size();i++) {
        CAliasIndex a = vtxPos[i];
        if(a.nHeight <= nHeightToFind) {
            foundAlias = a;
        }
		else
			break;
    }
	if(foundAlias.IsNull())
		foundAlias = vtxPos.back();
	// get transaction pointed to by alias
	uint256 blockHash;
	uint256 txHash = foundAlias.txHash;
	CTransaction tx;
	if (!GetTransaction(txHash, tx, blockHash))
	{
		if(fDebug)
			printf("getCurrencyToDRKSLKFromAlias() Failed to read transaction from disk\n");
		return "1";
	}


	vector<unsigned char> vchValue;
	int nHeight;
	bool found = false;
	uint256 hash;
	if (GetValueOfAliasTxHash(txHash, vchValue, hash, nHeight)) {
		string value = stringFromVch(vchValue);	
		Value outerValue;
		if (read_string(value, outerValue))
		{
			Object outerObj = outerValue.get_obj();
			Value ratesValue = find_value(outerObj, "rates");
			if (ratesValue.type() == array_type)
			{
				Array codes = ratesValue.get_array();
				BOOST_FOREACH(Value& code, codes)
				{					
					Object codeObj = code.get_obj();					
					Value currencyNameValue = find_value(codeObj, "currency");
					Value currencyAmountValue = find_value(codeObj, "rate");
					CAmount AmountFromValue(const Value& value);
					CAmount nAmount = AmountFromValue(currencyAmountValue);
					if (currencyNameValue.type() == str_type)
					{		
						string currencyCode = currencyNameValue.get_str();
						rateList.push_back(currencyCode);
						if(currencyCodeToFind == currencyCode)
						{
							Value precisionValue = find_value(codeObj, "precision");
							if(precisionValue.type() == int_type)
							{
								precision = precisionValue.get_int();
							}
							if(currencyAmountValue.type() == real_type)
							{
								found = true;
								nFee = nAmount;
							}
							else if(currencyAmountValue.type() == int_type)
							{
								found = true;
								nFee = currencyAmountValue.get_int()*COIN;
							}
						}
					}
				}
			}
		}
	}
	else
	{
		if(fDebug)
			printf("getCurrencyToDRKSLKFromAlias() Failed to get value from alias\n");
		return "1";
	}
	if(!found)
	{
		if(fDebug)
			printf("getCurrencyToDRKSLKFromAlias() currency not found in DRKSLK_RATES alias\n");
		return "0";
	}
	return "";

}
void PutToAliasList(std::vector<CAliasIndex> &aliasList, CAliasIndex& index) {
	int i = aliasList.size() - 1;
	BOOST_REVERSE_FOREACH(CAliasIndex &o, aliasList) {
        if(index.nHeight != 0 && o.nHeight == index.nHeight) {
        	aliasList[i] = index;
            return;
        }
        else if(o.txHash != 0 && o.txHash == index.txHash) {
        	aliasList[i] = index;
            return;
        }
        i--;
	}
    aliasList.push_back(index);
}



int GetMinActivateDepth() {
	bool fTestNet;
	if (fTestNet)
		return MIN_ACTIVATE_DEPTH_TESTNET;
	else
		return MIN_ACTIVATE_DEPTH;
}

bool IsAliasOp(int op) {
	return op == OP_ALIAS_ACTIVATE 
		|| op == OP_ALIAS_UPDATE;
}

bool IsMyAlias(const CTransaction& tx, const CTxOut& txout) {
	const CScript& scriptPubKey = RemoveAliasScriptPrefix(txout.scriptPubKey);
	CScript scriptSig;
	txnouttype whichTypeRet;
	if (!Solver(*pwalletMain, scriptPubKey, 0, 0, scriptSig, whichTypeRet))
		return false;
	return true;
}

string aliasFromOp(int op) {
	switch (op) {
	case OP_ALIAS_UPDATE:
		return "aliasupdate";
	case OP_ALIAS_ACTIVATE:
		return "aliasactivate";
	default:
		return "<unknown alias op>";
	}
}

CAmount GetAliasNetFee(const CTransaction& tx) {
	CAmount nFee = 0;
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		if (out.scriptPubKey.size() == 1 && out.scriptPubKey[0] == OP_RETURN)
			nFee += out.nValue;
	}
	return nFee;
}

int GetAliasHeight(vector<unsigned char> vchName) {
	vector<CAliasIndex> vtxPos;
	if (paliasdb->ExistsAlias(vchName)) {
		if (!paliasdb->ReadAlias(vchName, vtxPos))
			return error("GetAliasHeight() : failed to read from alias DB");
		if (vtxPos.empty())
			return -1;
		CAliasIndex& txPos = vtxPos.back();
		return txPos.nHeight;
	}
	return -1;
}


/**
 * [IsAliasMine description]
 * @param  tx [description]
 * @return    [description]
 */
bool IsAliasMine(const CTransaction& tx) {
	if (tx.nVersion != DRKSLK_TX_VERSION)
		return false;

	vector<vector<unsigned char> > vvch;
	int op, nOut;

	if (!DecodeAliasTx(tx, op, nOut, vvch, -1))
		return false;

	if (!IsAliasOp(op))
		return false;

	const CTxOut& txout = tx.vout[nOut];
	if (IsMyAlias(tx, txout)) {
		printf("IsAliasMine()  : found my transaction %s value %d\n",
				tx.GetHash().GetHex().c_str(), (int) txout.nValue);
		return true;
	}

	return false;
}

bool IsAliasMine(const CTransaction& tx, const CTxOut& txout) {
	if (tx.nVersion != DRKSLK_TX_VERSION)
		return false;

	vector<vector<unsigned char> > vvch;
	int op;

	if (!DecodeAliasScript(txout.scriptPubKey, op, vvch))
		return false;

	if (!IsAliasOp(op))
		return false;

	if (IsMyAlias(tx, txout)) {
		printf("IsAliasMine()  : found my transaction %s value %d\n",
				tx.GetHash().GetHex().c_str(), (int) txout.nValue);
		return true;
	}
	return false;
}

bool CheckAliasInputs(CBlockIndex *pindexBlock, const CTransaction &tx,
		CValidationState &state, CCoinsViewCache &inputs, bool fBlock,
		bool fMiner, bool fJustCheck) {

	if (!tx.IsCoinBase()) {

		bool found = false;

	    const COutPoint &prevOutput = NULL;
		const CCoins *prevCoins = NULL;
		vector<vector<unsigned char> > vvchPrevArgs;
		int prevOp;
		// Strict check - bug disallowed
	    for (unsigned int i = 0; i < tx.vin.size(); i++) { 
			const CTxIn& in = tx.vin[i];
			prevOutput = &in.prevout;
			prevCoins = &inputs.AccessCoins(&prevOutput->hash);
			vector<vector<unsigned char> > vvchPrevArgs;
			int prevOp;
		  		if (DecodeAliasScript(prevCoins->vout[prevOutput->n].scriptPubKey, prevOp, vvchPrevArgs)) {
		  			found = true;
		  		}
	    }
		if (!found)vvchPrevArgs.clear();
		// Make sure alias outputs are not spent by a regular transaction, or the alias would be lost
		if (tx.nVersion != DRKSLK_TX_VERSION) {
			if (found)
				return error("CheckAliasInputs() : a non-darksilk transaction with a darksilk input");
			return true;
		}

		// decode alias info from transaction
		vector<vector<unsigned char> > vvchArgs;
		int op, nOut;
		int nDepth;
		if (!DecodeAliasTx(tx, op, nOut, vvchArgs, -1))
			return error(
					"CheckAliasInputs() : could not decode darksilk alias info from tx %s",
					tx.GetHash().GetHex().c_str());
		
		CAmount nNetFee;

		// unserialize offer object from txn, check for valid
		CAliasIndex theAlias(tx);

		if (theAlias.IsNull())
			error("CheckAliasInputs() : null alias object");
		if(theAlias.vValue.size() > MAX_VALUE_LENGTH)
		{
			return error("alias value too big");
		}
		if (vvchArgs[0].size() > MAX_NAME_LENGTH)
			return error("alias hex guid too long");
		switch (op) {

		case OP_ALIAS_ACTIVATE:

			// validate inputs
			if (found)
				return error(
						"CheckAliasInputs() : aliasactivate tx pointing to previous darksilk tx");

			if (vvchArgs[1].size() > 20)
				return error("aliasactivate tx with rand too big");
			if (vvchArgs[2].size() > MAX_VALUE_LENGTH)
				return error("aliasactivate tx with value too long");
			if (fBlock && !fJustCheck) {

					// check for enough fees
				nNetFee = GetAliasNetFee(tx);
				if (nNetFee < GetAliasNetworkFee(OP_ALIAS_ACTIVATE, theAlias.nHeight))
					return error(
							"CheckAliasInputs() : OP_ALIAS_ACTIVATE got tx %s with fee too low %lu",
							tx.GetHash().GetHex().c_str(),
							(long unsigned int) nNetFee);		
			}
			break;

		case OP_ALIAS_UPDATE:

			if (!found
					|| (prevOp != OP_ALIAS_ACTIVATE && prevOp != OP_ALIAS_UPDATE))
				return error("aliasupdate tx without previous update tx");

			if (vvchArgs[1].size() > MAX_VALUE_LENGTH)
				return error("aliasupdate tx with value too long");

			// Check name
			if (vvchPrevArgs[0] != vvchArgs[0])
				return error("CheckAliasInputs() : aliasupdate alias mismatch");

			if (fBlock && !fJustCheck) {
				// TODO CPU intensive
				nDepth = CheckTransactionAtRelativeDepth(pindexBlock, prevCoins,
						GetAliasExpirationDepth());
				if ((fBlock || fMiner) && nDepth < 0)
					return error(
							"CheckAliasInputs() : aliasupdate on an expired alias, or there is a pending transaction on the alias");
				// verify enough fees with this txn
				nNetFee = GetAliasNetFee(tx);
				if (nNetFee < GetAliasNetworkFee(OP_ALIAS_UPDATE, theAlias.nHeight))
					return error(
							"CheckAliasInputs() : OP_ALIAS_UPDATE got tx %s with fee too low %lu",
							tx.GetHash().GetHex().c_str(),
							(long unsigned int) nNetFee);
			}

			break;

		default:
			return error(
					"CheckAliasInputs() : alias transaction has unknown op");
		}

		if (fBlock || (!fBlock && !fMiner && !fJustCheck)) {

			// get the alias from the DB
			vector<CAliasIndex> vtxPos;
			if (paliasdb->ExistsAlias(vvchArgs[0]) && !fJustCheck) {
				if (!paliasdb->ReadAlias(vvchArgs[0], vtxPos))
					return error(
							"CheckAliasInputs() : failed to read from alias DB");
			}

			if (!fMiner && !fJustCheck
					&& pindexBlock->nHeight != pindexBest->nHeight) {
				
				int nHeight = pindexBlock->nHeight;

				CAliasIndex txPos2;		
				const vector<unsigned char> &vchVal = vvchArgs[
					op == OP_ALIAS_ACTIVATE ? 2 : 1];
				txPos2.nHeight = nHeight;
				txPos2.vValue = vchVal;
				txPos2.txHash = tx.GetHash();

				PutToAliasList(vtxPos, txPos2);

				{
				TRY_LOCK(cs_main, cs_trymain);

				if (!paliasdb->WriteAlias(vvchArgs[0], vtxPos))
					return error( "CheckAliasInputs() :  failed to write to alias DB");
				

				printf(
						"CONNECTED ALIAS: name=%s  op=%s  hash=%s  height=%d\n",
						stringFromVch(vvchArgs[0]).c_str(),
						aliasFromOp(op).c_str(),
						tx.GetHash().ToString().c_str(), nHeight);
				}
			}
			
		}
	}
	return true;
}

bool ExtractAliasAddress(const CScript& script, string& address) {
	if (script.size() == 1 && script[0] == OP_RETURN) {
		address = string("network fee");
		return true;
	}
	vector<vector<unsigned char> > vvch;
	int op;
	if (!DecodeAliasScript(script, op, vvch))
		return false;

	string strOp = aliasFromOp(op);
	string strName;

	strName = stringFromVch(vvch[0]);

	address = strOp + ": " + strName;
	return true;
}


string stringFromValue(const Value& value) {
	string strName = value.get_str();
	return strName;
}

vector<unsigned char> vchFromValue(const Value& value) {
	string strName = value.get_str();
	unsigned char *strbeg = (unsigned char*) strName.c_str();
	return vector<unsigned char>(strbeg, strbeg + strName.size());
}

std::vector<unsigned char> vchFromString(const std::string &str) {
	unsigned char *strbeg = (unsigned char*) str.c_str();
	return vector<unsigned char>(strbeg, strbeg + str.size());
}

string stringFromVch(const vector<unsigned char> &vch) {
	string res;
	vector<unsigned char>::const_iterator vi = vch.begin();
	while (vi != vch.end()) {
		res += (char) (*vi);
		vi++;
	}
	return res;
}
bool CAliasIndex::UnserializeFromTx(const CTransaction &tx) {
    try 
    {
        CDataStream dsAlias(vchFromString(DecodeBase64(stringFromVch(tx.data))), SER_NETWORK, PROTOCOL_VERSION);
        dsAlias >> *this;
    } 
    catch (std::exception &e) {
        return false;
    }
    return true;
}


string CAliasIndex::SerializeToString() {
    // serialize cert object
    CDataStream dsAlias(SER_NETWORK, PROTOCOL_VERSION);
    dsAlias << *this;
    vector<unsigned char> vchData(dsAlias.begin(), dsAlias.end());
    return EncodeBase64(vchData.data(), vchData.size());
}
bool CAliasDB::ScanNames(const std::vector<unsigned char>& vchName,
		unsigned int nMax,
		std::vector<std::pair<std::vector<unsigned char>, CAliasIndex> >& nameScan) {

	leveldb::Iterator *pcursor = paliasdb->NewIterator();

	CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
	ssKeySet << make_pair(string("namei"), vchName);
	string sType;
	pcursor->Seek(ssKeySet.str());

	while (pcursor->Valid()) {
		boost::this_thread::interruption_point();
		try {
			leveldb::Slice slKey = pcursor->key();
			CDataStream ssKey(slKey.data(), slKey.data() + slKey.size(),
					SER_DISK, CLIENT_VERSION);

			ssKey >> sType;
			if (sType == "namei") {
				vector<unsigned char> vchName;
				ssKey >> vchName;
				leveldb::Slice slValue = pcursor->value();
				CDataStream ssValue(slValue.data(),
						slValue.data() + slValue.size(), SER_DISK,
						CLIENT_VERSION);
				vector<CAliasIndex> vtxPos;
				ssValue >> vtxPos;
				CAliasIndex txPos;
				if (!vtxPos.empty())
					txPos = vtxPos.back();
				nameScan.push_back(make_pair(vchName, txPos));
			}
			if (nameScan.size() >= nMax)
				break;

			pcursor->Next();
		} catch (std::exception &e) {
			return error("%s() : deserialize error", __PRETTY_FUNCTION__);
		}
	}
	delete pcursor;
	return true;
}

void rescanforaliases(CBlockIndex *pindexRescan) {
	printf("Scanning blockchain for names to create fast index...\n");
	paliasdb->ReconstructNameIndex(pindexRescan);
}

bool CAliasDB::ReconstructNameIndex(CBlockIndex *pindexRescan) {
	CDiskTxPos txindex;
	CBlockIndex* pindex = pindexRescan;
	
	{
		TRY_LOCK(pwalletMain->cs_wallet, cs_trylock);
		while (pindex) {
			CBlock block;
			block.ReadFromDisk(pindex);
			int nHeight = pindex->nHeight;
			uint256 txblkhash;

			BOOST_FOREACH(CTransaction& tx, block.vtx) {

				if (tx.nVersion != DRKSLK_TX_VERSION)
					continue;

				vector<vector<unsigned char> > vvchArgs;
				int op, nOut;

				// decode the alias op
				bool o = DecodeAliasTx(tx, op, nOut, vvchArgs, -1);
				if (!o || !IsAliasOp(op))
					continue;

				const vector<unsigned char> &vchName = vvchArgs[0];
				const vector<unsigned char> &vchValue = vvchArgs[
						op == OP_ALIAS_ACTIVATE ? 2 : 1];

				if (!GetTransaction(tx.GetHash(), tx, txblkhash))
					continue;

				// if name exists in DB, read it to verify
				vector<CAliasIndex> vtxPos;
				if (ExistsAlias(vchName)) {
					if (!ReadAlias(vchName, vtxPos))
						return error(
								"ReconstructNameIndex() : failed to read from alias DB");
				}

				// rebuild the alias object, store to DB
				CAliasIndex txName;
				txName.nHeight = nHeight;
				txName.vValue = vchValue;
				txName.txHash = tx.GetHash();

				PutToAliasList(vtxPos, txName);

				if (!WriteAlias(vchName, vtxPos))
					return error(
							"ReconstructNameIndex() : failed to write to alias DB");

			
				printf(
						"RECONSTRUCT ALIAS: op=%s alias=%s value=%s hash=%s height=%d\n",
						aliasFromOp(op).c_str(), stringFromVch(vchName).c_str(),
						stringFromVch(vchValue).c_str(),
						tx.GetHash().ToString().c_str(), nHeight);

			} /* TX */
			pindex = pindex->pnext;
		} /* BLOCK */
		Flush();
	} /* LOCK */
	return true;
}

CAmount GetAliasNetworkFee(opcodetype seed, unsigned int nHeight) {
	CAmount nFee = 0;
	CAmount nRate = 0;
	const vector<unsigned char> &vchCurrency = vchFromString("USD");
	vector<string> rateList;
	int precision;
	if(getCurrencyToDRKSLKFromAlias(vchCurrency, nRate, nHeight, rateList, precision) != "")
	{
		if(seed==OP_ALIAS_ACTIVATE)
		{
			nFee = 100 * COIN;
		}
		else if(seed==OP_ALIAS_UPDATE)
		{
			nFee = 100 * COIN;
		}
	}
	else
	{
		// 50 pips USD, 10k pips = $1USD
		nFee = nRate/200;
	}
	// Round up to CENT
	nFee += CENT - 1;
	nFee = (nFee / CENT) * CENT;
	return nFee;
}


int GetAliasExpirationDepth() {
	return 525600;
}

// For display purposes, pass the name height.
int GetAliasDisplayExpirationDepth() {
	return GetAliasExpirationDepth();
}

CScript RemoveAliasScriptPrefix(const CScript& scriptIn) {
	int op;
	vector<vector<unsigned char> > vvch;
	CScript::const_iterator pc = scriptIn.begin();

	if (!DecodeAliasScript(scriptIn, op, vvch, pc))
		throw runtime_error(
				"RemoveAliasScriptPrefix() : could not decode name script");
	return CScript(pc, scriptIn.end());
}

bool SignNameSignature(const CTransaction& txFrom, CTransaction& txTo,
		unsigned int nIn, int nHashType = SIGHASH_ALL, CScript scriptPrereq =
				CScript()) 
{
	assert(nIn < txTo.vin.size());
	CTxIn& txin = txTo.vin[nIn];
	assert(txin.prevout.n < txFrom.vout.size());
	const CTxOut& txout = txFrom.vout[txin.prevout.n];

	// Leave out the signature from the hash, since a signature can't sign itself.
	// The checksig op will also drop the signatures from its hash.
	const CScript& scriptPubKey = RemoveAliasScriptPrefix(txout.scriptPubKey);
	uint256 hash = SignatureHash(scriptPrereq + txout.scriptPubKey, txTo, nIn,
			nHashType);
	txnouttype whichTypeRet;

	if (!Solver(*pwalletMain, scriptPubKey, hash, nHashType, txin.scriptSig,
			whichTypeRet))
		return false;

	txin.scriptSig = scriptPrereq + txin.scriptSig;

	// Test the solution
	if (scriptPrereq.empty())
		if (!VerifyScript(txin.scriptSig, txout.scriptPubKey, txTo, nIn, 0, 0))
			return false;

	return true;
}

bool CreateTransactionWithInputTx(const vector<pair<CScript, CAmount> >& vecSend,
		CWalletTx& wtxIn, int nTxOut, CWalletTx& wtxNew,
		CReserveKey& reservekey, CAmount& nFeeRet, const std::string& txData) {
	CAmount nValue = 0;
	BOOST_FOREACH(const PAIRTYPE(CScript, CAmount)& s, vecSend) {
		if (nValue < 0)
			return false;
		nValue += s.second;
	}
	if (vecSend.empty() || nValue < 0)
		return false;

	wtxNew.BindWallet(pwalletMain);

	{
		nFeeRet = nTransactionFee;
		while(true) {
			wtxNew.vin.clear();
			wtxNew.vout.clear();
			wtxNew.fFromMe = true;
			wtxNew.data = vchFromString(txData);

			CAmount nTotalValue = nValue + nFeeRet;
			printf("CreateTransactionWithInputTx: total value = %d\n",
					(int) nTotalValue);
			double dPriority = 0;

			// vouts to the payees
			BOOST_FOREACH(const PAIRTYPE(CScript, CAmount)& s, vecSend)
				wtxNew.vout.push_back(CTxOut(s.second, s.first));

			CAmount nWtxinCredit = wtxIn.vout[nTxOut].nValue;
			CAmount nAmountFree = nTotalValue - nWtxinCredit;

			// Choose coins to use
			set<pair<const CWalletTx*, unsigned int> > setCoins;
			CAmount nValueIn = 0;
			printf(
					"CreateTransactionWithInputTx: SelectCoins(%s), nTotalValue = %s, nWtxinCredit = %s\n",
					FormatMoney(nAmountFree).c_str(),
					FormatMoney(nTotalValue).c_str(),
					FormatMoney(nWtxinCredit).c_str());
			if (nAmountFree > 0) {
				if (!pwalletMain->SelectCoinsAlias(nAmountFree, setCoins, nValueIn))
					return false;
			}

			printf(
					"CreateTransactionWithInputTx: selected %d tx outs, nValueIn = %s\n",
					(int) setCoins.size(), FormatMoney(nValueIn).c_str());

			vector<pair<const CWalletTx*, unsigned int> > vecCoins(
					setCoins.begin(), setCoins.end());

			BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int)& coin, vecCoins) {
				 	CAmount nCredit = coin.first->vout[coin.second].nValue;
				dPriority += (double) nCredit
						* coin.first->GetDepthInMainChain();
			}

			// Input tx always at first position
			vecCoins.insert(vecCoins.begin(), make_pair(&wtxIn, nTxOut));

			nValueIn += nWtxinCredit;
			dPriority += (double) nWtxinCredit * wtxIn.GetDepthInMainChain();

			// Fill a vout back to self with any change
			CAmount nChange = nValueIn - nTotalValue;
			if (nChange >= CENT) {
				// Note: We use a new key here to keep it from being obvious which side is the change.
				//  The drawback is that by not reusing a previous key, the change may be lost if a
				//  backup is restored, if the backup doesn't have the new private key for the change.
				//  If we reused the old key, it would be possible to add code to look for and
				//  rediscover unknown transactions that were written with keys of ours to recover
				//  post-backup change.

				// Reserve a new key pair from key pool
				CPubKey pubkey;
				assert(reservekey.GetReservedKey(pubkey));

				// -------------- Fill a vout to ourself, using same address type as the payment
				// Now sending always to hash160 (GetBitcoinAddressHash160 will return hash160, even if pubkey is used)
				CScript scriptChange;
				if (Hash160(vecSend[0].first) != 0)
					scriptChange.SetDestination(pubkey.GetID());
				else
					scriptChange << pubkey << OP_CHECKSIG;

				// Insert change txn at random position:
				vector<CTxOut>::iterator position = wtxNew.vout.begin()
						+ GetRandInt(wtxNew.vout.size());
				wtxNew.vout.insert(position, CTxOut(nChange, scriptChange));
			} else
				reservekey.ReturnKey();

			// Fill vin
			BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int)& coin, vecCoins)
				wtxNew.vin.push_back(CTxIn(coin.first->GetHash(), coin.second));

			// Sign
			int nIn = 0;
			BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int)& coin, vecCoins) {
				if (coin.first == &wtxIn
						&& coin.second == (unsigned int) nTxOut) {
					if (!SignNameSignature(*coin.first, wtxNew, nIn++))
						throw runtime_error("could not sign name coin output");
				} else {
					if (!SignSignature(*pwalletMain, *coin.first, wtxNew,
							nIn++))
						return false;
				}
			}

			// Limit size
			unsigned int nBytes = ::GetSerializeSize(*(CTransaction*) &wtxNew,
					SER_NETWORK, PROTOCOL_VERSION);
			if (nBytes >= MAX_BLOCK_SIZE_GEN / 5)
				return false;
			dPriority /= nBytes;

			// Check that enough fee is included
			CAmount nPayFee = nTransactionFee * (1 + (int64_t) nBytes / 1000);
			bool fAllowFree = AllowFree(dPriority);
			CAmount nMinFee = wtxNew.GetMinFee(1, fAllowFree);
			if (nFeeRet < max(nPayFee, nMinFee)) {
				nFeeRet = max(nPayFee, nMinFee);
				printf(
						"CreateTransactionWithInputTx: re-iterating (nFreeRet = %s)\n",
						FormatMoney(nFeeRet).c_str());
				continue;
			}

			// Fill vtxPrev by copying from previous transactions vtxPrev
			wtxNew.AddSupportingTransactions();
			wtxNew.fTimeReceivedIsTxTime = true;

			break;
		}

	}

	printf("CreateTransactionWithInputTx succeeded:\n%s",
			wtxNew.ToString().c_str());
	return true;
}
void EraseAlias(CWalletTx& wtx)
{
	UnspendInputs(wtx);
	wtx.RemoveFromMemoryPool();
	pwalletMain->EraseFromWallet(wtx.GetHash());
}
// nTxOut is the output from wtxIn that we should grab
string SendMoneyWithInputTx(CScript scriptPubKey, CAmount nValue, CAmount nNetFee,
		CWalletTx& wtxIn, CWalletTx& wtxNew, bool fAskFee,
		const string& txData) 
{
	int nTxOut = IndexOfNameOutput(wtxIn);
	CReserveKey reservekey(pwalletMain);
	CAmount nFeeRequired;
	vector<pair<CScript, CAmount> > vecSend;
	vecSend.push_back(make_pair(scriptPubKey, nValue));

	if (nNetFee) {
		CScript scriptFee;
		scriptFee << OP_RETURN;
		vecSend.push_back(make_pair(scriptFee, nNetFee));
	}

	if (!CreateTransactionWithInputTx(vecSend, wtxIn, nTxOut, wtxNew,
			reservekey, nFeeRequired, txData)) {
		string strError;
		if (nValue + nFeeRequired > pwalletMain->GetBalance())
			strError =
					strprintf(
							_(
									"Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds "),
							FormatMoney(nFeeRequired).c_str());
		else
			strError = _("Error: Transaction creation failed  ");
		printf("SendMoney() : %s", strError.c_str());
		return strError;
	}

#ifdef GUI
	if (fAskFee && !uiInterface.ThreadSafeAskFee(nFeeRequired))
	return "ABORTED";
#else
	if (fAskFee && !true)
		return "ABORTED";
#endif

	if (!pwalletMain->CommitTransaction(wtxNew, reservekey))
	{

		return _(
				"Error: The transaction was rejected.");
	}
	return "";
}


bool GetValueOfAliasTxHash(const uint256 &txHash, vector<unsigned char>& vchValue, uint256& hash, int& nHeight) {
	nHeight = GetTxHashHeight(txHash);
	CTransaction tx;
	uint256 blockHash;

	if (!GetTransaction(txHash, tx, blockHash))
		return error("GetValueOfAliasTxHash() : could not read tx from disk");

	if (!GetValueOfAliasTx(tx, vchValue))
		return error("GetValueOfAliasTxHash() : could not decode value from tx");

	hash = tx.GetHash();
	return true;
}

bool GetValueOfName(CAliasDB& dbName, const vector<unsigned char> &vchName,
		vector<unsigned char>& vchValue, int& nHeight) {
	vector<CAliasIndex> vtxPos;
	if (!paliasdb->ReadAlias(vchName, vtxPos) || vtxPos.empty())
		return false;

	CAliasIndex& txPos = vtxPos.back();
	nHeight = txPos.nHeight;
	vchValue = txPos.vValue;
	return true;
}

bool GetTxOfAlias(CAliasDB& dbName, const vector<unsigned char> &vchName,
		CTransaction& tx) {
	vector<CAliasIndex> vtxPos;
	if (!paliasdb->ReadAlias(vchName, vtxPos) || vtxPos.empty())
		return false;
	CAliasIndex& txPos = vtxPos.back();
	int nHeight = txPos.nHeight;
	if (nHeight + GetAliasExpirationDepth()
			< pindexBest->nHeight) {
		string name = stringFromVch(vchName);
		printf("GetTxOfAlias(%s) : expired", name.c_str());
		return false;
	}

	uint256 hashBlock;
	if (!GetTransaction(txPos.txHash, tx, hashBlock))
		return error("GetTxOfAlias() : could not read tx from disk");

	return true;
}

bool GetAliasAddress(const CTransaction& tx, std::string& strAddress) {
	int op, nOut = 0;
	vector<vector<unsigned char> > vvch;

	if (!DecodeAliasTx(tx, op, nOut, vvch, -1))
		return error("GetAliasAddress() : could not decode name tx.");

	const CTxOut& txout = tx.vout[nOut];

	const CScript& scriptPubKey = RemoveAliasScriptPrefix(txout.scriptPubKey);

	CTxDestination dest;
	ExtractDestination(scriptPubKey, dest);
	strAddress = CDarkSilkAddress(dest).ToString();
	return true;
}

bool GetAliasAddress(const CDiskTxPos& txPos, std::string& strAddress) {
	CTransaction tx;
	if (!tx.ReadFromDisk(txPos))
		return error("GetAliasAddress() : could not read tx from disk");
	return GetAliasAddress(tx, strAddress);
}
void GetAliasValue(const std::string& strName, std::string& strAddress) {
	vector<unsigned char> vchName = vchFromValue(strName);
	if (!paliasdb->ExistsAlias(vchName))
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Alias not found");

	// check for alias existence in DB
	vector<CAliasIndex> vtxPos;
	if (!paliasdb->ReadAlias(vchName, vtxPos))
		throw JSONRPCError(RPC_WALLET_ERROR,
				"failed to read from alias DB");
	if (vtxPos.size() < 1)
		throw JSONRPCError(RPC_WALLET_ERROR, "no alias result returned");

	// get transaction pointed to by alias
	uint256 blockHash;
	CTransaction tx;
	uint256 txHash = vtxPos.back().txHash;
	if (!GetTransaction(txHash, tx, blockHash))
		throw JSONRPCError(RPC_WALLET_ERROR,
				"failed to read transaction from disk");

	GetAliasAddress(tx, strAddress);	
}

int IndexOfNameOutput(const CTransaction& tx) {
	vector<vector<unsigned char> > vvch;

	int op;
	int nOut;
	bool good = DecodeAliasTx(tx, op, nOut, vvch, -1);

	if (!good)
		throw runtime_error("IndexOfNameOutput() : name output not found");
	return nOut;
}

bool GetAliasOfTx(const CTransaction& tx, vector<unsigned char>& name) {
	if (tx.nVersion != DRKSLK_TX_VERSION)
		return false;
	vector<vector<unsigned char> > vvchArgs;
	int op;
	int nOut;

	bool good = DecodeAliasTx(tx, op, nOut, vvchArgs, -1);
	if (!good)
		return error("GetAliasOfTx() : could not decode a darksilk tx");

	switch (op) {
	case OP_ALIAS_ACTIVATE:
	case OP_ALIAS_UPDATE:
		name = vvchArgs[0];
		return true;
	}
	return false;
}


bool GetValueOfAliasTx(const CTransaction& tx, vector<unsigned char>& value) {
	vector<vector<unsigned char> > vvch;

	int op;
	int nOut;

	if (!DecodeAliasTx(tx, op, nOut, vvch, -1))
		return false;

	switch (op) {
	case OP_ALIAS_ACTIVATE:
		value = vvch[2];
		return true;
	case OP_ALIAS_UPDATE:
		value = vvch[1];
		return true;
	default:
		return false;
	}
}

bool DecodeAliasTx(const CTransaction& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch, int nHeight) {
	bool found = false;


	// Strict check - bug disallowed
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		vector<vector<unsigned char> > vvchRead;
		if (DecodeAliasScript(out.scriptPubKey, op, vvchRead)) {
			nOut = i;
			found = true;
			vvch = vvchRead;
			break;
		}
	}
	if (!found)
		vvch.clear();

	return found;
}
bool DecodeAliasTxInputs(const CTransaction& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch, CCoinsViewCache &inputs) {
	bool found = false;

	const COutPoint &prevOutput = NULL;
	const CCoins *prevCoins = NULL;
    // Strict check - bug disallowed
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxIn& in = tx.vin[i];
		prevOutput = &in.prevout;
		prevCoins = &inputs.AccessCoins(&prevOutput->hash);
        vector<vector<unsigned char> > vvchRead;
        if (DecodeAliasScript(prevCoins->vout[prevOutput->n].scriptPubKey, op, vvchRead)) {
            nOut = i; found = true; vvch = vvchRead;
            break;
        }
    }
	if (!found)
		vvch.clear();

	return found;
}
bool GetValueOfAliasTx(const CCoins& tx, vector<unsigned char>& value) {
	vector<vector<unsigned char> > vvch;

	int op;
	int nOut;

	if (!DecodeAliasTx(tx, op, nOut, vvch, -1))
		return false;

	switch (op) {
	case OP_ALIAS_ACTIVATE:
		value = vvch[2];
		return true;
	case OP_ALIAS_UPDATE:
		value = vvch[1];
		return true;
	default:
		return false;
	}
}

bool DecodeAliasTx(const CCoins& tx, int& op, int& nOut,
		vector<vector<unsigned char> >& vvch, int nHeight) {
	bool found = false;

	// Strict check - bug disallowed
	for (unsigned int i = 0; i < tx.vout.size(); i++) {
		const CTxOut& out = tx.vout[i];
		vector<vector<unsigned char> > vvchRead;
		if (DecodeAliasScript(out.scriptPubKey, op, vvchRead)) {
			nOut = i;
			found = true;
			vvch = vvchRead;
			break;
		}
	}
	if (!found)
		vvch.clear();
	return found;
}
bool DecodeAliasScript(const CScript& script, int& op,
		vector<vector<unsigned char> > &vvch) {
	CScript::const_iterator pc = script.begin();
	return DecodeAliasScript(script, op, vvch, pc);
}

bool DecodeAliasScript(const CScript& script, int& op,
		vector<vector<unsigned char> > &vvch, CScript::const_iterator& pc) {
	opcodetype opcode;
	if (!script.GetOp(pc, opcode))
		return false;
	if (opcode < OP_1 || opcode > OP_16)
		return false;

	op = CScript::DecodeOP_N(opcode);

	for (;;) {
		vector<unsigned char> vch;
		if (!script.GetOp(pc, opcode, vch))
			return false;
		if (opcode == OP_DROP || opcode == OP_2DROP || opcode == OP_NOP)
			break;
		if (!(opcode >= 0 && opcode <= OP_PUSHDATA4))
			return false;
		vvch.push_back(vch);
	}

	// move the pc to after any DROP or NOP
	while (opcode == OP_DROP || opcode == OP_2DROP || opcode == OP_NOP) {
		if (!script.GetOp(pc, opcode))
			break;
	}

	pc--;

	if ((op == OP_ALIAS_ACTIVATE && vvch.size() == 3)
			|| (op == OP_ALIAS_UPDATE && vvch.size() == 2))
		return true;
	return false;
}

Value aliasnew(const Array& params, bool fHelp) {
	if (fHelp || params.size() != 2 )
		throw runtime_error(
				"aliasnew <aliasname> <value>\n"
						"<aliasname> alias name.\n"
						"<value> alias value, 1023 chars max.\n"
						"Perform a first update after an aliasnew reservation.\n"
						"Note that the first update will go into a block 12 blocks after the aliasnew, at the soonest."
						+ HelpRequiringPassphrase());

	vector<unsigned char> vchName = vchFromValue(params[0]);
	uint64 rand = GetRand((uint64) -1);
	vector<unsigned char> vchRand = CBigNum(rand).getvch();
	vector<unsigned char> vchValue;

	vchValue = vchFromValue(params[1]);

	if (vchValue.size() > MAX_VALUE_LENGTH)
		throw runtime_error("alias value cannot exceed 1023 bytes!");


	CDarkSilkAddress myAddress = CDarkSilkAddress(stringFromVch(vchName));
	if(myAddress.IsValid() && !myAddress.isAlias)
		throw runtime_error("alias name cannot be a darksilk address!");

	CWalletTx wtx;
	wtx.nVersion = DRKSLK_TX_VERSION;

	CTransaction tx;
	if (GetTxOfAlias(*paliasdb, vchName, tx)) {
		error("aliasactivate() : this alias is already active with tx %s",
				tx.GetHash().GetHex().c_str());
		throw runtime_error("this alias is already active");
	}

	EnsureWalletIsUnlocked();

	

	CPubKey newDefaultKey;
	pwalletMain->GetKeyFromPool(newDefaultKey, false);
	CScript scriptPubKeyOrig;
	scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());
	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_ALIAS_ACTIVATE) << vchName
			<< vchRand << vchValue << OP_2DROP << OP_2DROP;
	scriptPubKey += scriptPubKeyOrig;

    // build cert object
    CAliasIndex newAlias;
	newAlias.nHeight = nBestHeight;

    string bdata = newAlias.SerializeToString();
	// calculate network fees
	int64 nNetFee = GetAliasNetworkFee(OP_ALIAS_ACTIVATE, nBestHeight);
	// use the script pub key to create the vecsend which sendmoney takes and puts it into vout
    vector< pair<CScript, int64> > vecSend;
    vecSend.push_back(make_pair(scriptPubKey, MIN_AMOUNT));
	
	CScript scriptFee;
	scriptFee << OP_RETURN;
	vecSend.push_back(make_pair(scriptFee, nNetFee));

	// send the tranasction
	string strError = pwalletMain->SendMoney(vecSend, MIN_AMOUNT, wtx, false, bdata);
	if (strError != "")
	{
		throw JSONRPCError(RPC_WALLET_ERROR, strError);
	}
	return wtx.GetHash().GetHex();
}

Value aliasupdate(const Array& params, bool fHelp) {
	if (fHelp || 2 > params.size() || 3 < params.size())
		throw runtime_error(
				"aliasupdate <aliasname> <value> [<toaddress>]\n"
						"Update and possibly transfer an alias.\n"
						"<aliasname> alias name.\n"
						"<value> alias value, 1023 chars max.\n"
                        "<toaddress> receiver darksilk address, if transferring alias.\n"
						+ HelpRequiringPassphrase());

	vector<unsigned char> vchName = vchFromValue(params[0]);
	vector<unsigned char> vchValue = vchFromValue(params[1]);
	if (vchValue.size() > 519)
		throw runtime_error("alias value cannot exceed 1023 bytes!");
	CWalletTx wtx, wtxIn;
	wtx.nVersion = DRKSLK_TX_VERSION;
	CScript scriptPubKeyOrig;

    if (params.size() == 3) {
		string strAddress = params[2].get_str();
		CDarkSilkAddress myAddress = CDarkSilkAddress(strAddress);
		if (!myAddress.IsValid())
			throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
					"Invalid darksilk address");
		scriptPubKeyOrig.SetDestination(myAddress.Get());
	} else {
		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey, false);
		scriptPubKeyOrig.SetDestination(newDefaultKey.GetID());
	}

	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_ALIAS_UPDATE) << vchName << vchValue
			<< OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;

	

	// check for existing pending aliases
	if (ExistsInMempool(vchName, OP_ALIAS_ACTIVATE) || ExistsInMempool(vchName, OP_ALIAS_UPDATE)) {
		throw runtime_error("there are pending operations on that alias");
	}

	EnsureWalletIsUnlocked();

	CTransaction tx;
	if (!GetTxOfAlias(*paliasdb, vchName, tx))
		throw runtime_error("could not find an alias with this name");

    if(!IsAliasMine(tx)) {
		throw runtime_error("Cannot modify a transferred alias");
    }

	if (!pwalletMain->GetTransaction(tx.GetHash(), wtxIn)) 
		throw runtime_error("this alias is not in your wallet");

    CAliasIndex updateAlias;
	updateAlias.nHeight = nBestHeight;

    string bdata = updateAlias.SerializeToString();
	int64 nNetFee = GetAliasNetworkFee(OP_ALIAS_UPDATE, nBestHeight);

	string strError = SendMoneyWithInputTx(scriptPubKey, MIN_AMOUNT,
			nNetFee, wtxIn, wtx, false, bdata);
	if (strError != "")
		throw JSONRPCError(RPC_WALLET_ERROR, strError);


	return wtx.GetHash().GetHex();
}

Value aliaslist(const Array& params, bool fHelp) {
	if (fHelp || 1 < params.size())
		throw runtime_error("aliaslist [<aliasname>]\n"
				"list my own aliases.\n"
				"<aliasname> alias name to use as filter.\n");
	
	vector<unsigned char> vchName;

	if (params.size() == 1)
		vchName = vchFromValue(params[0]);

	vector<unsigned char> vchNameUniq;
	if (params.size() == 1)
		vchNameUniq = vchFromValue(params[0]);
	Array oRes;
	map<vector<unsigned char>, int> vNamesI;
	map<vector<unsigned char>, Object> vNamesO;

	{
		uint256 blockHash;
		uint256 hash;
		CTransaction tx, dbtx;
	
		vector<unsigned char> vchValue;
		int nHeight;
		BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet) {
			// get txn hash, read txn index
			hash = item.second.GetHash();
			if (!GetTransaction(hash, tx, blockHash, true))
				continue;
			// skip non-darksilk txns
			if (tx.nVersion != DRKSLK_TX_VERSION)
				continue;

			// decode txn, skip non-alias txns
			vector<vector<unsigned char> > vvch;
			int op, nOut;
			if (!DecodeAliasTx(tx, op, nOut, vvch, -1) || !IsAliasOp(op))
				continue;
			// get the txn height
			nHeight = GetTxHashHeight(hash);

			// get the txn alias name
			if (!GetAliasOfTx(tx, vchName))
				continue;

			// skip this alias if it doesn't match the given filter value
			if (vchNameUniq.size() > 0 && vchNameUniq != vchName)
				continue;
			// get last active name only
			if (vNamesI.find(vchName) != vNamesI.end() && (nHeight < vNamesI[vchName] || vNamesI[vchName] < 0))
				continue;

			// Read the database for the latest alias (vtxPos.back()) and ensure it is not transferred (isaliasmine).. 
			// if it IS transferred then skip over this alias whenever it is found(above vNamesI check) in your mapwallet
			// check for alias existence in DB
			// will only read the alias from the db once per name to ensure that it is not mine.
			vector<CAliasIndex> vtxPos;
			if (vNamesI.find(vchName) == vNamesI.end() && paliasdb->ReadAlias(vchName, vtxPos))
			{
				if (vtxPos.size() > 0)
				{
					// get transaction pointed to by alias
					uint256 txHash = vtxPos.back().txHash;
					if(GetTransaction(txHash, dbtx, blockHash, true))
					{
					
						nHeight = GetTxHashHeight(txHash);
						// Is the latest alais in the db transferred?
						if(!IsAliasMine(dbtx))
						{	
							// by setting this to -1, subsequent aliases with the same name won't be read from disk (optimization) 
							// because the latest alias tx doesn't belong to us anymore
							vNamesI[vchName] = -1;
							continue;
						}
						else
						{
							// get the value of the alias txn of the latest alias (from db)
							GetValueOfAliasTx(dbtx, vchValue);
						}
					}
					
				}
			}
			else
			{
				GetValueOfAliasTx(tx, vchValue);
			}
			int expired = 0;
			int expires_in = 0;
			int expired_block = 0;
			// build the output object
			Object oName;
			oName.push_back(Pair("name", stringFromVch(vchName)));
			oName.push_back(Pair("value", stringFromVch(vchValue)));
			oName.push_back(Pair("lastupdate_height", nHeight));
			expired_block = nHeight + GetAliasDisplayExpirationDepth();
			if(nHeight + GetAliasDisplayExpirationDepth() - pindexBest->nHeight <= 0)
			{
				expired = 1;
			}  
			if(expired == 0)
			{
				expires_in = nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight;
			}
			oName.push_back(Pair("expires_in", expires_in));
			oName.push_back(Pair("expires_on", expired_block));
			oName.push_back(Pair("expired", expired));
			vNamesI[vchName] = nHeight;
			vNamesO[vchName] = oName;					

		}
	}

	BOOST_FOREACH(const PAIRTYPE(vector<unsigned char>, Object)& item, vNamesO)
		oRes.push_back(item.second);

	return oRes;
}

/**
 * [aliasinfo description]
 * @param  params [description]
 * @param  fHelp  [description]
 * @return        [description]
 */
Value aliasinfo(const Array& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("aliasinfo <aliasname>\n"
				"Show values of an alias.\n");
	vector<unsigned char> vchName = vchFromValue(params[0]);
	CTransaction tx;
	Object oShowResult;

	{

		// check for alias existence in DB
		vector<CAliasIndex> vtxPos;
		if (!paliasdb->ReadAlias(vchName, vtxPos))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to read from alias DB");
		if (vtxPos.size() < 1)
			throw JSONRPCError(RPC_WALLET_ERROR, "no result returned");

		// get transaction pointed to by alias
		uint256 blockHash;
		uint256 txHash = vtxPos.back().txHash;
		if (!GetTransaction(txHash, tx, blockHash, true))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to read transaction from disk");

		Object oName;
		vector<unsigned char> vchValue;
		int nHeight;
		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;
		uint256 hash;
		if (GetValueOfAliasTxHash(txHash, vchValue, hash, nHeight)) {
			oName.push_back(Pair("name", stringFromVch(vchName)));
			string value = stringFromVch(vchValue);
			oName.push_back(Pair("value", value));
			oName.push_back(Pair("txid", tx.GetHash().GetHex()));
			string strAddress = "";
			GetAliasAddress(tx, strAddress);
			oName.push_back(Pair("address", strAddress));
			bool fAliasMine = IsAliasMine(tx)? true:  false;
			oName.push_back(Pair("isaliasmine", fAliasMine));
			bool fMine = pwalletMain->IsMine(tx)? true:  false;
			oName.push_back(Pair("ismine", fMine));
            oName.push_back(Pair("lastupdate_height", nHeight));
			expired_block = nHeight + GetAliasDisplayExpirationDepth();
			if(nHeight + GetAliasDisplayExpirationDepth() - pindexBest->nHeight <= 0)
			{
				expired = 1;
			}  
			if(expired == 0)
			{
				expires_in = nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight;
			}
			oName.push_back(Pair("expires_in", expires_in));
			oName.push_back(Pair("expires_on", expired_block));
			oName.push_back(Pair("expired", expired));
			oShowResult = oName;
		}
	}
	return oShowResult;
}

/**
 * [aliashistory description]
 * @param  params [description]
 * @param  fHelp  [description]
 * @return        [description]
 */
Value aliashistory(const Array& params, bool fHelp) {
	if (fHelp || 1 != params.size())
		throw runtime_error("aliashistory <aliasname>\n"
				"List all stored values of an alias.\n");
	Array oRes;
	vector<unsigned char> vchName = vchFromValue(params[0]);
	string name = stringFromVch(vchName);

	{
		vector<CAliasIndex> vtxPos;
		if (!paliasdb->ReadAlias(vchName, vtxPos))
			throw JSONRPCError(RPC_WALLET_ERROR,
					"failed to read from alias DB");

		CAliasIndex txPos2;
		uint256 txHash;
		uint256 blockHash;
		BOOST_FOREACH(txPos2, vtxPos) {
			txHash = txPos2.txHash;
			CTransaction tx;
			int expired = 0;
			int expires_in = 0;
			int expired_block = 0;
			Object oName;
			vector<unsigned char> vchValue;
			int nHeight;
			uint256 hash;
			if (GetValueOfAliasTxHash(txHash, vchValue, hash, nHeight)) {
				oName.push_back(Pair("name", name));
				string value = stringFromVch(vchValue);
				oName.push_back(Pair("value", value));
				oName.push_back(Pair("txid", tx.GetHash().GetHex()));
				string strAddress = "";
				GetAliasAddress(tx, strAddress);
				oName.push_back(Pair("address", strAddress));
	            oName.push_back(Pair("lastupdate_height", nHeight));
				expired_block = nHeight + GetAliasDisplayExpirationDepth();
				if(nHeight + GetAliasDisplayExpirationDepth() - pindexBest->nHeight <= 0)
				{
					expired = 1;
				}  
				if(expired == 0)
				{
					expires_in = nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight;
				}
				oName.push_back(Pair("expires_in", expires_in));
				oName.push_back(Pair("expires_on", expired_block));
				oName.push_back(Pair("expired", expired));
				oRes.push_back(oName);
			}
		}
	}
	return oRes;
}

/**
 * [aliasfilter description]
 * @param  params [description]
 * @param  fHelp  [description]
 * @return        [description]
 */
Value aliasfilter(const Array& params, bool fHelp) {
	if (fHelp || params.size() > 5)
		throw runtime_error(
				"aliasfilter [[[[[regexp] maxage=36000] from=0] nb=0] stat]\n"
						"scan and filter aliases\n"
						"[regexp] : apply [regexp] on aliases, empty means all aliases\n"
						"[maxage] : look in last [maxage] blocks\n"
						"[from] : show results from number [from]\n"
						"[nb] : show [nb] results, 0 means all\n"
						"[stat] : show some stats instead of results\n"
						"aliasfilter \"\" 5 # list aliases updated in last 5 blocks\n"
						"aliasfilter \"^name\" # list all aliases starting with \"name\"\n"
						"aliasfilter 36000 0 0 stat # display stats (number of names) on active aliases\n");

	string strRegexp;
	int nFrom = 0;
	int nNb = 0;
	int nMaxAge = 36000;
	bool fStat = false;
	int nCountFrom = 0;
	int nCountNb = 0;
	/* when changing this to match help, review bitcoinrpc.cpp RPCConvertValues() */
	if (params.size() > 0)
		strRegexp = params[0].get_str();

	if (params.size() > 1)
		nMaxAge = params[1].get_int();

	if (params.size() > 2)
		nFrom = params[2].get_int();

	if (params.size() > 3)
		nNb = params[3].get_int();

	if (params.size() > 4)
		fStat = (params[4].get_str() == "stat" ? true : false);

	Array oRes;

	vector<unsigned char> vchName;
	vector<pair<vector<unsigned char>, CAliasIndex> > nameScan;
	if (!paliasdb->ScanNames(vchName, 100000000, nameScan))
		throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

	pair<vector<unsigned char>, CAliasIndex> pairScan;
	BOOST_FOREACH(pairScan, nameScan) {
		string name = stringFromVch(pairScan.first);
		string nameToSearch = name;
		std::transform(nameToSearch.begin(), nameToSearch.end(), nameToSearch.begin(), ::tolower);
		std::transform(strRegexp.begin(), strRegexp.end(), strRegexp.begin(), ::tolower);
		// regexp
		using namespace boost::xpressive;
		smatch nameparts;
		sregex cregex = sregex::compile(strRegexp);
		if (strRegexp != "" && !regex_search(nameToSearch, nameparts, cregex))
			continue;

		CAliasIndex txName = pairScan.second;
		int nHeight = txName.nHeight;

		// max age
		if (nMaxAge != 0 && pindexBest->nHeight - nHeight >= nMaxAge)
			continue;

		// from limits
		nCountFrom++;
		if (nCountFrom < nFrom + 1)
			continue;


		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;
		Object oName;
		oName.push_back(Pair("name", name));
		CTransaction tx;
		uint256 blockHash;
		uint256 txHash = txName.txHash;
		if (!GetTransaction(txHash, tx, blockHash, true))
			continue;

		vector<unsigned char> vchValue;
		GetValueOfAliasTx(tx, vchValue);
		string value = stringFromVch(vchValue);
		oName.push_back(Pair("value", value));
		oName.push_back(Pair("txid", txHash.GetHex()));
        oName.push_back(Pair("lastupdate_height", nHeight));
		expired_block = nHeight + GetAliasDisplayExpirationDepth();
        if(nHeight + GetAliasDisplayExpirationDepth() - pindexBest->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight;
		}
		oName.push_back(Pair("expires_in", expires_in));
		oName.push_back(Pair("expires_on", expired_block));
		oName.push_back(Pair("expired", expired));

		
		oRes.push_back(oName);

		nCountNb++;
		// nb limits
		if (nNb > 0 && nCountNb >= nNb)
			break;
	}

	if (fStat) {
		Object oStat;
		oStat.push_back(Pair("blocks", (int) nBestHeight));
		oStat.push_back(Pair("count", (int) oRes.size()));
		return oStat;
	}

	return oRes;
}

/**
 * [aliasscan description]
 * @param  params [description]
 * @param  fHelp  [description]
 * @return        [description]
 */
Value aliasscan(const Array& params, bool fHelp) {
	if (fHelp || 2 > params.size())
		throw runtime_error(
				"aliasscan [<start-name>] [<max-returned>]\n"
						"scan all aliases, starting at start-name and returning a maximum number of entries (default 500)\n");

	vector<unsigned char> vchName;
	int nMax = 500;
	if (params.size() > 0)
		vchName = vchFromValue(params[0]);
	if (params.size() > 1) {
		Value vMax = params[1];
		ConvertTo<double>(vMax);
		nMax = (int) vMax.get_real();
	}

	Array oRes;

	vector<pair<vector<unsigned char>, CAliasIndex> > nameScan;
	if (!paliasdb->ScanNames(vchName, nMax, nameScan))
		throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

	pair<vector<unsigned char>, CAliasIndex> pairScan;
	BOOST_FOREACH(pairScan, nameScan) {
		Object oName;
		string name = stringFromVch(pairScan.first);
		oName.push_back(Pair("name", name));
		CTransaction tx;
		CAliasIndex txName = pairScan.second;
		uint256 blockHash;
		int expired = 0;
		int expires_in = 0;
		int expired_block = 0;
		int nHeight = txName.nHeight;
		if (!GetTransaction(txName.txHash, tx, blockHash, true))
			continue;

		vector<unsigned char> vchValue = txName.vValue;

		string value = stringFromVch(vchValue);
		oName.push_back(Pair("txid", txName.txHash.GetHex()));
		oName.push_back(Pair("value", value));
        oName.push_back(Pair("lastupdate_height", nHeight));
		expired_block = nHeight + GetAliasDisplayExpirationDepth();
		if(nHeight + GetAliasDisplayExpirationDepth() - pindexBest->nHeight <= 0)
		{
			expired = 1;
		}  
		if(expired == 0)
		{
			expires_in = nHeight + GetCertDisplayExpirationDepth() - pindexBest->nHeight;
		}
		oName.push_back(Pair("expires_in", expires_in));
		oName.push_back(Pair("expires_on", expired_block));
		oName.push_back(Pair("expired", expired));
		
		oRes.push_back(oName);
	}

	return oRes;
}

void UnspendInputs(CWalletTx& wtx) {
	set<CWalletTx*> setCoins;
	BOOST_FOREACH(const CTxIn& txin, wtx.vin) {
		if (!pwalletMain->IsMine(txin)) {
			printf("UnspendInputs(): !mine %s", txin.ToString().c_str());
			continue;
		}
		CWalletTx& prev = pwalletMain->mapWallet[txin.prevout.hash];
		unsigned int nOut = txin.prevout.n;

		printf("UnspendInputs(): %s:%d spent %d\n",
				prev.GetHash().ToString().c_str(), nOut, prev.IsSpent(nOut));

		if (nOut >= prev.vout.size())
			throw runtime_error("CWalletTx::MarkSpent() : nOut out of range");
		prev.vfSpent.resize(prev.vout.size());
		if (prev.vfSpent[nOut]) {
			prev.vfSpent[nOut] = false;
			prev.fAvailableCreditCached = false;
			prev.WriteToDisk();
		}

	}
}


/**
 * [aliasscan description]
 * @param  params [description]
 * @param  fHelp  [description]
 * @return        [description]
 */
Value getaliasfees(const Array& params, bool fHelp) {
	if (fHelp || 0 != params.size())
		throw runtime_error(
				"getaliasfees\n"
						"get current service fees for alias transactions\n");
	Object oRes;
	oRes.push_back(Pair("height", nBestHeight ));
	oRes.push_back(Pair("activate_fee", ValueFromAmount(GetAliasNetworkFee(OP_ALIAS_ACTIVATE, nBestHeight) )));
	oRes.push_back(Pair("update_fee", ValueFromAmount(GetAliasNetworkFee(OP_ALIAS_UPDATE, nBestHeight) )));
	return oRes;

}

