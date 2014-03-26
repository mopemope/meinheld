/**
 * original source ngx_times.c
 * 
 * http://nginx.net/
 */

#include "time_cache.h"

#define TIME_SLOTS   64

static uintptr_t        slot;
//static uint32_t         time_lock = 1;

volatile uintptr_t      current_msec;
volatile cache_time_t     *_cached_time;
volatile char       *err_log_time;
volatile char       *http_time;
volatile char       *http_log_time;

static cache_time_t        cached_time[TIME_SLOTS];
static char            cached_err_log_time[TIME_SLOTS]
                                    [sizeof("1970/09/28 12:00:00")];
static char            cached_http_time[TIME_SLOTS]
                                    [sizeof("Mon, 28 Sep 1970 06:00:00 GMT")];
static char            cached_http_log_time[TIME_SLOTS]
                                    [sizeof("28/Sep/1970:12:00:00 +0600")];


static char  *week[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static char  *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

void
cache_time_init(void)
{

    _cached_time = &cached_time[0];

    cache_time_update();
}


#define get_timezone(isdst) (- ((int)(isdst ? timezone + 3600 : timezone)) / 60)

void
cache_time_update(void)
{
    time_t sec = 0;
    uintptr_t msec = 0;
    char          *p0, *p1, *p2;
    cache_time_t      *tp;
    struct timeval   tv;
    time_t tt;
    struct tm *gmt, *p;

    gettimeofday(&tv, NULL);

    sec = tv.tv_sec;
    msec = tv.tv_usec / 1000;

    current_msec = (uintptr_t) sec * 1000 + msec;

    tp = &cached_time[slot];

    if (tp->sec == sec) {
        tp->msec = msec;
        return;
    }

    if (slot == TIME_SLOTS - 1) {
        slot = 0;
    } else {
        slot++;
    }

    tp = &cached_time[slot];

    tp->sec = sec;
    tp->msec = msec;

    tt = time(NULL);
    gmt = gmtime(&tt);

    p0 = &cached_http_time[slot][0];

    sprintf(p0, "%s, %02d %s %4d %02d:%02d:%02d GMT",
                       week[gmt->tm_wday], gmt->tm_mday,
                       months[gmt->tm_mon], gmt->tm_year + 1900,
                       gmt->tm_hour, gmt->tm_min, gmt->tm_sec);

    p = localtime(&tt);
    p->tm_mon++;
    p->tm_year += 1900;
    tp->gmtoff = (int)get_timezone(p->tm_isdst);

    p1 = &cached_err_log_time[slot][0];

    sprintf(p1, "%4d/%02d/%02d %02d:%02d:%02d",
                       p->tm_year, p->tm_mon,
                       p->tm_mday, p->tm_hour,
                       p->tm_min, p->tm_sec);

    p2 = &cached_http_log_time[slot][0];

    sprintf(p2, "%02d/%s/%d:%02d:%02d:%02d %c%02d%02d",
                       p->tm_mday, months[p->tm_mon - 1],
                       p->tm_year, p->tm_hour,
                       p->tm_min, p->tm_sec,
                       tp->gmtoff < 0 ? '-' : '+',
                       abs(tp->gmtoff / 60), abs(tp->gmtoff % 60));

    //memory_barrier();

    _cached_time = tp;
    http_time = p0;
    err_log_time = p1;
    http_log_time = p2;

}

