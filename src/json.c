// Copyright (c) Christopher D. Dickson <cdd@matoya.group>
//
// This Source Code Form is subject to the terms of the MIT License.
// If a copy of the MIT License was not distributed with this file,
// You can obtain one at https://spdx.org/licenses/MIT.html.

#include "matoya.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

enum json_type {
	JSON_BOOLEAN,
	JSON_NUMBER,
	JSON_STRING,
	JSON_OBJECT,
	JSON_ARRAY,
	JSON_NULL,
};

struct MTY_JSON {
	enum json_type type;

	union {
		bool boolean;
		double number;
		char *string;
		MTY_Hash *object;

		struct {
			size_t length;
			MTY_JSON **elements;
		} array;
	} value;
};


// Output

static void json_output_raw(const char *str, char **output, size_t *len)
{
	size_t new_len = strlen(str) + 1;
	size_t pos = *len;

	*len += new_len;
	*output = MTY_Realloc(*output, *len, 1);

	snprintf(*output + pos, new_len, "%s", str);
}

static void json_output_number(double number, char **output, size_t *len)
{
	char str[64];
	snprintf(str, 64, "%f", number);

	json_output_raw(str, output, len);
}

static void json_output_string(const char *str, char **output, size_t *len)
{
	// XXX TODO Must be escaped

	json_output_raw("\"", output, len);
	json_output_raw(str, output, len);
	json_output_raw("\"", output, len);
}

static void json_output(const MTY_JSON *item, char **output, size_t *len, bool pretty)
{
	switch (item->type) {
		case JSON_BOOLEAN:
			json_output_raw(item->value.boolean ? "true" : "false", output, len);
			break;
		case JSON_NUMBER:
			json_output_number(item->value.number, output, len);
			break;
		case JSON_STRING:
			json_output_string(item->value.string, output, len);
			break;
		case JSON_NULL:
			json_output_raw("null", output, len);
			break;
		case JSON_OBJECT: {
			uint64_t iter = 0;
			bool first = true;

			json_output_raw("{", output, len);

			for (const char *key = NULL; MTY_JSONObjGetNextKey(item, &iter, &key);) {
				if (!first)
					json_output_raw(",", output, len);

				json_output_string(key, output, len);
				json_output_raw(":", output, len);

				json_output(MTY_JSONObjGetItem(item, key), output, len, pretty);

				first = false;
			}

			json_output_raw("}", output, len);
			break;
		}
		case JSON_ARRAY:
			json_output_raw("[", output, len);

			for (size_t x = 0; x < item->value.array.length; x++) {
				if (x > 0)
					json_output_raw(",", output, len);

				json_output(MTY_JSONArrayGetItem(item, x), output, len, pretty);
			}

			json_output_raw("]", output, len);
			break;
	}
}

static char *json_print(const MTY_JSON *item, bool pretty)
{
	char *output = NULL;
	size_t len = 0;

	json_output(item, &output, &len, pretty);

	return output;
}


// Input

static MTY_JSON *json_create(enum json_type type, const void *val)
{
	MTY_JSON *item = MTY_Alloc(1, sizeof(MTY_JSON));

	item->type = type;

	switch (type) {
		case JSON_BOOLEAN:
			item->value.boolean = val ? *((const bool *) val) : false;
			break;
		case JSON_NUMBER:
			item->value.number = val ? *((const double *) val) : 0.0;
			break;
		case JSON_STRING:
			item->value.string = val ? MTY_Strdup(val) : MTY_Strdup("");
			break;
		case JSON_OBJECT:
			item->value.object = MTY_HashCreate(20);
			break;
	}

	return item;
}

static void json_parse_set_item(MTY_JSON **parent, const char *key, MTY_JSON *item)
{
	if (!*parent) {
		*parent = item;

	} else if ((*parent)->type == JSON_OBJECT) {
		MTY_JSONObjSetItem(*parent, key, item);

	} else if ((*parent)->type == JSON_ARRAY) {
		MTY_JSONArrayAppendItem(*parent, item);
	}
}

static size_t json_parse_boolean(const char *input, const char *key, MTY_JSON **parent)
{
	size_t consumed = 0;
	bool val = false;

	if (strstr(input, "true") == input) {
		consumed += 4;
		val = true;

	} else if (strstr(input, "false") == input) {
		consumed += 5;
	}

	if (consumed > 0)
		json_parse_set_item(parent, key, json_create(JSON_BOOLEAN, &val));

	return consumed;
}

static size_t json_parse_number(const char *input, const char *key, MTY_JSON **parent)
{
	return 0;
}

static size_t json_parse_string_raw(const char *input, char **parsed)
{
	MTY_Free(*parsed);
	*parsed = NULL;

	size_t consumed = 0;

	// XXX TODO Must unescape

	if (input[0] == '"') {
		input++;
		consumed++;
	}

	const char *end = strchr(input, '"');

	if (end) {
		size_t addtl = end - input;

		*parsed = MTY_Alloc(addtl + 1, 1);
		memcpy(*parsed, input, addtl);

		consumed += addtl + 1;
	}

	return consumed;
}

static size_t json_parse_string(const char *input, const char *key, MTY_JSON **parent)
{
	char *parsed = NULL;
	size_t consumed = json_parse_string_raw(input, &parsed);

	if (consumed > 0)
		json_parse_set_item(parent, key, json_create(JSON_STRING, parsed));

	MTY_Free(parsed);

	return consumed;
}

static size_t json_parse_null(const char *input, const char *key, MTY_JSON **parent)
{
	return 0;
}

MTY_JSON *MTY_JSONParse(const char *input)
{
	size_t len = strlen(input);

	MTY_JSON *stack[128] = {0};

	char *key = NULL;
	bool kv[128] = {0};

	size_t spos = 1;

	for (size_t x = 0; x < len;) {
		MTY_JSON *parent = stack[spos - 1];

		switch (input[x]) {
			// Boolean
			case 't':
			case 'f':
				x += json_parse_boolean(input + x, key, &stack[spos - 1]);
				break;

			// Number
			case '+':
			case '-':
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				x += json_parse_number(input + x, key, &stack[spos - 1]);
				break;

			// String
			case '"':
				if (kv[spos - 1]) {
					x += json_parse_string_raw(input + x, &key);

				} else {
					x += json_parse_string(input + x, key, &stack[spos - 1]);
				}
				break;

			// Null
			case 'n':
				x += json_parse_null(input + x, key, &stack[spos - 1]);
				break;

			// Object
			case '{':
				stack[spos++] = json_create(JSON_OBJECT, NULL);
				kv[spos - 1] = true;
				break;
			case '}':
				kv[spos - 1] = false;
				spos--;
				break;
			case ':':
				kv[spos - 1] = false;
				break;
			case ',':
				kv[spos - 1] = true;
				break;

			// Array
			case '[':
				stack[spos++] = json_create(JSON_ARRAY, NULL);
				kv[spos - 1] = false;
				break;
			case ']':
				kv[spos - 1] = false;
				spos--;
				break;

			// Skip
			default:
				x++;
				break;
		}
	}

	MTY_Free(key);

	return stack[0];
}

MTY_JSON *MTY_JSONReadFile(const char *path)
{
	MTY_JSON *j = NULL;
	void *jstr = MTY_ReadFile(path, NULL);

	if (jstr)
		j = MTY_JSONParse(jstr);

	MTY_Free(jstr);

	return j;
}

static void json_delete(void *ptr)
{
	MTY_JSON *item = ptr;

	if (!ptr)
		return;

	switch (item->type) {
		case JSON_STRING:
			MTY_Free(item->value.string);
			break;
		case JSON_OBJECT:
			MTY_HashDestroy(&item->value.object, json_delete);
			break;
		case JSON_ARRAY:
			for (size_t x = 0; x < item->value.array.length; x++)
				json_delete(item->value.array.elements[x]);

			MTY_Free(item->value.array.elements);
			break;
	}

	MTY_Free(item);
}

MTY_JSON *MTY_JSONDuplicate(const MTY_JSON *json)
{
	switch (json->type) {
		case JSON_BOOLEAN:
		case JSON_NUMBER:
		case JSON_NULL:
			return json_create(JSON_BOOLEAN, &json->value);
		case JSON_STRING:
			return json_create(JSON_STRING, json->value.string);

		case JSON_OBJECT: {
			MTY_JSON *item = json_create(JSON_OBJECT, NULL);
			uint64_t iter = 0;

			for (const char *key = NULL; MTY_JSONObjGetNextKey(json, &iter, &key);)
				MTY_JSONObjSetItem(item, key, MTY_JSONDuplicate(MTY_JSONObjGetItem(json, key)));

			return item;
		}

		case JSON_ARRAY: {
			MTY_JSON *item = json_create(JSON_ARRAY, NULL);

			for (size_t x = 0; x < json->value.array.length; x++)
				MTY_JSONArrayAppendItem(item, MTY_JSONDuplicate(json->value.array.elements[x]));

			return item;
		}
	}

	return NULL;
}

void MTY_JSONDestroy(MTY_JSON **json)
{
	if (!json || !*json)
		return;

	json_delete(*json);

	*json = NULL;
}

char *MTY_JSONSerialize(const MTY_JSON *json)
{
	if (!json)
		return MTY_Strdup("null");

	return json_print(json, false);
}

bool MTY_JSONWriteFile(const char *path, const MTY_JSON *json)
{
	char *jstr = json_print(json, true);

	bool r = MTY_WriteFile(path, jstr, strlen(jstr));
	MTY_Free(jstr);

	return r;
}

MTY_JSON *MTY_JSONObjCreate(void)
{
	return json_create(JSON_OBJECT, NULL);
}

MTY_JSON *MTY_JSONArrayCreate(void)
{
	return json_create(JSON_ARRAY, NULL);
}

bool MTY_JSONObjKeyExists(const MTY_JSON *json, const char *key)
{
	return MTY_JSONObjGetItem(json, key) ? true : false;
}

bool MTY_JSONObjGetNextKey(const MTY_JSON *json, uint64_t *iter, const char **key)
{
	if (!json || json->type != JSON_OBJECT)
		return false;

	return MTY_HashGetNextKey(json->value.object, iter, key);
}

void MTY_JSONObjDeleteItem(MTY_JSON *json, const char *key)
{
	if (!json || json->type != JSON_OBJECT)
		return;

	json_delete(MTY_HashPop(json->value.object, key));
}

const MTY_JSON *MTY_JSONObjGetItem(const MTY_JSON *json, const char *key)
{
	if (!json || json->type != JSON_OBJECT)
		return NULL;

	return MTY_HashGet(json->value.object, key);
}

void MTY_JSONObjSetItem(MTY_JSON *json, const char *key, MTY_JSON *value)
{
	if (!json || !value || json->type != JSON_OBJECT)
		return;

	json_delete(MTY_HashSet(json->value.object, key, value));
}

size_t MTY_JSONArrayGetLength(const MTY_JSON *json)
{
	if (!json || json->type != JSON_ARRAY)
		return 0;

	return json->value.array.length;
}

bool MTY_JSONArrayIndexExists(const MTY_JSON *json, size_t index)
{
	return MTY_JSONArrayGetItem(json, index) ? true : false;
}

const MTY_JSON *MTY_JSONArrayGetItem(const MTY_JSON *json, size_t index)
{
	if (!json || json->type != JSON_ARRAY)
		return NULL;

	return (index < json->value.array.length) ? json->value.array.elements[index] : NULL;
}

void MTY_JSONArrayAppendItem(MTY_JSON *json, MTY_JSON *value)
{
	if (!json || !value || json->type != JSON_ARRAY)
		return;

	json->value.array.elements = MTY_Realloc(json->value.array.elements,
		json->value.array.length + 1, sizeof(MTY_JSON *));

	json->value.array.elements[json->value.array.length++] = value;
}


// Typed getters and setters

static bool json_to_string(const MTY_JSON *json, char *value, size_t size)
{
	if (!json || json->type != JSON_STRING)
		return false;

	snprintf(value, size, "%s", json->value.string);

	return true;
}

static bool json_to_int(const MTY_JSON *json, int32_t *value)
{
	if (!json || json->type != JSON_NUMBER)
		return false;

	*value = lrint(json->value.number);

	return true;
}

static bool json_to_int16(const MTY_JSON *json, int16_t *value)
{
	int32_t v = 0;
	bool r = json_to_int(json, &v);

	*value = (int16_t) v;

	return r;
}

static bool json_to_int8(const MTY_JSON *json, int8_t *value)
{
	int32_t v = 0;
	bool r = json_to_int(json, &v);

	*value = (int8_t) v;

	return r;
}

static bool json_to_float(const MTY_JSON *json, float *value)
{
	if (!json || json->type != JSON_NUMBER)
		return false;

	*value = (float) json->value.number;

	return true;
}

static bool json_to_bool(const MTY_JSON *json, bool *value)
{
	if (!json || json->type != JSON_BOOLEAN)
		return false;

	*value = json->value.boolean;

	return true;
}

static bool json_is_null(const MTY_JSON *json)
{
	if (!json)
		return false;

	return json->type == JSON_NULL;
}

bool MTY_JSONObjGetString(const MTY_JSON *json, const char *key, char *val, size_t size)
{
	return json_to_string(MTY_JSONObjGetItem(json, key), val, size);
}

bool MTY_JSONObjGetInt(const MTY_JSON *json, const char *key, int32_t *val)
{
	return json_to_int(MTY_JSONObjGetItem(json, key), val);
}

bool MTY_JSONObjGetUInt(const MTY_JSON *json, const char *key, uint32_t *val)
{
	return json_to_int(MTY_JSONObjGetItem(json, key), (int32_t *) val);
}

bool MTY_JSONObjGetInt8(const MTY_JSON *json, const char *key, int8_t *val)
{
	return json_to_int8(MTY_JSONObjGetItem(json, key), val);
}

bool MTY_JSONObjGetUInt8(const MTY_JSON *json, const char *key, uint8_t *val)
{
	return json_to_int8(MTY_JSONObjGetItem(json, key), (int8_t *) val);
}

bool MTY_JSONObjGetInt16(const MTY_JSON *json, const char *key, int16_t *val)
{
	return json_to_int16(MTY_JSONObjGetItem(json, key), val);
}

bool MTY_JSONObjGetUInt16(const MTY_JSON *json, const char *key, uint16_t *val)
{
	return json_to_int16(MTY_JSONObjGetItem(json, key), (int16_t *) val);
}

bool MTY_JSONObjGetFloat(const MTY_JSON *json, const char *key, float *val)
{
	return json_to_float(MTY_JSONObjGetItem(json, key), val);
}

bool MTY_JSONObjGetBool(const MTY_JSON *json, const char *key, bool *val)
{
	return json_to_bool(MTY_JSONObjGetItem(json, key), val);
}

bool MTY_JSONObjIsValNull(const MTY_JSON *json, const char *key)
{
	return json_is_null(MTY_JSONObjGetItem(json, key));
}

void MTY_JSONObjSetString(MTY_JSON *json, const char *key, const char *val)
{
	MTY_JSONObjSetItem(json, key, json_create(JSON_STRING, val));
}

void MTY_JSONObjSetInt(MTY_JSON *json, const char *key, int32_t val)
{
	double dval = val;

	MTY_JSONObjSetItem(json, key, json_create(JSON_NUMBER, &dval));
}

void MTY_JSONObjSetUInt(MTY_JSON *json, const char *key, uint32_t val)
{
	double dval = val;

	MTY_JSONObjSetItem(json, key, json_create(JSON_NUMBER, &dval));
}

void MTY_JSONObjSetFloat(MTY_JSON *json, const char *key, float val)
{
	double dval = val;

	MTY_JSONObjSetItem(json, key, json_create(JSON_NUMBER, &dval));
}

void MTY_JSONObjSetBool(MTY_JSON *json, const char *key, bool val)
{
	MTY_JSONObjSetItem(json, key, json_create(JSON_BOOLEAN, &val));
}

void MTY_JSONObjSetNull(MTY_JSON *json, const char *key)
{
	MTY_JSONObjSetItem(json, key, json_create(JSON_NULL, NULL));
}

bool MTY_JSONArrayGetString(const MTY_JSON *json, uint32_t index, char *val, size_t size)
{
	return json_to_string(MTY_JSONArrayGetItem(json, index), val, size);
}

bool MTY_JSONArrayGetInt(const MTY_JSON *json, uint32_t index, int32_t *val)
{
	return json_to_int(MTY_JSONArrayGetItem(json, index), val);
}

bool MTY_JSONArrayGetUInt(const MTY_JSON *json, uint32_t index, uint32_t *val)
{
	return json_to_int(MTY_JSONArrayGetItem(json, index), (int32_t *) val);
}

bool MTY_JSONArrayGetFloat(const MTY_JSON *json, uint32_t index, float *val)
{
	return json_to_float(MTY_JSONArrayGetItem(json, index), val);
}

bool MTY_JSONArrayGetBool(const MTY_JSON *json, uint32_t index, bool *val)
{
	return json_to_bool(MTY_JSONArrayGetItem(json, index), val);
}

bool MTY_JSONArrayIsValNull(const MTY_JSON *json, uint32_t index)
{
	return json_is_null(MTY_JSONArrayGetItem(json, index));
}
