#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void smbios_init(void);
const char* smbios_get_board_vendor(void);
const char* smbios_get_board_name(void);

#ifdef __cplusplus
}
#endif
