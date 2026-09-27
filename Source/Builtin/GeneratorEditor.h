#pragma once

#include "SimpleParamEditor.h"
#include "SignalGenerator.h"

// Everything the generator has is a parameter, so this is a plain list of rows via SimpleParamEditor.
class GeneratorEditor : public SimpleParamEditor
{
public:
    explicit GeneratorEditor (SignalGenerator& generatorToEdit);
};
