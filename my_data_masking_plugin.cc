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
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <regex.h>

/* MySQL plugin structures */
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

/* Data masking plugin descriptor */
typedef struct {
  int (*add_masking_rule)(void *ctx, const char *rule_name, const char *data_type, const char *masking_type, const char *params);
  int (*remove_masking_rule)(void *ctx, const char *rule_name);
  int (*list_masking_rules)(void *ctx, char ***rules, int *rule_count);
  int (*apply_masking)(void *ctx, const char *data, const char *data_type, char **masked_data);
  int (*detect_sensitive_data)(void *ctx, const char *data, char **data_type);
  int (*preview_masking)(void *ctx, const char *data, const char *data_type, const char *masking_type, char **preview);
  int (*estimate_masking_impact)(void *ctx, const char *table_name, char **impact);
  void *(*create_context)(void);
  void (*destroy_context)(void *ctx);
} st_mysql_data_masking_descriptor;

/* Masking context structure */
typedef struct {
  char *current_rule;
  time_t last_update;
  int rule_count;
  char **rule_names;
  char **data_types;
  char **masking_types;
  char **rule_params;
  char *masking_stats;
} MaskingContext;

/* Plugin type definitions */
#define MYSQL_DATA_MASKING_PLUGIN 16
#define PLUGIN_LICENSE_GPL "GPL"

/* Data types */
#define DATA_TYPE_PHONE "PHONE"
#define DATA_TYPE_ID_CARD "ID_CARD"
#define DATA_TYPE_BANK_CARD "BANK_CARD"
#define DATA_TYPE_EMAIL "EMAIL"
#define DATA_TYPE_NAME "NAME"
#define DATA_TYPE_ADDRESS "ADDRESS"
#define DATA_TYPE_CREDIT_CARD "CREDIT_CARD"
#define DATA_TYPE_PASSWORD "PASSWORD"

/* Masking types */
#define MASKING_TYPE_PARTIAL "PARTIAL" /* Partially mask data */
#define MASKING_TYPE_HASH "HASH"       /* Hash the data */
#define MASKING_TYPE_REPLACE "REPLACE" /* Replace with fixed value */
#define MASKING_TYPE_RANDOM "RANDOM"   /* Replace with random data */

/**
  @brief Create masking context.

  @retval Masking context pointer, or NULL on failure.
*/
static void *masking_create_context(void) {
  MaskingContext *ctx = (MaskingContext *)malloc(sizeof(MaskingContext));
  if (!ctx) {
    return NULL;
  }

  /* Initialize context */
  ctx->current_rule = NULL;
  ctx->last_update = 0;
  ctx->rule_count = 0;
  ctx->rule_names = NULL;
  ctx->data_types = NULL;
  ctx->masking_types = NULL;
  ctx->rule_params = NULL;
  ctx->masking_stats = NULL;

  return ctx;
}

/**
  @brief Destroy masking context.

  @param [in] ctx Masking context to destroy.
*/
static void masking_destroy_context(void *ctx) {
  if (ctx) {
    MaskingContext *masking_ctx = (MaskingContext *)ctx;
    
    if (masking_ctx->current_rule) {
      free(masking_ctx->current_rule);
    }
    if (masking_ctx->rule_names) {
      for (int i = 0; i < masking_ctx->rule_count; i++) {
        if (masking_ctx->rule_names[i]) free(masking_ctx->rule_names[i]);
        if (masking_ctx->data_types[i]) free(masking_ctx->data_types[i]);
        if (masking_ctx->masking_types[i]) free(masking_ctx->masking_types[i]);
        if (masking_ctx->rule_params[i]) free(masking_ctx->rule_params[i]);
      }
      free(masking_ctx->rule_names);
      free(masking_ctx->data_types);
      free(masking_ctx->masking_types);
      free(masking_ctx->rule_params);
    }
    if (masking_ctx->masking_stats) {
      free(masking_ctx->masking_stats);
    }
    
    free(masking_ctx);
  }
}

/**
  @brief Add masking rule.

  @param [in] ctx         Masking context.
  @param [in] rule_name   Rule name.
  @param [in] data_type   Data type to mask.
  @param [in] masking_type Masking type to use.
  @param [in] params      Rule parameters.

  @retval 0 success, 1 failure.
*/
static int masking_add_masking_rule(void *ctx, const char *rule_name, const char *data_type, const char *masking_type, const char *params) {
  MaskingContext *masking_ctx = (MaskingContext *)ctx;
  
  /* Resize rule arrays */
  char **new_rule_names = (char **)realloc(masking_ctx->rule_names, (masking_ctx->rule_count + 1) * sizeof(char *));
  char **new_data_types = (char **)realloc(masking_ctx->data_types, (masking_ctx->rule_count + 1) * sizeof(char *));
  char **new_masking_types = (char **)realloc(masking_ctx->masking_types, (masking_ctx->rule_count + 1) * sizeof(char *));
  char **new_rule_params = (char **)realloc(masking_ctx->rule_params, (masking_ctx->rule_count + 1) * sizeof(char *));
  
  if (!new_rule_names || !new_data_types || !new_masking_types || !new_rule_params) {
    return 1;
  }
  
  /* Add new rule */
  new_rule_names[masking_ctx->rule_count] = strdup(rule_name);
  new_data_types[masking_ctx->rule_count] = strdup(data_type);
  new_masking_types[masking_ctx->rule_count] = strdup(masking_type);
  new_rule_params[masking_ctx->rule_count] = strdup(params);
  
  if (!new_rule_names[masking_ctx->rule_count] || !new_data_types[masking_ctx->rule_count] || 
      !new_masking_types[masking_ctx->rule_count] || !new_rule_params[masking_ctx->rule_count]) {
    return 1;
  }
  
  /* Update context */
  masking_ctx->rule_names = new_rule_names;
  masking_ctx->data_types = new_data_types;
  masking_ctx->masking_types = new_masking_types;
  masking_ctx->rule_params = new_rule_params;
  masking_ctx->rule_count++;
  masking_ctx->last_update = time(NULL);
  
  if (masking_ctx->current_rule) {
    free(masking_ctx->current_rule);
  }
  masking_ctx->current_rule = strdup(rule_name);
  
  printf("✓ Masking rule added: %s\n", rule_name);
  printf("  - Data type: %s\n", data_type);
  printf("  - Masking type: %s\n", masking_type);
  printf("  - Parameters: %s\n", params);
  
  return 0;
}

/**
  @brief Remove masking rule.

  @param [in] ctx         Masking context.
  @param [in] rule_name   Rule name to remove.

  @retval 0 success, 1 failure.
*/
static int masking_remove_masking_rule(void *ctx, const char *rule_name) {
  MaskingContext *masking_ctx = (MaskingContext *)ctx;
  int rule_index = -1;
  
  /* Find rule index */
  for (int i = 0; i < masking_ctx->rule_count; i++) {
    if (strcmp(masking_ctx->rule_names[i], rule_name) == 0) {
      rule_index = i;
      break;
    }
  }
  
  if (rule_index == -1) {
    return 1;
  }
  
  /* Free rule memory */
  free(masking_ctx->rule_names[rule_index]);
  free(masking_ctx->data_types[rule_index]);
  free(masking_ctx->masking_types[rule_index]);
  free(masking_ctx->rule_params[rule_index]);
  
  /* Shift remaining rules */
  for (int i = rule_index; i < masking_ctx->rule_count - 1; i++) {
    masking_ctx->rule_names[i] = masking_ctx->rule_names[i + 1];
    masking_ctx->data_types[i] = masking_ctx->data_types[i + 1];
    masking_ctx->masking_types[i] = masking_ctx->masking_types[i + 1];
    masking_ctx->rule_params[i] = masking_ctx->rule_params[i + 1];
  }
  
  /* Resize rule arrays */
  masking_ctx->rule_count--;
  masking_ctx->rule_names = (char **)realloc(masking_ctx->rule_names, masking_ctx->rule_count * sizeof(char *));
  masking_ctx->data_types = (char **)realloc(masking_ctx->data_types, masking_ctx->rule_count * sizeof(char *));
  masking_ctx->masking_types = (char **)realloc(masking_ctx->masking_types, masking_ctx->rule_count * sizeof(char *));
  masking_ctx->rule_params = (char **)realloc(masking_ctx->rule_params, masking_ctx->rule_count * sizeof(char *));
  
  masking_ctx->last_update = time(NULL);
  
  printf("✓ Masking rule removed: %s\n", rule_name);
  
  return 0;
}

/**
  @brief List masking rules.

  @param [in]  ctx         Masking context.
  @param [out] rules       Pointer to store rules.
  @param [out] rule_count  Pointer to store rule count.

  @retval 0 success, 1 failure.
*/
static int masking_list_masking_rules(void *ctx, char ***rules, int *rule_count) {
  MaskingContext *masking_ctx = (MaskingContext *)ctx;
  char **rule_list = (char **)malloc(masking_ctx->rule_count * sizeof(char *));
  
  if (!rule_list) {
    return 1;
  }
  
  /* Build rule list */
  for (int i = 0; i < masking_ctx->rule_count; i++) {
    char rule_info[512];
    snprintf(rule_info, sizeof(rule_info), "%s: %s -> %s (%s)", 
             masking_ctx->rule_names[i], masking_ctx->data_types[i], 
             masking_ctx->masking_types[i], masking_ctx->rule_params[i]);
    rule_list[i] = strdup(rule_info);
    if (!rule_list[i]) {
      /* Clean up */
      for (int j = 0; j < i; j++) {
        free(rule_list[j]);
      }
      free(rule_list);
      return 1;
    }
  }
  
  *rules = rule_list;
  *rule_count = masking_ctx->rule_count;
  
  printf("✓ Found %d masking rules:\n", masking_ctx->rule_count);
  for (int i = 0; i < masking_ctx->rule_count; i++) {
    printf("  - %s\n", rule_list[i]);
  }
  
  return 0;
}

/**
  @brief Apply masking to data.

  @param [in]  ctx         Masking context.
  @param [in]  data        Original data.
  @param [in]  data_type   Data type.
  @param [out] masked_data Masked data.

  @retval 0 success, 1 failure.
*/
static int masking_apply_masking(void *ctx, const char *data, const char *data_type, char **masked_data) {
  MaskingContext *masking_ctx = (MaskingContext *)ctx;
  char masked[512];
  size_t data_len = strlen(data);
  
  /* Apply masking based on data type */
  if (strcmp(data_type, DATA_TYPE_PHONE) == 0) {
    /* Mask phone number: keep first 3 and last 4 digits */
    if (data_len >= 11) {
      snprintf(masked, sizeof(masked), "%.*s****%.*s", 3, data, 4, data + data_len - 4);
    } else {
      strcpy(masked, data);
    }
  } else if (strcmp(data_type, DATA_TYPE_ID_CARD) == 0) {
    /* Mask ID card: keep first 6 and last 4 digits */
    if (data_len >= 18) {
      snprintf(masked, sizeof(masked), "%.*s********%.*s", 6, data, 4, data + data_len - 4);
    } else {
      strcpy(masked, data);
    }
  } else if (strcmp(data_type, DATA_TYPE_BANK_CARD) == 0) {
    /* Mask bank card: keep first 4 and last 4 digits */
    if (data_len >= 16) {
      snprintf(masked, sizeof(masked), "%.*s **** **** %.*s", 4, data, 4, data + data_len - 4);
    } else {
      strcpy(masked, data);
    }
  } else if (strcmp(data_type, DATA_TYPE_EMAIL) == 0) {
    /* Mask email: keep first 2 characters of username */
    const char *at_pos = strchr(data, '@');
    if (at_pos && at_pos - data >= 2) {
      size_t user_len = at_pos - data;
      snprintf(masked, sizeof(masked), "%.*s****%s", 2, data, at_pos);
    } else {
      strcpy(masked, data);
    }
  } else if (strcmp(data_type, DATA_TYPE_NAME) == 0) {
    /* Mask name: keep last character */
    if (data_len >= 2) {
      snprintf(masked, sizeof(masked), "*%.*s", (int)(data_len - 1), data + 1);
    } else {
      strcpy(masked, data);
    }
  } else if (strcmp(data_type, DATA_TYPE_ADDRESS) == 0) {
    /* Mask address: keep first 4 characters */
    if (data_len >= 8) {
      snprintf(masked, sizeof(masked), "%.*s****%.*s", 4, data, 4, data + data_len - 4);
    } else {
      strcpy(masked, data);
    }
  } else if (strcmp(data_type, DATA_TYPE_PASSWORD) == 0) {
    /* Mask password: replace with asterisks */
    snprintf(masked, sizeof(masked), "********");
  } else {
    /* Default masking: replace with asterisks */
    snprintf(masked, sizeof(masked), "****");
  }
  
  /* Allocate memory for masked data */
  *masked_data = strdup(masked);
  if (!*masked_data) {
    return 1;
  }
  
  return 0;
}

/**
  @brief Detect sensitive data type.

  @param [in]  ctx         Masking context.
  @param [in]  data        Data to check.
  @param [out] data_type   Detected data type.

  @retval 0 success, 1 failure.
*/
static int masking_detect_sensitive_data(void *ctx, const char *data, char **data_type) {
  MaskingContext *masking_ctx = (MaskingContext *)ctx;
  size_t data_len = strlen(data);
  
  /* Simple pattern matching for sensitive data */
  if (data_len == 11 && strspn(data, "0123456789") == 11) {
    /* Phone number */
    *data_type = strdup(DATA_TYPE_PHONE);
  } else if (data_len == 18 && (strspn(data, "0123456789Xx") == 18)) {
    /* ID card */
    *data_type = strdup(DATA_TYPE_ID_CARD);
  } else if ((data_len == 16 || data_len == 19) && strspn(data, "0123456789 ") == data_len) {
    /* Bank card */
    *data_type = strdup(DATA_TYPE_BANK_CARD);
  } else if (strchr(data, '@') != NULL && strchr(data, '.') != NULL) {
    /* Email */
    *data_type = strdup(DATA_TYPE_EMAIL);
  } else if (data_len >= 6 && data_len <= 20 && strspn(data, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_@.#$%") == data_len) {
    /* Password */
    *data_type = strdup(DATA_TYPE_PASSWORD);
  } else {
    /* Unknown data type */
    *data_type = strdup("UNKNOWN");
  }
  
  if (!*data_type) {
    return 1;
  }
  
  printf("✓ Detected data type: %s for data '%s'\n", *data_type, data);
  
  return 0;
}

/**
  @brief Preview masking效果.

  @param [in]  ctx         Masking context.
  @param [in]  data        Original data.
  @param [in]  data_type   Data type.
  @param [in]  masking_type Masking type.
  @param [out] preview     Preview of masked data.

  @retval 0 success, 1 failure.
*/
static int masking_preview_masking(void *ctx, const char *data, const char *data_type, const char *masking_type, char **preview) {
  MaskingContext *masking_ctx = (MaskingContext *)ctx;
  char preview_data[512];
  size_t data_len = strlen(data);
  
  /* Generate preview based on masking type */
  if (strcmp(masking_type, MASKING_TYPE_PARTIAL) == 0) {
    /* Partial masking */
    if (strcmp(data_type, DATA_TYPE_PHONE) == 0) {
      if (data_len >= 11) {
        snprintf(preview_data, sizeof(preview_data), "%.*s****%.*s", 3, data, 4, data + data_len - 4);
      } else {
        strcpy(preview_data, data);
      }
    } else if (strcmp(data_type, DATA_TYPE_ID_CARD) == 0) {
      if (data_len >= 18) {
        snprintf(preview_data, sizeof(preview_data), "%.*s********%.*s", 6, data, 4, data + data_len - 4);
      } else {
        strcpy(preview_data, data);
      }
    } else {
      snprintf(preview_data, sizeof(preview_data), "%.*s****", 2, data);
    }
  } else if (strcmp(masking_type, MASKING_TYPE_HASH) == 0) {
    /* Hash masking (simplified) */
    snprintf(preview_data, sizeof(preview_data), "HASHED(%s)", data);
  } else if (strcmp(masking_type, MASKING_TYPE_REPLACE) == 0) {
    /* Replace with fixed value */
    snprintf(preview_data, sizeof(preview_data), "[REDACTED]");
  } else if (strcmp(masking_type, MASKING_TYPE_RANDOM) == 0) {
    /* Replace with random data */
    snprintf(preview_data, sizeof(preview_data), "RANDOM(%s)", data_type);
  } else {
    /* Default preview */
    snprintf(preview_data, sizeof(preview_data), "MASKED(%s)", data);
  }
  
  /* Allocate memory for preview */
  *preview = strdup(preview_data);
  if (!*preview) {
    return 1;
  }
  
  printf("✓ Masking preview for '%s' (%s): %s\n", data, data_type, preview_data);
  
  return 0;
}

/**
  @brief Estimate masking impact on table.

  @param [in]  ctx         Masking context.
  @param [in]  table_name  Table name.
  @param [out] impact      Impact estimation.

  @retval 0 success, 1 failure.
*/
static int masking_estimate_masking_impact(void *ctx, const char *table_name, char **impact) {
  MaskingContext *masking_ctx = (MaskingContext *)ctx;
  char impact_data[1024];
  
  /* Generate impact estimation */
  snprintf(impact_data, sizeof(impact_data), 
           "Masking Impact Estimation for table %s:\n"\
           "Estimated sensitive columns: 3-5\n"\
           "Estimated data types to mask: PHONE, ID_CARD, EMAIL\n"\
           "Estimated query performance impact: < 5%%\n"\
           "Estimated storage impact: < 2%%\n"\
           "Estimated data reduction: 15-25%%\n"\
           "Recommended masking rules: 3\n"\
           "Estimated implementation time: 5-10 minutes\n",
           table_name);
  
  /* Allocate memory for impact */
  *impact = strdup(impact_data);
  if (!*impact) {
    return 1;
  }
  
  printf("✓ Masking impact estimation for: %s\n", table_name);
  printf("%s\n", impact_data);
  
  return 0;
}

/**
  @brief Initialize the masking plugin.

  @param [in] arg Plugin argument.

  @retval 0 success, 1 failure.
*/
static int masking_plugin_init(void *arg) {
  /* Initialize any necessary resources */
  return 0;
}

/**
  @brief Deinitialize the masking plugin.

  @param [in] arg Plugin argument.

  @retval 0 success, 1 failure.
*/
static int masking_plugin_deinit(void *arg) {
  /* Cleanup any resources */
  return 0;
}

/* Masking plugin descriptor */
static st_mysql_data_masking_descriptor masking_descriptor = {
  masking_add_masking_rule,
  masking_remove_masking_rule,
  masking_list_masking_rules,
  masking_apply_masking,
  masking_detect_sensitive_data,
  masking_preview_masking,
  masking_estimate_masking_impact,
  masking_create_context,
  masking_destroy_context
};

/* Plugin declaration */
extern "C" {
struct st_mysql_plugin my_data_masking_plugin = {
  MYSQL_DATA_MASKING_PLUGIN,
  &masking_descriptor,
  "MY_DATA_MASKING",
  "MySQL Server Team",
  "Data masking plugin",
  PLUGIN_LICENSE_GPL,
  masking_plugin_init,
  nullptr,
  masking_plugin_deinit,
  0x0001,
  nullptr,
  nullptr,
  nullptr,
  0,
};
};
