/*
 * Copyright (c) 2013, Thingsquare, http://www.thingsquare.com/.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/**
* Copyright (c) 2015 Atmel Corporation and
* 2012 - 2013, Thingsquare, http://www.thingsquare.com/. All rights reserved. 
*  
* Redistribution and use in source and binary forms, with or without 
* modification, are permitted provided that the following conditions are met:
* 
* 1. Redistributions of source code must retain the above copyright notice, this
* list of conditions and the following disclaimer.
* 
* 2. Redistributions in binary form must reproduce the above copyright notice, 
* this list of conditions and the following disclaimer in the documentation 
* and/or other materials provided with the distribution.
* 
* 3. Neither the name of Atmel nor the name of Thingsquare nor the names of its
* contributors may be used to endorse or promote products derived 
* from this software without specific prior written permission.  
* 
* 4. This software may only be redistributed and used in connection with an 
* Atmel microcontroller or Atmel wireless product.
* 
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE 
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, 
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "firestorm.h"
#include <stdint.h>
#include "rf233-const.h"
#include "rf233-config.h"
#include "rf233-arch.h"
#include "trx_access.h"
#include "rf233.h"

#define RF233_STATUS()                    rf233_status()
/*---------------------------------------------------------------------------*/
static int  on(void);
static int  off(void);
static void rf_generate_random_seed(void);
static void flush_buffer(void);
static uint8_t flag_transmit = 0;
static uint8_t ack_status = 0;
static volatile int radio_is_on = 0;
static volatile int pending_frame = 0;
static volatile int sleep_on = 0;

#define PC14 9
#define PC15 10
#define PA20 11

#define SLP_PIN PC14
#define RST_PIN PC15
#define RADIO_IRQ PA20

#define IEEE802154_CONF_PANID 0x1111

enum {
  RADIO_TX_OK        = 0,
  RADIO_TX_ERR       = 1,
  RADIO_TX_NOACK     = 2,
  RADIO_TX_COLLISION = 3
};

/*---------------------------------------------------------------------------*/
int rf233_init(void);
int rf233_prepare(const void *payload, unsigned short payload_len);
int rf233_transmit();
int rf233_send(const void *data, unsigned short len);
int rf233_read(void *buf, unsigned short bufsize);
int rf233_channel_clear(void);
int rf233_receiving_packet(void);
int rf233_pending_packet(void);
int rf233_on(void);
int rf233_off(void);
int rf233_sleep(void);


void ENTER_TRX_REGION() {} // Disable interrupts
void LEAVE_TRX_REGION() {} // Re-enable interrupts
void CLEAR_TRX_IRQ() {}    // Clear pending interrupts

/*---------------------------------------------------------------------------*/
/* convenience macros */
//#define RF233_STATUS()                    rf233_arch_status()
#define RF233_COMMAND(c)                  trx_reg_write(RF233_REG_TRX_STATE, c)

/* each frame has a footer consisting of LQI, ED, RX_STATUS added by the radio */
#define FOOTER_LEN                        3   /* bytes */
#define MAX_PACKET_LEN                    127 /* bytes, excluding the length (first) byte */
#define PACKETBUF_SIZE                    128 /* bytes, for even int writes */

/*---------------------------------------------------------------------------*/
#define _DEBUG_                 1
#define DEBUG_PRINTDATA       0    /* print frames to/from the radio; requires DEBUG == 1 */
#if _DEBUG_
#define PRINTF(...)       printf(__VA_ARGS__)
#else
#define PRINTF(...)       1
#endif

#define BUSYWAIT_UNTIL(cond, max_time)        \
  do {                                        \
    int counter = max_time;                   \
    while (!(cond) && counter > 0) {          \
      delay_ms(1);                            \
      counter--;                              \
    }                                         \
  } while(0)

// Register operations

int main() {
  char buf[10] = {0x61, 0xAA, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xdd};

  rf233_init();
  while (1) {
    rf233_send(buf, 10);
    delay_ms(2000);
  }
  //while(1) {}
}

uint8_t packetbuf[PACKETBUF_SIZE];

void* packetbuf_dataptr() {
  return (void*)packetbuf;
}

void packetbuf_clear() {
  int* ptr = (int*)packetbuf;
  for (int i = 0; i <  PACKETBUF_SIZE / 4; i++) {
    *ptr++ = 0x00000000;
  }
}

uint8_t trx_reg_read(uint8_t addr) {
	uint8_t command = addr | READ_ACCESS_COMMAND;
        char buf[2];
        buf[0] = command;
        buf[1] = 0;
        spi_read_write_sync(buf, buf, 2);
	return buf[1];
}

uint8_t trx_bit_read(uint8_t addr, uint8_t mask, uint8_t pos) {
        uint8_t ret;
        ret = trx_reg_read(addr);
        ret &= mask;
        ret >>= pos;
        return ret;
}

void trx_reg_write(uint8_t addr, uint8_t data) {
        uint8_t command = addr | WRITE_ACCESS_COMMAND;
        char buf[2];
        buf[0] = command;
        buf[1] = data;
        spi_write_sync(buf, 2);
        return;
}

void trx_bit_write(uint8_t reg_addr, 
		   uint8_t mask, 
		   uint8_t pos, 
		   uint8_t new_value) {
        uint8_t current_reg_value;
        current_reg_value = trx_reg_read(reg_addr);
        current_reg_value &= ~mask;
        new_value <<= pos;
        new_value &= mask;
        new_value |= current_reg_value;
        trx_reg_write(reg_addr, new_value);
}

void trx_sram_read(uint8_t addr, uint8_t *data, uint8_t length)  {
        uint8_t temp;
        temp = TRX_CMD_SR;
        spi_hold_low();
       /* Send the command byte */
        spi_write_byte(temp);
        /* Send the command byte */
        spi_write_byte(addr);

        /* Send the address from which the read operation should start */
        /* Upload the received byte in the user provided location */
	for (uint8_t i = 0; i < length; i++) {
          data[i] = spi_write_byte(0);
	}
        spi_release_low();
}

void trx_frame_read(uint8_t *data, uint8_t length)  {
  spi_hold_low();
  spi_write_byte(TRX_CMD_FR);
  for (uint8_t i = 0; i < length; i++) {
    data[i] = spi_write_byte(0);
  }
  spi_release_low();
}

void trx_frame_write(uint8_t *data, uint8_t length) {
  spi_hold_low();
  spi_write_byte(TRX_CMD_FW);
  for (uint8_t i = 0; i < length; i++) {
    spi_write_byte(data[i]);
  }
  spi_release_low();
}


/*---------------------------------------------------------------------------*/
/**
 * \brief      Get radio channel
 * \return     The radio channel
 */
int rf_get_channel(void) {
	uint8_t channel;
  channel=trx_reg_read(RF233_REG_PHY_CC_CCA) & PHY_CC_CCA_CHANNEL;
  //printf("rf233 channel%d\n",channel);
  return (int)channel;
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      Set radio channel
 * \param ch   The radio channel
 * \retval -1  Fail: channel number out of bounds
 * \retval 0   Success
 */
int rf_set_channel(uint8_t ch) {
  uint8_t temp;
  PRINTF("RF233: setting channel %u\n", ch);
  if(ch > 26 || ch < 11) {
    return -1;
  }

  /* read-modify-write to conserve other settings */
  temp = trx_reg_read(RF233_REG_PHY_CC_CCA);
  temp &=~ PHY_CC_CCA_CHANNEL;
  temp |= ch;
  trx_reg_write(RF233_REG_PHY_CC_CCA, temp);
  return 0;
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      Get transmission power
 * \return     The transmission power
 */ 
int rf233_get_txp(void) {
  PRINTF("RF233: get txp\n");
  return trx_reg_read(RF233_REG_PHY_TX_PWR_CONF) & PHY_TX_PWR_TXP;
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      Set transmission power
 * \param txp  The transmission power
 * \retval -1  Fail: transmission power out of bounds
 * \retval 0   Success
 */
int rf233_set_txp(uint8_t txp) {
  PRINTF("RF233: setting txp %u\n", txp);
  if(txp > TXP_M17) {
    /* undefined */
    return -1;
  }

  trx_reg_write(RF233_REG_PHY_TX_PWR_CONF, txp);
  return 0;
}


CB_TYPE interrupt_callback(int arg0, int arg2, int arg3, void* userdata) {
  volatile uint8_t irq_source;
  /* handle IRQ source (for what IRQs are enabled, see rf233-config.h) */
  irq_source = trx_reg_read(RF233_REG_IRQ_STATUS);
  PRINTF("RF233: Interrupt handler: 0x%x\n", (int)irq_source);
  if (irq_source == IRQ_RX_START) {
    PRINTF("RF233: Interrupt receive start.\n");
  } else if (irq_source == IRQ_TRX_DONE) {
    PRINTF("RF233: TRX_DONE handler.\n");
    // Completed a transmission
    if (flag_transmit != 0) {
      PRINTF("RF233: Interrupt transmit.\n");
      flag_transmit = 0;
      if (!(trx_reg_read(RF233_REG_TRX_STATE) & TRX_STATE_TRAC_STATUS)) {
        flag_transmit = ack_status = 1;
      }
      RF233_COMMAND(TRXCMD_RX_AACK_ON);
      PRINTF("RF233: TX complete, go back to RX with acks on.\n");
      return RADIO_TX;
    } else {
      PRINTF("RF233: Interrupt receive.\n");
      packetbuf_clear();
      pending_frame = 1;
      int len = rf233_read(packetbuf_dataptr(), MAX_PACKET_LEN);
      if (len > 0) {
        PRINTF("RF233: Received packet and read from device.\n");
      } else {
        PRINTF("RF233: Read failed.\n");
      }
      return RADIO_RX;
    }
  }
  return NONE;
}

/*---------------------------------------------------------------------------*/
/**
 * \brief      Init the radio
 * \return     Returns success/fail
 * \retval 0   Success
 */
int rf233_init(void) {
  volatile uint8_t regtemp;
  volatile uint8_t radio_state;  /* don't optimize this away, it's important */
  PRINTF("RF233: init.\n");

  /* init SPI and GPIOs, wake up from sleep/power up. */

  spi_init();
  // RF233 expects line low for CS, this is default SAM4L behavior
  //spi_set_chip_select(3);
  // POL = 0 means idle is low
  spi_set_chip_select(3);
  spi_set_polarity(0);
  // PHASE = 0 means sample leading edge
  spi_set_phase(0);
  spi_set_rate(400000);

    /* reset will put us into TRX_OFF state */
  /* reset the radio core */
  gpio_enable_output(RST_PIN);
  gpio_enable_output(SLP_PIN);
  gpio_clear(RST_PIN);
  delay_ms(1);
  gpio_set(RST_PIN);
  gpio_clear(SLP_PIN); /* be awake from sleep*/

  
  /* Read the PART_NUM register to verify that the radio is
   * working/responding. Could check in software, I just look at
   * the bus. If this is working, the first write should be 0x9C x00
   * and the return bytes should be 0x00 0x0B. - pal*/
  regtemp = trx_reg_read(RF233_REG_PART_NUM);

  /* before enabling interrupts, make sure we have cleared IRQ status */
  regtemp = trx_reg_read(RF233_REG_IRQ_STATUS);
  PRINTF("RF233: After wake from sleep\n");
  radio_state = rf233_status();
  PRINTF("RF233: Radio state 0x%04x\n", radio_state);

  if(radio_state == STATE_P_ON) {
    trx_reg_write(RF233_REG_TRX_STATE, TRXCMD_TRX_OFF);
  } 
  /* Assign regtemp to regtemp to avoid compiler warnings */
  regtemp = regtemp;
  // Set up interrupts
  gpio_interrupt_callback(interrupt_callback, NULL);
  gpio_enable_input(RADIO_IRQ, PullNone);
  gpio_clear(RADIO_IRQ);
  gpio_enable_interrupt(RADIO_IRQ, PullNone, RisingEdge);

  /* Configure the radio using the default values except these. */
  trx_reg_write(RF233_REG_TRX_CTRL_1,      RF233_REG_TRX_CTRL_1_CONF);
  trx_reg_write(RF233_REG_PHY_CC_CCA,      RF233_REG_PHY_CC_CCA_CONF);
  trx_reg_write(RF233_REG_PHY_TX_PWR, RF233_REG_PHY_TX_PWR_CONF);
  trx_reg_write(RF233_REG_TRX_CTRL_2,      RF233_REG_TRX_CTRL_2_CONF);
  trx_reg_write(RF233_REG_IRQ_MASK,        RF233_REG_IRQ_MASK_CONF);
  trx_reg_write(RF233_REG_XAH_CTRL_1,      0x02);
  trx_bit_write(SR_MAX_FRAME_RETRIES, 3);
  trx_bit_write(SR_MAX_CSMA_RETRIES, 4);
  PRINTF("RF233: Configured transciever.\n");
  {
    uint8_t addr[8];
    addr[0] = 0x22;
    addr[1] = 0x22;
    addr[2] = 0x22;
    addr[3] = 0x22;
    addr[4] = 0x22;
    addr[5] = 0x22;
    addr[6] = 0x22;
    addr[7] = 0x22;
    SetPanId(IEEE802154_CONF_PANID);
    
    SetIEEEAddr(addr);
    SetShortAddr(0x2222);
  }
  rf_generate_random_seed();
  
  for (uint8_t i = 0; i < 8; i++)   {
    regtemp = trx_reg_read(0x24 + i);
  }

  /* 11_09_rel */
  trx_reg_write(RF233_REG_TRX_RPC, 0xFF); /* Enable RPC feature by default */
  // regtemp = trx_reg_read(RF233_REG_PHY_TX_PWR);
  //PRINTF("RF233: Installed addresses. Turning on radio.");
  rf233_on();
  /* start the radio process */
  //process_start(&rf233_radio_process, NULL);
  return 0;
}

/*
 * \brief Generates a 16-bit random number used as initial seed for srand()
 *
 */
static void rf_generate_random_seed(void) {
  srand(55);
}

/*---------------------------------------------------------------------------*/
/**
 * \brief      prepare a frame and the radio for immediate transmission 
 * \param payload         Pointer to data to copy/send
 * \param payload_len     length of data to copy
 * \return     Returns success/fail, refer to radio.h for explanation
 */

static int counter = 0;
int rf233_prepare(const void *payload, unsigned short payload_len) {
  int i;
  uint8_t templen;
  uint8_t radio_status;
  uint8_t data[130];

  /* Add length of the FCS (2 bytes) */
  templen = payload_len + 2; 
  data[0] = templen;
  for (i = 0; i < templen; i++) {
    data[i + 1] = ((uint8_t*)payload)[i];
  }
  data[3] = (uint8_t)(counter & 0xff);
  counter++;

#if DEBUG_PRINTDATA
  PRINTF("RF233 prepare (%u/%u): 0x", payload_len, templen);
  for(i = 0; i < templen; i++) {
    PRINTF("%02x", *(uint8_t *)(payload + i));
  }
  PRINTF("\n");
#endif  /* DEBUG_PRINTDATA */
   
  PRINTF("RF233: prepare %u\n", payload_len);
  if(payload_len > MAX_PACKET_LEN) {
    PRINTF("RF233: error, frame too large to tx\n");
    return RADIO_TX_ERR;
  }

  /* check that the FIFO is clear to access */
  radio_status = rf233_status();
  if (radio_status == STATE_BUSY_RX_AACK ||
      radio_status == STATE_BUSY_TX_ARET) {
    PRINTF("RF233: TRX buffer unavailable: prep when %s\n", radio_status == STATE_BUSY_RX_AACK ? "rx" : "tx");
    return RADIO_TX_ERR;
  }

  /* Write packet to TX FIFO. */
  PRINTF("RF233 len = %u\n", payload_len);
  trx_frame_write((uint8_t *)data, templen+1);
  return RADIO_TX_OK;
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      Transmit a frame already put in the radio with 'prepare'
 * \param payload_len    Length of the frame to send
 * \return     Returns success/fail, refer to radio.h for explanation
 */
int rf233_transmit() {
  static uint8_t status_now;
  PRINTF("RF233: tx\n");

  /* prepare for TX */
  
  status_now = rf233_status();
  //status_now = trx_reg_read(RF233_REG_TRX_RPC);
  if (status_now == STATE_BUSY_RX_AACK || status_now == STATE_BUSY_TX_ARET) {
    PRINTF("RF233: collision, was receiving 0x%02X\n",status_now);
    /* NOTE: to avoid loops */
    return RADIO_TX_ERR;;
    // return RADIO_TX_COLLISION;
  }
  if (status_now != STATE_PLL_ON) {
    trx_reg_write(RF233_REG_TRX_STATE, STATE_PLL_ON);
    do {
      status_now = trx_bit_read(RF233_REG_TRX_STATUS, 0x1F, 0);
    } while (status_now == 0x1f);
  }
  
  if (rf233_status() != STATE_PLL_ON) {
    /* failed moving into PLL_ON state, gracefully try to recover */
    PRINTF("RF233: failed going to PLLON\n");
    RF233_COMMAND(TRXCMD_PLL_ON);   /* try again */
    static uint8_t state;
    state = rf233_status();
    if(state != STATE_PLL_ON) {
      PRINTF("RF233: graceful recovery (in tx) failed, giving up. State: 0x%02X\n", rf233_status());
      return RADIO_TX_ERR;
    }
  }
  
  /* perform transmission */
  flag_transmit = 1;
  RF233_COMMAND(TRXCMD_TX_ARET_ON);
  RF233_COMMAND(TRXCMD_TX_START);
  wait_for(RADIO_TX);

  PRINTF("RF233: tx ok\n\n");
  return RADIO_TX_OK;
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      Send data: first prepares, then transmits
 * \param payload         Pointer to data to copy/send
 * \param payload_len     length of data to copy
 * \return     Returns success/fail, refer to radio.h for explanation
 */
int rf233_send(const void *payload, unsigned short payload_len) {
  PRINTF("RF233: send %u\n", payload_len);
  if (rf233_prepare(payload, payload_len) != RADIO_TX_OK) {
    return RADIO_TX_ERR;
  } 
  return rf233_transmit();
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      read a received frame out of the radio buffer 
 * \param buf         pointer to where to copy received data
 * \param bufsize     Maximum size we can copy into bufsize
 * \return     Returns length of data read (> 0) if successful
 * \retval -1  Failed, was transmitting so FIFO is invalid
 * \retval -2  Failed, rx timed out (stuck in rx?)
 * \retval -3  Failed, too large frame for buffer
 * \retval -4  Failed, CRC/FCS failed (if USE_HW_FCS_CHECK is true)
 */
int rf233_read(void *buf, unsigned short bufsize) {
//  uint8_t radio_state;
  //uint8_t ed;       /* frame metadata */
  uint8_t frame_len = 0;
  uint8_t len = 0;
  //int rssi;

  PRINTF("RF233: Receiving.\n");
  
  if (pending_frame == 0) {
    PRINTF("RF233: No frame pending, abort.\n");
    return 0;
  }
  pending_frame = 0;

  /* get length of data in FIFO */
  trx_frame_read(&frame_len, 1);
  if (frame_len < 2) {
    frame_len = 2;
  }

  len = frame_len;
  /* FCS has already been stripped */
  len = frame_len - 2;

  if (frame_len == 0) {
    PRINTF("Frame is not long enough, abort.\n");
    return 0;
  }
  if (len > bufsize) {
    /* too large frame for the buffer, drop */
    PRINTF("RF233: too large frame for buffer, dropping (%u > %u).\n", frame_len, bufsize);
    flush_buffer();
    return 0;
  }
  PRINTF("RF233 read %u B\n", frame_len);

  /* read out the data into the buffer, disregarding the length and metadata bytes */
  trx_sram_read(1,(uint8_t *)buf, len);
  if (len >= 10) {
    header_t* header = (header_t*)buf;
    PRINTF("  FCF: %x\n", header->fcf);
    PRINTF("  SEQ: %x\n", header->seq);
    PRINTF("  PAN: %x\n", header->pan);
    PRINTF("  DST: %x\n", header->dest);
    PRINTF("  SRC: %x\n", header->src);
    {
      int k;
      PRINTF("RF233: Read frame (%u): ", frame_len);
      for(k = 0; k < frame_len; k++) {
        PRINTF("%02x", *((uint8_t *)buf + k));
      }
      PRINTF("\n");
    }
  }

  flush_buffer();

  return len;
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      perform a clear channel assessment 
 * \retval >0  Channel is clear
 * \retval 0   Channel is not clear
 */
int rf233_channel_clear(void) {
  uint8_t regsave;
  int was_off = 0;
  
  if(rf233_status() != STATE_RX_ON) {
    /* CCA can only be performed in RX state */
    was_off = 1;
    RF233_COMMAND(TRXCMD_RX_ON);
  }
   delay_ms(1);
  /* request a CCA, storing the channel number (set with the same reg) */
  regsave = trx_reg_read(RF233_REG_PHY_CC_CCA);
  regsave |= PHY_CC_CCA_DO_CCA | PHY_CC_CCA_MODE_CS_OR_ED;
  trx_reg_write(RF233_REG_PHY_CC_CCA, regsave);
  
  BUSYWAIT_UNTIL(trx_reg_read(RF233_REG_TRX_STATUS) & TRX_CCA_DONE, 1);
  //regsave = rf233_status();
  regsave = trx_reg_read(RF233_REG_TRX_STATUS);
  /* return to previous state */
  if (was_off) {
    RF233_COMMAND(TRXCMD_TRX_OFF);
  }
  else {
    RF233_COMMAND(TRXCMD_RX_AACK_ON);
  }

  /* check CCA */
  if((regsave & TRX_CCA_DONE) && (regsave & TRX_CCA_STATUS)) {
    PRINTF("RF233: CCA 1\n");
    return 1;
  }
  PRINTF("RF233: CCA 0\n");
  return 0;
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      check whether we are currently receiving a frame 
 * \retval >0  we are currently receiving a frame 
 * \retval 0   we are not currently receiving a frame 
 */
int rf233_receiving_packet(void) { 
  uint8_t trx_state;
  trx_state=rf233_status();
  if(trx_state == STATE_BUSY_RX_AACK) {
    PRINTF("RF233: Receiving frame\n");
    return 1;
  }
  PRINTF("RF233: not Receiving frame\n");
  return 0;
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      check whether we have a frame awaiting processing 
 * \retval >0  we have a frame awaiting processing 
 * \retval 0   we have not a frame awaiting processing 
 */
int rf233_pending_packet(void) {
  PRINTF("RF233: Frame %spending\n", pending_frame ? "" : "not ");
  return pending_frame;
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      switch the radio on to listen (rx) mode 
 * \retval 0   Success
 */
int rf233_on(void) {
  PRINTF("RF233: on\n");
  on();
  return 0;
}
/*---------------------------------------------------------------------------*/
/**
 * \brief      switch the radio off 
 * \retval 0   Success
 */
int rf233_off(void) {
  PRINTF("RF233: off\n");
  off();
  return 0;
}
 
void SetIEEEAddr(uint8_t *ieee_addr) {
	uint8_t *ptr_to_reg = ieee_addr;
	//for (uint8_t i = 0; i < 8; i++) {
		trx_reg_write((0x2b), *ptr_to_reg);
		ptr_to_reg++;
		trx_reg_write((0x2a), *ptr_to_reg);
		ptr_to_reg++;
		trx_reg_write((0x29), *ptr_to_reg);
		ptr_to_reg++;
		trx_reg_write((0x28), *ptr_to_reg);
		ptr_to_reg++;
		trx_reg_write((0x27), *ptr_to_reg);
		ptr_to_reg++;
		trx_reg_write((0x26), *ptr_to_reg);
		ptr_to_reg++;
		trx_reg_write((0x25), *ptr_to_reg);
		ptr_to_reg++;
		trx_reg_write((0x24), *ptr_to_reg);
		ptr_to_reg++;
	//}
}

 void SetPanId(uint16_t panId) {
	uint8_t *d = (uint8_t *)&panId;

	trx_reg_write(0x22, d[0]);
	trx_reg_write(0x23, d[1]);
}
 
void SetShortAddr(uint16_t addr) {
	uint8_t *d = (uint8_t *)&addr;

	trx_reg_write(0x20, d[0]);
	trx_reg_write(0x21, d[1]);
	trx_reg_write(0x2d, d[0] + d[1]);
}

/*---------------------------------------------------------------------------*/
/* switch the radio on */
int on(void) {
  /* Check whether radio is in sleep */
  if (sleep_on) {
    /* Wake the radio. It'll move to TRX_OFF state */
    wake_from_sleep();
    delay_ms(1);
    //printf("\r\nWake from sleep %d",rf233_get_channel());
    sleep_on = 0;
  }
  uint8_t state_now = rf233_status();
  if (state_now != STATE_PLL_ON &&
      state_now != STATE_TRX_OFF &&
      state_now != STATE_TX_ARET_ON) {
    /* fail, we need the radio transceiver to be in either of those states */
    return -1;
  }

  /* go to RX_ON state */
  RF233_COMMAND(TRXCMD_RX_AACK_ON);
  radio_is_on = 1;
  return 0;
}
/*---------------------------------------------------------------------------*/
/* switch the radio off */
int off(void) { 
  if(rf233_status() != STATE_RX_AACK_ON ) {
    /* fail, we need the radio transceiver to be in this state */
    return -1;
  }

  /* turn off the radio transceiver */
  RF233_COMMAND(TRXCMD_TRX_OFF);
  radio_is_on = 0;
  return 0;
}
/*---------------------------------------------------------------------------*/
/* Put the Radio in sleep mode */

int rf233_sleep(void) {
  int status;
  /* Check whether we're already sleeping */
  if (!sleep_on) {
    //printf("\r\n goto sleep %d",rf233_get_channel());
    //delay_ms(1);
    sleep_on = 1;
    /* Turn off the Radio */
    status = rf233_off();
    /* Set the SLP_PIN to high */
    if(status == 0) {
      goto_sleep();
    }
  }
  
  return 0;
}

/*---------------------------------------------------------------------------*/
/* 
 * Crude way of flushing the Tx/Rx FIFO: write the first byte as 0, indicating
 * a zero-length frame in the buffer. This is interpreted by the driver as an
 * empty buffer.
 */
static void flush_buffer(void) {
  /* NB: tentative untested implementation */
  uint8_t temp = 0;
  trx_frame_write(&temp, 1);
}

void goto_sleep(void) {
  gpio_set(SLP_PIN);
}
 
 void wake_from_sleep(void) {
  /* 
   * Triggers a radio state transition - assumes that the radio already is in
   * state SLEEP or DEEP_SLEEP and SLP pin is low. Refer to datasheet 6.6.
   * 
   * Note: this is the only thing that can get the radio from state SLEEP or 
   * state DEEP_SLEEP!
   */
  gpio_clear(SLP_PIN);
}

uint8_t rf233_status() {
	return (trx_reg_read(RF233_REG_TRX_STATUS) & TRX_STATUS);
}
/*---------------------------------------------------------------------------*/
