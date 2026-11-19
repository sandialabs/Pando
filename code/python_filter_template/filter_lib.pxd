from libc.stdint cimport uint32_t, uint16_t

cdef extern from "filter.hpp":
    ctypedef unsigned long long uint64_t
    ctypedef long long int64_t
    ctypedef int64_t vtx_t
    ctypedef uint64_t chain_info_t

    cdef struct dbkey_t:
        chain_info_t a
        vtx_t b
        vtx_t c
        
    cdef struct fn:
        void* state;
        void (*run)(const fn*, ...);
        void (*run_)(const fn*, ...);
    cdef struct DBAccess:
        fn make_new_entry
        fn get_entry_by_tags
        fn get_entry_by_key
        fn remove_tag_by_tags
        fn get_entries_by_tags
        fn subscribe_to_entry
        fn update_entry_val

        fn add_tag
        fn remove_tag

        const char* const* tags
        const char* value
        dbkey_t key

        void* group

        # Batched lookup: run(fn*, size_t n, const dbkey_t* keys, char** values)
        fn get_entries_by_keys

    cdef chain_info_t pack_chain_info(uint32_t a, uint16_t b, uint16_t c)
    cdef uint32_t get_blockchain_key(const char* blockchain)

    # Blockchain Key Constants, 31 bits
    cdef uint32_t BTC_KEY "BTC_KEY"
    cdef uint32_t ZEC_KEY "ZEC_KEY"
    cdef uint32_t ETH_KEY "ETH_KEY"
    cdef uint32_t XMR_KEY "XMR_KEY"
    cdef uint32_t USD_KEY "USD_KEY"
    cdef uint32_t DGB_KEY "DGB_KEY"
    cdef uint32_t DOGE_KEY "DOGE_KEY"
    cdef uint32_t BCH_KEY "BCH_KEY"
    cdef uint32_t POLY_KEY "POLY_KEY"
    cdef uint32_t DASH_KEY "DASH_KEY"
    cdef uint32_t LTC_KEY "LTC_KEY"
    cdef uint32_t FUN_KEY "FUN_KEY"
    cdef uint32_t BLK_KEY "BLK_KEY"
    cdef uint32_t CVC_KEY "CVC_KEY"
    cdef uint32_t USDT_KEY "USDT_KEY"
    cdef uint32_t RDD_KEY "RDD_KEY"
    cdef uint32_t OMG_KEY "OMG_KEY"
    cdef uint32_t TUSD_KEY "TUSD_KEY"
    cdef uint32_t GNT_KEY "GNT_KEY"
    cdef uint32_t SC_KEY "SC_KEY"
    cdef uint32_t ATOM_KEY "ATOM_KEY"
    cdef uint32_t XRP_KEY "XRP_KEY"
    cdef uint32_t MKR_KEY "MKR_KEY"
    cdef uint32_t MANA_KEY "MANA_KEY"
    cdef uint32_t STORJ_KEY "STORJ_KEY"
    cdef uint32_t BNT_KEY "BNT_KEY"
    cdef uint32_t GNO_KEY "GNO_KEY"
    cdef uint32_t SNT_KEY "SNT_KEY"
    cdef uint32_t ZRX_KEY "ZRX_KEY"
    cdef uint32_t RLC_KEY "RLC_KEY"
    cdef uint32_t ETC_KEY "ETC_KEY"
    cdef uint32_t BAT_KEY "BAT_KEY"
    cdef uint32_t REP_KEY "REP_KEY"
    cdef uint32_t DNT_KEY "DNT_KEY"
    cdef uint32_t DAI_KEY "DAI_KEY"
    cdef uint32_t USDC_KEY "USDC_KEY"
    cdef uint32_t RCN_KEY "RCN_KEY"
    # Filter-specific Key Constants, 16 bits
    cdef uint16_t TX_IN_EDGE_KEY "TX_IN_EDGE_KEY"
    cdef uint16_t TX_OUT_EDGE_KEY "TX_OUT_EDGE_KEY"
    cdef uint16_t UTXO_EDGE "UTXO_EDGE"
    cdef uint16_t UTXO_STATS "UTXO_STATS"
    cdef uint16_t TXTIME_KEY "TXTIME_KEY"
    cdef uint16_t DISTANCE_KEY "DISTANCE_KEY"
    cdef uint16_t BFS_INITIAL_VALUE "BFS_INITIAL_VALUE"
    cdef uint16_t EXCHANGE_RATE_KEY "EXCHANGE_RATE_KEY"
    cdef uint16_t OUT_VAL_KEY "OUT_VAL_KEY"
    cdef uint16_t OUT_VAL_ROUNDED_KEY "OUT_VAL_ROUNDED_KEY"
    cdef uint16_t ETL_TOKEN_KEY "ETL_TOKEN_KEY"
    cdef uint16_t ETL_TX_TOKEN_KEY "ETL_TX_TOKEN_KEY"
    cdef uint16_t SHAPESHIFT_KEY "SHAPESHIFT_KEY"
    cdef uint16_t NOT_UTXO_KEY "NOT_UTXO_KEY"
    cdef uint16_t UTXO_KEY "UTXO_KEY"

cdef extern from "dbkey.h":
    cdef const dbkey_t INITIAL_KEY
