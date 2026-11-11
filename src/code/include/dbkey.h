#ifndef DBKEY_H_
#define DBKEY_H_

#include "types.h"
#include "string.h"

/** @brief the DB key object (similar to an edge) */
typedef struct dbkey_t {
    chain_info_t a;
    vtx_t b;
    vtx_t c;
} dbkey_t;


#ifdef __cplusplus
namespace std {
    // From Boost: hash_combine_impl
    // https://www.boost.org/doc/libs/1_71_0/boost/container_hash/hash.hpp
	// Templated by https://stackoverflow.com/questions/7110301/generic-hash-for-tuples-in-unordered-map-unordered-set
    template <class T>
    inline void my_hash_combine(std::size_t& seed, T const& v)
    {
        seed ^= hash<T>()(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
    }

    template<>
    struct hash<dbkey_t> {
        std::size_t operator()(const dbkey_t &k) const {
            size_t res = 0x0;
            my_hash_combine(res, k.a);
            my_hash_combine(res, k.b);
            my_hash_combine(res, k.c);
            return res;
        }
    };
}

/** @brief Define the == operator for dbkey_t */
inline bool operator==(const dbkey_t& a, const dbkey_t& b) { return (a.a == b.a && a.b == b.b && a.c == b.c); }
/** @brief Define the != operator for dbkey_t */
inline bool operator!=(const dbkey_t& a, const dbkey_t& b) { return !(a == b); }
/** @brief Define the < operator for dbkey_t */
inline bool operator<(const dbkey_t& a, const dbkey_t& b) {
    if (a.a != b.a) return (a.a < b.a);
    else if (a.b != b.b) return (a.b < b.b);
    else if (a.c != b.c) return (a.c < b.c);
    else return false;
}
/** @brief Define the > operator for dbkey_t */
inline bool operator>(const dbkey_t& a, const dbkey_t& b) {return (b < a);}
#endif

/** @brief the initial value used for invalid/new keys */
const chain_info_t INIT_CH_IN = (1llu<<63);
const dbkey_t INITIAL_KEY = {INIT_CH_IN, -1, -1};

/** @brief Pack a 32-bit int and two 16-bit ints into a single 64-bit int. */
inline chain_info_t pack_chain_info(uint32_t a, uint16_t b, uint16_t c) {
    chain_info_t result = a;
    result = (result << 16) | b;
    result = (result << 16) | c;
    return result;
}

/** @brief Pack a 32-bit int and two 16-bit ints into a single 64-bit int. */
inline void unpack_chain_info(chain_info_t input, uint32_t* a, uint16_t* b, uint16_t* c) {
    if (a != NULL)
        *a = (uint32_t)((input & 0xFFFFFFFF00000000) >> 32);
    if (b != NULL)
        *b = (uint16_t)((input & 0xFFFF0000) >> 16);
    if (c != NULL)
        *c = (uint16_t)((input & 0xFFFF) );
}

inline bool is_random_key(dbkey_t k) {
    return (((k.a >> 63) & 1) == 1);
}
/** Sequencing Key Constants, 31 bits **/
#define SHORT (uint32_t)1
#define SEQ (uint32_t)0
#define QUAL (uint32_t)2
#define KMER_COUNTS (uint32_t)3
#define KMER_INDEX (uint32_t)4
#define READ_KMER_DELTA (uint32_t)5 
#define OVERLAP_CANDIDATES (uint32_t)6

/** Blockchain Key Constants, 31 bits **/
#define BTC_KEY (uint32_t)0
#define ZEC_KEY (uint32_t)1
#define ETH_KEY (uint32_t)2
#define XMR_KEY (uint32_t)3
#define USD_KEY (uint32_t)4
#define DGB_KEY (uint32_t)5
#define DOGE_KEY (uint32_t)6
#define BCH_KEY (uint32_t)7
#define POLY_KEY (uint32_t)8
#define DASH_KEY (uint32_t)9
#define LTC_KEY (uint32_t)10
#define FUN_KEY (uint32_t)11
#define BLK_KEY (uint32_t)12
#define CVC_KEY (uint32_t)13
#define USDT_KEY (uint32_t)14
#define RDD_KEY (uint32_t)15
#define OMG_KEY (uint32_t)16
#define TUSD_KEY (uint32_t)17
#define GNT_KEY (uint32_t)18
#define SC_KEY (uint32_t)19
#define ATOM_KEY (uint32_t)20
#define XRP_KEY (uint32_t)21
#define MKR_KEY (uint32_t)22
#define MANA_KEY (uint32_t)23
#define STORJ_KEY (uint32_t)24
#define BNT_KEY (uint32_t)25
#define GNO_KEY (uint32_t)26
#define SNT_KEY (uint32_t)27
#define ZRX_KEY (uint32_t)28
#define RLC_KEY (uint32_t)29
#define ETC_KEY (uint32_t)30
#define BAT_KEY (uint32_t)31
#define REP_KEY (uint32_t)32
#define DNT_KEY (uint32_t)33
#define DAI_KEY (uint32_t)34
#define USDC_KEY (uint32_t)35
#define RCN_KEY (uint32_t)36
#define OMNI_KEY (uint16_t)37
#define BNB_KEY (uint32_t)38
#define MATIC_KEY (uint32_t)39
#define PRODUCT_ENGAGEMENT_KEY (uint32_t)40
#define ETH_DAG_KEY (uint32_t)41

/** Filter-specific Key Constants, 16 bits */
#define TX_IN_EDGE_KEY (uint16_t)0
#define TX_OUT_EDGE_KEY (uint16_t)1
#define UTXO_EDGE (uint16_t)2
#define UTXO_STATS (uint16_t)3
#define TXTIME_KEY (uint16_t)4
#define DISTANCE_KEY (uint16_t)5
#define BFS_INITIAL_VALUE (uint16_t)6
#define EXCHANGE_RATE_KEY (uint16_t)7
#define OUT_VAL_KEY (uint16_t)8
#define OUT_VAL_ROUNDED_KEY (uint16_t)9
#define ETL_TOKEN_KEY (uint16_t)10
#define ETL_TX_TOKEN_KEY (uint16_t)11
#define SHAPESHIFT_KEY (uint16_t)12
#define VOUT_ADDR_KEY (uint16_t)13
#define BLOCK_NUMBER_LOOKUP_KEY (uint16_t)14
#define TX_KEY (uint16_t)15
#define ADDR_KEY (uint16_t)16
#define PRODUCT_ID_KEY (uint16_t)17
#define ETH_SOURCE_ROARING_BITMAP_KEY (uint16_t)18
#define ETH_DEST_ROARING_BITMAP_KEY (uint16_t)18

#define NOT_UTXO_KEY (uint16_t)0
#define UTXO_KEY (uint16_t)1

inline uint32_t get_blockchain_key(const char* blockchain) {
    if (strcmp(blockchain, "BTC") == 0)
        return BTC_KEY;
    else if (strcmp(blockchain, "ZEC") == 0)
        return ZEC_KEY;
    else if (strcmp(blockchain, "ETH") == 0)
        return ETH_KEY;
    else if (strcmp(blockchain, "XMR") == 0)
        return XMR_KEY;
    else if (strcmp(blockchain, "DGB") == 0)
        return DGB_KEY;
    else if (strcmp(blockchain, "DOGE") == 0)
        return DOGE_KEY;
    else if (strcmp(blockchain, "BCH") == 0)
        return BCH_KEY;
    else if (strcmp(blockchain, "POLY") == 0)
        return POLY_KEY;
    else if (strcmp(blockchain, "DASH") == 0)
        return DASH_KEY;
    else if (strcmp(blockchain, "LTC") == 0)
        return LTC_KEY;
    else if (strcmp(blockchain, "FUN") == 0)
        return FUN_KEY;
    else if (strcmp(blockchain, "BLK") == 0)
        return BLK_KEY;
    else if (strcmp(blockchain, "CVC") == 0)
        return CVC_KEY;
    else if (strcmp(blockchain, "USDT") == 0)
        return USDT_KEY;
    else if (strcmp(blockchain, "RDD") == 0)
        return RDD_KEY;
    else if (strcmp(blockchain, "OMG") == 0)
        return OMG_KEY;
    else if (strcmp(blockchain, "TUSD") == 0)
        return TUSD_KEY;
    else if (strcmp(blockchain, "GNT") == 0)
        return GNT_KEY;
    else if (strcmp(blockchain, "SC") == 0)
        return SC_KEY;
    else if (strcmp(blockchain, "ATOM") == 0)
        return ATOM_KEY;
    else if (strcmp(blockchain, "XRP") == 0)
        return XRP_KEY;
    else if (strcmp(blockchain, "MKR") == 0)
        return MKR_KEY;
    else if (strcmp(blockchain, "MANA") == 0)
        return MANA_KEY;
    else if (strcmp(blockchain, "STORJ") == 0)
        return STORJ_KEY;
    else if (strcmp(blockchain, "BNT") == 0)
        return BNT_KEY;
    else if (strcmp(blockchain, "GNO") == 0)
        return GNO_KEY;
    else if (strcmp(blockchain, "SNT") == 0)
        return SNT_KEY;
    else if (strcmp(blockchain, "ZRX") == 0)
        return ZRX_KEY;
    else if (strcmp(blockchain, "RLC") == 0)
        return RLC_KEY;
    else if (strcmp(blockchain, "ETC") == 0)
        return ETC_KEY;
    else if (strcmp(blockchain, "BAT") == 0)
        return BAT_KEY;
    else if (strcmp(blockchain, "REP") == 0)
        return REP_KEY;
    else if (strcmp(blockchain, "DNT") == 0)
        return DNT_KEY;
    else if (strcmp(blockchain, "DAI") == 0)
        return DAI_KEY;
    else if (strcmp(blockchain, "USDC") == 0)
        return USDC_KEY;
    else if (strcmp(blockchain, "RCN") == 0)
        return RCN_KEY;
    else if (strcmp(blockchain, "MATIC") == 0)
        return MATIC_KEY;
    else if (strcmp(blockchain, "ETL_TOKEN") == 0)
        return ETL_TOKEN_KEY;
    else if (strcmp(blockchain, "BNB") == 0)
        return BNB_KEY;
    else if (strcmp(blockchain, "ETH_DAG") == 0)
        return ETH_DAG_KEY;
    else
        return (uint32_t)-1;
}
#endif
