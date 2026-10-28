// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
// Mock interrupts: an ISR is a plain function the system test can call.
#pragma once
#define ISR(vector) void vector()
