#include <BWAPI.h>
#include <BWAPI/Client/Client.h>

#include <BWAPI/Client/GameImpl.h>

#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace BWAPI;

struct TestFailure : std::runtime_error {
  explicit TestFailure(const std::string& what) : std::runtime_error(what) {}
};

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw TestFailure(message);
  }
}

void expectEq(int expected, int actual, const std::string& message) {
  if (expected != actual) {
    throw TestFailure(message + " (expected=" + std::to_string(expected) + ", actual=" + std::to_string(actual) + ")");
  }
}

void clearEventState(GameData& data) {
  data.eventCount = 0;
  data.eventStringCount = 0;
}

void setMatchStartFrame(GameData& data) {
  clearEventState(data);
  data.isInGame = true;
  data.eventCount = 2;
  data.events[0].type = EventType::MatchStart;
  data.events[1].type = EventType::MatchFrame;
}

void setMatchFrame(GameData& data) {
  clearEventState(data);
  data.isInGame = true;
  data.eventCount = 1;
  data.events[0].type = EventType::MatchFrame;
}

void setMatchEnd(GameData& data) {
  clearEventState(data);
  data.isInGame = true;
  data.eventCount = 1;
  data.events[0].type = EventType::MatchEnd;
}

struct ScopedBroodwar {
  explicit ScopedBroodwar(GameData* data) {
    BroodwarPtr = new GameImpl(data);
    static_cast<GameImpl*>(BroodwarPtr)->setCommandData(data);
  }

  ~ScopedBroodwar() {
    delete static_cast<GameImpl*>(BroodwarPtr);
    BroodwarPtr = nullptr;
  }
};

struct TestClientEnv {
  GameData live{};
  Client client;
  ScopedBroodwar broodwar;
  int endFrame = 12;

  TestClientEnv() : broodwar(&live) {
    live.client_version = CLIENT_VERSION;
    live.frameCount = -1;
    live.playerCount = 0;
    live.forceCount = 1;
    live.hasLatCom = true;
    client.setConnectedForTests(true);
    client.setLiveDataForTests(&live);
    client.setStepServerFrameHookForTests([this]() {
      Sleep(2);
      ++live.frameCount;
      if (live.frameCount == 0) {
        setMatchStartFrame(live);
      } else if (live.frameCount < endFrame) {
        setMatchFrame(live);
      } else if (live.frameCount == endFrame) {
        setMatchEnd(live);
      } else {
        clearEventState(live);
        live.isInGame = false;
      }
      return true;
    });
  }
};

void testSyncExceptionPropagates() {
  TestClientEnv env;
  env.client.setAsync(false);
  env.client.setStepServerFrameHookForTests([]() -> bool {
    throw std::runtime_error("simulated step failure");
  });
  bool threw = false;
  try {
    env.client.update();
  } catch (const std::runtime_error&) {
    threw = true;
  }
  expect(threw, "sync mode should propagate client update exceptions");
}

void testAsyncExceptionPropagates() {
  TestClientEnv env;
  env.client.setAsync(true);
  env.client.setStepServerFrameHookForTests([]() -> bool {
    throw std::runtime_error("simulated async step failure");
  });
  bool threw = false;
  try {
    env.client.update();
  } catch (const std::runtime_error&) {
    threw = true;
  }
  expect(threw, "async mode should propagate client update exceptions");
}

void testSyncNoBuffering() {
  TestClientEnv env;
  env.client.setAsync(false);

  env.client.update();
  expectEq(0, env.client.data->frameCount, "sync frame 0 must be active");
  env.client.update();
  expectEq(1, env.client.data->frameCount, "sync frame 1 must be active");
  expectEq(env.live.frameCount, env.client.data->frameCount, "sync mode must not buffer frames");
}

void testAsyncBuffersThenCatchesUp() {
  TestClientEnv env;
  env.client.setAsync(true);
  env.client.setAsyncFrameBufferCapacity(4);
  env.client.setAsyncPumpMaxMillis(50);

  env.client.update();
  expectEq(0, env.client.data->frameCount, "async first visible frame must be 0");
  expectEq(0, env.client.getFramesBehind(), "bot should start with no backlog");

  int previous = env.client.data->frameCount;
  for (int i = 0; i < 6; ++i) {
    env.client.update();
    expect(env.client.data->frameCount >= previous, "bot view frame must be monotonic");
    previous = env.client.data->frameCount;
  }

  expect(env.client.data->frameCount <= env.live.frameCount, "bot frame must never exceed live frame");
}

void testFrameZeroWaitEnabled() {
  TestClientEnv env;
  env.client.setAsync(true);
  env.client.setAsyncFrameBufferCapacity(3);
  env.client.setFrameZeroWait(true);
  env.client.setAsyncPumpMaxMillis(50);

  env.client.update();
  expectEq(0, env.client.data->frameCount, "bot should observe frame 0 first");
  expectEq(0, env.client.getBufferedFrameCountForTests(), "frame-zero wait enabled should not buffer beyond frame 0");
  expectEq(0, env.client.getFramesBehind(), "bot should not fall behind at frame 0 when waiting is enabled");
}

void testFrameZeroWaitDisabled() {
  TestClientEnv env;
  env.client.setAsync(true);
  env.client.setAsyncFrameBufferCapacity(3);
  env.client.setFrameZeroWait(false);
  env.client.setAsyncPumpMaxMillis(50);

  env.client.update();
  expectEq(0, env.client.data->frameCount, "bot should observe frame 0 first");
  expect(env.client.getBufferedFrameCountForTests() >= 1, "frame-zero wait disabled should allow buffering after frame 0");
  expect(env.client.getFramesBehind() >= 1, "bot should be behind live frame when frame-zero wait is disabled");
}

void testMetricsCopyAndIntentionalBlock() {
  TestClientEnv env;
  env.client.setAsync(true);
  env.client.setAsyncFrameBufferCapacity(1);
  env.client.setAsyncPumpMaxMillis(0);

  env.client.update();
  env.client.update();

  expect(env.client.getAsyncAverageCopyMicros() > 0.0, "copy metric should be > 0");
  expect(env.client.getAsyncIntentionalBlockMicros() > 0, "intentional blocking metric should be > 0");
}

void testDynamicCopyRegions() {
  TestClientEnv env;
  env.client.setAsync(true);
  env.client.setAsyncFrameBufferCapacity(2);
  env.client.setAsyncPumpMaxMillis(50);

  env.client.update();
  expectEq(0, env.client.data->frameCount, "frame 0 required for template capture");

  env.live.getGroundHeight[0][0] = 1234;
  env.live.isVisible[2][3] = true;
  env.live.isOccupied[4][5] = true;

  env.client.update();

  expectEq(0, env.client.data->getGroundHeight[0][0], "static terrain region must remain template-backed");
  expect(env.client.data->isVisible[2][3], "dynamic visibility region must update");
  expect(env.client.data->isOccupied[4][5], "dynamic occupancy region must update");
}

void testAsyncDisablesLatencyCompensation() {
  TestClientEnv env;
  expect(Broodwar->isLatComEnabled(), "latency compensation should start enabled");

  env.client.setAsync(true);
  expect(!Broodwar->isLatComEnabled(), "enabling async must disable latency compensation");

  bool threw = false;
  try {
    Broodwar->setLatCom(true);
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "setLatCom(true) must throw while async mode is enabled");
}

void testAsyncUnsafeReadsLiveFrameWhenQueueEmpty() {
  TestClientEnv env;
  env.client.setAsync(true);
  env.client.setAsyncUnsafe(true);
  env.client.setAsyncFrameBufferCapacity(1);
  env.client.setAsyncPumpMaxMillis(0);

  env.client.update();
  expectEq(env.live.frameCount, env.client.data->frameCount, "async unsafe should consume live frame directly when queue is empty");
}

int run(const std::string& name, const std::function<void()>& fn) {
  try {
    fn();
    std::cout << "[PASS] " << name << '\n';
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "[FAIL] " << name << ": " << ex.what() << '\n';
    return 1;
  }
}

}  // namespace

int main() {
  int failures = 0;
  failures += run("sync_ExceptionPropagates", testSyncExceptionPropagates);
  failures += run("async_ExceptionPropagates", testAsyncExceptionPropagates);
  failures += run("sync_NoBuffering", testSyncNoBuffering);
  failures += run("async_BuffersThenCatchesUp", testAsyncBuffersThenCatchesUp);
  failures += run("async_FrameZeroWaitEnabled", testFrameZeroWaitEnabled);
  failures += run("async_FrameZeroWaitDisabled", testFrameZeroWaitDisabled);
  failures += run("async_MetricsCopyAndIntentionalBlock", testMetricsCopyAndIntentionalBlock);
  failures += run("async_DynamicCopyRegions", testDynamicCopyRegions);
  failures += run("async_DisablesLatencyCompensation", testAsyncDisablesLatencyCompensation);
  failures += run("async_UnsafeReadsLiveFrameWhenQueueEmpty", testAsyncUnsafeReadsLiveFrameWhenQueueEmpty);

  if (failures != 0) {
    std::cerr << "Failed tests: " << failures << '\n';
    return 1;
  }

  std::cout << "All asynchrony tests passed" << std::endl;
  return 0;
}
