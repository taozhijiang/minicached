#include <stdio.h>
#include "minicached.h"
#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include "gtest/gtest.h"
extern size_t minicached_mem_limit;

static RET_T load_settings(void)
{
    json_object *p_obj = NULL;
    json_object *p_class = NULL;
    json_object *p_store_obj = NULL;

    if( ! (p_obj = json_object_from_file("settings.json")) )
        return RET_NO;

    minicached_mem_limit = 64 * 1024 * 1024; //default 64M
    if(json_object_object_get_ex(p_obj,"minicached",&p_class))
    {
        if (json_object_object_get_ex(p_class,"total_mem",&p_store_obj))
            minicached_mem_limit = json_object_get_int(p_store_obj) * 1024 * 1024;
    }

    // dump settings
    st_d_print("TOTAL MEMORY: %lu", minicached_mem_limit);

    return RET_YES;
}

GTEST_API_ int main(int argc, char **argv) 
{

    /* Init here!*/
    load_settings();
    mnc_init();

    fprintf(stderr, "Running main() from gtest_main.cc\n");

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
