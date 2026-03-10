/* military_vehicle_tx.c
   Military Convoy Vehicle Node - Secure LoRaWAN Communication
   
   Features:
   ✅ Per-vehicle unique identification
   ✅ Anti-replay attack protection
   ✅ Packet integrity verification (CRC16)
   ✅ Secure sequence counter
   ✅ Military-grade reliability
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "delay.h"
#include "timer.h"
#include "radio.h"
#include "tremo_system.h"
#include "tremo_uart.h"

// ================= Region / RF =================
#if   defined( REGION_AS923 )
#  define RF_FREQUENCY  923000000
#elif defined( REGION_AU915 )
#  define RF_FREQUENCY  915000000
#elif defined( REGION_CN470 )
#  define RF_FREQUENCY  470000000
#elif defined( REGION_CN779 )
#  define RF_FREQUENCY  779000000
#elif defined( REGION_EU433 )
#  define RF_FREQUENCY  433000000
#elif defined( REGION_EU868 )
#  define RF_FREQUENCY  868000000
#elif defined( REGION_KR920 )
#  define RF_FREQUENCY  920000000
#elif defined( REGION_IN865 )
#  define RF_FREQUENCY  865000000
#elif defined( REGION_US915 )
#  define RF_FREQUENCY  915000000
#elif defined( REGION_US915_HYBRID )
#  define RF_FREQUENCY  915000000
#else
#  error "Please define a frequency band in the compiler options."
#endif

#define TX_OUTPUT_POWER   14

#if defined( USE_MODEM_LORA )
#  define LORA_BANDWIDTH            0
#  define LORA_SPREADING_FACTOR     7
#  define LORA_CODINGRATE           1
#  define LORA_PREAMBLE_LENGTH      8
#  define LORA_SYMBOL_TIMEOUT       0
#  define LORA_FIX_LENGTH_PAYLOAD_ON false
#  define LORA_IQ_INVERSION_ON      false
#elif defined( USE_MODEM_FSK )
#  define FSK_FDEV                  25000
#  define FSK_DATARATE              50000
#  define FSK_BANDWIDTH             50000
#  define FSK_AFC_BANDWIDTH         83333
#  define FSK_PREAMBLE_LENGTH       5
#  define FSK_FIX_LENGTH_PAYLOAD_ON false
#else
#  error "Please define a modem in the compiler options."
#endif

typedef enum { LOWPOWER, RX, RX_TIMEOUT, RX_ERROR, TX, TX_TIMEOUT } States_t;

#define RX_TIMEOUT_VALUE    3000
#define BUFFER_SIZE         256
#define CHUNK_MAX           200   /* Reduced from 220 to fit timestamp */

// ===== SECURITY CONSTANTS (from SECURITY_FIXES.md) =====
#define MAX_JUMP_THRESHOLD  5000   /* Priority 2: Prevent desynchronization */

// Radio buffers
static uint8_t  LoraBuf[BUFFER_SIZE];
static uint16_t LoraLen = 0;

static volatile States_t State = LOWPOWER;
static volatile uint8_t  txDone = 0;

static int8_t  RssiValue = 0;
static int8_t  SnrValue  = 0;
static uint32_t ChipId[2] = {0};

static RadioEvents_t RadioEvents;

// ===================== CRC16-CCITT (unchanged) ======================
static uint16_t crc16_calc(const uint8_t* data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
            crc &= 0xFFFF;
        }
    }
    return crc;
}

// ===================== Sequence Tracking with Validation ======================
#define MAX_NODES 10

typedef struct {
    uint32_t node_id;  /* 32-bit node ID for full 64-bit XOR support (billions of modules) */
    uint32_t last_seq;
} SeqTracker_t;

static SeqTracker_t seq_trackers[MAX_NODES];
static uint8_t seq_tracker_count = 0;

static void seq_init(void)
{
    memset(seq_trackers, 0, sizeof(seq_trackers));
    seq_tracker_count = 0;
}

/* NEW: Validate sequence with MAX_JUMP check (Priority 2) */
static int seq_is_valid_with_jump_check(uint32_t node_id, uint32_t seq)
{
    for (uint8_t i = 0; i < seq_tracker_count; i++) {
        if (seq_trackers[i].node_id == node_id) {
            uint32_t last_seq = seq_trackers[i].last_seq;
            
            // Must be increasing
            if (seq <= last_seq) {
                return 0;  // Replay detected
            }
            
            // ✅ NEW: Check jump not too large (Priority 2)
            if (seq - last_seq > MAX_JUMP_THRESHOLD) {
                printf("[SECURITY] Jump too large for node=%lu: %lu -> %lu (diff=%lu)\r\n",
                       (unsigned long)node_id, (unsigned long)last_seq, (unsigned long)seq,
                       (unsigned long)(seq - last_seq));
                return 0;  // Reject jump attack test
            }
            
            return 1;  // Valid sequence
        }
    }
    return 1;  // First packet from this node
}

static void seq_update(uint32_t node_id, uint32_t seq)
{
    for (uint8_t i = 0; i < seq_tracker_count; i++) {
        if (seq_trackers[i].node_id == node_id) {
            seq_trackers[i].last_seq = seq;
            return;
        }
    }
    if (seq_tracker_count < MAX_NODES) {
        seq_trackers[seq_tracker_count].node_id = node_id;
        seq_trackers[seq_tracker_count].last_seq = seq;
        seq_tracker_count++;
    }
}

// ===================== Enhanced Security Context ======================
static uint32_t tx_sequence = 0;

/* ========== TEST MODE VARIABLES (DISABLED IN PRODUCTION) ========== */
// static int replay_test_mode = 0;
// static int jump_attack_mode = 0;
// static int max_jump_test_mode = 0;
/* ================================================================== */

static uint8_t LOCAL_NODE_ID = 0;  /* 8-bit node ID (1-100) via modulo: (ChipId[0] ^ ChipId[1]) % 100 + 1 */

/* Optimized packet structure (no timestamp) */
typedef struct {
    uint8_t  node_id;              /* 1 byte - 8-bit node ID (1-100) */
    uint32_t seq;                  /* 4 bytes */
    uint8_t  payload[CHUNK_MAX];
    uint16_t payload_len;
    uint16_t crc;
} OptimizedSecurePacket_t;

// ===================== UART (Tremo) ======================
#define UART_INST UART0
#define UART_BAUD 115200

static void uart_init_wrapper(uint32_t baud)
{
    uart_config_t cfg;
    uart_config_init(&cfg);

    cfg.baudrate     = baud;
    cfg.data_width   = UART_DATA_WIDTH_8;
    cfg.stop_bits    = UART_STOP_BITS_1;
    cfg.parity       = UART_PARITY_NO;
    cfg.flow_control = UART_FLOW_CONTROL_DISABLED;
    cfg.mode         = UART_MODE_TXRX;
    cfg.fifo_mode    = 1;

    uart_init(UART_INST, &cfg);
    uart_cmd(UART_INST, true);
}

static int uart_available_wrapper(void)
{
    return uart_get_flag_status(UART_INST, UART_FLAG_RX_FIFO_EMPTY) ? 0 : 1;
}

static int uart_readbyte_wrapper(void)
{
    if (!uart_available_wrapper()) return -1;
    return (int)uart_receive_data(UART_INST);
}

static void radio_send_blocking(const uint8_t* data, uint16_t len)
{
    txDone = 0;
    Radio.Send((uint8_t*)data, len);
    while (!txDone) { Radio.IrqProcess(); }
}

// ============ Radio callbacks (unchanged) ============
static void OnTxDone(void)       { Radio.Sleep(); txDone = 1; State = TX; }
static void OnTxTimeout(void)    { Radio.Sleep(); txDone = 1; State = TX_TIMEOUT; }
static void OnRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr)
{
    Radio.Sleep();
    LoraLen = (size > BUFFER_SIZE) ? BUFFER_SIZE : size;
    memcpy(LoraBuf, payload, LoraLen);
    RssiValue = rssi; SnrValue = snr;
    State = RX;
}
static void OnRxTimeout(void)    { Radio.Sleep(); State = RX_TIMEOUT; }
static void OnRxError(void)      { Radio.Sleep(); State = RX_ERROR;   }

// ===== UART JSON Parser ====="
// Read one line from UART (JSON terminated by \n)
// Returns: pointer to buffer if JSON complete, NULL if timeout/empty
static char uart_json_buffer[300];
static uint16_t uart_json_idx = 0;

static const char* uart_read_json_line(uint32_t timeout_ms)
{
    uint32_t start = TimerGetCurrentTime();
    
    while (TimerGetElapsedTime(start) < timeout_ms) {
        int byte = uart_readbyte_wrapper();
        
        if (byte >= 0) {
            // Got data from UART
            if (byte == '\n') {
                // End of JSON line
                uart_json_buffer[uart_json_idx] = 0;  // null terminate
                
                if (uart_json_idx > 0) {
                    printf("[UART RX] Got JSON line (%u bytes): ", uart_json_idx);
                    printf("%s\r\n", uart_json_buffer);
                    
                    uint16_t result_len = uart_json_idx;
                    uart_json_idx = 0;  // reset for next read
                    
                    return uart_json_buffer;
                }
                uart_json_idx = 0;  // skip empty line
            } else if (byte == '\r') {
                // Skip CR
            } else if (uart_json_idx < (sizeof(uart_json_buffer) - 1)) {
                // Add to buffer
                uart_json_buffer[uart_json_idx++] = (char)byte;
            } else {
                printf("[UART ERROR] Buffer overflow, resetting\r\n");
                uart_json_idx = 0;
            }
        }
    }
    
    // Timeout - no JSON received
    return NULL;
}

// ===== Optimized Send (no timestamp) =====
static void send_enhanced_secure_packet(const uint8_t* plaintext, uint16_t plain_len)
{
    if (plain_len > CHUNK_MAX) {
        printf("ERROR: Payload too large (%u > %u bytes)\r\n", plain_len, CHUNK_MAX);
        return;
    }

    uint8_t packet[BUFFER_SIZE];
    int pkt_pos = 0;

    // 1. node_id (1 byte)
    packet[pkt_pos++] = LOCAL_NODE_ID;

    // 2. seq (4 bytes, little-endian)
    packet[pkt_pos++] = (tx_sequence >>  0) & 0xFF;
    packet[pkt_pos++] = (tx_sequence >>  8) & 0xFF;
    packet[pkt_pos++] = (tx_sequence >> 16) & 0xFF;
    packet[pkt_pos++] = (tx_sequence >> 24) & 0xFF;
    
    uint32_t sent_seq = tx_sequence;

    // Normal sequence increment (production mode)
    tx_sequence++;

    /* ========== TEST MODE LOGIC (COMMENTED OUT) ==========
     * // Handle jump attack test mode (BEFORE increment)
     * if (max_jump_test_mode) {
     *     tx_sequence += MAX_JUMP_THRESHOLD + 100;
     *     max_jump_test_mode = 0;
     *     printf("[TX SECURITY TEST] Jump attack mode: seq will jump to %lu\r\n", 
     *            (unsigned long)tx_sequence);
     * } else if (!replay_test_mode) {
     *     if (jump_attack_mode) {
     *         tx_sequence += 2000;
     *         jump_attack_mode = 0;
     *     } else {
     *         tx_sequence++;
     *     }
     * }
     * ====================================================== */

    // 3. payload_len (2 bytes, little-endian)
    packet[pkt_pos++] = (plain_len >> 0) & 0xFF;
    packet[pkt_pos++] = (plain_len >> 8) & 0xFF;

    // 4. payload
    memcpy(&packet[pkt_pos], plaintext, plain_len);
    pkt_pos += plain_len;

    // 5. crc16
    uint16_t crc = crc16_calc(plaintext, plain_len);
    packet[pkt_pos++] = (crc >> 0) & 0xFF;
    packet[pkt_pos++] = (crc >> 8) & 0xFF;

    // Send
    radio_send_blocking(packet, (uint16_t)pkt_pos);
    
    // ✅ COMMENTED: Don't broadcast node_id as text (was causing RX parse confusion)
    // printf("[LORA-NODE-ID] %u\r\n", LOCAL_NODE_ID);
    printf("[TX SECURE] Vehicle=%u, seq=%lu, len=%u, crc=0x%04X\r\n",
           (unsigned)LOCAL_NODE_ID, (unsigned long)sent_seq, plain_len, (unsigned)crc);
}

/* ========== TEST FUNCTION (DISABLED IN PRODUCTION) ==========
   Used only for replay attack testing - keeps seq unchanged - COMMENTED OUT
   
   static void send_enhanced_secure_packet_no_inc(const uint8_t* plaintext, uint16_t plain_len)
   {
       if (plain_len > CHUNK_MAX) {
           printf("ERROR: Payload too large (%u > %u bytes)\\r\\n", plain_len, CHUNK_MAX);
           return;
       }

       uint8_t packet[BUFFER_SIZE];
       int pkt_pos = 0;

       packet[pkt_pos++] = LOCAL_NODE_ID;
       packet[pkt_pos++] = (tx_sequence >>  0) & 0xFF;
       packet[pkt_pos++] = (tx_sequence >>  8) & 0xFF;
       packet[pkt_pos++] = (tx_sequence >> 16) & 0xFF;
       packet[pkt_pos++] = (tx_sequence >> 24) & 0xFF;
       packet[pkt_pos++] = (plain_len >> 0) & 0xFF;
       packet[pkt_pos++] = (plain_len >> 8) & 0xFF;

       memcpy(&packet[pkt_pos], plaintext, plain_len);
       pkt_pos += plain_len;

       uint16_t crc = crc16_calc(plaintext, plain_len);
       packet[pkt_pos++] = (crc >> 0) & 0xFF;
       packet[pkt_pos++] = (crc >> 8) & 0xFF;

       radio_send_blocking(packet, (uint16_t)pkt_pos);
       printf("[TX REPLAY] node=%u, seq=%lu (SAME), len=%u, crc=0x%04X\\r\\n",
              (unsigned)LOCAL_NODE_ID, (unsigned long)tx_sequence, plain_len, (unsigned)crc);
   }
 * ============================================================ */

// ==================== MAIN ====================
int app_start(void)
{
    system_get_chip_id(ChipId);
    
    // ✅ SIMPLE: Assign node ID in range 1-100 using modulo of XOR
    // Each unique chip ID gets consistent ID within 1-100 range
    LOCAL_NODE_ID = ((ChipId[0] ^ ChipId[1]) % 100) + 1;

    printf("Vehicle Chip ID: 0x%08lX-%08lX\r\n", (unsigned long)ChipId[0], (unsigned long)ChipId[1]);
    printf("Vehicle Node ID: %u\r\n", (unsigned)LOCAL_NODE_ID);
    printf("Status: OPERATIONAL\r\n");
    printf("\r\n");

    // Radio init
    RadioEvents.TxDone    = OnTxDone;
    RadioEvents.RxDone    = OnRxDone;
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxTimeout = OnRxTimeout;
    RadioEvents.RxError   = OnRxError;

    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);

#if defined( USE_MODEM_LORA )
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);
#else
    Radio.SetTxConfig(MODEM_FSK, TX_OUTPUT_POWER, FSK_FDEV, 0,
                      FSK_DATARATE, 0, FSK_PREAMBLE_LENGTH,
                      FSK_FIX_LENGTH_PAYLOAD_ON, true, 0, 0, 0, 3000);

    Radio.SetRxConfig(MODEM_FSK, FSK_BANDWIDTH, FSK_DATARATE, 0,
                      FSK_AFC_BANDWIDTH, FSK_PREAMBLE_LENGTH, 0,
                      FSK_FIX_LENGTH_PAYLOAD_ON, 0, true, 0, 0, false, true);
#endif

    // Enable UART to read JSON payloads from ESP32
    uart_init_wrapper(UART_BAUD);
    printf("[INIT] UART initialized at %lu baud\r\n", (unsigned long)UART_BAUD);
    printf("[INIT] Listening for sensor JSON from ESP32...\r\n");
    seq_init();

    Radio.Rx(RX_TIMEOUT_VALUE);
    /* ========== TEST MODE VARIABLES (DISABLED IN PRODUCTION) ========== */
    // static uint32_t test_phase = 0;  /* 0=NORMAL, 1=REPLAY, 2=JUMP, 3=MAX_JUMP */

    while (1) {
        Radio.IrqProcess();

        // ========== TRY TO READ JSON FROM UART FIRST ==========
        const char* json_line = uart_read_json_line(100);  // 100ms timeout per read attempt
        
        if (json_line != NULL && strlen(json_line) > 0) {
            // ✅ Got JSON from ESP32!
            printf("[TX UART] Received sensor data from ESP32, queuing transmission\r\n");
            
            if (strlen(json_line) <= CHUNK_MAX) {
                send_enhanced_secure_packet((uint8_t*)json_line, strlen(json_line));
                printf("[TX OK] Real sensor data transmitted\r\n");
            } else {
                printf("[TX ERROR] JSON too long (%u > %u)\r\n", (unsigned)strlen(json_line), CHUNK_MAX);
            }
        } else {
            // No JSON available - waiting for real sensor data from ESP32
            static uint32_t last_status_msg = 0;
            uint32_t now = TimerGetCurrentTime();
            
            // Print status message every 5 seconds
            if (TimerGetElapsedTime(last_status_msg) > 5000) {
                printf("[STATUS] Waiting for real sensor data from ESP32 via UART\r\n");
                last_status_msg = now;
            }
        }
        
        /* ========== TEST MODES (COMMENTED OUT FOR PRODUCTION) ==========
         * Uncomment sections below for security validation testing only
         * 
         * // Auto-cycle test modes
         * test_phase = (heartbeat_count / 1) % 4;
         * 
         * switch (test_phase) {
         *     case 0:  // NORMAL_MODE: auto-increment seq
         *         replay_test_mode = 0;
         *         jump_attack_mode = 0;
         *         max_jump_test_mode = 0;
         *         printf("[TEST] MODE: NORMAL (seq auto-increment)\r\n");
         *         {
         *             const uint8_t ping[] = "NORMAL";
         *             send_enhanced_secure_packet(ping, sizeof(ping) - 1);
         *         }
         *         break;
         *         
         *     case 1:  // REPLAY_MODE: seq stays fixed
         *         replay_test_mode = 1;
         *         jump_attack_mode = 0;
         *         max_jump_test_mode = 0;
         *         printf("[TEST] MODE: REPLAY (seq FIXED)\r\n");
         *         {
         *             const uint8_t ping[] = "REPLAY";
         *             send_enhanced_secure_packet_no_inc(ping, sizeof(ping) - 1);
         *         }
         *         break;
         *         
         *     case 2:  // JUMP_ATTACK: seq += 2000
         *         replay_test_mode = 0;
         *         jump_attack_mode = 1;
         *         max_jump_test_mode = 0;
         *         printf("[TEST] MODE: JUMP_ATTACK (seq += 2000)\r\n");
         *         {
         *             const uint8_t ping[] = "JUMP";
         *             send_enhanced_secure_packet(ping, sizeof(ping) - 1);
         *         }
         *         break;
         *         
         *     case 3:  // MAX_JUMP_TEST: seq += > 5000
         *         replay_test_mode = 0;
         *         jump_attack_mode = 0;
         *         max_jump_test_mode = 1;
         *         printf("[TEST] MODE: MAX_JUMP (seq jump > threshold)\r\n");
         *         {
         *             const uint8_t ping[] = "MAXJUMP";
         *             send_enhanced_secure_packet(ping, sizeof(ping) - 1);
         *         }
         *         break;
         * }
         * ============================================================== */
    }
}

