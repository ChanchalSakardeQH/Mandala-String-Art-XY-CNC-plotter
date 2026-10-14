#pragma once
#include <Arduino.h>

enum ExecResult : uint8_t { EX_OK, EX_BUSY, EX_ERROR };

// Executes one G-code line. EX_BUSY means "planner full, call again later with the same line".
// fromJob: line comes from a job file at byte [offset]; "; STEP ..." comments then become progress markers.
ExecResult gcodeExecute(const char* line, bool fromJob, uint32_t offset, String& err);

// "$J=" jog body, e.g. "G91 X10 F1000". Does not change modal state.
ExecResult gcodeJog(const char* body, String& err);

void gcodeResetModal();
