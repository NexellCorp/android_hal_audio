/* $Id: //depot/bc/bluesuite_2_3/devHost/common/bool.h#1 $ */

#ifndef BOOL_H

#define BOOL_H

#if !defined(__cplusplus) && !(__bool_true_false_are_defined)
#if __STDC_VERSION__ >= 199901L
# include <stdbool.h>
#else
#define __bool_true_false_are_defined 1

#define null   ((void*) 0)

#define true   1
#define false  0
#define bool _Bool

#if (!defined(__GNUC__)) || __GNUC__ < 3

/*
    This messing around is to try and find a type which has the same
    size as the bool used by the corresponding C++ compiler on each
    platform.

    This is an 'int' on most platforms, but is a 'char' for:
        Visual C++ 5.0 and later
        GCC on Linux
*/
#if (defined (_MSC_VER) && (_MSC_VER >= 1100)) || (defined (linux) && defined(__GNUC__))
//typedef unsigned char _Bool;
#else
typedef unsigned int _Bool;
#endif

#endif /* !defined(__GNUC__) ||  __GNUC__ < 3 */

#endif /* __STDC_VERSION__ >= 199901L */

#endif /* ndef __cplusplus && ndef __bool_true_false_are_defined */

#endif
