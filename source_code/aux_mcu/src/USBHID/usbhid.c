/**
 * \file    usbhid.c
 * \author  MBorregoTrujillo
 * \date    22-February-2018
 * \brief   USB HID Protocol
 */
#include <string.h>
#include "usbhid.h"
#include "dma.h"
#include "comm.h"
#include "driver_sercom.h"

/* USBHID Commands */
#define USBHID_CMD_PING         (0x0001)

/** Function like macros */
#define USBHID_new_msg_received(flip)   (flip != rx_msg_flip)
#define USBHID_check_pkt_counter(cnt)   ((cnt != rx_pkt_counter) || \
                                         (rx_pkt_counter >= USBHID_MAX_TOTAL_PKTS))

/** USBHID specific types  -------------------------------------------------  */

/** Typedef of the first two bytes of the HID packet structure */
typedef struct {
    /* Byte 0, LSB first */
    uint8_t len :6;
    uint8_t final_ack :1;
    uint8_t msg_flip  :1;
    /* Byte 1, LSB first */
    uint8_t total_pkts    :4;
    uint8_t pkt_id        :4;
} T_usbhid_control;

/** HID Packet Structure */
typedef struct{
    T_usbhid_control control;
    uint8_t payload[USBHID_MAX_PAYLOAD];
} T_usbhid_pkt;

/** CMD Packet Structure */
typedef struct{
    /* Byte0,1 */
    uint16_t cmd;
    /* Byte2,3 */
    uint16_t len;
    /* Byte4,n */
    uint8_t* data;
} T_usbhid_msg;

/** Private data Declaration -----------------------------------------------  */

/** Message Buffer allocation */
static uint8_t usbhid_rx_buffer[USBHID_MAX_MSG_SIZE];
/** Stores the current message flip */
static bool rx_msg_flip;
/** Counts the number of packets to be received */
static uint8_t rx_pkt_counter;
/** Total message length */
static uint16_t rx_msg_size;
/** USB data copied in USB interrupt callback */
static T_usbhid_pkt usb_data;
/** Flag to mark new data from USB */
static bool usb_isr_received;

/**
 * \fn      usbhid_init
 * \brief   Initialize USBHID internal variables
 */
void usbhid_init(void){
    rx_msg_flip = false;
    rx_pkt_counter = 0u;
    rx_msg_size = 0u;
    usb_isr_received = false;
}

/**
 * \fn      usbhid_task
 * \brief   cyclic task to process USB data
 */
void usbhid_task(void){
    if(usb_isr_received){
        T_usbhid_pkt *pkt = &usb_data;
        T_comm_pkt_status pkt_status;
        bool err = false;

        /* Check new message flip bit  */
        if(USBHID_new_msg_received(pkt->control.msg_flip)){
            rx_msg_size = 0u;
            rx_pkt_counter = 0u;
            rx_msg_flip = pkt->control.msg_flip;
        }

        /* Analyze packet counter */
        if(USBHID_check_pkt_counter(pkt->control.pkt_id)){
            err = true;
        }

        /* Process incoming packet if no error has been detected */
        if( !err ){
            /* Copy from incoming pkt to buffer ( */
            memcpy(&usbhid_rx_buffer[rx_msg_size], pkt->payload, pkt->control.len );
            rx_msg_size+= pkt->control.len;
            rx_pkt_counter++;

            /* Pkt status */
            pkt_status.msg_start = (pkt->control.pkt_id == 0);
            pkt_status.msg_end = (pkt->control.pkt_id == pkt->control.total_pkts);

            /* Send pkt to MAIN MCU by means of USART */
            comm_usb_process_in_pkt(pkt_status, pkt->payload, pkt->control.len);

            /* Check if the end of packet has arrived */
            if(pkt_status.msg_end){
                /* Answer to Host */
                if(pkt->control.final_ack){
                    udi_hid_generic_send_report_in((uint8_t*)pkt, pkt->control.len+USBHID_PKT_HEADER_SIZE);
                }
                /* Reset Counters */
                rx_msg_size = 0u;
                rx_pkt_counter = 0u;
            }
        } else{
            /* Protocol Error detected, reinitialize counters */
            rx_msg_size = 0u;
            rx_pkt_counter = 0u;
            /* Send Error message ? */
        }

        usb_isr_received = false;
    }
}

/**
 * \fn          usbhid_usb_callback
 * \brief       Callback function which receives raw hid packet to be processed
 *              following minible USB HID Protocol. The callback is called from
 *              udi_hid_generic_report_out_received function and configured in
 *              conf_usb.h file
 * \param data  Pointer to raw hid packet received from USB
 */
void usbhid_usb_callback(uint8_t *data){
        memcpy(&usb_data, data, sizeof(usb_data));
        usb_isr_received = true;
}

/**
 * \fn              usbhid_send_to_usb
 * \brief           Function to be called to send message to USB, the message follow
 *                  USBHID specification and do packet fragmentation if the size is
 *                  greater than 62
 * \param buff      Pointer to data
 * \param buff_len  Data length
 */
void usbhid_send_to_usb(uint8_t* buff, uint16_t buff_len){
    T_usbhid_pkt pkt;
    uint16_t buff_idx = 0;

    /* Compute packet fragmentation */
    pkt.control.total_pkts = buff_len / USBHID_MAX_PAYLOAD;

    if( ((buff_len%USBHID_MAX_PAYLOAD) == 0) &&
        (buff_len > 0) ){
        pkt.control.total_pkts--;
    }

    for(pkt.control.pkt_id = 0; pkt.control.pkt_id <= pkt.control.total_pkts; pkt.control.pkt_id++){
        /* Compute Header */
        pkt.control.len = ((uint16_t)(buff_len - buff_idx) >= USBHID_MAX_PAYLOAD) ? (USBHID_MAX_PAYLOAD) : (uint8_t)(buff_len - buff_idx);
        pkt.control.msg_flip = rx_msg_flip;
        pkt.control.final_ack = false;

        /* copy data to pkt.payload */
        memcpy(pkt.payload, &buff[buff_idx], pkt.control.len);
        buff_idx += pkt.control.len;

        /*
         * After this call we can use the buffer again, as it copies to an internal
         * buffer to send the data through USB
         */
        while(!udi_hid_generic_send_report_in((uint8_t*)&pkt, pkt.control.len+USBHID_PKT_HEADER_SIZE));
    }
}
