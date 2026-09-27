#pragma once

#include "SimpleParamEditor.h"
#include "LossyChannel.h"

class LossyEditor : public SimpleParamEditor
{
public:
    explicit LossyEditor (LossyChannel& channelToEdit);
};
