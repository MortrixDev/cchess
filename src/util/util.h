#ifndef UTIL_UTIL_H
#define UTIL_UTIL_H

// Number of elements in an array. Not type-checked: handing it a pointer
// silently yields nonsense.
#define COUNT(a) ((int)(sizeof (a) / sizeof (a)[0]))

#endif
