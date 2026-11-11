#include "helpers.hpp"
#include <sstream>
#include <boost/multiprecision/cpp_int.hpp>

using namespace std;
using namespace pando;
using namespace rapidjson;

typedef boost::multiprecision::uint128_t uint128_t;

template<typename ...Args>
void fail_(const DBAccess *access, const char* filter_name, const char* filter_fail_tag, Args && ...args) {
    cerr << "[ERROR] unable to run filter: " << filter_name;
    (cerr << " " << ... << args);
    cerr << endl;
    access->add_tag.run(&access->add_tag, filter_fail_tag);
}

bool rollback([[maybe_unused]] const DBAccess *access) {return true;}

bool should_run_eth_based_tx_vals(const DBAccess* access, const char* crypto_tag, const char* filter_done_tag, const char* filter_fail_tag, const char* inactive_tag) {
	auto tags = access->tags;
	bool found_block = false;
	bool found_tx = false;

	while ((*tags)[0] != '\0') {
	    if (string(*tags) == crypto_tag)
		found_block = true;
	    if (string(*tags) == "tx_csv")
		found_tx = true;
	    if (string(*tags) == inactive_tag)
		return false;
	    if (string(*tags) == filter_done_tag)
		return false;
	    if (string(*tags) == filter_fail_tag)
		return false;
	    ++tags;
	}

	if (found_block && found_tx)
	    return true;
	return false;

}

void eth_based_tx_vals(const DBAccess *access, const char* crypto_tag, const char* filter_name, const char* filter_done_tag, const char* filter_fail_tag, const char* inactive_tag) {
    const uint32_t crypto_id = get_blockchain_key(crypto_tag);
	
    //Parse the tx a bit further
    Document d;
    d.Parse(access->value);

    if (d.HasParseError()) return fail_(access, filter_name, filter_fail_tag, "cannot parse");

    // Find the transactions in the json
    if (!d.HasMember("transactions")) return fail_(access, filter_name, filter_fail_tag, "no member transactions");
    auto &tx = d["transactions"];
    if (!tx.IsObject()) return fail_(access, filter_name, filter_fail_tag, "tx is not an object");
    if (!tx.HasMember("hash")) return fail_(access, filter_name, filter_fail_tag, "hash not found");
    auto &txid = tx["hash"];
    if (!txid.IsString()) return fail_(access, filter_name, filter_fail_tag, "txid is not a string");
    string txid_s;
    txid_s.assign(txid.GetString());

    vtx_t tx_hash_key;
    stringstream tx_hash_ss;
    tx_hash_ss << hex << txid_s.substr(0,15);
    tx_hash_ss >> tx_hash_key;

    //Lookup the time of the transaction
    chain_info_t chain_info = pack_chain_info(crypto_id, TXTIME_KEY, 0);
    vtx_t src_key;
    stringstream ss;
    ss << txid_s.substr(0,15);
    ss >> hex >> src_key;
    dbkey_t time_lookup_key { chain_info, src_key, 0 };

    char* time_ret = nullptr;
    access->get_entry_by_key.run(&access->get_entry_by_key, time_lookup_key, &time_ret);
    string time_ret_s;
    time_t timestamp;
    if (time_ret != nullptr) {
        time_ret_s.assign(time_ret);
        free(time_ret);
        timestamp = stoi(time_ret_s);
    } else {
        access->add_tag.run(&access->add_tag, inactive_tag);
        access->subscribe_to_entry.run(&access->subscribe_to_entry, access->key, time_lookup_key, inactive_tag);
        return;
    }

    //For each vout value, create a new entry for it
    if (!tx.HasMember("value")) return fail_(access, filter_name, filter_fail_tag, "tx had no member \"value\"");
    auto &value_str = tx["value"];
    if (!value_str.IsString()) return fail_(access, filter_name, filter_fail_tag, "\"value\" was not a string");
    //convert value to a float
    stringstream value_ss (value_str.GetString());
    uint128_t value_wei_int;
    value_ss >> hex >> value_wei_int; 

    //Create an entry for the out value in ETH

    double value_wei = double(value_wei_int);
    uint64_t wei_to_eth = 1000000000000000000ull;
    double value = value_wei / wei_to_eth;
    
    string lower_crypto_name = string(crypto_tag);
    for (auto& x : lower_crypto_name) {
        x = tolower(x);
    }

    string crypto_out_tag = "out_val_" + lower_crypto_name;
    const char* new_tags[] = {crypto_tag, crypto_out_tag.c_str(), ""};
    chain_info_t ci = pack_chain_info(crypto_id, OUT_VAL_KEY, 0);
    vtx_t b_key = timestamp;
    dbkey_t new_key = {ci, b_key, tx_hash_key};
    string val_eth_s = to_string(value);
    access->make_new_entry.run(&access->make_new_entry, new_tags, val_eth_s.c_str(), new_key);
    //Create the equivalent entry converted to USD
    float exchange_rate = get_exchange_rate(timestamp, access, inactive_tag, crypto_id);
    if (exchange_rate < 0) {
         return;
    }
    const char* new_tags_usd[] = {crypto_tag, "out_val_usd", ""};
    chain_info_t ci_usd = pack_chain_info(USD_KEY, OUT_VAL_KEY, 0);
    dbkey_t new_key_usd = {ci_usd, b_key, tx_hash_key};
    string val_usd_s = to_string(value * exchange_rate);
    access->make_new_entry.run(&access->make_new_entry, new_tags_usd, val_usd_s.c_str(), new_key_usd);

    // Add a tag indicating parsing was successful
    access->add_tag.run(&access->add_tag, filter_done_tag);
}

