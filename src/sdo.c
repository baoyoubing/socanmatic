/* -*- mode: C; c-basic-offset: 4 -*- */
/* ex: set shiftwidth=4 tabstop=4 expandtab: */
/*
 * Copyright (c) 2008-2013, Georgia Tech Research Corporation
 * All rights reserved.
 *
 * Author(s): Neil T. Dantam <ntd@gatech.edu>
 * Georgia Tech Humanoid Robotics Lab
 * Under Direction of Prof. Mike Stilman <mstilman@cc.gatech.edu>
 *
 *
 * This file is provided under the following "BSD-style" License:
 *
 *
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *   AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *   POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include <assert.h>
#include <string.h>
#include "socanmatic.h"
#include "socanmatic_private.h"

/**********/
/** DEFS **/
/**********/

// Create a struct can_frame from a canmat_sdo_msg_t
enum canmat_status canmat_sdo2can (struct can_frame *dst, const canmat_sdo_msg_t *src, const int is_tx ) {
    // FIXME: better message validation
    assert( src->length <= 4 );

    // Set the message ID and the length
    dst->can_id = (canid_t)(is_tx ? CANMAT_SDO_RESP_ID(src->node) : CANMAT_SDO_REQ_ID(src->node));

    // set indices
    dst->data[1] = (uint8_t)(src->index & 0xFF);
    dst->data[2] = (uint8_t)((src->index >> 8) & 0xFF);
    dst->data[3] = src->subindex;

    struct canmat_sdo_cmd_ex cmd = { .cs = src->cmd_spec,
                                     .x  = 0};

    uint8_t len = 0;
    if( ( is_tx && CANMAT_SCS_EX_DL == cmd.cs) ||
        (!is_tx && CANMAT_CCS_EX_UL == cmd.cs) )
    { // no data to send
        cmd.n = cmd.e = cmd.s = 0;
        memset(dst->data+4, 0, 4);
    } else if ( ( is_tx && CANMAT_SCS_EX_UL == cmd.cs) ||
                (!is_tx && CANMAT_CCS_EX_DL == cmd.cs) )
    { // got some data
        switch( src->data_type ) {
            // size 4
        case CANMAT_DATA_TYPE_REAL32:
        case CANMAT_DATA_TYPE_UNSIGNED32:
        case CANMAT_DATA_TYPE_INTEGER32:
            len = 4;
            canmat_byte_stle32( dst->data+4, src->data.u32 );
            break;
            // size 2
        case CANMAT_DATA_TYPE_UNSIGNED16:
        case CANMAT_DATA_TYPE_INTEGER16:
            len = 2;
            canmat_byte_stle16( dst->data+4, src->data.u16 );
            break;
            // size 1
        case CANMAT_DATA_TYPE_UNSIGNED8:
        case CANMAT_DATA_TYPE_INTEGER8:
            len = 1;
            dst->data[4] = src->data.u8;
            break;
        case CANMAT_DATA_TYPE_VOID:
            len = 0;
            break;
        default:
            return CANMAT_ERR_PARAM;
        }
        // now set the bits
        cmd.s = 1;
        cmd.e = 1;
        cmd.n = (unsigned) ( (4-len) & 0x3 );
    } else {
        return CANMAT_ERR_PARAM;
    }

    canmat_sdo2can_cmd_ex( dst, cmd );

    dst->can_dlc = 8; //(uint8_t)(len + 4);

    return CANMAT_OK;
}


// TODO: segmented messages

// Create a canmat_sdo_msg_t from a struct can_frame
enum canmat_status canmat_can2sdo(
    canmat_sdo_msg_t *dst, const struct can_frame *src, enum canmat_data_type data_type )
{
    if( src->can_dlc < 4 ) return CANMAT_ERR_PROTO;

    // Function
    uint16_t func = canmat_frame_func(src);
    _Bool is_tx;
    if( CANMAT_FUNC_CODE_SDO_TX == func ) {
        is_tx = 1;
    } else if ( CANMAT_FUNC_CODE_SDO_RX == func ) {
        is_tx = 0;
    } else {
        return CANMAT_ERR_PROTO;
    }

    // command
    struct canmat_sdo_cmd_ex cmd = canmat_can2sdo_cmd_ex( src );
    dst->cmd_spec = cmd.cs;

    // validate command / ensure expedited
    if( CANMAT_CS_ABORT == dst->cmd_spec ) /* same code for client and server */ {
        dst->data_type = CANMAT_DATA_TYPE_UNSIGNED32;
        dst->length = 4;
    } else if( ( is_tx && CANMAT_SCS_EX_DL == cmd.cs ) ||
               (!is_tx && CANMAT_CCS_EX_UL == cmd.cs ) )
    {
        dst->length = 0;
        dst->data_type = CANMAT_DATA_TYPE_VOID;
    } else if( ( is_tx && CANMAT_SCS_EX_UL == cmd.cs ) ||
               (!is_tx && CANMAT_CCS_EX_DL == cmd.cs ) )
    {
        dst->data_type = data_type;
        if( cmd.s ) {
            dst->length = (uint8_t) (4 - cmd.n);
        } else {
            dst->length = (uint8_t) (src->can_dlc - 4);
        }
    } else { // unhandled SDO transfer type
        return CANMAT_ERR_PROTO;
    }


    // node
    dst->node = (uint8_t)(src->can_id & CANMAT_NODE_MASK);

    // indices
    dst->index = canmat_can2sdo_index( src );
    dst->subindex = canmat_can2sdo_subindex( src );

    // Should we be more tolerant of incorrectly set sizes?
    switch( dst->data_type ) {
        // size 4
    case CANMAT_DATA_TYPE_REAL32:
    case CANMAT_DATA_TYPE_UNSIGNED32:
    case CANMAT_DATA_TYPE_INTEGER32:
        if( src->can_dlc < 8 || dst->length != 4 ) return CANMAT_ERR_PROTO;
        assert(4 == dst->length);
        dst->data.u32 = canmat_byte_ldle32( src->data+4 );
        break;
        // size 2
    case CANMAT_DATA_TYPE_UNSIGNED16:
    case CANMAT_DATA_TYPE_INTEGER16:
        if( src->can_dlc < 6 || dst->length != 2 ) return CANMAT_ERR_PROTO;
        assert(2 == dst->length);
        dst->data.u16 = canmat_byte_ldle16( src->data+4 );
        break;
        // size 1
    case CANMAT_DATA_TYPE_UNSIGNED8:
    case CANMAT_DATA_TYPE_INTEGER8:
        if( src->can_dlc < 5 || dst->length != 1 ) return CANMAT_ERR_PROTO;
        assert(1 == dst->length);
        dst->data.u8 = src->data[4];
        break;
    case CANMAT_DATA_TYPE_VOID:
        if( dst->length != 0 ) return CANMAT_ERR_PROTO;
        break;
    default:
        return CANMAT_ERR_PARAM;
    }

    return CANMAT_CS_ABORT == dst->cmd_spec ? CANMAT_ERR_ABORT : CANMAT_OK;
}


/// Send and SDO request and wait for the response
static canmat_status_t canmat_sdo_query(
    canmat_iface_t *cif, const canmat_sdo_msg_t *req,
    canmat_sdo_msg_t *resp ) {
    // send
    {
        struct can_frame can;
        canmat_sdo2can( &can, req, 0 );
        canmat_status_t r  = canmat_iface_send( cif, &can );
        if( CANMAT_OK != r ) return r;
    }
    // recv
    {
        canmat_status_t r;
        struct can_frame can;
        do {
            r = canmat_iface_recv( cif, &can );
        } while ( CANMAT_OK == r &&
                  can.can_id != (canid_t)CANMAT_SDO_RESP_ID(req->node) );

        if( CANMAT_OK == r ) {
            r = canmat_can2sdo( resp, &can, req->data_type );
        }
        return r;
    }
}

canmat_status_t canmat_sdo_dl(
    canmat_iface_t *cif,
    const canmat_sdo_msg_t *req, canmat_sdo_msg_t *resp )
{
    canmat_sdo_msg_t req1;
    memcpy(&req1, req, sizeof(req1));
    req1.cmd_spec = CANMAT_CCS_EX_DL;
    return canmat_sdo_query( cif, &req1, resp );
}

canmat_status_t canmat_sdo_ul(
    canmat_iface_t *cif,
    const canmat_sdo_msg_t *req, canmat_sdo_msg_t *resp )
{
    canmat_sdo_msg_t req1;
    memcpy(&req1, req, sizeof(req1));
    req1.cmd_spec = CANMAT_CCS_EX_UL;
    return canmat_sdo_query( cif, &req1, resp );
}

canmat_status_t canmat_sdo_query_resp( canmat_iface_t *cif, const canmat_sdo_msg_t *resp ) {
    struct can_frame can;
    canmat_sdo2can( &can, resp, 1 );
    return  canmat_iface_send( cif, &can );
}

int canmat_sdo_print( FILE *f, const canmat_sdo_msg_t *sdo ) {
    fprintf(f, "%02x.%02x[%04x.%02x]",
            sdo->node, sdo->cmd_spec,
            sdo->index, sdo->subindex);
    int i;
    for( i = 0; i < sdo->length; i++ ) {
        fprintf(f,"%c%02x",
                i ? ':' : ' ',
                sdo->data.byte[i] );

    }
    fputc('\n', f);
    return 0;
}

const char *canmat_sdo_strerror( uint32_t u32 ) {
    switch (u32)
    {
    case CANMAT_ABORT_RESET:
        return "Error Reset or no Error";
    case CANMAT_ABORT_TOGGLE_NOT_ALTERNATED:
        return "Toggle bit not alternated";
    case CANMAT_ABORT_SDO_TIMEOUT:
        return "SDO protocol timed out";
    case CANMAT_ABORT_INVALID_CMD_SPEC:
        return "Client/server command specifier not valid or unknown";
    case CANMAT_ABORT_INVALID_BLOCK_SIZE:
        return "Invalid block size (block mode only)";
    case CANMAT_ABORT_INVALID_SEQ_NO:
        return "Invalid sequence number (block mode only)";
    case CANMAT_ABORT_CRC:
        return "CRC error (block mode only)";
    case CANMAT_ABORT_OOM:
        return "Out of memory";
    case CANMAT_ABORT_UNSUPP_ACCESS:
        return "Unsupported access to an object";
    case CANMAT_ABORT_WRITE_ONLY:
        return "Attempt to read a write only object";
    case CANMAT_ABORT_READ_ONLY:
        return "Attempt to write a read only object";
    case CANMAT_ABORT_OBJ_EXIST:
        return "Object does not exist in the object dictionary";
    case CANMAT_ABORT_UNMAPPABLE:
        return "Object cannot be mapped to the PDO";
    case CANMAT_ABORT_PDO_OVERFLOW:
        return "The number and length of the objects to be mapped would exceed PDO length";
    case CANMAT_ABORT_INCOMPAT_PARAM:
        return "General parameter incompatibility reason";
    case CANMAT_ABORT_INCOMPAT_DEV:
        return "General internal incompatibility in the device";
    case CANMAT_ABORT_ERR_HW:
        return "Access failed due to an hardware error";
    case CANMAT_ABORT_DATA:
        return "Data type does not match, length of service parameter does not match";
    case CANMAT_ABORT_DATA_TOO_HI:
        return "Data type does not match, length of service parameter too high";
    case CANMAT_ABORT_DATA_TOO_LO:
        return "Data type does not match, length of service parameter too low";
    case CANMAT_ABORT_SUBINDEX_EXIST:
        return "Sub-index does not exist";
    case CANMAT_ABORT_RANGE:
        return "Value range of parameter exceeded (only for write access)";
    case CANMAT_ABORT_RANGE_TOO_HI:
        return "Value of parameter written too high";
    case CANMAT_ABORT_RANGE_TOO_LO:
        return "Value of parameter written too low";
    case CANMAT_ABORT_MAX_BELOW_MIN:
        return "Maximum value is less than minimum value";
    case CANMAT_ABORT_GENERAL:
        return "general error";
    case CANMAT_ABORT_STORE:
        return "Data cannot be transferred or stored to the application";
    case CANMAT_ABORT_STORE_LOCAL:
        return "Data cannot be transferred or stored to the application because of local control";
    case CANMAT_ABORT_STORE_DEV_STATE:
        return "Data cannot be transferred or stored to the application because of the present device state";
    case CANMAT_ABORT_DICT:
        return "Object dictionary dynamic generation fails or no object dictionary is present (e.g. object dictionary is generated from file and generation fails because of an file error)";
    default: return "Unknown";
    }
}


/* ex: set shiftwidth=4 tabstop=4 expandtab: */
/* Local Variables:                          */
/* mode: c                                   */
/* c-basic-offset: 4                         */
/* indent-tabs-mode:  nil                    */
/* End:                                      */
