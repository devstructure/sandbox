#ifndef MACROS_H
#define MACROS_H

#include <stdio.h>
#include <stdlib.h>

#define RETURN(i) result = (i); goto error;

#define WARN(code, m) if (code) { perror(m); goto error; }

#define FATAL(code, m) if (code) { perror(m); exit(-1); }

#endif
