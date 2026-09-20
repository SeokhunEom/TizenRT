/* Test-only ioctl protocol, not a Health Monitor product API. */
#ifndef HM_ARMV8M_CONTROL_H
#define HM_ARMV8M_CONTROL_H
#include <tinyara/fs/ioctl.h>
#define HM_TESTIOC_STATUS _TESTIOC(240)
#define HM_TESTIOC_RECOVER _TESTIOC(241)
#endif
