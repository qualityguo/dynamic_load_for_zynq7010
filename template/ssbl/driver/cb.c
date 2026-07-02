/***************************** Include Files *********************************/
#include "cb.h"

/************************** Constant Definitions *****************************/
#define CB_INIT(var, val)			atomic_init(&(var), (val))
#define CB_LOAD(var)				atomic_load(&(var))
#define CB_STORE(var, val)			atomic_store(&(var), (val))
#define CB_IS_READY(b)				((b) != NULL && (b)->buff != NULL && (b)->size > 0)
#define CB_MIN(a, b)				((a) < (b)? (a) : (b))
#define CB_MAX(a, b)				((a) > (b)? (a) : (b))

/************************** Function Prototypes ******************************/
uint8_t cb_read_ex(CirCularBuffer_t* buff, void* data, uint32_t btr, uint32_t* bread);
uint8_t cb_write_ex(CirCularBuffer_t* buff, const void* data, uint32_t btw, uint32_t* bwritten);


uint8_t cb_init(CirCularBuffer_t* buff, void* buffdata, uint32_t size)
{
	if(buff == NULL || buffdata == NULL || size == 0)
	{
		return 0;
	}
	buff->buff = buffdata;
	buff->size = size;
	CB_INIT(buff->w_ptr, 0);
	CB_INIT(buff->r_ptr, 0);
	return 1;
}

uint32_t cb_get_free(const CirCularBuffer_t* buff)
{
	uint32_t size = 0, w_ptr = 0, r_ptr = 0;

	if(!CB_IS_READY(buff))
	{
		return 0;
	}

	w_ptr = CB_LOAD(buff->w_ptr);
	r_ptr = CB_LOAD(buff->r_ptr);

	if(w_ptr >= r_ptr)
	{
		size = buff->size - (w_ptr - r_ptr);
	}
	else
	{
		size = r_ptr - w_ptr;
	}

	return size - 1;					// 有效空间size-1
}


uint32_t cb_get_full(const CirCularBuffer_t* buff)
{
	uint32_t size = 0, w_ptr = 0, r_ptr = 0;

	if(!CB_IS_READY(buff))
	{
		return 0;
	}

	w_ptr = CB_LOAD(buff->w_ptr);
	r_ptr = CB_LOAD(buff->r_ptr);

	if(w_ptr >= r_ptr)
	{
		size = w_ptr - r_ptr;
	}
	else
	{
		size = buff->size - (r_ptr - w_ptr);
	}

	return size;
}

uint32_t cb_read(CirCularBuffer_t* buff, void* data, uint32_t btr)
{
	uint32_t read = 0;
	if(cb_read_ex(buff, data, btr, &read))
	{
		return read;
	}
	return 0;
}

uint8_t cb_read_ex(CirCularBuffer_t* buff, void* data, uint32_t btr, uint32_t* bread)
{
	uint32_t tocopy = 0, full = 0, r_ptr = 0;
	uint8_t* d_ptr = data;

	if(!CB_IS_READY(buff) || data == NULL || btr == 0)
	{
		return 0;
	}

	/*
	 * cal maximum number of bytes available to read
	 */
	full = cb_get_full(buff);
	if(full == 0)
	{
		return 0;
	}
	if(full < btr)						/* only warning */
	{
//		log_w("check cb's capacity");
	}

	btr = CB_MIN(full, btr);
	r_ptr = CB_LOAD(buff->r_ptr);

	/* step1: read data from linear buffer */
	tocopy = CB_MIN(buff->size - r_ptr, btr);
	memcpy(d_ptr, &buff->buff[r_ptr], tocopy);
	d_ptr += tocopy;
	r_ptr += tocopy;
	btr -= tocopy;

	/* step2: read data from beginning of buffer */
	if(btr > 0)
	{
		memcpy(d_ptr, &buff->buff[0], btr);
		r_ptr = btr;
	}

	/* step3: check end of buffer */
	if(r_ptr >= buff->size)
	{
		r_ptr = 0;
	}

	CB_STORE(buff->r_ptr, r_ptr);
	if(bread != NULL)
	{
		*bread = tocopy + btr;
	}

	return 1;
}

uint32_t cb_write(CirCularBuffer_t* buff, const void* data, uint32_t btw)
{
	uint32_t written = 0;
	if(cb_write_ex(buff, data, btw, &written))
	{
		return written;
	}
	return 0;
}

uint8_t cb_write_ex(CirCularBuffer_t* buff, const void* data, uint32_t btw, uint32_t* bwritten)
{
	uint32_t tocopy = 0, free = 0, w_ptr = 0;
	const uint8_t* d_ptr = data;

	if(!CB_IS_READY(buff) || data == NULL || btw == 0)
	{
		return 0;
	}

	/*
	 * cal maximum number of bytes available to write
	 */
	free = cb_get_free(buff);
	if(free == 0)
	{
		return 0;
	}
	if(free < btw)						/* only warning */
	{
//		log_w("check cb's capacity");
	}
	btw = CB_MIN(free, btw);
	w_ptr = CB_LOAD(buff->w_ptr);

	/* step1: write data to linear buffer */
	tocopy = CB_MIN(buff->size - w_ptr, btw);
	memcpy(&buff->buff[w_ptr], d_ptr, tocopy);
	d_ptr += tocopy;
	w_ptr += tocopy;
	btw -= tocopy;

	/* step2: write data to beginning of buffer */
	if(btw > 0)
	{
		memcpy(&buff->buff[0], d_ptr, btw);
		w_ptr = btw;
	}

	/* step3: check end of buffer */
	if(w_ptr >= buff->size)
	{
		w_ptr = 0;
	}

	CB_STORE(buff->w_ptr, w_ptr);
	if(bwritten != NULL)
	{
		*bwritten = tocopy + btw;
	}

	return 1;
}

uint32_t cb_peak(const CirCularBuffer_t* buff, uint32_t skip_count, void *data, uint32_t btp)
{
	uint32_t tocopy = 0, full = 0, r_ptr = 0;
	uint8_t* d_ptr = data;

	if(!CB_IS_READY(buff) || data == NULL || btp == 0)
	{
		return 0;
	}

	/*
	 * cal maximum number of bytes available to read
	 */
	full = cb_get_full(buff);
	if(skip_count >= full)
	{
		return 0;
	}
	r_ptr = CB_LOAD(buff->r_ptr);
	r_ptr += skip_count;
	full -= skip_count;
	if(r_ptr > buff->size)
	{
		r_ptr -= buff->size;
	}
	btp = CB_MIN(full, btp);

	/* step1: read data from linear buffer */
	tocopy = CB_MIN(buff->size - r_ptr, btp);
	memcpy(d_ptr, &buff->buff[r_ptr], tocopy);
	d_ptr += tocopy;
	btp -= tocopy;

	/* step2: read data from beginning of buffer */
	if(btp > 0)
	{
		memcpy(d_ptr, &buff->buff[0], btp);
	}

	return tocopy + btp;
}

void cb_reset(CirCularBuffer_t* buff)
{
	if(CB_IS_READY(buff))
	{
		CB_STORE(buff->r_ptr, 0);
		CB_STORE(buff->w_ptr, 0);
	}
}
