// This target is expected to FAIL to build.
//
// It exists to ensure QCurl private headers are not accidentally installed or
// reachable from the public install surface.
#include <QCNetworkReply_p.h>

int main()
{
    return 0;
}
