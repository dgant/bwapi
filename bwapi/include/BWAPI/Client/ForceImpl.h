#pragma once
#include <BWAPI.h>
#include "ForceData.h"
#include <string>

namespace BWAPI
{
  struct GameData;
  class ForceImpl : public ForceInterface
  {
    private:
      const ForceData* self;
      int id;
    public:
      ForceImpl(int id);
      void setData(const GameData* data);
      virtual int getID() const override;
      virtual std::string getName() const override;
      virtual Playerset getPlayers() const override;
  };
}
