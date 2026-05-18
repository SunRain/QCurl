// This target is expected to FAIL to build against the default Core stage.
// Blocking Extras headers must be available only after explicitly installing
// the BlockingExtrasDevelopment component.
#include <QCBlockingNetworkClient.h>

int main()
{
    return 0;
}
