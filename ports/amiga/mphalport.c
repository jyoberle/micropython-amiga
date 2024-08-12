#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "py/runtime.h"
#include "py/mpconfig.h"
#include "py/misc.h"

#include <clib/alib_protos.h>
#include <proto/dos.h>
#include <signal.h>
#include <dos.h>
#include <macros.h>
#include <termios.h>

static int sig_mask = 0;
static bool CSI_seq = false;

// Receive single character
// We need to translate CSI (155) into the sequence 27, 91
int mp_hal_stdin_rx_chr(void) {
    if(CSI_seq) {
        CSI_seq = false;
        return(91); // return the second char for cursor keys
    }

    int c = getchar();

    if(c == 155) // CSI
    {
        CSI_seq = true;
        return(27); // return the first char for cursor keys
    }

    return(c);
}

// Send string of given length
void mp_hal_stdout_tx_strn(const char *str, size_t len) {
    write(STDOUT_FILENO,str,len);
}

mp_uint_t mp_hal_ticks_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

mp_uint_t mp_hal_ticks_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

mp_uint_t mp_hal_ticks_cpu(void) {
    struct timeval tv;

	tv.tv_micro = 0L;
	DoTimer(&tv,0,TR_GETSYSTIME);

    return(tv.tv_micro);
}

uint64_t mp_hal_time_ns(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_usec * 1000ULL;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    usleep(ms * 1000UL);
}

void mp_hal_delay_us(mp_uint_t us) {
    usleep(us);
}

#if MICROPY_USE_READLINE == 1
void mp_hal_stdio_mode_raw(void) {
   BPTR fh = Open("CONSOLE:",MODE_OLDFILE);
    if(fh)
    {
        SetMode(fh,DOSTRUE); // RAW mode
        Close(fh);
    }

    // Ctrl-C is handled by MicroPython
    sig_mask = sigblock(SIGF_ABORT);
    __check_abort_enabled = FALSE;
}

void mp_hal_stdio_mode_orig(void) {
    // restore terminal settings
    BPTR fh = Open("CONSOLE:",MODE_OLDFILE);
    if(fh)
    {
        SetMode(fh,DOSFALSE); // Cooked mode
        Close(fh);
    }

    sigsetmask(sig_mask);
    __check_abort_enabled = TRUE;
}

void mp_hal_get_random(size_t n, void *buf)
{
    byte *bufPtr = (byte *)buf;

    for(size_t i=0;i < n;i++)
        bufPtr[i] = (byte)RangeRand(256);
}
#endif
