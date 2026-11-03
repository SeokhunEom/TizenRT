#include <assert.h>
#include <string.h>
#include "iotvticalendar.h"

/* String support used by other calendar entry points. */
char *OICStrcpy(char *dest, size_t size, const char *source)
{
    if (size)
    {
        size_t count = strlen(source);
        if (count >= size) count = size - 1;
        memcpy(dest, source, count);
        dest[count] = '\0';
    }
    return dest;
}

int main(void)
{
    IotvtICalRecur_t recur = {0};
    assert(ParseRecur("FREQ=DAILY", &recur) == IOTVTICAL_SUCCESS);
    assert(recur.freq == FREQ_DAILY);
    assert(ParseRecur("FREQ=DAILY;BYDAY=MO,WE,FR", &recur) == IOTVTICAL_SUCCESS);
    assert(recur.byDay == (MONDAY | WEDNESDAY | FRIDAY));
    assert(ParseRecur("FREQ=DAILY;", &recur) == IOTVTICAL_SUCCESS);
    assert(ParseRecur("", &recur) == IOTVTICAL_INVALID_RRULE);
    char long_rule[100];
    memset(long_rule, 'A', sizeof(long_rule));
    long_rule[sizeof(long_rule) - 1] = '\0';
    assert(ParseRecur(long_rule, &recur) == IOTVTICAL_INVALID_RRULE);
    return 0;
}
