// This target is expected to FAIL to build against the default Core stage.
// Other Extras headers must be available only after explicitly installing
// the OtherExtrasDevelopment component.
#include <QCNetworkDiagnostics.h>

int main()
{
    return 0;
}
