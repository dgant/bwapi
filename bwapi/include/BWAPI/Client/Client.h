#pragma once
#include "GameData.h"
#include "GameImpl.h"
#include "ForceImpl.h"
#include "PlayerImpl.h"
#include "UnitImpl.h"
#include "GameTable.h"

#include <windows.h>
#include <deque>
#include <memory>
#include <cstdint>
#include <functional>


namespace BWAPI
{
  class Client
  {
  public:
    Client();
    ~Client();

    bool isConnected() const;
    bool connect();
    void disconnect();
    void update();
    void setAsync(bool enabled);
    bool isAsync() const;
    void setAsyncFrameBufferCapacity(int capacity);
    int getAsyncFrameBufferCapacity() const;
    void setAsyncUnsafe(bool enabled);
    bool isAsyncUnsafe() const;
    void setFrameZeroWait(bool enabled);
    bool isFrameZeroWaitEnabled() const;
    void setAsyncPumpMaxMillis(int millis);
    int getAsyncPumpMaxMillis() const;
    double getAsyncAverageCopyMicros() const;
    std::uint64_t getAsyncIntentionalBlockMicros() const;
    int getFramesBehind() const;
    void setStepServerFrameHookForTests(std::function<bool()> hook);
    void setLiveDataForTests(GameData* simulatedLiveData);
    void setConnectedForTests(bool isConnected);
    int getBufferedFrameCountForTests() const;

    GameData* data = nullptr;
  private:
    bool stepServerFrame();
    void queueCurrentFrame();
    void copyDynamicFrame(GameData* dst, const GameData* src) const;
    void pumpAsyncFrames();
    void processActiveFrameEvents();

    HANDLE      pipeObjectHandle;
    HANDLE      mapFileHandle;
    HANDLE      gameTableFileHandle;
    GameTable*  gameTable = nullptr;
    GameData*   liveData = nullptr;
    std::deque<std::unique_ptr<GameData>> frameQueue;
    std::deque<std::unique_ptr<GameData>> freeFrameQueue;
    std::unique_ptr<GameData> activeFrame;
    std::unique_ptr<GameData> staticTemplate;
    bool asyncEnabled = false;
    int asyncFrameBufferCapacity = 10;
    bool asyncUnsafe = false;
    bool frameZeroWaitEnabled = true;
    int asyncPumpMaxMillis = 8;
    std::uint64_t asyncCopyMicrosTotal = 0;
    std::uint64_t asyncCopyCount = 0;
    std::uint64_t asyncIntentionalBlockMicros = 0;
    std::function<bool()> stepServerFrameHook;

    bool connected = false;
  };
  extern Client BWAPIClient;
}
