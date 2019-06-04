#include "ZZC.hpp"
#include <cmath>

struct Divider : Module {
  enum ParamIds {
    IN_RATIO_PARAM,
    OUT_RATIO_PARAM,
    SWING_PARAM,
    NUM_PARAMS
  };
  enum InputIds {
    IN_RATIO_INPUT,
    OUT_RATIO_INPUT,
    SWING_INPUT,
    PHASE_INPUT,
    RESET_INPUT,
    NUM_INPUTS
  };
  enum OutputIds {
    CLOCK_OUTPUT,
    PHASE_OUTPUT,
    NUM_OUTPUTS
  };
  enum LightIds {
    EXT_PHASE_MODE_LED,
    NUM_LIGHTS
  };

  LowFrequencyOscillator oscillator;

  float from = 1.0f;
  float to = 1.0f;
  float ratio = 1.0f;
  float swing = 50.0f;

  float phaseIn = 0.0f;
  float lastPhaseIn = 0.0f;
  float lastPhaseInDelta = 0.0f;
  bool lastPhaseInState = false;

  double halfPhaseOut = 0.0;
  double lastHalfPhaseOut = 0.0;
  float phaseOut = 0.0f;

  PulseGenerator clockPulseGenerator;
  PulseGenerator resetPulseGenerator;
  bool clockPulse = false;
  bool resetPulse = false;
  bool gateMode = false;

  SchmittTrigger clockTrigger;
  SchmittTrigger resetTrigger;

  inline void processRatioInputs() {
    if (inputs[IN_RATIO_INPUT].isConnected()) {
      from = std::roundf(clamp(inputs[IN_RATIO_INPUT].value, 0.0f, 10.0f) / 10.0f * (params[IN_RATIO_PARAM].value - 1) + 1);
    } else {
      from = params[IN_RATIO_PARAM].value;
    }
    if (inputs[OUT_RATIO_INPUT].isConnected()) {
      to = std::roundf(clamp(inputs[OUT_RATIO_INPUT].value, 0.0f, 10.0f) / 10.0f * (params[OUT_RATIO_PARAM].value - 1) + 1);
    } else {
      to = params[OUT_RATIO_PARAM].value;
    }
    ratio = to / from;
  }

  inline void processSwingInput() {
    if (inputs[SWING_INPUT].isConnected()) {
      float swingParam = params[SWING_PARAM].value;
      float swingInput = clamp(inputs[SWING_INPUT].value / 5.0f, -1.0f, 1.0f);
      if (swingInput < 0.0f) {
        swing = swingParam + (swingParam - 1.0f) * swingInput;
      } else if (swingInput > 0.0f) {
        swing = swingParam + (99.0f - swingParam) * swingInput;
      }
    } else {
      swing = params[SWING_PARAM].value;
    }
  }

  Divider() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}
  void process(const ProcessArgs &args) override;

  json_t *dataToJson() override {
    json_t *rootJ = json_object();
    json_object_set_new(rootJ, "gateMode", json_boolean(gateMode));
    return rootJ;
  }

  void dataFromJson(json_t *rootJ) override {
    json_t *gateModeJ = json_object_get(rootJ, "gateMode");
    if (gateModeJ) { gateMode = json_boolean_value(gateModeJ); }
  }
};


void Divider::process(const ProcessArgs &args) {
  processRatioInputs();
  processSwingInput();

  if (resetTrigger.process(inputs[RESET_INPUT].value)) {
    phaseOut = 0.0f;
    halfPhaseOut = 0.0;
    lastHalfPhaseOut = 0.0f;
    clockPulseGenerator.trigger(gateMode ? 1e-4f : 1e-3f);
  } else if (inputs[PHASE_INPUT].isConnected()) {
    if (lastPhaseInState) {
      phaseIn = inputs[PHASE_INPUT].value;
      float phaseInDelta = phaseIn - lastPhaseIn;
      if (fabsf(phaseInDelta) > 0.1f && (sgn(phaseInDelta) != sgn(lastPhaseInDelta))) {
        phaseInDelta = lastPhaseInDelta;
      }
      lastPhaseInDelta = phaseInDelta;
      halfPhaseOut += phaseInDelta * ratio * 0.5f;
    }
  }

  // halfPhaseOut = eucmod(halfPhaseOut, 10.0f);
  while (halfPhaseOut >= 10.0) {
    halfPhaseOut = halfPhaseOut - 10.0;
  }
  while (halfPhaseOut < 0.0) {
    halfPhaseOut = halfPhaseOut + 10.0;
  }

  // Swing resulting phase
  float swingTresh = swing / 10.0f;
  if (halfPhaseOut < swingTresh) {
    phaseOut = halfPhaseOut / swingTresh * 10.0f;
  } else {
    float swingRem = 10.0f - swingTresh;
    float phaseGoes = halfPhaseOut - swingTresh;
    phaseOut = phaseGoes / swingRem * 10.0f;
  }

  // Trigger swinged beat
  if (!gateMode) {
    if ((lastHalfPhaseOut < swingTresh && swingTresh <= halfPhaseOut) ||
        (lastHalfPhaseOut > swingTresh && swingTresh >= halfPhaseOut)) {
      clockPulseGenerator.trigger(gateMode ? 1e-4f : 1e-3f);
    }
  }

  lastHalfPhaseOut = halfPhaseOut;

  lastPhaseIn = inputs[PHASE_INPUT].value;
  lastPhaseInState = inputs[PHASE_INPUT].isConnected();


  outputs[PHASE_OUTPUT].value = phaseOut;

  clockPulse = clockPulseGenerator.process(engineGetSampleTime());
  if (gateMode) {
    outputs[CLOCK_OUTPUT].value = phaseOut < 5.0f && !clockPulse ? 10.0f : 0.0f;
  } else {
    outputs[CLOCK_OUTPUT].value = clockPulse ? 10.0f : 0.0f;
  }

  lights[EXT_PHASE_MODE_LED].value = inputs[PHASE_INPUT].isConnected() ? 0.5f : 0.0f;
}


struct DividerWidget : ModuleWidget {
  DividerWidget(Divider *module);
  void appendContextMenu(Menu *menu) override;
};

DividerWidget::DividerWidget(Divider *module) : ModuleWidget(module) {
  setPanel(SVG::load(assetPlugin(pluginInstance, "res/panels/Divider.svg")));

  RatioDisplayWidget *ratioDisplay = new RatioDisplayWidget();
  ratioDisplay->box.pos = Vec(9.0f, 94.0f);
  ratioDisplay->box.size = Vec(57.0f, 21.0f);
  if (module) {
    ratioDisplay->from = &module->from;
    ratioDisplay->to = &module->to;
  }
  addChild(ratioDisplay);

  addParam(createParam<ZZC_CrossKnobSnappy>(Vec(12.5, 39.5), module, Divider::IN_RATIO_PARAM, 1.0f, 99.0f, 1.0f));
  addParam(createParam<ZZC_CrossKnobSnappy>(Vec(12.5, 123.5), module, Divider::OUT_RATIO_PARAM, 1.0f, 99.0f, 1.0f));

  addInput(createInput<ZZC_PJ_Port>(Vec(8, 191), module, Divider::SWING_INPUT));
  addParam(createParam<ZZC_Knob25>(Vec(42.5, 191.0), module, Divider::SWING_PARAM, 1.0f, 99.0f, 50.0f));

  addInput(createInput<ZZC_PJ_Port>(Vec(8, 233), module, Divider::IN_RATIO_INPUT));
  addInput(createInput<ZZC_PJ_Port>(Vec(42.5, 233), module, Divider::OUT_RATIO_INPUT));

  addInput(createInput<ZZC_PJ_Port>(Vec(8, 275), module, Divider::PHASE_INPUT));
  addChild(createLight<TinyLight<GreenLight>>(Vec(30, 275), module, Divider::EXT_PHASE_MODE_LED));
  addInput(createInput<ZZC_PJ_Port>(Vec(42.5, 275), module, Divider::RESET_INPUT));
  addOutput(createOutput<ZZC_PJ_Port>(Vec(8, 320), module, Divider::CLOCK_OUTPUT));
  addOutput(createOutput<ZZC_PJ_Port>(Vec(42.5, 320), module, Divider::PHASE_OUTPUT));

  addChild(createWidget<ZZC_Screw>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ZZC_Screw>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ZZC_Screw>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ZZC_Screw>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
}


struct DividerGateModeItem : MenuItem {
  Divider *divider;
  void onAction(const event::Action &e) override {
    divider->gateMode ^= true;
  }
  void step() override {
    rightText = CHECKMARK(divider->gateMode);
  }
};

void DividerWidget::appendContextMenu(Menu *menu) {
  menu->addChild(new MenuSeparator());

  Divider *divider = dynamic_cast<Divider*>(module);
  assert(divider);

  DividerGateModeItem *gateModeItem = createMenuItem<DividerGateModeItem>("Gate Mode");
  gateModeItem->divider = divider;
  menu->addChild(gateModeItem);
}


Model *modelDivider = createModel<Divider, DividerWidget>("Divider");
