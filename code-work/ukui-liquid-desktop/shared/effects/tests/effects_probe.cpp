// Prints the policy as C++ sees it; used by the Python parity test.
#include "../EffectsPolicy.h"
#include <cstdio>
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    const auto m=LquiEffects::motion();
    static const char *levels[]={"full","balanced","lite","off"};
    std::printf("level=%s motion=%s gpu=%d pointer=%d\n",levels[int(LquiEffects::level())],m==LquiEffects::Motion::On?"on":m==LquiEffects::Motion::Off?"off":"system",
                int(LquiEffects::gpuAllowed()),int(LquiEffects::pointerLight()));
    return 0;
}
