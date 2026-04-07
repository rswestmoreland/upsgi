#ifdef UPSGI_XML

#include "upsgi.h"

extern struct upsgi_server upsgi;

#ifdef UPSGI_XML_LIBXML2

#include <libxml/parser.h>
#include <libxml/tree.h>

void upsgi_xml_config(char *filename, struct wsgi_request *wsgi_req, char *magic_table[]) {
	xmlDoc *doc = NULL;
	xmlNode *element = NULL;
	xmlNode *node = NULL;

	xmlChar *node_mode;

	char *colon;

	char *xml_id;
	char *xml_content;
	size_t xml_size = 0;

	LIBXML_TEST_VERSION

	if (upsgi_check_scheme(filename)) {
		colon = upsgi_get_last_char(filename, '/');
		colon = upsgi_get_last_char(colon, ':');
	}
	else {
		colon = upsgi_get_last_char(filename, ':');
	}
	if (colon) {
		colon[0] = 0;
		colon++;
		if (*colon == 0) {
			upsgi_log("invalid xml id\n");
			exit(1);
		}
		upsgi_log("[upsgi] using xml upsgi id: %s\n", colon);
	}

	xml_content = upsgi_open_and_read(filename, &xml_size, 0, magic_table);

	doc = xmlReadMemory(xml_content, xml_size, filename, NULL, 0);
	if (doc == NULL) {
		upsgi_log("[upsgi] could not parse file %s.\n", filename);
		exit(1);
	}

	upsgi_log_initial("[upsgi] parsing config file %s\n", filename);

	element = xmlDocGetRootElement(doc);
	if (element == NULL) {
		upsgi_log("[upsgi] invalid xml config file.\n");
		exit(1);
	}
	if (strcmp((char *) element->name, "upsgi")) {
		for (node = element->children; node; node = node->next) {
			element = NULL;
			if (node->type == XML_ELEMENT_NODE) {
				if (!strcmp((char *) node->name, "upsgi")) {
					xml_id = (char *) xmlGetProp(node, (const xmlChar *) "id");

					if (colon && xml_id) {
						if (strcmp(colon, xml_id)) {
							continue;
						}
					}
					element = node;
					break;
				}
			}
		}

		if (!element) {
			upsgi_log("[upsgi] invalid xml root element, <upsgi> expected.\n");
			exit(1);
		}
	}


	// first check for options
	for (node = element->children; node; node = node->next) {

		node_mode = xmlGetProp(node, (const xmlChar *) "mode");
		if (upsgi.mode && node_mode) {
			if (strcmp(upsgi.mode, (char *) node_mode)) {
				continue;
			}
		}

		xml_id = (char *) xmlGetProp(node, (const xmlChar *) "id");
		if (colon && xml_id) {
			if (strcmp(colon, xml_id)) {
				continue;
			}
		}

		if (node->type == XML_CDATA_SECTION_NODE) {
			if (node->content) {
				add_exported_option((char *) "eval", strdup((char *) node->content), 0);
			}
		}
		else if (node->type == XML_ELEMENT_NODE) {

			if (!strcmp((char *) node->name, (char *) "app")) {

				char *mountpoint = (char *) xmlGetProp(node, (const xmlChar *) "mountpoint");
				char *domain = (char *) xmlGetProp(node, (const xmlChar *) "domain");

				if (!node->children) {
					add_exported_option("app", strdup(""), 0);
				}
				else if (node->children && node->children->content && !node->children->next) {
					char *opt_value = strdup((char *) node->children->content);

					if (mountpoint) {
						opt_value = upsgi_concat3(mountpoint, "=", opt_value);
					}
					else if (domain) {
						opt_value = upsgi_concat3(domain, "|=", opt_value);
					}
					add_exported_option("mount", opt_value, 0);
					add_exported_option("app", strdup(""), 0);
				}
				else if (node->children && node->children->next && node->children->next->children && node->children->next->children->content) {

					char *opt_value = strdup((char *) node->children->next->children->content);

					if (mountpoint) {
						opt_value = upsgi_concat3(mountpoint, "=", opt_value);
					}
					else if (domain) {
						opt_value = upsgi_concat3(domain, "|=", opt_value);
					}

					add_exported_option("mount", opt_value, 0);
					add_exported_option("app", strdup(""), 0);
				}
			}
			else {

				if (node->children) {
					add_exported_option(strdup((char *) node->name), strdup((char *) node->children->content), 0);
				}
				else {
					add_exported_option(strdup((char *) node->name), strdup("1"), 0);
				}
			}
		}
	}
	/* We can safely free resources */

	if (colon) colon[0] = ':';

	xmlFreeDoc(doc);
	xmlCleanupParser();

}


#endif

#ifdef UPSGI_XML_EXPAT

#include <expat.h>

int upsgi_xml_found_stanza = 0;
char *upsgi_xml_found_opt_key = NULL;

static void startElement(void *xml_id, const XML_Char * name, const XML_Char ** attrs) {


	if (!upsgi_xml_found_stanza) {
		if (xml_id) {
			if (!attrs[0])
				return;
			if (!attrs[1])
				return;
			if (strcmp("id", attrs[0]))
				return;
			if (strcmp((char *) xml_id, attrs[1]))
				return;
		}
		if (!strcmp("upsgi", name))
			upsgi_xml_found_stanza = 1;
	}
	else {
		upsgi_xml_found_opt_key = (char *) name;
	}
}


static void textElement(void *xml_id, const char *s, int len) {

	if (!upsgi_xml_found_stanza)
		return;
	if (upsgi_xml_found_opt_key) {
		add_exported_option(strdup(upsgi_xml_found_opt_key), upsgi_concat2n((char *) s, len, (char *) "", 0), 0);
		upsgi_xml_found_opt_key = NULL;
	}
}

static void endElement(void *xml_id, const XML_Char * name) {

	if (!upsgi_xml_found_stanza)
		return;

	if (!strcmp(name, "upsgi")) {
		upsgi_xml_found_stanza = 0;
		return;
	}

	if (!upsgi_xml_found_opt_key)
		return;

	add_exported_option(strdup(upsgi_xml_found_opt_key), strdup("1"), 0);
	upsgi_xml_found_opt_key = NULL;
}

void upsgi_xml_config(char *filename, struct wsgi_request *wsgi_req, char *magic_table[]) {

	char *colon;

	char *xml_content;
	size_t xml_size = 0;
	int done = 0;

	if (upsgi_check_scheme(filename)) {
		colon = upsgi_get_last_char(filename, '/');
		colon = upsgi_get_last_char(colon, ':');
	}
	else {
		colon = upsgi_get_last_char(filename, ':');
	}
	if (colon) {
		colon[0] = 0;
		colon++;
		if (*colon == 0) {
			upsgi_log("invalid xml id\n");
			exit(1);
		}
		upsgi_log("[upsgi] using xml upsgi id: %s\n", colon);
	}

	xml_content = upsgi_open_and_read(filename, &xml_size, 0, magic_table);

	upsgi_log("[upsgi] parsing config file %s\n", filename);

	XML_Parser parser = XML_ParserCreate(NULL);
	XML_SetUserData(parser, NULL);
	if (colon) {
		XML_SetUserData(parser, colon);
	}

	XML_SetElementHandler(parser, startElement, endElement);
	XML_SetCharacterDataHandler(parser, textElement);

	do {
		if (!XML_Parse(parser, xml_content, xml_size, done)) {
			if (XML_GetErrorCode(parser) != XML_ERROR_JUNK_AFTER_DOC_ELEMENT) {
				upsgi_log("unable to parse xml file: %s (line %d)\n", XML_ErrorString(XML_GetErrorCode(parser)), XML_GetCurrentLineNumber(parser));
				exit(1);
			}
			else {
				break;
			}
		}

	} while (!done);

	if (colon) colon[0] = ':';

	// we can safely free, as we have a copy of datas
	XML_ParserFree(parser);
}

#endif

#else
#warning "*** XML configuration support is disabled ***"
#endif
