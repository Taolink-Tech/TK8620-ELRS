#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Abstract class that is extended to provide an interface to a handset.
 *
 * There are three implementations of the Handset class
 *
 * - CRSFHandset - implements the CRSF protocol for communicating with the handset
 * - PPMHandset - PPM protocol, can be connected to the DSC/trainer port for simple non-CRSF handsets
 * - AutoDetect - this implementation uses an RMT channel to auto-detect a PPM or CRSF handset and swap the
 *   global `handset` variable to point to instance of the actual implementation. This allows a TX module
 *   to be moved between a CRSF capable handset and PPM only handset e.g. an EdgeTX radio and a surface radio.
 */
typedef struct {
    bool controllerConnected;
    volatile uint32_t RCdataLastRecv;
    int32_t RequestedRCpacketInterval; // default to 200hz as per 'normal'

    /**
     * @brief Start the handset protocol
     */
    void (*Begin)();
    /**
     * @brief Process any pending input data from the handset
     */

    /**
     * @brief register a function to be called when a request to update a parameter is send from the handset
     * @param callback
     */
    void (*registerParameterUpdateCallback)(void (*callback)(uint8_t type, uint8_t index, uint8_t arg));
    /**
     * Register callback functions for state information about the connection or handset
     * @param connectedCallback called when the protocol detects a stable connection to the handset
     * @param disconnectedCallback called when the protocol loses its connection to the handset
     * @param RecvModelUpdateCallback called when the handset sends a message to set the current model number
     */
    void (*registerCallbacks)(void (*connectedCallback)(), void (*disconnectedCallback)(), void (*RecvModelUpdateCallback)(), void (*bindingCommandCallback)());
    bool (*IsArmed)(void);
    bool (*handleInput)();
    void (*sendTelemetryToTX)(uint8_t *data);
    void (*RCdataCallback)();  // called when there is new RC data
    void (*disconnected)();    // called when RC packet stream is lost
    void (*connected)();       // called when RC packet stream is regained
    void (*RecvModelUpdate)(); // called when model id changes, ie command from Radio
    void (*RecvParameterUpdate)(uint8_t type, uint8_t index, uint8_t arg); // called when recv parameter update req, ie from LUA
    void (*OnBindingCommand)(); // Called when a binding command is received

 } Handset_t;
 
 extern Handset_t handset;




