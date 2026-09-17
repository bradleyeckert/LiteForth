#ifndef _FATSO_H_
#define _FATSO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

const char* get_error_message(int err_code);

int lfAPIdecimal(void);
int lfAPIhex(void);
int lfAPIdotPage(void);
int lfAPIdotPages(void);
int lfAPIdump(void);
int lfAPIdumpIns(void);
int lfAPIdasm(void);

#ifdef __cplusplus
}
#endif

#endif /* _FATSO_H_ */