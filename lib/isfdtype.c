#include <config.h>

#ifndef HAVE_ISFDTYPE

int
isfdtype(int fd, int fdtype)
{
  return 0;
}

#endif /* !HAVE_ISFDTYPE */
