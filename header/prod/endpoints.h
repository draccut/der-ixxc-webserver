#if __has_include("build/example/endpoints.h")
    #include "build/example/endpoints.h"
    #include "build/generated/path_crawled.h"
#else
    #include "../../../frontends/endpoint_list.h"
    #include "build/generated/path_crawled.h"
#endif
