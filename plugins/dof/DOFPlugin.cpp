// license:GPLv3+

#include <cassert>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <charconv>
#include <thread>
#include <mutex>
#include <vector>
#include <cmath>
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#include <pthread.h>
#endif

#include "plugins/VPXPlugin.h"
#include "plugins/ControllerPlugin.h"
#include "plugins/LoggingPlugin.h"

#pragma warning(push)
#pragma warning(disable : 4251) // xxx needs dll-interface
#include "DOF/DOF.h"
#pragma warning(pop)

// UDP Broadcasting
#include "udp/dof_udp_system.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <locale>
#endif

using namespace std;

///////////////////////////////////////////////////////////////////////////////
//
// Direct Output Framework plugin
//
// TODO the polling system needs to be extended to also listen for B2S controller

namespace DOFPlugin {

static const MsgPluginAPI* msgApi = nullptr;
static VPXPluginAPI* vpxApi = nullptr;
static uint32_t endpointId;

static unsigned int onControllerGameStartId;
static unsigned int onControllerGameEndId;

static unsigned int getDevSrcId;
static unsigned int onDevSrcChangedId;
static unsigned int getInputSrcId;
static unsigned int onInputSrcChangedId;

static std::mutex sourceMutex;
static bool isRunning = false;

// PinMAME Device Source
static DevSrcId pinmameDevSrc = {};
static InputSrcId pinmameInputSrc = {};
static unsigned int nPmSolenoids = 0;
static int pmGiIndex = -1;
static unsigned int nPmGIs = 0;
static int pmLampIndex = -1;
static unsigned int nPmLamps = 0;

// Additional Device Sources (B2S, custom controllers, etc.)
struct AdditionalDevSrc {
   DevSrcId devSrc;
   std::string endpointName;
   unsigned int nDevices;
};
static std::vector<AdditionalDevSrc> additionalDevSources;

static std::thread pollThread;

static DOF::DOF* pDOF = nullptr;

// UDP Broadcasting
static DOFUDP::EventCollector* pEventCollector = nullptr;

static void OnPollStates(void* userData);

LPI_USE();
#define LOGD LPI_LOGD
#define LOGI LPI_LOGI
#define LOGW LPI_LOGW
#define LOGE LPI_LOGE

LPI_IMPLEMENT

void LIBDOFCALLBACK OnDOFLog(DOF_LogLevel logLevel, const char* format, va_list args)
{
   va_list args_copy;
   va_copy(args_copy, args);
   int size = vsnprintf(nullptr, 0, format, args_copy);
   va_end(args_copy);
   if (size > 0) {
      char* const buffer = static_cast<char*>(malloc(size + 1));
      vsnprintf(buffer, size + 1, format, args);
      switch(logLevel) {
         case DOF_LogLevel_INFO:
            LOGI("%s", buffer);
            break;
         case DOF_LogLevel_DEBUG:
            LOGD("%s", buffer);
            break;
         case DOF_LogLevel_ERROR:
            LOGE("%s", buffer);
            break;
         default:
            break;
      }
      free(buffer);
   }
}

#ifdef _WIN32
static void SetThreadName(const std::string& name)
{
   const int size_needed = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
   if (size_needed <= 1)
      return;
   std::wstring wstr(size_needed - 1, L'\0');
   if (MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wstr.data(), size_needed) == 0)
      return;
   HRESULT hr = SetThreadDescription(GetCurrentThread(), wstr.c_str());
}
#else
static void SetThreadName(const std::string& name)
{
#ifdef __APPLE__
   pthread_setname_np(name.c_str());
#elif defined(__linux__) || defined(__ANDROID__)
   pthread_setname_np(pthread_self(), name.c_str());
#endif
}
#endif

static void PollThread(const string& tablePath, const string& gameId)
{
   assert(pDOF != nullptr);
   SetThreadName("DOF.PollThread"s);
   pDOF->Init(tablePath.c_str(), gameId.c_str());
   bool isInitialState = true;
   vector<bool> wireStates, solStates, lampStates, giStates;
   while (isRunning)
   {
      {
         std::lock_guard<std::mutex> lock(sourceMutex);

         isInitialState |= wireStates.size() != pinmameInputSrc.nInputs;
         isInitialState |= solStates.size() != nPmSolenoids;
         isInitialState |= lampStates.size() != nPmLamps;
         isInitialState |= giStates.size() != nPmGIs;

         wireStates.resize(pinmameInputSrc.nInputs);
         for (unsigned int i = 0; i < pinmameInputSrc.nInputs; i++)
         {
            bool state = pinmameInputSrc.GetInputState(i);
            if (isInitialState || (wireStates[i] != state))
            {
               pDOF->DataReceive('W', i + 1, state ? 1 : 0);
               // UDP Broadcast
               if (pEventCollector)
                  pEventCollector->Wire(i + 1, state ? 1 : 0);
               wireStates[i] = state;
            }
         }

         solStates.resize(nPmSolenoids);
         for (unsigned int i = 0; i < nPmSolenoids; i++)
         {
            float state = pinmameDevSrc.GetFloatState(i);
            if (isInitialState || (solStates[i] && state < 0.25f) || (!solStates[i] && state > 0.75f))
            {
               bool binaryState = state > 0.5f;
               pDOF->DataReceive('S', i + 1, binaryState ? 1 : 0);
               // UDP Broadcast
               if (pEventCollector)
                  pEventCollector->Solenoid(i + 1, static_cast<uint16_t>(state * 255));
               solStates[i] = binaryState;
            }
         }

         lampStates.resize(nPmLamps);
         for (unsigned int i = 0; i < nPmLamps; i++)
         {
            float state = pinmameDevSrc.GetFloatState(pmLampIndex + i);
            if (isInitialState || (lampStates[i] && state < 0.25f) || (!lampStates[i] && state > 0.75f))
            {
               bool binaryState = state > 0.5f;
               pDOF->DataReceive('L', i + 1, binaryState ? 1 : 0);
               // UDP Broadcast
               if (pEventCollector)
                  pEventCollector->Lamp(i + 1, static_cast<uint16_t>(state * 255));
               lampStates[i] = binaryState;
            }
         }

         giStates.resize(nPmGIs);
         for (unsigned int i = 0; i < nPmGIs; i++)
         {
            float state = pinmameDevSrc.GetFloatState(pmGiIndex + i);
            if (isInitialState || (giStates[i] && state < 0.25f) || (!giStates[i] && state > 0.75f))
            {
               bool binaryState = state > 0.5f;
               pDOF->DataReceive('G', i + 1, binaryState ? 1 : 0);
               // UDP Broadcast
               if (pEventCollector)
                  pEventCollector->GI(i + 1, static_cast<uint16_t>(state * 255));
               giStates[i] = binaryState;
            }
         }

         // Poll additional device sources (B2S, custom controllers, etc.)
         static std::vector<std::vector<float>> additionalDeviceStates;
         additionalDeviceStates.resize(additionalDevSources.size());

         for (size_t srcIdx = 0; srcIdx < additionalDevSources.size(); srcIdx++)
         {
            const auto& src = additionalDevSources[srcIdx];
            auto& states = additionalDeviceStates[srcIdx];

            isInitialState |= states.size() != src.nDevices;
            states.resize(src.nDevices);

            for (unsigned int i = 0; i < src.nDevices; i++)
            {
               float state = src.devSrc.GetFloatState(i);

               // Check for significant state change or initial state
               if (isInitialState || fabsf(states[i] - state) > 0.05f)
               {
                  // Broadcast event based on device groupId
                  uint16_t groupId = src.devSrc.deviceDefs[i].groupId;

                  if (pEventCollector)
                  {
                     // Map groupId to event types
                     // 0x0100 = GI, 0x0200 = Lamps, 0x0300 = Mechs, etc.
                     switch (groupId)
                     {
                        case 0x0100: // GI
                           pEventCollector->GI(i + 1, static_cast<uint16_t>(state * 255));
                           break;
                        case 0x0200: // Lamps
                           pEventCollector->Lamp(i + 1, static_cast<uint16_t>(state * 255));
                           break;
                        case 0x0300: // Mechs/Solenoids
                           pEventCollector->Solenoid(i + 1, static_cast<uint16_t>(state * 255));
                           break;
                        default:
                           // Generic event for unknown types
                           pEventCollector->Lamp(i + 1, static_cast<uint16_t>(state * 255));
                           break;
                     }
                  }

                  states[i] = state;
               }
            }
         }

         isInitialState = false;
      }

      // Fixed update at 60 FPS
      std::this_thread::sleep_for(std::chrono::microseconds(16666));
   }
   pDOF->Finish();
}


static void OnControllerGameStart(const unsigned int eventId, void* userData, void* msgData)
{
   const CtlOnGameStartMsg* msg = static_cast<const CtlOnGameStartMsg*>(msgData);
   assert(msg != nullptr && msg->gameId != nullptr);

   if (pollThread.joinable())
   {
      LOGE("DOFPlugin: Invalid state, game start happened while already running");
      isRunning = false;
      pollThread.join();
   }

   if (pDOF) {
      LOGI("DOFPlugin: OnControllerGameStart: gameId=%s", msg->gameId);
      isRunning = true;
      VPXTableInfo tableInfo;
      vpxApi->GetTableInfo(&tableInfo);
      pollThread = std::thread(PollThread, tableInfo.path, msg->gameId);

      // UDP Broadcast: Table loaded event
      if (pEventCollector) {
         pEventCollector->TableLoaded(tableInfo.path, msg->gameId);
      }
   }
}

static void OnControllerGameEnd(const unsigned int eventId, void* userData, void* msgData)
{
   if (pDOF) {
      LOGI("DOFPlugin: OnControllerGameEnd");

      // UDP Broadcast: Table unloaded event
      if (pEventCollector) {
         pEventCollector->TableUnloaded();
      }

      isRunning = false;
      if (pollThread.joinable())
         pollThread.join();
   }
}

static void ClearDevices()
{
   // Clear PinMAME devices
   delete[] pinmameDevSrc.deviceDefs;
   nPmSolenoids = 0;
   pmGiIndex = -1;
   nPmGIs = 0;
   pmLampIndex = -1;
   nPmLamps = 0;
   memset(&pinmameDevSrc, 0, sizeof(pinmameDevSrc));

   // Clear additional device sources
   for (auto& src : additionalDevSources) {
      delete[] src.devSrc.deviceDefs;
   }
   additionalDevSources.clear();
}

static void OnDevSrcChanged(const unsigned int eventId, void* userData, void* msgData)
{
   std::lock_guard<std::mutex> lock(sourceMutex);
   ClearDevices();

   GetDevSrcMsg getSrcMsg = { 0, 0, nullptr };
   msgApi->BroadcastMsg(endpointId, getDevSrcId, &getSrcMsg);
   if (getSrcMsg.count == 0)
   {
      LOGI("DOFPlugin: OnDevSrcChanged - No source");
      return;
   }

   getSrcMsg = { getSrcMsg.count, 0, new DevSrcId[getSrcMsg.count] };
   msgApi->BroadcastMsg(endpointId, getDevSrcId, &getSrcMsg);
   MsgEndpointInfo info;
   for (unsigned int i = 0; i < getSrcMsg.count; i++)
   {
      memset(&info, 0, sizeof(info));
      msgApi->GetEndpointInfo(getSrcMsg.entries[i].id.endpointId, &info);

      std::string endpointName = (info.id != nullptr) ? std::string(info.id) : "";

      if (endpointName == "PinMAME")
      {
         // Handle PinMAME (primary source)
         pinmameDevSrc = getSrcMsg.entries[i];
         if (pinmameDevSrc.deviceDefs)
         {
            pinmameDevSrc.deviceDefs = new DeviceDef[pinmameDevSrc.nDevices];
            memcpy(pinmameDevSrc.deviceDefs, getSrcMsg.entries[i].deviceDefs, getSrcMsg.entries[i].nDevices * sizeof(DeviceDef));
         }
      }
      else if (!endpointName.empty())
      {
         // Handle additional device sources (B2S, custom controllers, etc.)
         AdditionalDevSrc addSrc;
         addSrc.endpointName = endpointName;
         addSrc.devSrc = getSrcMsg.entries[i];
         addSrc.nDevices = getSrcMsg.entries[i].nDevices;

         if (addSrc.devSrc.deviceDefs)
         {
            addSrc.devSrc.deviceDefs = new DeviceDef[addSrc.nDevices];
            memcpy(addSrc.devSrc.deviceDefs, getSrcMsg.entries[i].deviceDefs,
                   getSrcMsg.entries[i].nDevices * sizeof(DeviceDef));
         }

         additionalDevSources.push_back(addSrc);
         LOGI("DOFPlugin: Found additional device source: %s (%d devices)",
              endpointName.c_str(), addSrc.nDevices);
      }
   }
   delete[] getSrcMsg.entries;

   for (unsigned int i = 0; i < pinmameDevSrc.nDevices; i++)
   {
      if (pinmameDevSrc.deviceDefs[i].groupId == 0x0100)
      {
         if (pmGiIndex == -1)
            pmGiIndex = i;
         nPmGIs++;
      }
      else if (pinmameDevSrc.deviceDefs[i].groupId == 0x0200)
      {
         if (pmLampIndex == -1)
            pmLampIndex = i;
         nPmLamps++;
      }
      else if ((pmGiIndex == -1) && (pmLampIndex == -1))
         nPmSolenoids++;
   }

   LOGI("DOFPlugin: OnDevSrcChanged - Found %d PinMAME devices (Sol:%d, Lamps:%d, GI:%d)", 
        pinmameDevSrc.nDevices, nPmSolenoids, nPmLamps, nPmGIs);
}

static void OnInputSrcChanged(const unsigned int eventId, void* userData, void* msgData)
{
   std::lock_guard<std::mutex> lock(sourceMutex);
   delete[] pinmameInputSrc.inputDefs;
   memset(&pinmameInputSrc, 0, sizeof(pinmameInputSrc));

   GetInputSrcMsg getSrcMsg = { 1024, 0, new InputSrcId[1024] };
   msgApi->BroadcastMsg(endpointId, getInputSrcId, &getSrcMsg);

   MsgEndpointInfo info;
   for (unsigned int i = 0; i < getSrcMsg.count; i++)
   {
      memset(&info, 0, sizeof(info));
      msgApi->GetEndpointInfo(getSrcMsg.entries[i].id.endpointId, &info);
      if (info.id != nullptr && info.id == "PinMAME"s)
      {
         pinmameInputSrc = getSrcMsg.entries[i];
         if (pinmameInputSrc.inputDefs)
         {
            pinmameInputSrc.inputDefs = new DeviceDef[pinmameInputSrc.nInputs];
            memcpy(pinmameInputSrc.inputDefs, getSrcMsg.entries[i].inputDefs, getSrcMsg.entries[i].nInputs * sizeof(DeviceDef));
         }
         break;
      }
   }
   delete[] getSrcMsg.entries;

   LOGI("DOFPlugin: OnInputSrcChanged - Found %d PinMAME inputs", pinmameInputSrc.nInputs);
}

}

using namespace DOFPlugin;

MSGPI_EXPORT void MSGPIAPI DOFPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
   msgApi = api;
   endpointId = sessionId;

   LPISetup(endpointId, msgApi);

   memset(&pinmameDevSrc, 0, sizeof(pinmameDevSrc));
   ClearDevices();

   unsigned int getVpxApiId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API);
   msgApi->BroadcastMsg(endpointId, getVpxApiId, &vpxApi);
   msgApi->ReleaseMsgID(getVpxApiId);

   msgApi->SubscribeMsg(endpointId, onControllerGameStartId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_EVT_ON_GAME_START), OnControllerGameStart, nullptr);
   msgApi->SubscribeMsg(endpointId, onControllerGameEndId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_EVT_ON_GAME_END), OnControllerGameEnd, nullptr);

   getDevSrcId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DEVICE_GET_SRC_MSG);
   onDevSrcChangedId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DEVICE_ON_SRC_CHG_MSG);
   getInputSrcId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_INPUT_GET_SRC_MSG);
   onInputSrcChangedId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_INPUT_ON_SRC_CHG_MSG);

   msgApi->SubscribeMsg(endpointId, onDevSrcChangedId, OnDevSrcChanged, nullptr);
   msgApi->SubscribeMsg(endpointId, onInputSrcChangedId, OnInputSrcChanged, nullptr);

   OnDevSrcChanged(onDevSrcChangedId, nullptr, nullptr);
   OnInputSrcChanged(onInputSrcChangedId, nullptr, nullptr);

   VPXInfo vpxInfo;
   vpxApi->GetVpxInfo(&vpxInfo);

   DOF::Config* pConfig = DOF::Config::GetInstance();
   pConfig->SetLogCallback(OnDOFLog);
   pConfig->SetBasePath(vpxInfo.prefPath);

   pDOF = new DOF::DOF();

   // Initialize UDP Broadcasting
   DOFUDP::BroadcasterConfig udpConfig;
   udpConfig.enabled = GetSettingBool(const_cast<MsgPluginAPI*>(msgApi), "DOF", "UDPBroadcastEnabled", true);
   udpConfig.address = GetSettingString(const_cast<MsgPluginAPI*>(msgApi), "DOF", "UDPBroadcastAddress", "255.255.255.255");
   udpConfig.port = GetSettingInt(const_cast<MsgPluginAPI*>(msgApi), "DOF", "UDPBroadcastPort", 7778);
   udpConfig.maxPacketsPerSecond = GetSettingInt(const_cast<MsgPluginAPI*>(msgApi), "DOF", "UDPMaxPacketsPerSecond", 120);
   udpConfig.queueSize = GetSettingInt(const_cast<MsgPluginAPI*>(msgApi), "DOF", "UDPQueueSize", 4096);

   if (udpConfig.enabled) {
      if (DOFUDP::InitializeDOFUDPBroadcaster(udpConfig)) {
         pEventCollector = DOFUDP::GetDOFEventCollector();
         if (pEventCollector) {
            LOGI("DOFPlugin: UDP Broadcasting initialized on %s:%d", udpConfig.address.c_str(), udpConfig.port);
         }
      } else {
         LOGE("DOFPlugin: Failed to initialize UDP Broadcasting");
      }
   } else {
      LOGI("DOFPlugin: UDP Broadcasting disabled");
   }
}

MSGPI_EXPORT void MSGPIAPI DOFPluginUnload()
{
   isRunning = false;
   if (pollThread.joinable())
      pollThread.join();

   // Shutdown UDP Broadcasting
   if (pEventCollector) {
      LOGI("DOFPlugin: Shutting down UDP Broadcasting");
      DOFUDP::ShutdownDOFUDPBroadcaster();
      pEventCollector = nullptr;
   }

   ClearDevices();
   delete[] pinmameInputSrc.inputDefs;
   memset(&pinmameInputSrc, 0, sizeof(pinmameInputSrc));

   delete pDOF;
   pDOF = nullptr;

   msgApi->UnsubscribeMsg(onControllerGameStartId, OnControllerGameStart);
   msgApi->UnsubscribeMsg(onControllerGameEndId, OnControllerGameEnd);
   msgApi->UnsubscribeMsg(onDevSrcChangedId, OnDevSrcChanged);
   msgApi->UnsubscribeMsg(onInputSrcChangedId, OnInputSrcChanged);

   msgApi->ReleaseMsgID(onControllerGameStartId);
   msgApi->ReleaseMsgID(onControllerGameEndId);
   msgApi->ReleaseMsgID(getDevSrcId);
   msgApi->ReleaseMsgID(onDevSrcChangedId);
   msgApi->ReleaseMsgID(getInputSrcId);
   msgApi->ReleaseMsgID(onInputSrcChangedId);

   msgApi = nullptr;
}
