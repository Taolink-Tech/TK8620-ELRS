#ifndef H_CRSF_CONTROLLER
#define H_CRSF_CONTROLLER

#include "handset.h"
#include "crsf_protocol.h"

typedef struct {
    Handset_t *handset;
    // /**
    //  * @brief End the handset protocol
    //  */
    // void (*End)(void);


    // static uint8_t modelId;         // The model ID as received from the Transmitter
    bool ForwardDevicePings; // true if device pings should be forwarded OTA
    bool elrsLUAmode;

    uint32_t GoodPktsCountResult; // need to latch the results
    uint32_t BadPktsCountResult;  // need to latch the results

    void (*packetQueueExtended)(uint8_t type, void *data, uint8_t len);

    // void setPacketInterval(int32_t PacketInterval) override;
    // void JustSentRFpacket() override;

    uint8_t (*getModelID)(void);

    uint8_t (*GetMaxPacketBytes)(void);
    // static uint32_t GetCurrentBaudRate() { return UARTrequestedBaud; }
    // int getMinPacketInterval() const override;

    union inBuffer_U inBuffer; 

//     /// OpenTX mixer sync ///
    volatile uint32_t dataLastRecv;
//     volatile int32_t OpenTXsyncOffset = 0;
//     volatile int32_t OpenTXsyncWindow = 0;
//     volatile int32_t OpenTXsyncWindowSize = 0;
//     uint32_t OpenTXsyncLastSent = 0;

//     /// UART Handling ///
    uint8_t SerialInPacketPtr; // index where we are reading/writing
    bool halfDuplex;
//     bool transmitting = false;
    uint32_t GoodPktsCount;
    uint32_t BadPktsCount;
//     uint32_t UARTwdtLastChecked = 0;
    uint8_t maxPacketBytes;
    uint8_t maxPeriodBytes;

//     static uint8_t UARTcurrentBaudIdx;
//     static uint32_t UARTrequestedBaud;

//     void sendSyncPacketToTX();
//     void adjustMaxPacketSize();
//     void duplex_set_RX() const;
//     void duplex_set_TX() const;
//     void RcPacketToChannelsData();
//     void alignBufferToSync(uint8_t startIdx);
//     bool ProcessPacket();
//     bool UARTwdt();
//     uint32_t autobaud();
//     void flush_port_input();
} CRSFHandset_t;

extern CRSFHandset_t CRSFHandset;

void CRSFHandsetRcPacketToChannelsData(void);
void CRSFHandset_makeLinkStatisticsPacket(uint8_t *buffer);
void CRSFHandset_UartInBufRst(void);
void CRSFHandset_FlushOutput(void);
#endif
