#ifndef __CB_H_
#define __CB_H_

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

/**************************** Type Definitions *******************************/
typedef struct CirCularDefinition{
    uint8_t * 	buff;                   // data buffer
    uint32_t 	size;                  	// capacity
    volatile uint32_t w_ptr;       		// write index
    volatile uint32_t r_ptr;        	// read index
}CirCularBuffer_t;

/************************** Function Prototypes ******************************/
uint8_t 	cb_init(CirCularBuffer_t* buff, void* buffdata, uint32_t size);
uint32_t 	cb_read(CirCularBuffer_t* buff, void* data, uint32_t btr);
uint32_t	cb_write(CirCularBuffer_t* buff, const void* data, uint32_t btw);
uint32_t 	cb_peak(const CirCularBuffer_t* buff, uint32_t skip_count, void *data, uint32_t btp);
void 		cb_reset(CirCularBuffer_t* buff);
uint32_t 	cb_get_full(const CirCularBuffer_t* buff);
uint32_t 	cb_get_free(const CirCularBuffer_t* buff);

#ifdef __cplusplus
}
#endif


#endif /* __CB_H_ */
