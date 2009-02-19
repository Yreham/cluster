#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#define printk printf
#define pv(struct, member, fmt) printf("  "#member" = "fmt"\n", struct->member);

#define ENTER(x)
#define EXIT(x)
#define RET(x) return
#define RETURN(x, y) return y

#define WANT_GFS_CONVERSION_FUNCTIONS
#include "gfs_ondisk.h"

