#include <signal.h>

#undef SIGSTKSZ
#define SIGSTKSZ 32768

#define CATCH_CONFIG_MAIN
#include "catch.hpp"
