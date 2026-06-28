#ifndef __HANDOFF_H_
#define __HANDOFF_H_

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#include <stdint.h>

/************************** Function Prototypes ******************************/
void jump_to_app(uint32_t app_load_addr) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* __HANDOFF_H_ */
