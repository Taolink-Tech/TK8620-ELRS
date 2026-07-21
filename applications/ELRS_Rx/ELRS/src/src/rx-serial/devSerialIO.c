#include <stdbool.h>
#include <stddef.h>
#include "common.h"
#include "device.h"
#include "SerialIO.h"
#include "CRSF.h"
#include "SerialCRSF.h"
#include "tk86xx_api.h"

#define NO_SERIALIO_INTERVAL 1000

extern SerialIO_t serialIO;

typedef enum {
    troiPass = 0,               // Allow all packets through, normal operation
    troiDisableAwaitConfirm,    // Have received one packet with another model selected, awaiting confirm to Inhibit
    troiInhibit,                // Inhibit all output
    troiEnableAwaitConfirm,     // Have received one packet with this model selected, awaiting confirm to Pass
} teamraceOutputInhibitState_e;

typedef struct devserial_ctx_s {
  SerialIO_t *io;
  bool frameAvailable;          
  bool frameMissed ;
  connectionState_e lastConnectionState;
  uint8_t lastTeamracePosition;
  teamraceOutputInhibitState_e teamraceOutputInhibitState;
} devserial_ctx_t;

static devserial_ctx_t serial0;

static void initialize()
{
    Tk86xxSerialRegisterRxCallback(SerialIO_UartRxCallback);
}

void crsfRCFrameAvailable()
{
    serial0.frameAvailable = true;
}

void crsfRCFrameMissed()
{
    serial0.frameMissed = true;
}

static int start()
{
    serial0.io = &serialIO;
    serial0.lastConnectionState = disconnected;

    return DURATION_IMMEDIATELY;
}

static int event(devserial_ctx_t *ctx)
{
    if (ctx->io != NULL)
    {
        if (ctx->lastConnectionState != connectionState)
        {
            ctx->io->failsafe = (connectionState == disconnected);
        }
        ctx->lastConnectionState = connectionState;
    }

    return DURATION_IGNORE;
}

static int event0()
{
    return event(&serial0);
}

/***
 * @brief: Convert the current TeamraceChannel value to the appropriate config value for comparison
*/
// static uint8_t teamraceChannelToConfigValue()
// {
//     // SWITCH3b is 1,2,3,4,5,6,x,Mid
//     //             0 1 2 3 4 5    7
//     // Config values are Disabled,1,2,3,Mid,4,5,6
//     //                      0     1 2 3  4  5 6 7
//     uint8_t retVal = CRSF_to_SWITCH3b(ChannelData[config.GetTeamraceChannel()]);
//     switch (retVal)
//     {
//         case 0: // passthrough
//         case 1: // passthrough
//         case 2:
//             return retVal + 1;
//         case 3: // passthrough
//         case 4: // passthrough
//         case 5:
//             return retVal + 2;
//         case 7:
//             return 4; // "Mid"
//         default:
//             // CRSF_to_SWITCH3b should only return 0-5,7 but we must return a value
//             return 0;
//     }
// }

/***
 * @brief: Determine if FrameAvailable and it should be sent to FC
 * @return: TRUE if a new frame is available and should be processed
*/
static bool confirmFrameAvailable(devserial_ctx_t *ctx)
{
    if (!ctx->frameAvailable)
        return false;

    // ctx->frameAvailable = false;

    // // ModelMatch failure always prevents passing the frame on
    // if (!connectionHasModelMatch)
    //     return false;

    // const uint8_t CONFIG_TEAMRACE_POS_OFF = 0;
    // if (config.GetTeamracePosition() == CONFIG_TEAMRACE_POS_OFF)
    // {
    //     ctx->teamraceOutputInhibitState = troiPass;
    //     return true;
    // }

    // // Pass the packet on if in troiPass (of course) or
    // // troiDisableAwaitConfirm (keep sending channels until the teamracepos stabilizes)
    // bool retVal = ctx->teamraceOutputInhibitState < troiInhibit;

    // uint8_t newTeamracePosition = teamraceChannelToConfigValue();

    // switch (ctx->teamraceOutputInhibitState)
    // {
    //     case troiPass:
    //         // User appears to be switching away from this model, wait for confirm
    //         if (newTeamracePosition != config.GetTeamracePosition())
    //             ctx->teamraceOutputInhibitState = troiDisableAwaitConfirm;
    //         break;

    //     case troiDisableAwaitConfirm:
    //         // Must receive the same new position twice in a row for state to change
    //         if (ctx->lastTeamracePosition == newTeamracePosition)
    //         {
    //             if (newTeamracePosition != config.GetTeamracePosition())
    //                 ctx->teamraceOutputInhibitState = troiInhibit; // disable output
    //             else
    //                 ctx->teamraceOutputInhibitState = troiPass; // return to normal
    //         }
    //         break;

    //     case troiInhibit:
    //         // User appears to be switching to this model, wait for confirm
    //         if (newTeamracePosition == config.GetTeamracePosition())
    //             ctx->teamraceOutputInhibitState = troiEnableAwaitConfirm;
    //         break;

    //     case troiEnableAwaitConfirm:
    //         // Must receive the same new position twice in a row for state to change
    //         if (ctx->lastTeamracePosition == newTeamracePosition)
    //         {
    //             if (newTeamracePosition == config.GetTeamracePosition())
    //                 ctx->teamraceOutputInhibitState = troiPass; // return to normal
    //             else
    //                 ctx->teamraceOutputInhibitState = troiInhibit; // back to disabled
    //         }
    //         break;
    // }

    // ctx->lastTeamracePosition = newTeamracePosition;
    // // troiPass or troiDisablePending indicate the model is selected still,
    // // however returning true if troiDisablePending means this RX could send
    // // telemetry and we do not want that
    // teamraceHasModelMatch = ctx->teamraceOutputInhibitState == troiPass;
    // return retVal;
    return true;
}

static int timeout(devserial_ctx_t *ctx)
{
    // if (*(ctx->io) == NULL)
    // {
    //     return NO_SERIALIO_INTERVAL;
    // }

    // if (connectionState == serialUpdate)
    // {
    //     return DURATION_NEVER;  // stop callbacks when doing serial update
    // }

    bool missed = ctx->frameMissed;
    ctx->frameMissed = false;

    // Verify there is new ChannelData and they should be sent on
    bool sendChannels = confirmFrameAvailable(ctx);
    serial0.frameAvailable = false;
    return (ctx->io)->sendRCFrame(sendChannels, missed, ChannelData);
}

void handleSerialIO() 
{
    // still get telemetry and send link stats if there's no model match
    if (serial0.io != NULL)
    {
        serial0.io->processSerialInput();
        serial0.io->sendQueuedData(serial0.io->getMaxSerialWriteSize());
    }
}

static int timeout0()
{
  return timeout(&serial0);
}

device_t Serial0_device = {
    .initialize = initialize,
    .start = start,
    .event = event0,
    .timeout = timeout0
};
