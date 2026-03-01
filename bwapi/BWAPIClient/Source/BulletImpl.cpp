#include <BWAPI.h>
#include <BWAPI/Client/GameImpl.h>
#include <BWAPI/Client/BulletImpl.h>

namespace BWAPI
{
  BulletImpl::BulletImpl(int _index)
    : self(nullptr)
    , index(_index)
  {
    setData(BWAPI::BWAPIClient.data);
  }
  void BulletImpl::setData(const GameData* data)
  {
    self = &(data->bullets[index]);
  }
}
