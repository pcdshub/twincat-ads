// The EPICS application already builds adsApp/src/AdsLib.cpp, which implements
// the local-router calls. Compile the sibling upstream ADS discovery source
// under a distinct object name so GetRemoteAddress/AddRemoteRoute are linked
// without colliding with that local wrapper translation unit.
#include "../../ADS/AdsLib/AdsLib.cpp"
