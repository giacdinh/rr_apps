#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <json/json.h>
#include <json/json_tokener.h>
#include "BVcloud.h"

#define JSON_TRUE	1
#define JSON_FALSE	0

const char *process_json_data(char* ptr, char* search_key, int* getint)
{
	char* tmp;
	boolean value;
	json_object* jobj = json_tokener_parse(ptr);
	enum json_type type;

	json_object_object_foreach(jobj, key, val)
	{
		//logger_cloud("Search_Key: %s  -- Key: %s", search_key, key);
		if (0 == strcmp((char*)search_key, (char*)key))
		{
			type = json_object_get_type(val);
			switch (type)
			{
				case json_type_int:
					*getint = json_object_get_int(val);
					return NULL;
					break;
				case json_type_boolean:
					*getint = json_object_get_boolean(val);
					return NULL;
					break;
				case json_type_string:
					return json_object_get_string(val);
					break;
			}
		}
	}
	logger_cloud("%s: no match for key search: %s", __FUNCTION__, search_key);
	return NULL;
}
