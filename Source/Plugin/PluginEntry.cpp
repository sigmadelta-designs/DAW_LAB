#include "Builtin/SharedLink.h"

// One of these is compiled into each DAW_LAB plugin, with the stage picked by the target:
//   DAWLAB_STAGE_HEADER  the stage's header
//   DAWLAB_STAGE_CLASS   the stage's class
//   DAWLAB_STAGE_ARGS    extra constructor arguments (optional)
#include DAWLAB_STAGE_HEADER

#ifndef DAWLAB_STAGE_ARGS
 #define DAWLAB_STAGE_ARGS
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DAWLAB_STAGE_CLASS (SharedLink::acquire() DAWLAB_STAGE_ARGS);
}
