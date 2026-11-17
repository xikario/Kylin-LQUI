// Prints the policy as C++ sees it; used by the Python parity test.
#include "../EffectsPolicy.h"
#include <cstdio>
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    const auto m=LquiEffects::motion();
    std::printf("motion=%s gpu=%d pointer=%d\n",m==LquiEffects::Motion::On?"on":m==LquiEffects::Motion::Off?"off":"system",
                int(LquiEffects::gpuAllowed()),int(LquiEffects::pointerLight()));
    return 0;
}
