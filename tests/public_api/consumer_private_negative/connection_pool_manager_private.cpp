// This translation unit is expected to FAIL to build.
//
// It ensures newly introduced private companion headers stay out of the
// staged public install surface.
#include <QCNetworkConnectionPoolManager_p.h>

int qcurl_public_api_consumer_private_connection_pool_probe()
{
    return 0;
}
