#include <stdlib.h>
#include <shuttlesock/common.h>
bool shuso_config_file_parser_initialize(shuso_t *ctx);
bool shuso_config_file_parse(shuso_t *ctx, const char *config_file_path);
bool shuso_config_string_parse(shuso_t *ctx, const char *config);
