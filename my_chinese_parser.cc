/* Copyright (c) 2026, MySQL Server Team

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* MySQL full-text parser structures */
struct st_mysql_ftparser {
  int (*init)(void *param);
  int (*parse)(void *param);
  int (*deinit)(void *param);
};

enum enum_ft_token_type {
  FT_TOKEN_EOF = 0,
  FT_TOKEN_WORD,
  FT_TOKEN_STOPWORD,
  FT_TOKEN_LEFT_PAREN,
  FT_TOKEN_RIGHT_PAREN
};

struct MYSQL_FTPARSER_BOOLEAN_INFO {
  enum_ft_token_type type;
  char yesno;
  char wasign;
  char weight_adjust;
  char trunc;
  char prev;
  char *quot;
};

struct MYSQL_FTPARSER_PARAM {
  char *doc;
  int length;
  int (*mysql_add_word)(MYSQL_FTPARSER_PARAM *param, char *word, int word_len, MYSQL_FTPARSER_BOOLEAN_INFO *boolean_info);
  void *mysql_ftparam;
  const void *cs;
  int flags;
  int mode;
};

#define MYSQL_FTFLAGS_NEED_COPY 1
#define MYSQL_FTPARSER_SIMPLE_MODE 0
#define MYSQL_FTPARSER_BOOLEAN_MODE 1
#define MYSQL_FTPARSER_QUERY_MODE 2

/* Chinese parser structure */
typedef struct {
  // Add any parser-specific data here
  char *buffer;
  int buffer_size;
} ChineseParserData;

/**
  @brief Check if a character is a Chinese character.

  @param [in] c Character to check.

  @retval true if character is Chinese, false otherwise.
*/
static bool is_chinese_char(unsigned char c) {
  // Chinese characters are in the range 0x80-0xFF in UTF-8 (multibyte)
  // or in the range 0x4E00-0x9FFF in Unicode
  return c >= 0x80;
}

/**
  @brief Simple Chinese word segmentation.

  This is a simple implementation that segments Chinese text by character.
  In a real-world scenario, you would integrate a more sophisticated
  Chinese word segmentation library like ICTCLAS,结巴分词, or HanLP.

  @param [in]  param      Parser parameters.
  @param [in]  text       Text to segment.
  @param [in]  text_len   Length of text.

  @retval 0 success
  @retval 1 failure
*/
static int chinese_segment(MYSQL_FTPARSER_PARAM *param, const char *text, int text_len) {
  int i = 0;
  while (i < text_len) {
    unsigned char c = (unsigned char)text[i];
    
    if (is_chinese_char(c)) {
      // Handle Chinese character (assuming UTF-8 encoding)
      // In UTF-8, Chinese characters are 3 bytes
      if (i + 2 < text_len) {
        // Extract 3-byte Chinese character
        if (param->mysql_add_word(param, (char *)&text[i], 3, nullptr)) {
          return 1;
        }
        i += 3;
      } else {
        // Invalid UTF-8 sequence, skip
        i++;
      }
    } else if (isalnum(c)) {
      // Handle alphanumeric characters (as single words)
      int start = i;
      while (i < text_len && (isalnum((unsigned char)text[i]) || text[i] == '_')) {
        i++;
      }
      int word_len = i - start;
      if (word_len > 0) {
        if (param->mysql_add_word(param, (char *)&text[start], word_len, nullptr)) {
          return 1;
        }
      }
    } else {
      // Skip other characters
      i++;
    }
  }
  return 0;
}

/**
  @brief Initialize the Chinese parser.

  @param [in] param Parser parameters.

  @retval 0 success
  @retval 1 failure
*/
static int chinese_parser_init(void *param) {
  MYSQL_FTPARSER_PARAM *ftp_param = (MYSQL_FTPARSER_PARAM *)param;
  ChineseParserData *parser_data = (ChineseParserData *)malloc(sizeof(ChineseParserData));
  
  if (!parser_data) {
    return 1;
  }
  
  parser_data->buffer = nullptr;
  parser_data->buffer_size = 0;
  
  // Store parser data in mysql_ftparam
  ftp_param->mysql_ftparam = parser_data;
  
  return 0;
}

/**
  @brief Parse text with Chinese segmentation.

  @param [in] param Parser parameters.

  @retval 0 success
  @retval 1 failure
*/
static int chinese_parser_parse(void *param) {
  MYSQL_FTPARSER_PARAM *ftp_param = (MYSQL_FTPARSER_PARAM *)param;
  
  if (!ftp_param || !ftp_param->doc || ftp_param->length <= 0) {
    return 1;
  }
  
  // Perform Chinese segmentation
  return chinese_segment(ftp_param, ftp_param->doc, ftp_param->length);
}

/**
  @brief Deinitialize the Chinese parser.

  @param [in] param Parser parameters.

  @retval 0 success
  @retval 1 failure
*/
static int chinese_parser_deinit(void *param) {
  MYSQL_FTPARSER_PARAM *ftp_param = (MYSQL_FTPARSER_PARAM *)param;
  ChineseParserData *parser_data = (ChineseParserData *)ftp_param->mysql_ftparam;
  
  if (parser_data) {
    if (parser_data->buffer) {
      free(parser_data->buffer);
    }
    free(parser_data);
  }
  
  return 0;
}

/* Chinese parser descriptor */
static struct st_mysql_ftparser chinese_parser = {
  chinese_parser_init,
  chinese_parser_parse,
  chinese_parser_deinit
};

/* Plugin declaration */
extern "C" {
struct st_mysql_plugin {
  int type;
  void *descriptor;
  const char *name;
  const char *author;
  const char *description;
  const char *license;
  int (*init)(void *);
  int (*check_uninstall)(void *);
  int (*deinit)(void *);
  unsigned int version;
  struct st_mysql_show_var *status_vars;
  struct st_mysql_sys_var *system_vars;
  void *reserved1;
  unsigned int flags;
};

#define MYSQL_FTPARSER_PLUGIN 11
#define PLUGIN_LICENSE_GPL "GPL"

struct st_mysql_plugin my_chinese_parser_plugin = {
  MYSQL_FTPARSER_PLUGIN,
  &chinese_parser,
  "MY_CHINESE_PARSER",
  "MySQL Server Team",
  "Chinese full-text parser plugin",
  PLUGIN_LICENSE_GPL,
  nullptr,
  nullptr,
  nullptr,
  0x0001,
  nullptr,
  nullptr,
  nullptr,
  0,
};
};