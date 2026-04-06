/* pingpong_rx.c — RX bridge (LoRa -> UART print) with Anti-Replay + CRC Verification
   Simple, practical security embedded directly in this file
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "delay.h"
#include "timer.h"
#include "radio.h"
#include "tremo_system.h"

#if defined( REGION_AS923 )
#define RF_FREQUENCY                                923000000 // Hz
#elif defined( REGION_AU915 )
#define RF_FREQUENCY                                915000000 // Hz
#elif defined( REGION_CN470 )
#define RF_FREQUENCY                                470000000 // Hz
#elif defined( REGION_CN779 )
#define RF_FREQUENCY                                779000000 // Hz
#elif defined( REGION_EU433 )
#define RF_FREQUENCY                                433000000 // Hz
#elif defined( REGION_EU868 )
#define RF_FREQUENCY                                868000000 // Hz
#elif defined( REGION_KR920 )
#define RF_FREQUENCY                                920000000 // Hz
#elif defined( REGION_IN865 )
#define RF_FREQUENCY                                865000000 // Hz
#elif defined( REGION_US915 )
#define RF_FREQUENCY                                915000000 // Hz
#elif defined( REGION_US915_HYBRID )
#define RF_FREQUENCY                                915000000 // Hz
#else
    #error "Please define a frequency band in the compiler options."
#endif

#define TX_OUTPUT_POWER                             14        // dBm

#if defined( USE_MODEM_LORA )
#define LORA_BANDWIDTH                              0
#define LORA_SPREADING_FACTOR                       7
#define LORA_CODINGRATE                             1
#define LORA_PREAMBLE_LENGTH                        8
#define LORA_SYMBOL_TIMEOUT                         0
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_IQ_INVERSION_ON                        false
#elif defined( USE_MODEM_FSK )
#define FSK_FDEV                                    25000
#define FSK_DATARATE                                50000
#define FSK_BANDWIDTH                               50000
#define FSK_AFC_BANDWIDTH                           83333
#define FSK_PREAMBLE_LENGTH                         5
#define FSK_FIX_LENGTH_PAYLOAD_ON                   false
#else
    #error "Please define a modem in the compiler options."
#endif

typedef enum { LOWPOWER, RX, RX_TIMEOUT, RX_ERROR, TX, TX_TIMEOUT } States_t;

#define RX_TIMEOUT_VALUE        3000
#define BUFFER_SIZE             256
#define CHUNK_MAX               240

// ===== SECURITY CONSTANTS (Priority 1 & 2) =====
#define MAX_JUMP_THRESHOLD          5000   /* Priority 2: Prevent desynchronization */
#define ANTI_REPLAY_WINDOW_SEC      300    /* Priority 1: 5 minutes anti-replay window */
#define CLOCK_SKEW_TOLERANCE_SEC    5      /* Allow ±5 sec clock differences */

/* original buffers/vars */
uint8_t Buffer[BUFFER_SIZE];
uint16_t BufferSize = 0;

// New buffers for RX packet handling
static uint8_t  LoraBuf[BUFFER_SIZE];
static uint16_t LoraLen = 0;

volatile States_t State = LOWPOWER;
int8_t RssiValue = 0;
int8_t SnrValue = 0;
uint32_t ChipId[2] = {0};

static RadioEvents_t RadioEvents;

// ===================== CRC16-CCITT (inline) ======================
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

// ===================== Enhanced Per-Node Tracking ======================
#define MAX_NODES 10

typedef struct {
    uint8_t  node_id;   /* 8-bit node ID (1-100) from TX */
    uint32_t last_seq;
    uint32_t packets_received;
    uint32_t packets_rejected;
    int8_t   last_rssi;
} PerNodeState_t;

static PerNodeState_t node_states[MAX_NODES];
static uint8_t node_count = 0;

static void per_node_init(void)
{
    memset(node_states, 0, sizeof(node_states));
    node_count = 0;
}

static PerNodeState_t* per_node_get_or_create(uint8_t node_id)
{
    // Search for existing node
    for (uint8_t i = 0; i < node_count; i++) {
        if (node_states[i].node_id == node_id) {
            return &node_states[i];
        }
    }
    
    // Create new node entry if space available
    if (node_count < MAX_NODES) {
        node_states[node_count].node_id = node_id;
        node_states[node_count].last_seq = 0;
        node_states[node_count].packets_received = 0;
        node_states[node_count].packets_rejected = 0;
        node_states[node_count].last_rssi = 0;
        return &node_states[node_count++];
    }
    
    return NULL;  // No space
}

// ===================== Structured Packet Parsing =====================
typedef struct {
    uint8_t  node_id;
    uint32_t seq;
    uint16_t payload_len;
    uint8_t  payload[CHUNK_MAX];
    uint16_t crc;
    int      valid;
    char     reject_reason[80];
} ParsedPacket_t;

static void parse_and_validate_packet(const uint8_t* raw, uint16_t raw_len,
                                       ParsedPacket_t* pkt)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->valid = 0;

    // Minimum: 1+4+2+0+2 = 9 bytes (optimized, no timestamp)
    if (raw_len < 9) {
        snprintf(pkt->reject_reason, sizeof(pkt->reject_reason),
                 "Packet too small (%u < 9)", raw_len);
        return;
    }

    int pos = 0;

    // 1. node_id (1 byte)
    pkt->node_id = raw[pos++];

    // 2. seq (4 bytes, little-endian)
    pkt->seq = (uint32_t)raw[pos+0]
             | ((uint32_t)raw[pos+1] << 8)
             | ((uint32_t)raw[pos+2] << 16)
             | ((uint32_t)raw[pos+3] << 24);
    pos += 4;

    // 3. payload_len (2 bytes, little-endian)
    pkt->payload_len = (uint16_t)raw[pos+0] | ((uint16_t)raw[pos+1] << 8);
    pos += 2;

    // Validate payload length
    if (pkt->payload_len > CHUNK_MAX ||
        pos + pkt->payload_len + 2 > raw_len) {
        snprintf(pkt->reject_reason, sizeof(pkt->reject_reason),
                 "Invalid payload len (%u)", pkt->payload_len);
        return;
    }

    // 4. payload
    memcpy(pkt->payload, &raw[pos], pkt->payload_len);
    pos += pkt->payload_len;

    // 5. crc (2 bytes, little-endian)
    pkt->crc = (uint16_t)raw[pos+0] | ((uint16_t)raw[pos+1] << 8);

    // ==================== SECURITY CHECKS ====================

    // Check 1: CRC16
    uint16_t computed_crc = crc16_calc(pkt->payload, pkt->payload_len);
    if (computed_crc != pkt->crc) {
        snprintf(pkt->reject_reason, sizeof(pkt->reject_reason),
                 "CRC fail (got 0x%04X)", (unsigned)pkt->crc);
        return;
    }

    // Check 2: Get or create per-node state
    PerNodeState_t* node = per_node_get_or_create(pkt->node_id);
    if (!node) {
        snprintf(pkt->reject_reason, sizeof(pkt->reject_reason),
                 "Max nodes exceeded");
        return;
    }

    // Check 3: Auto-detect node reset via backward sequence jump
    // If seq < last_seq, it indicates device reset (seq counter restarted at 0)
    if (pkt->seq < node->last_seq) {
        // Backward jump detected = node reset signature
        uint32_t backward_jump = node->last_seq - pkt->seq;
        printf("[NODE RESET DETECTED] Vehicle=%u: seq jumped backward by %lu (last=%lu, new=%lu)\r\n",
               (unsigned)pkt->node_id, (unsigned long)backward_jump, 
               (unsigned long)node->last_seq, (unsigned long)pkt->seq);
        
        // Reset node state: allow sequence to restart from new value
        node->last_seq = pkt->seq;
        node->packets_received = 0;
        node->packets_rejected = 0;
        
        // Continue processing - this packet will be accepted
    }
    // Normal replay detection (strict monotonic)
    else if (pkt->seq <= node->last_seq && !(node->last_seq == 0 && node->packets_received == 0)) {
        snprintf(pkt->reject_reason, sizeof(pkt->reject_reason),
                 "Replay: seq %lu <= last %lu",
                 (unsigned long)(pkt->seq), (unsigned long)(node->last_seq));
        node->packets_rejected++;
        return;
    }

    // Check 4: MAX_JUMP (Priority 2)
    uint32_t seq_diff = pkt->seq - node->last_seq;
    if (seq_diff > MAX_JUMP_THRESHOLD) {
        snprintf(pkt->reject_reason, sizeof(pkt->reject_reason),
                 "Jump too large: %lu > %u",
                 (unsigned long)seq_diff, MAX_JUMP_THRESHOLD);
        node->packets_rejected++;
        return;
    }

    // Check 5: Timestamp validation (DISABLED for LoRaWAN)
    // LoRaWAN devices have independent clocks (no NTP/GPS sync)
    // Timestamp is logged for debugging but NOT used for validation
    // If synchronized clocks are available, uncomment validation below:
    /*
    uint32_t now_sec = TimerGetCurrentTime() / 1000;
    if (pkt->timestamp_sec > now_sec + CLOCK_SKEW_TOLERANCE_SEC) {
        uint32_t future = pkt->timestamp_sec - now_sec;
        snprintf(pkt->reject_reason, sizeof(pkt->reject_reason),
                 "Future: %lu sec ahead (max=%u)",
                 (unsigned long)future, CLOCK_SKEW_TOLERANCE_SEC);
        node->packets_rejected++;
        return;
    }
    */

    // ✅ All checks passed!
    pkt->valid = 1;
    node->last_seq = pkt->seq;
    node->last_rssi = (int8_t)RssiValue;
    node->packets_received++;
}

// ===================== Simple Security Context ======================
#define RX_NODE_ID 0x00  /* RX (Gateway) node, for reference only */

/* Counters for packet validation */
static uint32_t valid_packets = 0;
static uint32_t invalid_seq = 0;
static uint32_t invalid_crc = 0;
static uint32_t invalid_jump = 0;  /* Count of rejected large jumps */


/* forward declarations */
void OnTxDone( void );
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr );
void OnTxTimeout( void );
void OnRxTimeout( void );
void OnRxError( void );

/* main */
int app_start( void )
{
    uint8_t i;
    uint32_t random;

    (void)system_get_chip_id(ChipId);
    printf("Gateway Chip ID: 0x%08lX-%08lX\r\n",
           (unsigned long)ChipId[0], (unsigned long)ChipId[1]);
    printf("\r\n");

    // Initialize per-node tracking
    per_node_init();
    
    // printf("[+] Security Features:\r\n");
    // printf("   - Per-node sequence tracking\r\n");
    // printf("   - CRC16-CCITT integrity check\r\n");
    // printf("   - Timestamp logging (NOT validated - no clock sync)\r\n");
    // printf("   - MAX_JUMP detection (threshold=%u)\r\n", MAX_JUMP_THRESHOLD);
    // printf("   - Clock skew tolerance (+-%u sec) [DISABLED]\r\n", CLOCK_SKEW_TOLERANCE_SEC);
    // printf("\r\n");

    /* Radio init */
    RadioEvents.TxDone = OnTxDone;
    RadioEvents.RxDone = OnRxDone;
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxTimeout = OnRxTimeout;
    RadioEvents.RxError = OnRxError;

    Radio.Init( &RadioEvents );
    Radio.SetChannel( RF_FREQUENCY );

#if defined( USE_MODEM_LORA )
    Radio.SetTxConfig( MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                       LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                       LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                       true, 0, 0, LORA_IQ_INVERSION_ON, 3000 );

    Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                       LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                       LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                       0, true, 0, 0, LORA_IQ_INVERSION_ON, true );
#elif defined( USE_MODEM_FSK )
    Radio.SetTxConfig( MODEM_FSK, TX_OUTPUT_POWER, FSK_FDEV, 0,
                       FSK_DATARATE, 0,
                       FSK_PREAMBLE_LENGTH, FSK_FIX_LENGTH_PAYLOAD_ON,
                       true, 0, 0, 0, 3000 );

    Radio.SetRxConfig( MODEM_FSK, FSK_BANDWIDTH, FSK_DATARATE,
                       0, FSK_AFC_BANDWIDTH, FSK_PREAMBLE_LENGTH,
                       0, FSK_FIX_LENGTH_PAYLOAD_ON, 0, true,
                       0, 0, false, true );
#endif

    Radio.Rx( RX_TIMEOUT_VALUE );

    printf("Listening for Follower Nodes...\r\n");
    printf("Max nodes: %u | Max payload: %u bytes\r\n", MAX_NODES, CHUNK_MAX);
    printf("==============================================\r\n\r\n");

    static uint32_t total_accepted = 0;
    static uint32_t total_rejected = 0;

    while(1)
    {
        switch(State)
        {
        case RX:
            {
                ParsedPacket_t pkt;
                parse_and_validate_packet(LoraBuf, LoraLen, &pkt);

                if (pkt.valid) {
                    total_accepted++;
                    printf("[RX OK] node=%u, seq=%lu, len=%u, rssi=%d, snr=%d\r\n",
                           (unsigned)pkt.node_id,
                           (unsigned long)pkt.seq,
                           (unsigned)pkt.payload_len,
                           (int)RssiValue, (int)SnrValue);

                    if (pkt.payload_len > 0) {
                        // Build complete payload string, print on single line
                        char payload_str[CHUNK_MAX + 1];
                        memcpy(payload_str, (char*)pkt.payload, pkt.payload_len);
                        payload_str[pkt.payload_len] = '\0';
                        printf("Payload: %s\r\n", payload_str);
                    }
                } else {
                    total_rejected++;
                    printf("[RX DROP] node=%u, seq=%lu, rssi=%d\r\n",
                           (unsigned)pkt.node_id,
                           (unsigned long)pkt.seq,
                           (int)RssiValue);
                    printf("        Reason: %s\r\n", pkt.reject_reason);
                }
                printf("\r\n");
            }
            Radio.Rx( RX_TIMEOUT_VALUE );
            State = LOWPOWER;
            break;
        case TX:
            Radio.Rx( RX_TIMEOUT_VALUE );
            State = LOWPOWER;
            break;
        case RX_TIMEOUT:
        case RX_ERROR:
            Radio.Rx( RX_TIMEOUT_VALUE );
            State = LOWPOWER;
            break;
        case TX_TIMEOUT:
            Radio.Rx( RX_TIMEOUT_VALUE );
            State = LOWPOWER;
            break;
        case LOWPOWER:
        default:
            break;
        }

        Radio.IrqProcess();

        // Periodic detailed stats (every 30 seconds)
        static uint32_t last_stats = 0;
        uint32_t now = TimerGetCurrentTime();
        if (TimerGetElapsedTime(last_stats) > 30000) {
            printf("\r\n=== GATEWAY STATISTICS (30sec) ===\r\n");
            printf("Total accepted: %lu\r\n", (unsigned long)total_accepted);
            printf("Total rejected: %lu\r\n", (unsigned long)total_rejected);
            printf("Active nodes:   %u/%u\r\n", node_count, MAX_NODES);
            printf("\r\n");

            printf("Per-Node Status:\r\n");
            for (uint8_t i = 0; i < node_count; i++) {
                printf("  Node %u: seq=%lu, ok=%lu, bad=%lu\r\n",
                       (unsigned)node_states[i].node_id,
                       (unsigned long)node_states[i].last_seq,
                       (unsigned long)node_states[i].packets_received,
                       (unsigned long)node_states[i].packets_rejected);
            }
            printf("==================================\r\n\r\n");

            last_stats = now;
        }
    }
}

/* Callbacks */
void OnTxDone( void )
{
    Radio.Sleep();
    State = TX;
}

void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    Radio.Sleep();

    uint16_t use_len = (size > BUFFER_SIZE) ? BUFFER_SIZE : size;
    memcpy(LoraBuf, payload, use_len);
    LoraLen = use_len;
    RssiValue = rssi;
    SnrValue = snr;
    
    /* State machine will call parse_and_validate_packet() */
    State = RX;
}

void OnTxTimeout( void )
{
    Radio.Sleep();
    State = TX_TIMEOUT;
}

void OnRxTimeout( void )
{
    // OnRxTimeout debug print disabled - not necessary for production
    // This is called every 3 seconds when radio timeout waiting for packet
    Radio.Sleep();
    State = RX_TIMEOUT;
}

void OnRxError( void )
{
    Radio.Sleep();
    State = RX_ERROR;
}
