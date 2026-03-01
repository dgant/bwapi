#include <BWAPI/Client/Client.h>
#include <windows.h>
#include <sstream>
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstddef>

namespace BWAPI
{
  Client BWAPIClient;
  Client::Client()
    : pipeObjectHandle(INVALID_HANDLE_VALUE)
    , mapFileHandle(INVALID_HANDLE_VALUE)
    , gameTableFileHandle(INVALID_HANDLE_VALUE)
  {}
  Client::~Client()
  {
    this->disconnect();
  }
  bool Client::isConnected() const
  {
    return this->connected;
  }
  bool Client::connect()
  {
    if ( this->connected )
    {
      std::cout << "Already connected." << std::endl;
      return true;
    }

    int serverProcID    = -1;
    int gameTableIndex  = -1;

    this->gameTable = NULL;
    this->gameTableFileHandle = OpenFileMappingA(FILE_MAP_WRITE | FILE_MAP_READ, FALSE, "Local\\bwapi_shared_memory_game_list" );
    if ( !this->gameTableFileHandle )
    {
      std::cerr << "Game table mapping not found." << std::endl;
      return false;
    }
    this->gameTable = static_cast<GameTable*>( MapViewOfFile(this->gameTableFileHandle, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(GameTable)) );
    if ( !this->gameTable )
    {
      std::cerr << "Unable to map Game table." << std::endl;
      return false;
    }

    //Find row with most recent keep alive that isn't connected
    DWORD latest = 0;
    for(int i = 0; i < GameTable::MAX_GAME_INSTANCES; i++)
    {
      std::cout << i << " | " << gameTable->gameInstances[i].serverProcessID << " | " << gameTable->gameInstances[i].isConnected << " | " << gameTable->gameInstances[i].lastKeepAliveTime << std::endl;
      if (gameTable->gameInstances[i].serverProcessID != 0 && !gameTable->gameInstances[i].isConnected)
      {
        if ( gameTableIndex == -1 || latest == 0 || gameTable->gameInstances[i].lastKeepAliveTime < latest )
        {
          latest = gameTable->gameInstances[i].lastKeepAliveTime;
          gameTableIndex = i;
        }
      }
    }

    if (gameTableIndex != -1)
      serverProcID = gameTable->gameInstances[gameTableIndex].serverProcessID;

    if (serverProcID == -1)
    {
      std::cerr << "No server proc ID" << std::endl;
      return false;
    }
    
    std::stringstream sharedMemoryName;
    sharedMemoryName << "Local\\bwapi_shared_memory_";
    sharedMemoryName << serverProcID;

    std::stringstream communicationPipe;
    communicationPipe << "\\\\.\\pipe\\bwapi_pipe_";
    communicationPipe << serverProcID;

    pipeObjectHandle = CreateFileA(communicationPipe.str().c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if ( pipeObjectHandle == INVALID_HANDLE_VALUE )
    {
      std::cerr << "Unable to open communications pipe: " << communicationPipe.str() << std::endl;
      CloseHandle(gameTableFileHandle);
      return false;
    }

    COMMTIMEOUTS c;
    c.ReadIntervalTimeout         = 100;
    c.ReadTotalTimeoutMultiplier  = 100;
    c.ReadTotalTimeoutConstant    = 2000;
    c.WriteTotalTimeoutMultiplier = 100;
    c.WriteTotalTimeoutConstant   = 2000;
    SetCommTimeouts(pipeObjectHandle,&c);

    std::cout << "Connected" << std::endl;
    mapFileHandle = OpenFileMappingA(FILE_MAP_WRITE | FILE_MAP_READ, FALSE, sharedMemoryName.str().c_str());
    if (mapFileHandle == INVALID_HANDLE_VALUE || mapFileHandle == NULL)
    {
      std::cerr << "Unable to open shared memory mapping: " << sharedMemoryName.str() << std::endl;
      CloseHandle(pipeObjectHandle);
      CloseHandle(gameTableFileHandle);
      return false;
    }
    liveData = static_cast<GameData*>( MapViewOfFile(mapFileHandle, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(GameData)) );
    data = liveData;
    if ( data == nullptr )
    {
      std::cerr << "Unable to map game data." << std::endl;
      return false;
    }

    // Create new instance of Game/Broodwar
    if ( BWAPI::BroodwarPtr )
      delete static_cast<GameImpl*>(BWAPI::BroodwarPtr);
    BWAPI::BroodwarPtr = new GameImpl(data);
    assert( BWAPI::BroodwarPtr != nullptr );
    static_cast<GameImpl*>(BWAPI::BroodwarPtr)->setCommandData(liveData);

    if (BWAPI::CLIENT_VERSION != BWAPI::Broodwar->getClientVersion())
    {
      //error
      std::cerr << "Error: Client and Server are not compatible!" << std::endl;
      std::cerr << "Client version: " << BWAPI::CLIENT_VERSION << std::endl;
      std::cerr << "Server version: " << BWAPI::Broodwar->getClientVersion() << std::endl;
      disconnect();
      Sleep(2000);
      return false;
    }
    //wait for permission from server before we resume execution
    int code = 1;
    while ( code != 2 )
    {
      DWORD receivedByteCount;
      BOOL success = ReadFile(pipeObjectHandle, &code, sizeof(code), &receivedByteCount, NULL);
      if ( !success )
      {
        disconnect();
        std::cerr << "Unable to read pipe object." << std::endl;
        return false;
      }
    }
    
    std::cout << "Connection successful" << std::endl;
    assert( BWAPI::BroodwarPtr != nullptr);

    this->connected = true;
    return true;
  }
  void Client::disconnect()
  {
    if ( !this->connected ) return;
    
    if ( gameTableFileHandle != INVALID_HANDLE_VALUE )
      CloseHandle(gameTableFileHandle);
    gameTableFileHandle = INVALID_HANDLE_VALUE;

    if ( pipeObjectHandle != INVALID_HANDLE_VALUE )
      CloseHandle(pipeObjectHandle);
    pipeObjectHandle = INVALID_HANDLE_VALUE;
    
    if ( mapFileHandle != INVALID_HANDLE_VALUE )
      CloseHandle(mapFileHandle);
    mapFileHandle = INVALID_HANDLE_VALUE;
    liveData = nullptr;
    data = nullptr;
    frameQueue.clear();
    freeFrameQueue.clear();
    activeFrame.reset();
    staticTemplate.reset();

    this->connected = false;
    std::cout << "Disconnected" << std::endl;

    if ( BWAPI::BroodwarPtr )
      delete static_cast<GameImpl*>(BWAPI::BroodwarPtr);
    BWAPI::BroodwarPtr = nullptr;
  }
  bool Client::stepServerFrame()
  {
    if (stepServerFrameHook)
      return stepServerFrameHook();

    DWORD writtenByteCount;
    int code = 1;
    BOOL writeOk = WriteFile(pipeObjectHandle, &code, sizeof(code), &writtenByteCount, NULL);
    if (!writeOk)
      return false;

    while (code != 2)
    {
      DWORD receivedByteCount;
      BOOL success = ReadFile(pipeObjectHandle, &code, sizeof(code), &receivedByteCount, NULL);
      if ( !success )
        return false;
    }
    return true;
  }
  void Client::queueCurrentFrame()
  {
    auto copyStart = std::chrono::steady_clock::now();
    std::unique_ptr<GameData> snapshot;
    if (!freeFrameQueue.empty())
    {
      snapshot = std::move(freeFrameQueue.front());
      freeFrameQueue.pop_front();
    }
    else
    {
      snapshot = std::make_unique<GameData>();
      if (staticTemplate)
        std::memcpy(snapshot.get(), staticTemplate.get(), sizeof(GameData));
    }

    if (!staticTemplate || liveData->frameCount == 0)
    {
      std::memcpy(snapshot.get(), liveData, sizeof(GameData));
      if (!staticTemplate)
        staticTemplate = std::make_unique<GameData>();
      std::memcpy(staticTemplate.get(), snapshot.get(), sizeof(GameData));
    }
    else
    {
      std::memcpy(snapshot.get(), staticTemplate.get(), sizeof(GameData));
      copyDynamicFrame(snapshot.get(), liveData);
    }
    auto copyEnd = std::chrono::steady_clock::now();
    asyncCopyMicrosTotal += static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(copyEnd - copyStart).count());
    asyncCopyCount++;
    frameQueue.push_back(std::move(snapshot));
  }
  void Client::copyDynamicFrame(GameData* dst, const GameData* src) const
  {
    // Copy everything up to map static terrain data.
    std::memcpy(dst, src, offsetof(GameData, getGroundHeight));

    // These tile-state arrays change during the game and must be kept current.
    std::memcpy(dst->isVisible, src->isVisible, sizeof(dst->isVisible));
    std::memcpy(dst->isExplored, src->isExplored, sizeof(dst->isExplored));
    std::memcpy(dst->hasCreep, src->hasCreep, sizeof(dst->hasCreep));
    std::memcpy(dst->isOccupied, src->isOccupied, sizeof(dst->isOccupied));

    // Copy match/runtime/event/command data in a single tail memcpy.
    const size_t tailOffset = offsetof(GameData, isInGame);
    std::memcpy(reinterpret_cast<char*>(dst) + tailOffset,
                reinterpret_cast<const char*>(src) + tailOffset,
                sizeof(GameData) - tailOffset);
  }
  void Client::pumpAsyncFrames()
  {
    if (!asyncEnabled || asyncFrameBufferCapacity <= 0)
      return;

    auto pumpStart = std::chrono::steady_clock::now();
    while (connected && static_cast<int>(frameQueue.size()) < asyncFrameBufferCapacity)
    {
      if (!stepServerFrame())
      {
        disconnect();
        return;
      }
      queueCurrentFrame();

      // Frame zero wait: preserve original startup semantics and avoid running ahead at frame 0.
      if (frameZeroWaitEnabled && liveData->frameCount == 0)
        break;

      if (asyncPumpMaxMillis > 0)
      {
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - pumpStart).count();
        if (elapsedMs >= asyncPumpMaxMillis)
          break;
      }
    }
  }
  void Client::processActiveFrameEvents()
  {
    for(int i = 0; i < data->eventCount; ++i)
    {
      EventType::Enum type(data->events[i].type);

      if ( type == EventType::MatchStart )
        static_cast<GameImpl*>(BWAPI::BroodwarPtr)->onMatchStart();
      if ( type == EventType::MatchFrame || type == EventType::MenuFrame )
        static_cast<GameImpl*>(BWAPI::BroodwarPtr)->onMatchFrame();
    }
    if ( BWAPI::BroodwarPtr != nullptr && static_cast<GameImpl*>(BWAPI::BroodwarPtr)->inGame && !Broodwar->isInGame() )
      static_cast<GameImpl*>(BWAPI::BroodwarPtr)->onMatchEnd();
  }
  void Client::update()
  {
    if (!connected || BWAPI::BroodwarPtr == nullptr)
      return;

    if (activeFrame)
    {
      freeFrameQueue.push_back(std::move(activeFrame));
      activeFrame.reset();
    }

    if (!asyncEnabled)
    {
      if (!stepServerFrame())
      {
        std::cout << "failed, disconnecting" << std::endl;
        disconnect();
        return;
      }
      data = liveData;
      static_cast<GameImpl*>(BWAPI::BroodwarPtr)->setReadData(data);
      static_cast<GameImpl*>(BWAPI::BroodwarPtr)->setCommandData(liveData);
      processActiveFrameEvents();
      return;
    }

    if (frameQueue.empty())
    {
      auto blockStart = std::chrono::steady_clock::now();
      if (!stepServerFrame())
      {
        std::cout << "failed, disconnecting" << std::endl;
        disconnect();
        return;
      }
      auto blockEnd = std::chrono::steady_clock::now();
      asyncIntentionalBlockMicros += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(blockEnd - blockStart).count());
      if (!asyncUnsafe)
        queueCurrentFrame();
    }

    if (asyncUnsafe && frameQueue.empty())
    {
      data = liveData;
      activeFrame.reset();
    }
    else
    {
      activeFrame = std::move(frameQueue.front());
      frameQueue.pop_front();
      data = activeFrame.get();
    }
    static_cast<GameImpl*>(BWAPI::BroodwarPtr)->setReadData(data);
    static_cast<GameImpl*>(BWAPI::BroodwarPtr)->setCommandData(liveData);
    processActiveFrameEvents();

    pumpAsyncFrames();
  }
  void Client::setAsync(bool enabled)
  {
    asyncEnabled = enabled;
    frameQueue.clear();
    freeFrameQueue.clear();
    activeFrame.reset();
    staticTemplate.reset();
    if (enabled && liveData != nullptr)
      liveData->hasLatCom = false;
    if (enabled && BWAPI::BroodwarPtr != nullptr)
      BWAPI::Broodwar->setLatCom(false);
  }
  bool Client::isAsync() const
  {
    return asyncEnabled;
  }
  void Client::setAsyncFrameBufferCapacity(int capacity)
  {
    if (capacity < 1)
      capacity = 1;
    asyncFrameBufferCapacity = capacity;
    while (static_cast<int>(frameQueue.size()) > asyncFrameBufferCapacity)
      frameQueue.pop_back();
  }
  int Client::getAsyncFrameBufferCapacity() const
  {
    return asyncFrameBufferCapacity;
  }
  void Client::setAsyncUnsafe(bool enabled)
  {
    asyncUnsafe = enabled;
  }
  bool Client::isAsyncUnsafe() const
  {
    return asyncUnsafe;
  }
  void Client::setFrameZeroWait(bool enabled)
  {
    frameZeroWaitEnabled = enabled;
  }
  bool Client::isFrameZeroWaitEnabled() const
  {
    return frameZeroWaitEnabled;
  }
  void Client::setAsyncPumpMaxMillis(int millis)
  {
    asyncPumpMaxMillis = millis < 0 ? 0 : millis;
  }
  int Client::getAsyncPumpMaxMillis() const
  {
    return asyncPumpMaxMillis;
  }
  double Client::getAsyncAverageCopyMicros() const
  {
    if (asyncCopyCount == 0)
      return 0.0;
    return static_cast<double>(asyncCopyMicrosTotal) / static_cast<double>(asyncCopyCount);
  }
  std::uint64_t Client::getAsyncIntentionalBlockMicros() const
  {
    return asyncIntentionalBlockMicros;
  }
  int Client::getFramesBehind() const
  {
    if (!liveData || !data)
      return 0;
    const int diff = liveData->frameCount - data->frameCount;
    return diff > 0 ? diff : 0;
  }
  void Client::setStepServerFrameHookForTests(std::function<bool()> hook)
  {
    stepServerFrameHook = std::move(hook);
  }
  void Client::setLiveDataForTests(GameData* simulatedLiveData)
  {
    liveData = simulatedLiveData;
    data = simulatedLiveData;
  }
  void Client::setConnectedForTests(bool isConnected)
  {
    connected = isConnected;
  }
  int Client::getBufferedFrameCountForTests() const
  {
    return static_cast<int>(frameQueue.size());
  }
}
