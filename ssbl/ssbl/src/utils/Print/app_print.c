#include "xcoresightpsdcc.h"
#include "xuartps_hw.h"
#include "app_print.h"
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include "tx_api.h"

#if defined (__aarch64__) || defined (__arch64__)					// 64λ���������Զ����������
	#define CORESIGHTPS_BASEADDRESS		0xFE800000U
	#define UART0PS_BASEADDRESS			0xFF000000U
	#define UART1PS_BASEADDRESS			0xFF010000U
#else
	#define CORESIGHTPS_BASEADDRESS		0xF8800000U
	#define UART0PS_BASEADDRESS			0xE0000000U
	#define UART1PS_BASEADDRESS			0xE0001000U
#endif


static void my_outbyte(char c);
char my_inbyte(void);

static void my_outbyte(char c)
{
	#ifdef USE_CORESIGHTPS_PRINT
		XCoresightPs_DccSendByte(CORESIGHTPS_BASEADDRESS, c);
	#endif
	#ifdef USE_UARTPS0_PRINT
		XUartPs_SendByte(UART0PS_BASEADDRESS, c);
	#endif
	#ifdef USE_UARTPS1_PRINT
		XUartPs_SendByte(UART1PS_BASEADDRESS, c);
	#endif
}

char my_inbyte(void) {
	#ifdef USE_CORESIGHTPS_PRINT
		return XCoresightPs_DccRecvByte(CORESIGHTPS_BASEADDRESS);
	#endif
	#ifdef USE_UARTPS0_PRINT
		return XUartPs_RecvByte(UART0PS_BASEADDRESS);
	#endif
	#ifdef USE_UARTPS1_PRINT
		return XUartPs_RecvByte(UART1PS_BASEADDRESS);
	#endif
}


typedef struct params_s {
	int32_t len;
	int32_t num1;
	int32_t num2;
    char 	pad_character;
    int32_t do_padding;
    int32_t left_flag;
    int32_t unsigned_flag;
} params_t;

static int32_t getnum( char** linep);
static void padding( const int32_t l_flag, const struct params_s *par);
static void outnum( const int32_t n, const int32_t base, struct params_s *par);
static void outs(const char* lp, struct params_s *par);
static void app_vprintf(const char *ctrl1, va_list argp);

void app_printf( const char *ctrl1, ...)
{
	va_list argp;
	va_start(argp, ctrl1);
	app_vprintf(ctrl1, argp);
	va_end(argp);
}

static void padding( const int32_t l_flag, const struct params_s *par)
{
	int32_t i;

    if ((par->do_padding != 0) && (l_flag != 0) && (par->len < par->num1)) {
		i=(par->len);
        for (; i<(par->num1); i++) {
            my_outbyte( par->pad_character);
		}
    }
}

static int32_t getnum(char** linep)
{
	int32_t n = 0;
	int32_t ResultIsDigit = 0;
	char* cptr = *linep;

	while (cptr != NULL) {
		ResultIsDigit = isdigit(((int32_t)*cptr));
		if (ResultIsDigit == 0)
			break;
		n = ((n*10) + (((int32_t)*cptr) - (int32_t)'0'));
		cptr += 1;
	}

	*linep = ((char*)(cptr));
	return(n);
}

static void outnum( const int32_t n, const int32_t base, struct params_s *par)
{
	int32_t negative;
	int32_t i;
    char outbuf[32];
    const char digits[] = "0123456789ABCDEF";
    uint32_t num;
    for(i = 0; i<32; i++) {
	outbuf[i] = '0';
    }

    /* Check if number is negative                   */
    if ((par->unsigned_flag == 0) && (base == 10) && (n < 0L)) {
        negative = 1;
		num =(-(n));
    }
    else{
        num = n;
        negative = 0;
    }

    /* Build number (backwards) in outbuf            */
    i = 0;
    do {
		outbuf[i] = digits[(num % base)];
		i++;
		num /= base;
    } while (num > 0);

    if (negative != 0) {
		outbuf[i] = '-';
		i++;
	}

    outbuf[i] = '\0';
    i--;

    /* Move the converted number to the buffer and   */
    /* add in the padding where needed.              */
    par->len = (int32_t)strlen(outbuf);
    padding( !(par->left_flag), par);
    while (&outbuf[i] >= outbuf) {
    	my_outbyte( outbuf[i] );
		i--;
    }
    padding( par->left_flag, par);
}

#if defined (__aarch64__) || defined (__arch64__)
static void outnum1( const int64_t n, const int32_t base, params_t *par)
{
    int32_t negative;
	int32_t i;
    char outbuf[64];
    const char digits[] = "0123456789ABCDEF";
    uint64_t num;
    for(i = 0; i<64; i++) {
	outbuf[i] = '0';
    }

    /* Check if number is negative                   */
    if ((par->unsigned_flag == 0) && (base == 10) && (n < 0L)) {
        negative = 1;
		num =(-(n));
    }
    else{
        num = (n);
        negative = 0;
    }

    /* Build number (backwards) in outbuf            */
    i = 0;
    do {
		outbuf[i] = digits[(num % base)];
		i++;
		num /= base;
    } while (num > 0);

    if (negative != 0) {
		outbuf[i] = '-';
		i++;
	}

    outbuf[i] = '\0';
    i--;

    /* Move the converted number to the buffer and   */
    /* add in the padding where needed.              */
    par->len = (int32_t)strlen(outbuf);
    padding( !(par->left_flag), par);
    while (&outbuf[i] >= outbuf) {
		my_outbyte( outbuf[i] );
		i--;
}
    padding( par->left_flag, par);
}
#endif

static void outs(const char* lp, struct params_s *par)
{
    char* LocalPtr;
	LocalPtr = lp;
    /* pad on left if needed                         */
	if(LocalPtr != NULL) {
		par->len = (int32_t)strlen( LocalPtr);
		padding( !(par->left_flag), par);
		/* Move string to the buffer                     */
		while (((*LocalPtr) != (char)0) && ((par->num2) != 0)) {
			(par->num2)--;
			my_outbyte(*LocalPtr);
			LocalPtr += 1;
		}
	}

    /* Pad on right if needed                        */
    /* CR 439175 - elided next stmt. Seemed bogus.   */
    /* par->len = strlen( lp)                      */
    padding( par->left_flag, par);
}

static void app_vprintf(const char *ctrl1, va_list argp)
{
	int32_t Check;
#if defined (__aarch64__) || defined (__arch64__)
	int32_t long_flag;
#endif
    int32_t dot_flag;
    params_t par;
    char ch;
    char *ctrl = (char *)ctrl1;

    while ((ctrl != NULL) && (*ctrl != (char)0)) {

        /* move format string chars to buffer until a  */
        /* format control is found.                    */
        if (*ctrl != '%') {
            my_outbyte(*ctrl);
			ctrl += 1;
            continue;
        }

        /* initialize all the flags for this format.   */
        dot_flag = 0;
#if defined (__aarch64__) || defined (__arch64__)
		long_flag = 0;
#endif
        par.unsigned_flag = 0;
		par.left_flag = 0;
		par.do_padding = 0;
        par.pad_character = ' ';
        par.num2=32767;
		par.num1=0;
		par.len=0;

 try_next:
		if(ctrl != NULL) {
			ctrl += 1;
		}
		if(ctrl != NULL) {
			ch = *ctrl;
		} else {
			break;
		}

        if (isdigit((int32_t)ch) != 0) {
            if (dot_flag != 0) {
                par.num2 = getnum(&ctrl);
			}
            else {
                if (ch == '0') {
                    par.pad_character = '0';
				}
				if(ctrl != NULL) {
			par.num1 = getnum(&ctrl);
				}
                par.do_padding = 1;
            }
            if(ctrl != NULL) {
			ctrl -= 1;
			}
            goto try_next;
        }

        switch (tolower((int32_t)ch)) {
            case '%':
                my_outbyte( '%');
                Check = 1;
                break;

            case '-':
                par.left_flag = 1;
                Check = 0;
                break;

            case '.':
                dot_flag = 1;
                Check = 0;
                break;

            case 'l':
			#if defined (__aarch64__) || defined (__arch64__)
                long_flag = 1;
            #endif
                Check = 0;
                break;

            case 'u':
                par.unsigned_flag = 1;
                /* fall through */
            case 'i':
            case 'd':
                #if defined (__aarch64__) || defined (__arch64__)
                if (long_flag != 0){
			        outnum1((int64_t)va_arg(argp, int64_t), 10L, &par);
                }
                else {
                    outnum( va_arg(argp, int32_t), 10L, &par);
                }
                #else
                    outnum( va_arg(argp, int32_t), 10L, &par);
                #endif
				Check = 1;
                break;
            case 'p':
			    #if defined (__aarch64__) || defined (__arch64__)
                par.unsigned_flag = 1;
			    outnum1((int64_t)va_arg(argp, int64_t), 16L, &par);
			    Check = 1;
                break;
                #endif
            case 'X':
            case 'x':
                par.unsigned_flag = 1;
                #if defined (__aarch64__) || defined (__arch64__)
                if (long_flag != 0) {
				    outnum1((int64_t)va_arg(argp, int64_t), 16L, &par);
				}
				else {
				    outnum((int32_t)va_arg(argp, int32_t), 16L, &par);
                }
                #else
                outnum((int32_t)va_arg(argp, int32_t), 16L, &par);
                #endif
                Check = 1;
                break;

            case 's':
                outs( va_arg(argp, char *), &par);
                Check = 1;
                break;

            case 'c':
                my_outbyte( va_arg( argp, int32_t));
                Check = 1;
                break;

            case '\\':
                switch (*ctrl) {
                    case 'a':
                        my_outbyte( ((char)0x07));
                        break;
                    case 'h':
                        my_outbyte( ((char)0x08));
                        break;
                    case 'r':
                        my_outbyte( ((char)0x0D));
                        break;
                    case 'n':
                        my_outbyte( ((char)0x0D));
                        my_outbyte( ((char)0x0A));
                        break;
                    default:
                        my_outbyte( *ctrl);
                        break;
                }
                ctrl += 1;
                Check = 0;
                break;

            default:
		Check = 1;
		break;
        }
        if(Check == 1) {
			if(ctrl != NULL) {
				ctrl += 1;
			}
                continue;
        }
        goto try_next;
    }
}

