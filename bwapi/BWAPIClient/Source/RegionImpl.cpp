#include <BWAPI/Client/Client.h>
#include <BWAPI/Client/RegionImpl.h>

namespace BWAPI
{
  RegionImpl::RegionImpl(int index)
    : id(index)
    , self ( nullptr )
  {
    setData(BWAPI::BWAPIClient.data);
  }
  void RegionImpl::setData(GameData* data)
  {
    self = data ? &data->regions[id] : nullptr;
  }
  void RegionImpl::setNeighbors()
  {
    int accessibleBestDist    = 99999;
    int inaccessibleBestDist  = 99999;
    for ( int i = 0; i < self->neighborCount; ++i )
    {
      BWAPI::Region neighbor = Broodwar->getRegion(self->neighbors[i]);
      this->neighbors.insert(neighbor);

      // Obtain the closest accessible and inaccessible Regions from their Region center
      int dst = this->getCenter().getApproxDistance(neighbor->getCenter());
      if ( this->getRegionGroupID() == neighbor->getRegionGroupID() )
      {
        if ( dst < accessibleBestDist )
        {
          accessibleBestDist = dst;
          this->closestAccessibleRgn = neighbor;
        }
      }
      else if ( dst < inaccessibleBestDist )
      {
        inaccessibleBestDist = dst;
        this->closestInaccessibleRgn = neighbor;
      }
    }
  }
};
