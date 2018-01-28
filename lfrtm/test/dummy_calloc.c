#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void *
calloc(size_t nmemb, size_t size)
{
	if( (nmemb == 1) && (size == 1) )
	{
		return NULL;
	}

	void *data = malloc(nmemb * size);
	if(data)
	{
		memset(data, 0x0, nmemb * size);
		return data;
	}

	return NULL;
}
