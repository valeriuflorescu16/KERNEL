#ifndef __philosophers_H
#define __philosophers_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

//#include <unistd.h>

#include "libc.h"
#include "string.h"

typedef struct  {

    bool available;
    int owner;
    bool reserved;

} forks_t;


#endif