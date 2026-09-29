#pragma once

#include "Psvr2TriggerPolicy.h"

void KharvoxPsvr2IpcStart();
void KharvoxPsvr2SubmitTrigger(const kharvox::psvr2::TriggerCommand& command);
void KharvoxPsvr2IpcRequestStop();
