/**
 * Constants and types used throughout ElGA
 *
 * Author: Kasimir Gabert
 *
 * Copyright 2021 National Technology & Engineering Solutions of Sandia, LLC
 * (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
 * Government retains certain rights in this software.
 *
 * Please see the LICENSE.md file for license information.
 */

#ifndef TYPES_HPP
#define TYPES_HPP

#include <cstdint>
#include <stdexcept>
#include <stdlib.h>

#include <functional>
#include <tuple>

#include <signal.h>

#include <iostream>
#include <string>

#include "integer_hash.hpp"

/** Parameters */
#define START_PORT 17200
#define PUB_OFFSET 100
#define PULL_OFFSET 200
#define HIGHWATERMARK 500000
#define STARTING_VAGENTS 100
#define MAP_LOCALNUM_OFFSET 1
#define RESPONDER_LOCALNUM_OFFSET 2

/** Version information */
#define ELGA_MAJOR 1
#define ELGA_MINOR 0

/** Configurable types */
using msg_type_t = uint8_t;
using localnum_t = uint16_t;
using aid_t = uint16_t;

using addr_t = uint64_t; /* 32-bit IPv4 addr + localnum_t + aid_t */

using vertex_t = uint64_t;
using timestamp_t = uint64_t;
using weight_t = double;
using batch_t = uint32_t;
using it_t = int32_t;

// Note: these are explicitly integers, avoiding ambiguity with compiler
// specific enums
/** Constants used for protocols */
#define HANDSHAKE                     0xa0
#define NUM_NEIGHBORS                 0xa1
#define ADD_NEIGHBOR                  0xa2
#define RING_SIZE                     0xa3
#define ADD_ENTRY                     0xa4
#define DB_SIZE                       0xa5
#define ADD_FILTER_DIR                0xa6
#define INSTALL_FILTER                0xa7
#define PROCESS                       0xa8
#define ADD_TAG_TO_ENTRY              0xa9
#define START_BARRIER_WAIT            0xaa
#define BARRIER_MSG_DIST              0xab
#define END_BARRIER                   0xac
#define AT_BARRIER                    0xad
#define STAGE_CLOSE                   0xae
#define CHECK_AT_BARRIER              0xaf
#define STATS                         0xb0
#define ADD_DB_FILE                   0xb1
#define ADD_FILTER_DIR_BROADCAST      0xb2
#define INSTALL_FILTER_BROADCAST      0xb3
#define PROCESS_BROADCAST             0xb4
#define PRINT_ENTRIES                 0xb5
#define REMOVE_TAG_FROM_ENTRY         0xb6
#define UPDATE_ENTRY_VAL              0xb7
#define SUBSCRIBE_TO_ENTRY            0xb8
#define MAP_INSERT                    0xb9
#define MAP_RETRIEVE                  0xba
#define GET_ENTRY_BY_KEY              0xbb
#define MAP_SIZE                      0xbc
#define MAP_GET_KEYS                  0xbd
#define MAP_DOES_KEY_EXIST            0xbe
#define MAP_GET_ENTRIES               0xbf
#define INSTALLED_FILTERS             0xc0
#define GET_ENTRIES                   0xc1
#define INC_RECV_DIST                 0xc2
#define MAP_RETRIEVE_IF_EXISTS        0xc3
#define MAP_INSERT_MULTIPLE           0xc4
#define QUERY                         0xc5
#define GET_NEIGHBORS                 0xc6
#define PROCESSING                    0xc7
#define EXPORT_DB                     0xc8
#define EXPORT_DB_BROADCAST           0xc9
#define IMPORT_DB                     0xca
#define IMPORT_DB_DISTRIBUTE          0xcb
#define CLEAR_FILTERS_BROADCAST       0xcc
#define CLEAR_FILTERS                 0xcd
#define ALGPROCESS                    0xce
#define ALG_INTERNAL_COMPUTE          0xcf
#define ALG_VERTICES                  0xd0
#define GET_STATE                     0xd1
#define EXPORT_DB_WITH_TAG            0xd2
#define EXPORT_DB_WITH_TAG_BROADCAST  0xd3
#define WANT_HEARTBEAT                0xfe
#define HEARTBEAT                     0xff

extern localnum_t local_base;
extern localnum_t local_max;

#endif
