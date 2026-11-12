#pragma once
#include "mini_etg/model.hpp"

namespace mini_etg::firmware {
bool begin_panel();
const char* state_name(RoundState state);
const char* fault_name(FaultCode fault);
void draw_panel(const Snapshot* saved, TickMs elapsed, const char* notice);
} // namespace mini_etg::firmware
