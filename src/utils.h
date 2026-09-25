#ifndef _FATSO_H_
#define _FATSO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    int code;
    const char* msg;
} ErrorMapping;

const char* get_error_message(int err_code);

int lfAPI_beginTest(void);
int lfAPI_doTest(void);
int lfAPI_endTest(void);
int lfAPI_decimal(void);
int lfAPI_hex(void);
int lfAPI_dotPage(void);
int lfAPI_dotPages(void);
int lfAPI_dump(void);
int lfAPI_dumpIns(void);
int lfAPI_dasm(void);
int lfAPI_dotEss(void);
int lfAPI_dot(void);
int lfAPI_see(void);

#ifdef __cplusplus
}
#endif

#endif /* _FATSO_H_ */