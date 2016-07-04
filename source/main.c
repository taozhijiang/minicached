#include <stdio.h>

#include "minicached.h"

int main(int argc, char* argv[])
{
    mnc_init();

    mnc_item_test();

    while (1)
    {
        sleep(3);
    }
    return 0;
}
