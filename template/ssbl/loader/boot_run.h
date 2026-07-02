#ifndef __BOOT_RUN_H_
#define __BOOT_RUN_H_

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/


/************************** Function Prototypes ******************************/
/* 成功不返回（jump_to_app），失败返回负数 */
int  boot_run(const char* app_path, const char *bit_path);


#ifdef __cplusplus
}
#endif

#endif /* __BOOT_RUN_H_ */
