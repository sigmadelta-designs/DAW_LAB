#pragma once

#include "SimpleParamEditor.h"
#include "NoiseInjector.h"

class NoiseEditor : public SimpleParamEditor
{
public:
    explicit NoiseEditor (NoiseInjector& injectorToEdit);
};
