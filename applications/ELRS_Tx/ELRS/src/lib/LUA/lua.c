#include <stdbool.h>
#include "lua.h"
#include "common.h"
#include "CRSF.h"
#include "logging.h"
#include "telemetry.h"
#include "CRSFHandset.h"
#include "helpers.h"

void EnterRxBindingModeSafely(void) __attribute__((weak));
extern Telemetry_t telemetry __attribute__((weak));
//LUA VARIABLES//

static uint8_t luaWarningFlags = 0b00000000; //8 flag, 1 bit for each flag. set the bit to 1 to show specific warning. 3 MSB is for critical flag
static void (*devicePingCallback)() = NULL;

#define LUA_MAX_PARAMS 64
static uint8_t parameterType;
static uint8_t parameterIndex;
static uint8_t parameterArg;
static volatile bool UpdateParamReq = false;

static luaItem_folder_t luaAgentLite = {
    .common = {
        .name = "HooJ",
        .type = CRSF_FOLDER,
        .id = 0,
        .parent = 0
      },
  };

static luaPropertiesCommon_t *paramDefinitions[LUA_MAX_PARAMS] = {(luaPropertiesCommon_t *)&luaAgentLite, NULL}; // array of luaItem_*
static luaCallback paramCallbacks[LUA_MAX_PARAMS] = {NULL};

static uint8_t lastLuaField = 0;
static uint8_t nextStatusChunk = 0;

void luaResetParameters(void)
{
  memset(paramDefinitions, 0, sizeof(paramDefinitions));
  memset(paramCallbacks, 0, sizeof(paramCallbacks));
  paramDefinitions[0] = (luaPropertiesCommon_t *)&luaAgentLite;
  lastLuaField = 0;
  nextStatusChunk = 0;
  luaWarningFlags = 0;
}

static uint8_t luaSelectionOptionMax(const char *strOptions)
{
  // Returns the max index of the semicolon-delimited option string
  // e.g. A;B;C;D = 3
  uint8_t retVal = 0;
  while (true)
  {
    char c = *strOptions++;
    if (c == ';')
      ++retVal;
    else if (c == '\0')
      return retVal;
  }
}

static uint8_t *copy_cstr_u8(uint8_t *dst, const char *src)
{
  const char *s = src ? src : "";
  while (*s) {
    *dst++ = (uint8_t)*s++;
  }
  *dst++ = 0;
  return dst;
}

uint8_t getLabelLength(char *text, char separator){
  char *c = (char*)text;
  //get label length up to null or lua separator ;
  while(*c != separator && *c != '\0'){
    c++;
  }
  return c-text;
}

uint8_t findLuaSelectionLabel(const void *luaStruct, char *outarray, uint8_t value)
{
  const luaItem_selection_t *p1 = (const luaItem_selection_t *)luaStruct;
  char *c = (char *)p1->options;
  uint8_t count = 0;
  while (*c != '\0'){
    //if count is equal to the parameter value, print out the label to the array
    if(count == value){
      uint8_t labelLength = getLabelLength(c,';');
      //write label to destination array
      strlcpy(outarray, c, labelLength+1);
      strlcpy(outarray + labelLength, p1->units, strlen(p1->units)+1);
      return strlen(outarray);
    }
    //increment the count until value is found
    if(*c == ';'){
      count++;
    }
    c++;
  }
  return 0;
}

static uint8_t *luaTextSelectionStructToArray(const void *luaStruct, uint8_t *next)
{
  const luaItem_selection_t *p1 = (const luaItem_selection_t *)luaStruct;
  next = copy_cstr_u8(next, p1->options);
  *next++ = p1->value; // value
  *next++ = 0; // min
  *next++ = luaSelectionOptionMax(p1->options); //max
  *next++ = 0; // default value
  return copy_cstr_u8(next, p1->units);
}

static uint8_t *luaCommandStructToArray(const void *luaStruct, uint8_t *next)
{
  const struct luaItem_command *p1 = (const struct luaItem_command *)luaStruct;
  *next++ = p1->step;
  *next++ = luaCommandTimeout(p1); // timeout in 10ms
  return copy_cstr_u8(next, p1->info);
}

static uint8_t *luaInt8StructToArray(const void *luaStruct, uint8_t *next)
{
  const struct luaItem_int8 *p1 = (const struct luaItem_int8 *)luaStruct;
  memcpy(next, &p1->properties, sizeof(p1->properties));
  next += sizeof(p1->properties);
  *next++ = 0; // default value
  return copy_cstr_u8(next, p1->units);
}

static uint8_t *luaInt16StructToArray(const void *luaStruct, uint8_t *next)
{
  const struct luaItem_int16 *p1 = (const struct luaItem_int16 *)luaStruct;
  memcpy(next, &p1->properties, sizeof(p1->properties));
  next += sizeof(p1->properties);
  *next++ = 0; // default value byte 1
  *next++ = 0; // default value byte 2
  return copy_cstr_u8(next, p1->units);
}

static uint8_t *luaStringStructToArray(const void *luaStruct, uint8_t *next)
{
  const struct luaItem_string *p1 = (const struct luaItem_string *)luaStruct;
  return copy_cstr_u8(next, p1->value);
}

static uint8_t *luaFolderStructToArray(const void *luaStruct, uint8_t *next)
{
  const luaItem_folder_t *p1 = (const luaItem_folder_t *)luaStruct;
  uint8_t *childParameters;
  if(p1->dyn_name != NULL){
    childParameters = copy_cstr_u8(next, p1->dyn_name);
  } else {
    childParameters = copy_cstr_u8(next, p1->common.name);
  }
  for (int i=1;i<=lastLuaField;i++)
  {
    if (paramDefinitions[i]->parent == p1->common.id)
    {
      *childParameters++ = i;
    }
  }
  *childParameters++ = 0xFF;
  return childParameters;
}

/***
 * @brief: Turn a lua param structure into a chunk of CRSF_FRAMETYPE_PARAMETER_SETTINGS_ENTRY frame and queue it
 * @returns: Number of chunks left to send after this one
 */
static uint8_t sendCRSFparam(uint8_t fieldChunk, luaPropertiesCommon_t *luaData)
{
  uint8_t dataType = luaData->type & CRSF_FIELD_TYPE_MASK;

  // 256 max payload + (FieldID + ChunksRemain + Parent + Type)
  // Chunk 1: (FieldID + ChunksRemain + Parent + Type) + fieldChunk0 data
  // Chunk 2-N: (FieldID + ChunksRemain) + fieldChunk1 data
  uint8_t chunkBuffer[256+4];
  memset(chunkBuffer, 0, sizeof(chunkBuffer));
  // Start the field payload at 2 to leave room for (FieldID + ChunksRemain)
  chunkBuffer[2] = luaData->parent;
  chunkBuffer[3] = dataType;

  // Set the hidden flag
  chunkBuffer[3] |= luaData->type & CRSF_FIELD_HIDDEN ? 0x80 : 0;
  if (CRSFHandset.elrsLUAmode) {
    chunkBuffer[3] |= luaData->type & CRSF_FIELD_ELRS_HIDDEN ? 0x80 : 0;
  }

  // Copy the name to the buffer starting at chunkBuffer[4]
  uint8_t *chunkStart = copy_cstr_u8(&chunkBuffer[4], luaData->name);
  uint8_t *dataEnd;

  switch(dataType) {
    case CRSF_TEXT_SELECTION:
      dataEnd = luaTextSelectionStructToArray(luaData, chunkStart);
      break;
    case CRSF_COMMAND:
      dataEnd = luaCommandStructToArray(luaData, chunkStart);
      break;
    case CRSF_INT8: // fallthrough
    case CRSF_UINT8:
      dataEnd = luaInt8StructToArray(luaData, chunkStart);
      break;
    case CRSF_INT16: // fallthrough
    case CRSF_UINT16:
      dataEnd = luaInt16StructToArray(luaData, chunkStart);
      break;
    case CRSF_STRING: // fallthrough
    case CRSF_INFO:
      dataEnd = luaStringStructToArray(luaData, chunkStart);
      break;
    case CRSF_FOLDER:
      // re-fetch the lua data name, because luaFolderStructToArray will decide whether
      // to return the fixed name or dynamic name.
      dataEnd = luaFolderStructToArray(luaData, &chunkBuffer[4]);
      break;
    case CRSF_FLOAT:
    case CRSF_OUT_OF_RANGE:
    default:
      return 0;
  }

  // dataEnd is the next free byte after the payload that begins at chunkBuffer[2]
  uint8_t dataSize = (uint8_t)(dataEnd - (chunkBuffer + 2));
  // Maximum number of chunked bytes that can be sent in one response
  // 6 bytes CRSF header/CRC: Dest, Len, Type, ExtSrc, ExtDst, CRC
  // 2 bytes Lua chunk header: FieldId, ChunksRemain

  uint8_t chunkMax;
  chunkMax = CRSFHandset.GetMaxPacketBytes() - 6 - 2;

  // How many chunks needed to send this field (rounded up)
  uint8_t chunkCnt = (dataSize + chunkMax - 1) / chunkMax;
  // Data left to send is adjustedSize - chunks sent already
  uint8_t chunkSize = MIN((uint8_t)(dataSize - (fieldChunk * chunkMax)), chunkMax);

  // Move chunkStart back 2 bytes to add (FieldId + ChunksRemain) to each packet
  chunkStart = &chunkBuffer[fieldChunk * chunkMax];
  chunkStart[0] = luaData->id;                 // FieldId
  chunkStart[1] = chunkCnt - (fieldChunk + 1); // ChunksRemain
  CRSFHandset.packetQueueExtended(CRSF_FRAMETYPE_PARAMETER_SETTINGS_ENTRY, chunkStart, chunkSize + 2); 

  return chunkCnt - (fieldChunk+1);
}

static void pushResponseChunk(struct luaItem_command *cmd) 
{
  // DBGLN("sending response for [%s] chunk=%u step=%u", cmd->common.name, nextStatusChunk, cmd->step);
  if (sendCRSFparam(nextStatusChunk, (luaPropertiesCommon_t *)cmd) == 0) {
    nextStatusChunk = 0;
  } else {
    nextStatusChunk++;
  }
}

void sendLuaCommandResponse(struct luaItem_command *cmd, luaCmdStep_e step, const char *message) 
{
  cmd->step = step;
  cmd->info = message;
  nextStatusChunk = 0;
  pushResponseChunk(cmd);
}

__attribute__((weak)) uint8_t luaCommandTimeout(const struct luaItem_command *cmd)
{
  (void)cmd;
  return 200;
}

static void luaSupressCriticalErrors()
{
  // clear the critical error bits of the warning flags
  luaWarningFlags &= 0b00011111;
}

void setLuaWarningFlag(lua_Flags_e flag, bool value)
{
  if (value)
  {
    luaWarningFlags |= 1 << (uint8_t)flag;
  }
  else
  {
    luaWarningFlags &= ~(1 << (uint8_t)flag);
  }
}

static void updateElrsFlags()
{
  // DBGLN("connectionState:%d connectionHasModelMatch:%d", connectionState, connectionHasModelMatch);
  setLuaWarningFlag(LUA_FLAG_MODEL_MATCH, connectionState == connected && connectionHasModelMatch == false);
  setLuaWarningFlag(LUA_FLAG_CONNECTED, connectionState == connected);
  setLuaWarningFlag(LUA_FLAG_ISARMED, CRSFHandset.handset->IsArmed());
}

void sendELRSstatus()
{
  const char *messages[] = { //higher order = higher priority
    "",                   //status2 = connected status
    "",                   //status1, reserved for future use
    "Model Mismatch",     //warning3, model mismatch
    "[ ! Armed ! ]",      //warning2, AUX1 high / armed
    "",           //warning1, reserved for future use
    "Not while connected",  //critical warning3, trying to change a protected value while connected
    "Baud rate too low",  //critical warning2, changing packet rate and baud rate too low
    ""   //critical warning1, reserved for future use
  };
  const char * warningInfo = "";

  for (int i=7 ; i>=0 ; i--)
  {
      if (luaWarningFlags & (1<<i))
      {
          warningInfo = messages[i];
          break;
      }
  }
  uint8_t buffer[sizeof(tagLuaElrsParams_t) + strlen(warningInfo) + 1];
  tagLuaElrsParams_t * const params = (tagLuaElrsParams_t *)buffer;

  params->pktsBad = CRSFHandset.BadPktsCountResult;
  params->pktsGood = htobe16(CRSFHandset.GoodPktsCountResult);
  params->flags = luaWarningFlags;
  // DBGLN("luaWarningFlags:%x", luaWarningFlags);
  // to support sending a params.msg, buffer should be extended by the strlen of the message
  // and copied into params->msg (with trailing null)
  strcpy(params->msg, warningInfo);
  CRSFHandset.packetQueueExtended(0x2E, &buffer, sizeof(buffer));
}

void luaRegisterDevicePingCallback(void (*callback)())
{
  devicePingCallback = callback;
}

void luaParamUpdateReq(uint8_t type, uint8_t index, uint8_t arg)
{
    // DBGLN("LUA: param update req type=0x%02X index=0x%02X arg=0x%02X", type, index, arg);
    parameterType = type;
    parameterIndex = index;
    parameterArg = arg;
    UpdateParamReq = true;
}

void registerLUAParameter(void *definition, luaCallback callback, uint8_t parent)
{
  luaPropertiesCommon_t *p = (luaPropertiesCommon_t *)definition;
  if (lastLuaField + 1 >= LUA_MAX_PARAMS)
  {
    return;
  }
  lastLuaField++;
  p->id = lastLuaField;
  p->parent = parent;
  paramDefinitions[lastLuaField] = p;
  paramCallbacks[lastLuaField] = callback;
}

bool luaHandleUpdateParameter(void)
{
  if (UpdateParamReq == false)
  {
    return false;
  }

  switch(parameterType)
  {
    case CRSF_FRAMETYPE_PARAMETER_WRITE:
      if (parameterIndex == 0)
      {
        // special case for elrs linkstat request
        // DBGLN("ELRS status request");
        updateElrsFlags();
        sendELRSstatus();
      } else if (parameterIndex == 0x2E) {
        luaSupressCriticalErrors();
      } else {
        uint8_t id = parameterIndex;
        uint8_t arg = parameterArg;
        luaPropertiesCommon_t *p = paramDefinitions[id];
        // DBGLN("Set Lua [%s]=%u", p->name, arg);
        if (id < LUA_MAX_PARAMS && paramCallbacks[id]) {
          // While the command is executing, the handset will send `WRITE state=lcsQuery`.
          // paramCallbacks will set the value when nextStatusChunk == 0, or send any
          // remaining chunks when nextStatusChunk != 0
          if (arg == lcsQuery && nextStatusChunk != 0) {
            pushResponseChunk((struct luaItem_command *)p);
          } else {
            paramCallbacks[id](p, arg);
          }
        }
      }
      break;

    case CRSF_FRAMETYPE_DEVICE_PING:
        devicePingCallback();
        luaSupressCriticalErrors();
        sendLuaDevicePacket();
        break;

    case CRSF_FRAMETYPE_PARAMETER_READ:
      {
        uint8_t fieldId = parameterIndex;
        uint8_t fieldChunk = parameterArg;
        // DBGLN("Read lua param %u %u", fieldId, fieldChunk);
        if (fieldId < LUA_MAX_PARAMS && paramDefinitions[fieldId])
        {
          struct luaItem_command *field = (struct luaItem_command *)paramDefinitions[fieldId];
          uint8_t dataType = field->common.type & CRSF_FIELD_TYPE_MASK;
          // On first chunk of a command, reset the step/info of the command
          if (dataType == CRSF_COMMAND && fieldChunk == 0)
          {
            field->step = lcsIdle;
            field->info = "";
          }
          // // Queue the parameter chunk.
          sendCRSFparam(fieldChunk, &field->common);
        }
      }
      break;

    // This is a bit of a hack, it just so happens that the parameterIndex and parameterArg parameters
    // are in the same place as the bind command. This should be handled further up the receive chain
    // but the call in Telemetry.ShouldCallEnterBind() only works if serial data is coming in so the
    // whole stack needs a bit of a refactor to not have similar code duplicated all over
    case CRSF_FRAMETYPE_COMMAND:
      break;

    default:
      DBGLN("Unknown LUA %x", parameterType);
  }

  UpdateParamReq = false;
  return true;
}

void sendLuaDevicePacket(void)
{
  uint8_t deviceInformation[DEVICE_INFORMATION_LENGTH];
  CRSF_GetDeviceInformation(deviceInformation, lastLuaField);
  // does append header + crc again so subtract size from length
  CRSFHandset.packetQueueExtended(CRSF_FRAMETYPE_DEVICE_INFO, deviceInformation + sizeof(crsf_ext_header_t), DEVICE_INFORMATION_PAYLOAD_LENGTH);
}
