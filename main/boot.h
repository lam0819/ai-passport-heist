// Sharing the board with the AI Passport assistant.
//
// The assistant lives in ota_0 and the game in ota_1 of the same partition
// table; the assistant's menu boots the game, and the game boots back.
#pragma once

#include <stdbool.h>

void boot_mark_valid(void);          // keep the bootloader from rolling back
bool boot_assistant_available(void); // a different app is in the other slot
void boot_to_assistant(void);        // does not return when it works
