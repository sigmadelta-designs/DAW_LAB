#pragma once

#include "DdrSimpleParamEditor.h"
#include "DdrController.h"

class DdrControllerEditor : public DdrSimpleParamEditor
{
public:
    explicit DdrControllerEditor (DdrController& controllerToEdit);
};
