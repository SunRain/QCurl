// This target is expected to FAIL to build against the default Core stage.
// Test Support headers must be available only after explicitly installing
// the TestSupportDevelopment component.
#include <QCNetworkMockHandler.h>
#include <QCNetworkTestSupport.h>

int main()
{
    return 0;
}
