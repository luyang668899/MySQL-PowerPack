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

/* Simplified MySQL plugin structures */
#define MYSQL_AUDIT_INTERFACE_VERSION 0x0002

/* Event classes */
enum mysql_event_class_t {
  MYSQL_AUDIT_GENERAL_CLASS,
  MYSQL_AUDIT_CONNECTION_CLASS,
  MYSQL_AUDIT_PARSE_CLASS,
  MYSQL_AUDIT_AUTHORIZATION_CLASS,
  MYSQL_AUDIT_TABLE_ACCESS_CLASS,
  MYSQL_AUDIT_GLOBAL_VARIABLE_CLASS,
  MYSQL_AUDIT_SERVER_STARTUP_CLASS,
  MYSQL_AUDIT_SERVER_SHUTDOWN_CLASS,
  MYSQL_AUDIT_COMMAND_CLASS,
  MYSQL_AUDIT_QUERY_CLASS,
  MYSQL_AUDIT_STORED_PROGRAM_CLASS,
  MYSQL_AUDIT_AUTHENTICATION_CLASS,
  MYSQL_AUDIT_MESSAGE_CLASS,
  MYSQL_AUDIT_CLASS_MAX
};

/* General event subclass */
enum mysql_event_general_subclass {
  MYSQL_AUDIT_GENERAL_LOG,
  MYSQL_AUDIT_GENERAL_ERROR,
  MYSQL_AUDIT_GENERAL_RESULT,
  MYSQL_AUDIT_GENERAL_STATUS
};

/* Connection event subclass */
enum mysql_event_connection_subclass {
  MYSQL_AUDIT_CONNECTION_CONNECT,
  MYSQL_AUDIT_CONNECTION_DISCONNECT,
  MYSQL_AUDIT_CONNECTION_CHANGE_USER,
  MYSQL_AUDIT_CONNECTION_PRE_AUTHENTICATE
};

/* Query event subclass */
enum mysql_event_query_subclass {
  MYSQL_AUDIT_QUERY_START,
  MYSQL_AUDIT_QUERY_NESTED_START,
  MYSQL_AUDIT_QUERY_STATUS_END,
  MYSQL_AUDIT_QUERY_NESTED_STATUS_END
};

/* Table access event subclass */
enum mysql_event_table_access_subclass {
  MYSQL_AUDIT_TABLE_ACCESS_READ,
  MYSQL_AUDIT_TABLE_ACCESS_INSERT,
  MYSQL_AUDIT_TABLE_ACCESS_UPDATE,
  MYSQL_AUDIT_TABLE_ACCESS_DELETE
};

/* Global variable event subclass */
enum mysql_event_global_variable_subclass {
  MYSQL_AUDIT_GLOBAL_VARIABLE_GET,
  MYSQL_AUDIT_GLOBAL_VARIABLE_SET
};

/* Event masks */
#define MYSQL_AUDIT_GENERAL_ALL              0x0000000F
#define MYSQL_AUDIT_CONNECTION_ALL           0x0000000F
#define MYSQL_AUDIT_PARSE_ALL                0x00000003
#define MYSQL_AUDIT_TABLE_ACCESS_ALL         0x0000000F
#define MYSQL_AUDIT_GLOBAL_VARIABLE_ALL      0x00000003
#define MYSQL_AUDIT_SERVER_STARTUP_ALL       0x00000001
#define MYSQL_AUDIT_SERVER_SHUTDOWN_ALL      0x00000001
#define MYSQL_AUDIT_COMMAND_ALL              0x00000003
#define MYSQL_AUDIT_QUERY_ALL                0x0000000F
#define MYSQL_AUDIT_STORED_PROGRAM_ALL       0x00000001
#define MYSQL_AUDIT_AUTHENTICATION_ALL       0x0000001F
#define MYSQL_AUDIT_MESSAGE_ALL              0x00000003

/* String structure */
typedef struct {
  const char *str;
  size_t length;
} LEX_CSTRING;

/* THD structure (forward declaration) */
typedef struct THD THD;

/* Audit event structures */
struct mysql_event_general {
  enum mysql_event_general_subclass event_subclass;
  LEX_CSTRING message;
};

struct mysql_event_connection {
  enum mysql_event_connection_subclass event_subclass;
  LEX_CSTRING user;
  LEX_CSTRING host;
  LEX_CSTRING database;
};

struct mysql_event_query {
  enum mysql_event_query_subclass event_subclass;
  int sql_command_id;
  LEX_CSTRING query;
};

struct mysql_event_table_access {
  enum mysql_event_table_access_subclass event_subclass;
  LEX_CSTRING table_database;
  LEX_CSTRING table_name;
};

struct mysql_event_global_variable {
  enum mysql_event_global_variable_subclass event_subclass;
  LEX_CSTRING variable_name;
  LEX_CSTRING variable_value;
};

/* Audit plugin descriptor */
struct st_mysql_audit {
  int interface_version;
  void (*release_thd)(THD *thd);
  int (*notify)(THD *thd, enum mysql_event_class_t event_class, const void *event);
  unsigned long event_mask[MYSQL_AUDIT_CLASS_MAX];
};

/* Plugin type definitions */
#define MYSQL_AUDIT_PLUGIN 7
#define PLUGIN_LICENSE_GPL "GPL"
#define PLUGIN_AUTHOR_ORACLE "Oracle Corporation"

/* Plugin declaration macros */
#define mysql_declare_plugin(x) extern "C" {
#define mysql_declare_plugin_end   };

/* Event names */
static const char *event_names[MYSQL_AUDIT_CLASS_MAX][4] = {
  {
    "MYSQL_AUDIT_GENERAL_LOG",
    "MYSQL_AUDIT_GENERAL_ERROR",
    "MYSQL_AUDIT_GENERAL_RESULT",
    "MYSQL_AUDIT_GENERAL_STATUS"
  },
  {
    "MYSQL_AUDIT_CONNECTION_CONNECT",
    "MYSQL_AUDIT_CONNECTION_DISCONNECT",
    "MYSQL_AUDIT_CONNECTION_CHANGE_USER",
    "MYSQL_AUDIT_CONNECTION_PRE_AUTHENTICATE"
  },
  {
    "MYSQL_AUDIT_PARSE_PREPARSE",
    "MYSQL_AUDIT_PARSE_POSTPARSE",
    NULL,
    NULL
  },
  {
    "MYSQL_AUDIT_AUTHORIZATION_USER",
    "MYSQL_AUDIT_AUTHORIZATION_DB",
    "MYSQL_AUDIT_AUTHORIZATION_TABLE",
    "MYSQL_AUDIT_AUTHORIZATION_COLUMN"
  },
  {
    "MYSQL_AUDIT_TABLE_ACCESS_READ",
    "MYSQL_AUDIT_TABLE_ACCESS_INSERT",
    "MYSQL_AUDIT_TABLE_ACCESS_UPDATE",
    "MYSQL_AUDIT_TABLE_ACCESS_DELETE"
  },
  {
    "MYSQL_AUDIT_GLOBAL_VARIABLE_GET",
    "MYSQL_AUDIT_GLOBAL_VARIABLE_SET",
    NULL,
    NULL
  },
  {
    "MYSQL_AUDIT_SERVER_STARTUP_STARTUP",
    NULL,
    NULL,
    NULL
  },
  {
    "MYSQL_AUDIT_SERVER_SHUTDOWN_SHUTDOWN",
    NULL,
    NULL,
    NULL
  },
  {
    "MYSQL_AUDIT_COMMAND_START",
    "MYSQL_AUDIT_COMMAND_END",
    NULL,
    NULL
  },
  {
    "MYSQL_AUDIT_QUERY_START",
    "MYSQL_AUDIT_QUERY_NESTED_START",
    "MYSQL_AUDIT_QUERY_STATUS_END",
    "MYSQL_AUDIT_QUERY_NESTED_STATUS_END"
  },
  {
    "MYSQL_AUDIT_STORED_PROGRAM_EXECUTE",
    NULL,
    NULL,
    NULL
  },
  {
    "MYSQL_AUDIT_AUTHENTICATION_FLUSH",
    "MYSQL_AUDIT_AUTHENTICATION_AUTHID_CREATE",
    "MYSQL_AUDIT_AUTHENTICATION_CREDENTIAL_CHANGE",
    "MYSQL_AUDIT_AUTHENTICATION_AUTHID_RENAME"
  },
  {
    "MYSQL_AUDIT_MESSAGE_INTERNAL",
    "MYSQL_AUDIT_MESSAGE_USER",
    NULL,
    NULL
  }
};

/** Audit log file pointer. */
static FILE *audit_log_file = nullptr;

/**
  @brief Get current timestamp in string format.

  @param [out] buffer Buffer to store timestamp.
  @param [in]  size   Buffer size.

  @retval Timestamp string.
*/
static char *get_timestamp(char *buffer, size_t size) {
  time_t now = time(nullptr);
  struct tm *tm_info = localtime(&now);
  strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
  return buffer;
}

/**
  @brief Write audit log entry.

  @param [in] event_name Event name.
  @param [in] message    Audit message.
*/
static void write_audit_log(const char *event_name, const char *message) {
  if (!audit_log_file) return;

  char timestamp[20];
  get_timestamp(timestamp, sizeof(timestamp));

  fprintf(audit_log_file, "[%s] [%s] %s\n", timestamp, event_name, message);
  fflush(audit_log_file);
}

/**
  @brief Get event name from event class and subclass.

  @param [in] event_class    Event class.
  @param [in] event_subclass Event subclass.

  @retval Event name string.
*/
static const char *get_event_name(enum mysql_event_class_t event_class, unsigned long event_subclass) {
  int index = 0;
  unsigned long mask = 1;
  while (mask < event_subclass) {
    mask <<= 1;
    index++;
  }

  if (event_class < MYSQL_AUDIT_CLASS_MAX && index < 4 && event_names[event_class][index]) {
    return event_names[event_class][index];
  }
  return "UNKNOWN_EVENT";
}

/**
  @brief Audit plugin notification function.

  @param [in] thd         Connection context.
  @param [in] event_class Event class.
  @param [in] event       Event data.

  @retval 0 success
  @retval 1 failure
*/
static int my_audit_notify(THD *thd, enum mysql_event_class_t event_class, const void *event) {
  char buffer[4096] = {0};
  unsigned long event_subclass = *((unsigned long *)event);
  const char *event_name = get_event_name(event_class, event_subclass);

  switch (event_class) {
    case MYSQL_AUDIT_GENERAL_CLASS:
    {
      const struct mysql_event_general *event_general = (const struct mysql_event_general *)event;
      snprintf(buffer, sizeof(buffer), "message=\"%.*s\"", 
               (int)event_general->message.length, event_general->message.str);
      break;
    }
    case MYSQL_AUDIT_CONNECTION_CLASS:
    {
      const struct mysql_event_connection *event_connection = (const struct mysql_event_connection *)event;
      snprintf(buffer, sizeof(buffer), "user=\"%.*s\" host=\"%.*s\" db=\"%.*s\"", 
               (int)event_connection->user.length, event_connection->user.str,
               (int)event_connection->host.length, event_connection->host.str,
               (int)event_connection->database.length, event_connection->database.str);
      break;
    }
    case MYSQL_AUDIT_QUERY_CLASS:
    {
      const struct mysql_event_query *event_query = (const struct mysql_event_query *)event;
      snprintf(buffer, sizeof(buffer), "sql_command_id=\"%d\" query=\"%.*s\"", 
               event_query->sql_command_id,
               (int)event_query->query.length, event_query->query.str);
      break;
    }
    case MYSQL_AUDIT_TABLE_ACCESS_CLASS:
    {
      const struct mysql_event_table_access *event_table = (const struct mysql_event_table_access *)event;
      snprintf(buffer, sizeof(buffer), "db=\"%.*s\" table=\"%.*s\"", 
               (int)event_table->table_database.length, event_table->table_database.str,
               (int)event_table->table_name.length, event_table->table_name.str);
      break;
    }
    case MYSQL_AUDIT_GLOBAL_VARIABLE_CLASS:
    {
      const struct mysql_event_global_variable *event_gvar = (const struct mysql_event_global_variable *)event;
      snprintf(buffer, sizeof(buffer), "name=\"%.*s\" value=\"%.*s\"", 
               (int)event_gvar->variable_name.length, event_gvar->variable_name.str,
               (int)event_gvar->variable_value.length, event_gvar->variable_value.str);
      break;
    }
    case MYSQL_AUDIT_SERVER_STARTUP_CLASS:
      snprintf(buffer, sizeof(buffer), "Server startup");
      break;
    case MYSQL_AUDIT_SERVER_SHUTDOWN_CLASS:
      snprintf(buffer, sizeof(buffer), "Server shutdown");
      break;
    default:
      snprintf(buffer, sizeof(buffer), "Event class: %d", event_class);
      break;
  }

  write_audit_log(event_name, buffer);
  return 0;
}

/**
  @brief Initialize the plugin at server start or plugin installation.

  @param [in] arg Plugin argument.

  @retval 0 success
  @retval 1 failure
*/
static int my_audit_plugin_init(void *arg) {
  // Open audit log file
  const char *default_path = "./mysql_audit.log";
  audit_log_file = fopen(default_path, "a");
  if (!audit_log_file) {
    return 1;
  }

  char timestamp[20];
  get_timestamp(timestamp, sizeof(timestamp));
  fprintf(audit_log_file, "[%s] [SERVER_STARTUP] Audit plugin initialized\n", timestamp);
  fflush(audit_log_file);

  return 0;
}

/**
  @brief Terminate the plugin at server shutdown or plugin deinstallation.

  @param [in] arg Plugin argument.

  @retval 0 success
  @retval 1 failure
*/
static int my_audit_plugin_deinit(void *arg) {
  if (audit_log_file) {
    char timestamp[20];
    get_timestamp(timestamp, sizeof(timestamp));
    fprintf(audit_log_file, "[%s] [SERVER_SHUTDOWN] Audit plugin terminated\n", timestamp);
    fclose(audit_log_file);
    audit_log_file = nullptr;
  }
  return 0;
}

/* Audit plugin descriptor */
static struct st_mysql_audit my_audit_descriptor = {
  MYSQL_AUDIT_INTERFACE_VERSION,
  nullptr,
  my_audit_notify,
  {
    MYSQL_AUDIT_GENERAL_ALL,
    MYSQL_AUDIT_CONNECTION_ALL,
    MYSQL_AUDIT_PARSE_ALL,
    0,
    MYSQL_AUDIT_TABLE_ACCESS_ALL,
    MYSQL_AUDIT_GLOBAL_VARIABLE_ALL,
    MYSQL_AUDIT_SERVER_STARTUP_ALL,
    MYSQL_AUDIT_SERVER_SHUTDOWN_ALL,
    MYSQL_AUDIT_COMMAND_ALL,
    MYSQL_AUDIT_QUERY_ALL,
    MYSQL_AUDIT_STORED_PROGRAM_ALL,
    MYSQL_AUDIT_AUTHENTICATION_ALL,
    MYSQL_AUDIT_MESSAGE_ALL
  }
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

struct st_mysql_plugin my_audit_simple_plugin = {
  MYSQL_AUDIT_PLUGIN,
  &my_audit_descriptor,
  "MY_AUDIT_SIMPLE",
  "MySQL Server Team",
  "Simple audit logging plugin",
  PLUGIN_LICENSE_GPL,
  my_audit_plugin_init,
  nullptr,
  my_audit_plugin_deinit,
  0x0001,
  nullptr,
  nullptr,
  nullptr,
  0,
};
};