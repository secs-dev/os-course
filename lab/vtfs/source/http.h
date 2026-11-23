#ifndef VTFS_HTTP_H
#define VTFS_HTTP_H

#include <linux/types.h>

int vtfs_http_call(const char *token, const char *method, const char *argument,
		   void *response, size_t size);

#endif
