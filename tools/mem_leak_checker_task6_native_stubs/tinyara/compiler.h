#ifndef __MLC_TASK6_NATIVE_COMPILER_H
#define __MLC_TASK6_NATIVE_COMPILER_H

#define FAR
#define CODE
#define noreturn_function __attribute__((noreturn))
#define GET_RETURN_ADDRESS() __builtin_return_address(0)

#endif
