#pragma once

#include "DdrSimpleParamEditor.h"
#include "DdrLossyChannel.h"

class DdrLossyChannelEditor : public DdrSimpleParamEditor
{
public:
    explicit DdrLossyChannelEditor (DdrLossyChannel& channelToEdit);
};
