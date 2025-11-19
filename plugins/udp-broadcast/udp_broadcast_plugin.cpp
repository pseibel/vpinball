// license:GPLv3+

#include <thread>
#include <mutex>
#include <vector>
#include <cstring>
#include <string>
#include <chrono>
#include <cmath>

#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#include <pthread.h>
#endif

#include "MsgPlugin.h"
#include "VPXPlugin.h"
#include "ControllerPlugin.h"
#include "LoggingPlugin.h"
#include "udp_broadcast_api.h"
#include "dof_udp_system.h"
#include "dof_event_collector.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <locale>
#endif

namespace UDPBroadcast {

///////////////////////////////////////////////////////////////////////////////
// Plugin State
///////////////////////////////////////////////////////////////////////////////

static const MsgPluginAPI* msgApi = nullptr;
static VPXPluginAPI* vpxApi = nullptr;
static uint32_t endpointId = 0;

// Message IDs
static unsigned int getVpxApiId = 0;
static unsigned int getApiMsgId = 0;
static unsigned int onGameStartId = 0;
static unsigned int onGameEndId = 0;

static unsigned int getDevSrcId = 0;
static unsigned int onDevSrcChangedId = 0;
static unsigned int getInputSrcId = 0;
static unsigned int onInputSrcChangedId = 0;
static unsigned int getSegSrcId = 0;
static unsigned int onSegSrcChangedId = 0;

// Event collectors for each stream
static DOFUDP::EventCollector* pDeviceEventCollector = nullptr;
static DOFUDP::EventCollector* pRGBEventCollector = nullptr;
static DOFUDP::EventCollector* pScoreEventCollector = nullptr;

// Polling state
static std::mutex sourceMutex;
static bool isRunning = false;
static std::thread pollThread;

// Device sources
struct DeviceSource {
   DevSrcId devSrc;
   std::string endpointName;
   unsigned int nDevices;
};
static std::vector<DeviceSource> deviceSources;

// Input sources
struct InputSource {
   InputSrcId inputSrc;
   std::string endpointName;
};
static std::vector<InputSource> inputSources;

// Segment display sources
struct SegmentDisplaySource {
   SegSrcId segSrc;
   std::string endpointName;
   unsigned int nElements;
   std::vector<uint32_t> lastFrameIds;
};
static std::vector<SegmentDisplaySource> segmentDisplaySources;

// Logging
LPI_USE();
#define LOGD LPI_LOGD
#define LOGI LPI_LOGI
#define LOGW LPI_LOGW
#define LOGE LPI_LOGE

LPI_IMPLEMENT

///////////////////////////////////////////////////////////////////////////////
// Thread Name Helper
///////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
void SetThreadName(const std::string& name)
{
   const int size_needed = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
   if (size_needed <= 1)
      return;
   std::wstring wstr(size_needed - 1, L'\0');
   if (MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wstr.data(), size_needed) == 0)
      return;
   SetThreadDescription(GetCurrentThread(), wstr.c_str());
}
#else
void SetThreadName(const std::string& name)
{
#ifdef __APPLE__
   pthread_setname_np(name.c_str());
#elif defined(__linux__) || defined(__ANDROID__)
   pthread_setname_np(pthread_self(), name.c_str());
#endif
}
#endif

///////////////////////////////////////////////////////////////////////////////
// Polling Thread
///////////////////////////////////////////////////////////////////////////////

static void PollThread()
{
   SetThreadName("UDPBroadcast.Poll");
   LOGI("UDPBroadcast: Poll thread started");

   // State tracking for all sources
   std::vector<std::vector<float>> deviceStates;
   std::vector<std::vector<bool>> inputStates;
   bool isInitialState = true;

   while (isRunning)
   {
      {
         std::lock_guard<std::mutex> lock(sourceMutex);

         // Poll device sources
         deviceStates.resize(deviceSources.size());
         for (size_t srcIdx = 0; srcIdx < deviceSources.size(); srcIdx++)
         {
            const auto& src = deviceSources[srcIdx];
            auto& states = deviceStates[srcIdx];

            isInitialState |= states.size() != src.nDevices;
            states.resize(src.nDevices);

            for (unsigned int i = 0; i < src.nDevices; i++)
            {
               float state = src.devSrc.GetFloatState(i);

               // Check for significant state change or initial state
               if (isInitialState || fabsf(states[i] - state) > 0.05f)
               {
                  // Broadcast event based on device groupId
                  uint16_t groupId = src.devSrc.deviceDefs ? src.devSrc.deviceDefs[i].groupId : 0;

                  if (pDeviceEventCollector)
                  {
                     // Map groupId to event types
                     // 0x0100 = GI, 0x0200 = Lamps, 0x0300 = Mechs, etc.
                     switch (groupId)
                     {
                        case 0x0100: // GI
                           pDeviceEventCollector->GI(i + 1, static_cast<uint16_t>(state * 255), groupId, 0);
                           break;
                        case 0x0200: // Lamps
                           pDeviceEventCollector->Lamp(i + 1, static_cast<uint16_t>(state * 255), groupId, 0);
                           break;
                        case 0x0300: // Mechs/Solenoids
                           pDeviceEventCollector->Solenoid(i + 1, static_cast<uint16_t>(state * 255), groupId, 0);
                           break;
                        default:
                           // Generic event for unknown types - treat as lamps
                           pDeviceEventCollector->Lamp(i + 1, static_cast<uint16_t>(state * 255), groupId, 0);
                           break;
                     }
                  }

                  states[i] = state;
               }
            }
         }

         // Poll input sources (wires/switches)
         inputStates.resize(inputSources.size());
         for (size_t srcIdx = 0; srcIdx < inputSources.size(); srcIdx++)
         {
            const auto& src = inputSources[srcIdx];
            auto& states = inputStates[srcIdx];

            isInitialState |= states.size() != src.inputSrc.nInputs;
            states.resize(src.inputSrc.nInputs);

            for (unsigned int i = 0; i < src.inputSrc.nInputs; i++)
            {
               bool state = src.inputSrc.GetInputState(i);
               if (isInitialState || (states[i] != state))
               {
                  if (pDeviceEventCollector) {
                     uint16_t groupId = src.inputSrc.inputDefs ? src.inputSrc.inputDefs[i].groupId : 0;
                     pDeviceEventCollector->Wire(i + 1, state ? 1 : 0, groupId, 0);
                  }
                  states[i] = state;
               }
            }
         }

         // Poll segment display sources
         for (size_t srcIdx = 0; srcIdx < segmentDisplaySources.size(); srcIdx++)
         {
            auto& src = segmentDisplaySources[srcIdx];

            // Get current display state
            SegDisplayFrame frame = src.segSrc.GetState(src.segSrc.id);

            // Only send if frame changed (check frame ID)
            if (frame.frameId != src.lastFrameIds[0])
            {
               src.lastFrameIds[0] = frame.frameId;

               // Convert element types to uint8_t array
               uint8_t elementTypes[CTLPI_SEG_MAX_DISP_ELEMENTS];
               for (unsigned int i = 0; i < src.nElements; i++) {
                  elementTypes[i] = static_cast<uint8_t>(src.segSrc.elementType[i]);
               }

               // Submit to Score stream collector
               if (pScoreEventCollector) {
                  pScoreEventCollector->SegmentDisplay(
                     src.segSrc.id.id,           // displayId
                     src.segSrc.groupId.id,      // groupId
                     frame.frameId,              // frameId
                     src.segSrc.hardware,        // hardware hint
                     src.nElements,              // nElements
                     elementTypes,               // elementTypes
                     frame.frame                 // segment data (floats)
                  );
               }
            }
         }

         isInitialState = false;
      }

      // Fixed update at 60 FPS
      std::this_thread::sleep_for(std::chrono::microseconds(16666));
   }

   LOGI("UDPBroadcast: Poll thread stopped");
}

///////////////////////////////////////////////////////////////////////////////
// Source Management
///////////////////////////////////////////////////////////////////////////////

static void ClearDeviceSources()
{
   for (auto& src : deviceSources) {
      delete[] src.devSrc.deviceDefs;
   }
   deviceSources.clear();
}

static void ClearInputSources()
{
   for (auto& src : inputSources) {
      delete[] src.inputSrc.inputDefs;
   }
   inputSources.clear();
}

static void ClearSegmentSources()
{
   segmentDisplaySources.clear();
}

static void OnDevSrcChanged(const unsigned int eventId, void* userData, void* msgData)
{
   std::lock_guard<std::mutex> lock(sourceMutex);
   ClearDeviceSources();

   GetDevSrcMsg getSrcMsg = { 0, 0, nullptr };
   msgApi->BroadcastMsg(endpointId, getDevSrcId, &getSrcMsg);
   if (getSrcMsg.count == 0)
   {
      LOGI("UDPBroadcast: No device sources");
      return;
   }

   getSrcMsg = { getSrcMsg.count, 0, new DevSrcId[getSrcMsg.count] };
   msgApi->BroadcastMsg(endpointId, getDevSrcId, &getSrcMsg);

   MsgEndpointInfo info;
   for (unsigned int i = 0; i < getSrcMsg.count; i++)
   {
      memset(&info, 0, sizeof(info));
      msgApi->GetEndpointInfo(getSrcMsg.entries[i].id.endpointId, &info);

      DeviceSource src;
      src.endpointName = (info.id != nullptr) ? std::string(info.id) : "Unknown";
      src.devSrc = getSrcMsg.entries[i];
      src.nDevices = getSrcMsg.entries[i].nDevices;

      if (src.devSrc.deviceDefs)
      {
         src.devSrc.deviceDefs = new DeviceDef[src.nDevices];
         memcpy(src.devSrc.deviceDefs, getSrcMsg.entries[i].deviceDefs,
                getSrcMsg.entries[i].nDevices * sizeof(DeviceDef));
      }

      deviceSources.push_back(src);
      LOGI("UDPBroadcast: Found device source: %s (%d devices)",
           src.endpointName.c_str(), src.nDevices);
   }
   delete[] getSrcMsg.entries;

   LOGI("UDPBroadcast: Discovered %d device source(s)", (int)deviceSources.size());
}

static void OnInputSrcChanged(const unsigned int eventId, void* userData, void* msgData)
{
   std::lock_guard<std::mutex> lock(sourceMutex);
   ClearInputSources();

   GetInputSrcMsg getSrcMsg = { 1024, 0, new InputSrcId[1024] };
   msgApi->BroadcastMsg(endpointId, getInputSrcId, &getSrcMsg);

   MsgEndpointInfo info;
   for (unsigned int i = 0; i < getSrcMsg.count; i++)
   {
      memset(&info, 0, sizeof(info));
      msgApi->GetEndpointInfo(getSrcMsg.entries[i].id.endpointId, &info);

      InputSource src;
      src.endpointName = (info.id != nullptr) ? std::string(info.id) : "Unknown";
      src.inputSrc = getSrcMsg.entries[i];

      if (src.inputSrc.inputDefs)
      {
         src.inputSrc.inputDefs = new DeviceDef[src.inputSrc.nInputs];
         memcpy(src.inputSrc.inputDefs, getSrcMsg.entries[i].inputDefs,
                getSrcMsg.entries[i].nInputs * sizeof(DeviceDef));
      }

      inputSources.push_back(src);
      LOGI("UDPBroadcast: Found input source: %s (%d inputs)",
           src.endpointName.c_str(), src.inputSrc.nInputs);
   }
   delete[] getSrcMsg.entries;

   LOGI("UDPBroadcast: Discovered %d input source(s)", (int)inputSources.size());
}

static void OnSegSrcChanged(const unsigned int eventId, void* userData, void* msgData)
{
   std::lock_guard<std::mutex> lock(sourceMutex);
   ClearSegmentSources();

   // Query all segment display sources (phase 1: get count)
   GetSegSrcMsg getSrcMsg = { 0, 0, nullptr };
   msgApi->BroadcastMsg(endpointId, getSegSrcId, &getSrcMsg);

   if (getSrcMsg.count == 0) {
      LOGI("UDPBroadcast: No segment display sources");
      return;
   }

   // Query all segment display sources (phase 2: get actual data)
   getSrcMsg.maxEntryCount = getSrcMsg.count;
   getSrcMsg.count = 0;
   getSrcMsg.entries = new SegSrcId[getSrcMsg.maxEntryCount];
   msgApi->BroadcastMsg(endpointId, getSegSrcId, &getSrcMsg);

   // Store all segment display sources
   MsgEndpointInfo info;
   for (unsigned int i = 0; i < getSrcMsg.count; i++) {
      memset(&info, 0, sizeof(info));
      msgApi->GetEndpointInfo(getSrcMsg.entries[i].id.endpointId, &info);

      SegmentDisplaySource src;
      src.segSrc = getSrcMsg.entries[i];
      src.endpointName = info.id ? info.id : "Unknown";
      src.nElements = getSrcMsg.entries[i].nElements;
      src.lastFrameIds.resize(1, 0);  // Track one frame ID per display

      segmentDisplaySources.push_back(src);

      LOGI("UDPBroadcast: Found segment display - Endpoint:%s Elements:%d Hardware:0x%08X",
           src.endpointName.c_str(), src.nElements, getSrcMsg.entries[i].hardware);
   }

   delete[] getSrcMsg.entries;

   LOGI("UDPBroadcast: Discovered %d segment display source(s)", (int)segmentDisplaySources.size());
}

// Extract table name from full path
// Example: "/path/to/tables/Attack from Mars.vpx" -> "Attack from Mars"
static std::string ExtractTableName(const std::string& fullPath)
{
   if (fullPath.empty()) return std::string();

   // Find last path separator
   size_t lastSep = fullPath.find_last_of("/\\");
   std::string filename = (lastSep != std::string::npos) ? fullPath.substr(lastSep + 1) : fullPath;

   // Remove file extension
   size_t lastDot = filename.find_last_of('.');
   if (lastDot != std::string::npos) {
      filename = filename.substr(0, lastDot);
   }

   return filename;
}

static void OnGameStart(const unsigned int eventId, void* userData, void* eventData)
{
   const CtlOnGameStartMsg* msg = static_cast<const CtlOnGameStartMsg*>(eventData);

   if (!msg || !msg->gameId) {
      LOGW("UDPBroadcast: OnGameStart received invalid data");
      return;
   }

   LOGI("UDPBroadcast: Game started - ROM: %s", msg->gameId);

   // Start polling thread
   if (!isRunning) {
      isRunning = true;
      pollThread = std::thread(PollThread);
   }

   // Broadcast table info (name + ROM)
   if (pDeviceEventCollector && vpxApi) {
      VPXTableInfo tableInfo;
      vpxApi->GetTableInfo(&tableInfo);
      std::string tableName = ExtractTableName(tableInfo.path);
      pDeviceEventCollector->TableInfo(tableName.c_str(), msg->gameId);
      LOGD("UDPBroadcast: Broadcasted table info - Table:'%s' ROM:'%s'",
           tableName.c_str(), msg->gameId);
   }
}

static void OnGameEnd(const unsigned int eventId, void* userData, void* eventData)
{
   LOGI("UDPBroadcast: Game ended");

   // Stop polling thread
   if (isRunning) {
      isRunning = false;
      if (pollThread.joinable())
         pollThread.join();
   }

   // Broadcast table unload (empty TableInfo signals end of session)
   if (pDeviceEventCollector) {
      pDeviceEventCollector->TableInfo("", "");
      LOGD("UDPBroadcast: Broadcasted table unload (empty TableInfo)");
   }
}

///////////////////////////////////////////////////////////////////////////////
// API Implementation Functions
///////////////////////////////////////////////////////////////////////////////

void API_Solenoid(uint8_t id, uint16_t value)
{
   if (pDeviceEventCollector)
      pDeviceEventCollector->Solenoid(id, value);
}

void API_Lamp(uint8_t id, uint16_t value)
{
   if (pDeviceEventCollector)
      pDeviceEventCollector->Lamp(id, value);
}

void API_GI(uint8_t id, uint16_t value)
{
   if (pDeviceEventCollector)
      pDeviceEventCollector->GI(id, value);
}

void API_RGB(uint8_t id, uint8_t r, uint8_t g, uint8_t b)
{
   if (pRGBEventCollector)
      pRGBEventCollector->RGB(id, r, g, b);
}

void API_Wire(uint8_t id, uint16_t value)
{
   if (pDeviceEventCollector)
      pDeviceEventCollector->Wire(id, value);
}

void API_TableInfo(const char* tableName, const char* romName)
{
   if (pDeviceEventCollector)
      pDeviceEventCollector->TableInfo(tableName, romName);
}

void API_SegmentDisplay(uint8_t displayId, uint8_t displayType, uint8_t digitCount, const uint16_t* segments)
{
   // Note: This is a simplified API. Full implementation would convert to internal format
   // For now, this is a placeholder for other plugins to submit segment displays
   if (pScoreEventCollector) {
      // TODO: Convert simplified format to internal SegmentDisplay format
      // This would require creating element types array and converting uint16_t to float
   }
}

uint64_t API_GetEventsSubmitted()
{
   return pDeviceEventCollector ? pDeviceEventCollector->GetStatistics().eventsSent : 0;
}

uint64_t API_GetEventsDropped()
{
   return pDeviceEventCollector ? pDeviceEventCollector->GetStatistics().eventsDropped : 0;
}

uint64_t API_GetSegmentDisplaysSubmitted()
{
   return pScoreEventCollector ? pScoreEventCollector->GetStatistics().eventsSent : 0;
}

uint64_t API_GetSegmentDisplaysDropped()
{
   return pScoreEventCollector ? pScoreEventCollector->GetStatistics().eventsDropped : 0;
}

uint64_t API_GetTableInfosSubmitted()
{
   // Table info is sent via device stream, count as events
   return pDeviceEventCollector ? pDeviceEventCollector->GetStatistics().eventsSent : 0;
}

uint64_t API_GetTableInfosDropped()
{
   return pDeviceEventCollector ? pDeviceEventCollector->GetStatistics().eventsDropped : 0;
}

// API structure instance
static UDPBroadcastAPI api = {
   API_Solenoid,
   API_Lamp,
   API_GI,
   API_RGB,
   API_Wire,
   API_TableInfo,
   API_SegmentDisplay,
   {
      API_GetEventsSubmitted,
      API_GetEventsDropped,
      API_GetSegmentDisplaysSubmitted,
      API_GetSegmentDisplaysDropped,
      API_GetTableInfosSubmitted,
      API_GetTableInfosDropped
   }
};

///////////////////////////////////////////////////////////////////////////////
// Plugin Event Handlers
///////////////////////////////////////////////////////////////////////////////

void onGetAPI(const unsigned int eventId, void* userData, void* eventData)
{
   // Return API pointer to caller
   UDPBroadcastAPI** apiPtr = static_cast<UDPBroadcastAPI**>(eventData);
   if (apiPtr)
      *apiPtr = &api;
}

///////////////////////////////////////////////////////////////////////////////
// Configuration Loading (using new properties API)
///////////////////////////////////////////////////////////////////////////////

// Device Stream Settings
static MSGPI_BOOL_SETTING(deviceStreamEnabled, "device.enabled", "Device Stream Enabled", "Enable UDP broadcasting of device events", true, true);
static char deviceStreamAddr[64] = "255.255.255.255";
static MsgSettingDef deviceStreamAddress { .propId="device.address", .name="Device Stream Address", .description="Broadcast address for device stream", .isUserEditable=1, .type=MSGPI_SETTING_TYPE_STRING, .stringDef = { "255.255.255.255", deviceStreamAddr, sizeof(deviceStreamAddr) } };
static MSGPI_INT_SETTING(deviceStreamPort, "device.port", "Device Stream Port", "UDP port for device events", true, 1024, 65535, 7778);
static MSGPI_INT_SETTING(deviceStreamMaxPPS, "device.maxPacketsPerSecond", "Device Max Packets/Sec", "Maximum packets per second for device stream", true, 1, 1000, 120);
static MSGPI_INT_SETTING(deviceStreamQueueSize, "device.queueSize", "Device Queue Size", "Event queue size for device stream", true, 64, 16384, 4096);

// RGB Stream Settings
static MSGPI_BOOL_SETTING(rgbStreamEnabled, "rgb.enabled", "RGB Stream Enabled", "Enable UDP broadcasting of RGB events", true, true);
static char rgbStreamAddr[64] = "255.255.255.255";
static MsgSettingDef rgbStreamAddress { .propId="rgb.address", .name="RGB Stream Address", .description="Broadcast address for RGB stream", .isUserEditable=1, .type=MSGPI_SETTING_TYPE_STRING, .stringDef = { "255.255.255.255", rgbStreamAddr, sizeof(rgbStreamAddr) } };
static MSGPI_INT_SETTING(rgbStreamPort, "rgb.port", "RGB Stream Port", "UDP port for RGB events", true, 1024, 65535, 7779);
static MSGPI_INT_SETTING(rgbStreamMaxPPS, "rgb.maxPacketsPerSecond", "RGB Max Packets/Sec", "Maximum packets per second for RGB stream", true, 1, 1000, 120);
static MSGPI_INT_SETTING(rgbStreamQueueSize, "rgb.queueSize", "RGB Queue Size", "Event queue size for RGB stream", true, 64, 16384, 4096);

// Score Stream Settings
static MSGPI_BOOL_SETTING(scoreStreamEnabled, "score.enabled", "Score Stream Enabled", "Enable UDP broadcasting of score/segment events", true, true);
static char scoreStreamAddr[64] = "255.255.255.255";
static MsgSettingDef scoreStreamAddress { .propId="score.address", .name="Score Stream Address", .description="Broadcast address for score stream", .isUserEditable=1, .type=MSGPI_SETTING_TYPE_STRING, .stringDef = { "255.255.255.255", scoreStreamAddr, sizeof(scoreStreamAddr) } };
static MSGPI_INT_SETTING(scoreStreamPort, "score.port", "Score Stream Port", "UDP port for score/segment events", true, 1024, 65535, 7781);
static MSGPI_INT_SETTING(scoreStreamMaxPPS, "score.maxPacketsPerSecond", "Score Max Packets/Sec", "Maximum packets per second for score stream", true, 1, 1000, 60);
static MSGPI_INT_SETTING(scoreStreamQueueSize, "score.queueSize", "Score Queue Size", "Event queue size for score stream", true, 64, 16384, 256);

bool LoadConfiguration()
{
   // Load Device Stream configuration from properties
   DOFUDP::BroadcasterConfig deviceConfig;
   deviceConfig.enabled = deviceStreamEnabled.boolDef.val != 0;
   deviceConfig.address = std::string(deviceStreamAddress.stringDef.val);
   deviceConfig.port = deviceStreamPort.intDef.val;
   deviceConfig.maxPacketsPerSecond = deviceStreamMaxPPS.intDef.val;
   deviceConfig.queueSize = deviceStreamQueueSize.intDef.val;

   // Load RGB Stream configuration from properties
   DOFUDP::BroadcasterConfig rgbConfig;
   rgbConfig.enabled = rgbStreamEnabled.boolDef.val != 0;
   rgbConfig.address = std::string(rgbStreamAddress.stringDef.val);
   rgbConfig.port = rgbStreamPort.intDef.val;
   rgbConfig.maxPacketsPerSecond = rgbStreamMaxPPS.intDef.val;
   rgbConfig.queueSize = rgbStreamQueueSize.intDef.val;

   // Load Score Stream configuration from properties
   DOFUDP::BroadcasterConfig scoreConfig;
   scoreConfig.enabled = scoreStreamEnabled.boolDef.val != 0;
   scoreConfig.address = std::string(scoreStreamAddress.stringDef.val);
   scoreConfig.port = scoreStreamPort.intDef.val;
   scoreConfig.maxPacketsPerSecond = scoreStreamMaxPPS.intDef.val;
   scoreConfig.queueSize = scoreStreamQueueSize.intDef.val;

   // Initialize streams
   if (deviceConfig.enabled) {
      if (DOFUDP::InitializeStream(DOFUDP::StreamType::DEVICE, deviceConfig)) {
         pDeviceEventCollector = DOFUDP::GetEventCollector(DOFUDP::StreamType::DEVICE);
         if (pDeviceEventCollector) {
            LOGI("UDPBroadcast: Device Stream initialized on %s:%d",
                 deviceConfig.address.c_str(), deviceConfig.port);
         }
      } else {
         LOGE("UDPBroadcast: Failed to initialize Device Stream");
      }
   } else {
      LOGI("UDPBroadcast: Device Stream disabled");
   }

   if (rgbConfig.enabled) {
      if (DOFUDP::InitializeStream(DOFUDP::StreamType::RGB, rgbConfig)) {
         pRGBEventCollector = DOFUDP::GetEventCollector(DOFUDP::StreamType::RGB);
         if (pRGBEventCollector) {
            LOGI("UDPBroadcast: RGB Stream initialized on %s:%d",
                 rgbConfig.address.c_str(), rgbConfig.port);
         }
      } else {
         LOGE("UDPBroadcast: Failed to initialize RGB Stream");
      }
   } else {
      LOGI("UDPBroadcast: RGB Stream disabled");
   }

   if (scoreConfig.enabled) {
      if (DOFUDP::InitializeStream(DOFUDP::StreamType::SCORE, scoreConfig)) {
         pScoreEventCollector = DOFUDP::GetEventCollector(DOFUDP::StreamType::SCORE);
         if (pScoreEventCollector) {
            LOGI("UDPBroadcast: Score Stream initialized on %s:%d",
                 scoreConfig.address.c_str(), scoreConfig.port);
         }
      } else {
         LOGE("UDPBroadcast: Failed to initialize Score Stream");
      }
   } else {
      LOGI("UDPBroadcast: Score Stream disabled");
   }

   return (pDeviceEventCollector != nullptr) || (pRGBEventCollector != nullptr) || (pScoreEventCollector != nullptr);
}

} // namespace UDPBroadcast

using namespace UDPBroadcast;

///////////////////////////////////////////////////////////////////////////////
// Plugin Entry Points
///////////////////////////////////////////////////////////////////////////////

MSGPI_EXPORT void MSGPIAPI UDPBroadcastPluginLoad(const uint32_t sessionId, const MsgPluginAPI* msgPluginApi)
{
   msgApi = msgPluginApi;
   endpointId = sessionId;

   // Setup logging
   LPISetup(endpointId, msgApi);

   LOGI("UDPBroadcast: Plugin loading...");

   // Get VPX API for table info access
   getVpxApiId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API);
   msgApi->BroadcastMsg(endpointId, getVpxApiId, &vpxApi);
   msgApi->ReleaseMsgID(getVpxApiId);

   // Register message handler for API requests
   getApiMsgId = msgApi->GetMsgID(UDPBCAST_NAMESPACE, UDPBCAST_MSG_GET_API);
   msgApi->SubscribeMsg(endpointId, getApiMsgId, onGetAPI, nullptr);

   // Subscribe to controller events
   onGameStartId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_EVT_ON_GAME_START);
   onGameEndId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_EVT_ON_GAME_END);
   msgApi->SubscribeMsg(endpointId, onGameStartId, OnGameStart, nullptr);
   msgApi->SubscribeMsg(endpointId, onGameEndId, OnGameEnd, nullptr);

   // Subscribe to source change notifications
   getDevSrcId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DEVICE_GET_SRC_MSG);
   onDevSrcChangedId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DEVICE_ON_SRC_CHG_MSG);
   getInputSrcId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_INPUT_GET_SRC_MSG);
   onInputSrcChangedId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_INPUT_ON_SRC_CHG_MSG);
   getSegSrcId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_SEG_GET_SRC_MSG);
   onSegSrcChangedId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_SEG_ON_SRC_CHG_MSG);

   msgApi->SubscribeMsg(endpointId, onDevSrcChangedId, OnDevSrcChanged, nullptr);
   msgApi->SubscribeMsg(endpointId, onInputSrcChangedId, OnInputSrcChanged, nullptr);
   msgApi->SubscribeMsg(endpointId, onSegSrcChangedId, OnSegSrcChanged, nullptr);

   // Query current sources
   OnDevSrcChanged(onDevSrcChangedId, nullptr, nullptr);
   OnInputSrcChanged(onInputSrcChangedId, nullptr, nullptr);
   OnSegSrcChanged(onSegSrcChangedId, nullptr, nullptr);

   // Register all settings with the new properties API
   msgApi->RegisterSetting(endpointId, &deviceStreamEnabled);
   msgApi->RegisterSetting(endpointId, &deviceStreamAddress);
   msgApi->RegisterSetting(endpointId, &deviceStreamPort);
   msgApi->RegisterSetting(endpointId, &deviceStreamMaxPPS);
   msgApi->RegisterSetting(endpointId, &deviceStreamQueueSize);

   msgApi->RegisterSetting(endpointId, &rgbStreamEnabled);
   msgApi->RegisterSetting(endpointId, &rgbStreamAddress);
   msgApi->RegisterSetting(endpointId, &rgbStreamPort);
   msgApi->RegisterSetting(endpointId, &rgbStreamMaxPPS);
   msgApi->RegisterSetting(endpointId, &rgbStreamQueueSize);

   msgApi->RegisterSetting(endpointId, &scoreStreamEnabled);
   msgApi->RegisterSetting(endpointId, &scoreStreamAddress);
   msgApi->RegisterSetting(endpointId, &scoreStreamPort);
   msgApi->RegisterSetting(endpointId, &scoreStreamMaxPPS);
   msgApi->RegisterSetting(endpointId, &scoreStreamQueueSize);

   // Load configuration and initialize streams
   LoadConfiguration();

   LOGI("UDPBroadcast: Plugin loaded successfully");
}

MSGPI_EXPORT void MSGPIAPI UDPBroadcastPluginUnload()
{
   LOGI("UDPBroadcast: Plugin unloading...");

   // Stop polling thread
   if (isRunning) {
      isRunning = false;
      if (pollThread.joinable())
         pollThread.join();
   }

   // Shutdown all streams
   DOFUDP::ShutdownAllStreams();
   pDeviceEventCollector = nullptr;
   pRGBEventCollector = nullptr;
   pScoreEventCollector = nullptr;

   // Clear all sources
   ClearDeviceSources();
   ClearInputSources();
   ClearSegmentSources();

   // Unsubscribe from all messages
   if (msgApi && endpointId)
   {
      msgApi->UnsubscribeMsg(getApiMsgId, onGetAPI);
      msgApi->UnsubscribeMsg(onGameStartId, OnGameStart);
      msgApi->UnsubscribeMsg(onGameEndId, OnGameEnd);
      msgApi->UnsubscribeMsg(onDevSrcChangedId, OnDevSrcChanged);
      msgApi->UnsubscribeMsg(onInputSrcChangedId, OnInputSrcChanged);
      msgApi->UnsubscribeMsg(onSegSrcChangedId, OnSegSrcChanged);

      msgApi->ReleaseMsgID(getApiMsgId);
      msgApi->ReleaseMsgID(onGameStartId);
      msgApi->ReleaseMsgID(onGameEndId);
      msgApi->ReleaseMsgID(getDevSrcId);
      msgApi->ReleaseMsgID(onDevSrcChangedId);
      msgApi->ReleaseMsgID(getInputSrcId);
      msgApi->ReleaseMsgID(onInputSrcChangedId);
      msgApi->ReleaseMsgID(getSegSrcId);
      msgApi->ReleaseMsgID(onSegSrcChangedId);
   }

   msgApi = nullptr;

   LOGI("UDPBroadcast: Plugin unloaded");
}
