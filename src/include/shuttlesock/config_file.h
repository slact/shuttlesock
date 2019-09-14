#include <stdlib.h>
#include <shuttlesock/common.h>
bool shuso_config_file_parser_initialize(shuso_t *S);
bool shuso_config_file_parse(shuso_t *S, const char *config_file_path);
bool shuso_config_string_parse(shuso_t *S, const char *config);
