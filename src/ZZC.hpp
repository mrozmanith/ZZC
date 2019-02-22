#include "rack.hpp"
#include "window.hpp"

#include "shared.hpp"
#include "widgets.hpp"

using namespace rack;

// Forward-declare the Plugin, defined in ZZC.cpp
extern Plugin *plugin;

// Forward-declare each Model, defined in each module source file
extern Model *modelClock;
extern Model *modelDivider;
extern Model *modelFN3;
extern Model *modelSCVCA;
extern Model *modelSH8;
extern Model *modelSRC;
