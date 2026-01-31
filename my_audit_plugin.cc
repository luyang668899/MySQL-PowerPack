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

#include <mysql/plugin.h>
#include <mysql/plugin_audit.h>
#include <mysqld_error.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#include "lex_string.h"
#include "m_string.h"
#include "my_compiler.h"
#include "my_inttypes.h"
#include "my_macros.h"
#include "my_sys.h"
#include "mysql/psi/mysql_mutex.h"
#include "mysql/strings/m_ctype.h"
#include "nulls.h"
#include "string_with_len.h"
#include "strxnmov.h"
#include "thr_mutex.h"

/** Event strings. */
LEX_CSTRING event_names[][6] = {
    /** MYSQL_AUDIT_GENERAL_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_GENERAL_LOG")},
        {STRING_WITH_LEN("MYSQL_AUDIT_GENERAL_ERROR")},
        {STRING_WITH_LEN("MYSQL_AUDIT_GENERAL_RESULT")},
        {STRING_WITH_LEN("MYSQL_AUDIT_GENERAL_STATUS")},
    },
    /** MYSQL_AUDIT_CONNECTION_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_CONNECTION_CONNECT")},
        {STRING_WITH_LEN("MYSQL_AUDIT_CONNECTION_DISCONNECT")},
        {STRING_WITH_LEN("MYSQL_AUDIT_CONNECTION_CHANGE_USER")},
        {STRING_WITH_LEN("MYSQL_AUDIT_CONNECTION_PRE_AUTHENTICATE")},
    },
    /** MYSQL_AUDIT_PARSE_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_PARSE_PREPARSE")},
        {STRING_WITH_LEN("MYSQL_AUDIT_PARSE_POSTPARSE")},
    },
    /** MYSQL_AUDIT_AUTHORIZATION_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_AUTHORIZATION_USER")},
        {STRING_WITH_LEN("MYSQL_AUDIT_AUTHORIZATION_DB")},
        {STRING_WITH_LEN("MYSQL_AUDIT_AUTHORIZATION_TABLE")},
        {STRING_WITH_LEN("MYSQL_AUDIT_AUTHORIZATION_COLUMN")},
        {STRING_WITH_LEN("MYSQL_AUDIT_AUTHORIZATION_PROCEDURE")},
        {STRING_WITH_LEN("MYSQL_AUDIT_AUTHORIZATION_PROXY")},
    },
    /** MYSQL_AUDIT_TABLE_ROW_ACCES_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_TABLE_ACCESS_READ")},
        {STRING_WITH_LEN("MYSQL_AUDIT_TABLE_ACCESS_INSERT")},
        {STRING_WITH_LEN("MYSQL_AUDIT_TABLE_ACCESS_UPDATE")},
        {STRING_WITH_LEN("MYSQL_AUDIT_TABLE_ACCESS_DELETE")},
    },
    /** MYSQL_AUDIT_GLOBAL_VARIABLE_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_GLOBAL_VARIABLE_GET")},
        {STRING_WITH_LEN("MYSQL_AUDIT_GLOBAL_VARIABLE_SET")},
    },
    /** MYSQL_AUDIT_SERVER_STARTUP_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_SERVER_STARTUP_STARTUP")},
    },
    /** MYSQL_AUDIT_SERVER_SHUTDOWN_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_SERVER_SHUTDOWN_SHUTDOWN")},
    },
    /** MYSQL_AUDIT_COMMAND_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_COMMAND_START")},
        {STRING_WITH_LEN("MYSQL_AUDIT_COMMAND_END")},
    },
    /** MYSQL_AUDIT_QUERY_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_QUERY_START")},
        {STRING_WITH_LEN("MYSQL_AUDIT_QUERY_NESTED_START")},
        {STRING_WITH_LEN("MYSQL_AUDIT_QUERY_STATUS_END")},
        {STRING_WITH_LEN("MYSQL_AUDIT_QUERY_NESTED_STATUS_END")},
    },
    /** MYSQL_AUDIT_STORED_PROGRAM_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_STORED_PROGRAM_EXECUTE")},
    },
    /** MYSQL_AUDIT_AUTHENTICATION_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_AUTHENTICATION_FLUSH")},
        {STRING_WITH_LEN("MYSQL_AUDIT_AUTHENTICATION_AUTHID_CREATE")},
        {STRING_WITH_LEN("MYSQL_AUDIT_AUTHENTICATION_CREDENTIAL_CHANGE")},
        {STRING_WITH_LEN("MYSQL_AUDIT_AUTHENTICATION_AUTHID_RENAME")},
        {STRING_WITH_LEN("MYSQL_AUDIT_AUTHENTICATION_AUTHID_DROP")},
    },
    /** MYSQL_AUDIT_MESSAGE_CLASS */
    {
        {STRING_WITH_LEN("MYSQL_AUDIT_MESSAGE_INTERNAL")},
        {STRING_WITH_LEN("MYSQL_AUDIT_MESSAGE_USER")},
    }};

/** Audit log file pointer. */
static FILE *audit_log_file = nullptr;

/** Record buffer mutex. */
static mysql_mutex_t g_audit_mutex;

/** Plugin has been installed. */
static bool g_plugin_installed = false;

/** Log file path. */
static char *log_file_path = nullptr;

static MYSQL_THDVAR_STR(log_file, PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC, "Audit log file path", nullptr, nullptr, "/var/log/mysql_audit.log");
static MYSQL_THDVAR_INT(log_level, PLUGIN_VAR_RQCMDARG, "Audit log level (0=error, 1=warning, 2=info, 3=debug)", nullptr, nullptr, 2, 0, 3, 0);

/**
  @brief Converts event_class and event_subclass into a string.

  @param [in] event_class    Event class value.
  @param [in] event_subclass Event subclass value.

  @retval Event name.
*/
static LEX_CSTRING event_to_str(unsigned int event_class, unsigned long event_subclass) {
  int count;
  for (count = 0; event_subclass; count++, event_subclass >>= 1)
    ;

  return event_names[event_class][count - 1];
}

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

  @param [in] thd        Connection context.
  @param [in] event_name Event name.
  @param [in] message    Audit message.
*/
static void write_audit_log(MYSQL_THD thd, LEX_CSTRING event_name, const char *message) {
  if (!audit_log_file) return;

  char timestamp[20];
  get_timestamp(timestamp, sizeof(timestamp));

  mysql_mutex_lock(&g_audit_mutex);
  fprintf(audit_log_file, "[%s] [%s] %s\n", timestamp, event_name.str, message);
  fflush(audit_log_file);
  mysql_mutex_unlock(&g_audit_mutex);
}

/**
  @brief Initialize the plugin at server start or plugin installation.

  @param [in] arg Plugin argument.

  @retval 0 success
  @retval 1 failure
*/
static int my_audit_plugin_init(void *arg [[maybe_unused]]) {
  mysql_mutex_init(PSI_NOT_INSTRUMENTED, &g_audit_mutex, MY_MUTEX_INIT_FAST);

  // Open audit log file
  const char *default_path = "/var/log/mysql_audit.log";
  audit_log_file = fopen(default_path, "a");
  if (!audit_log_file) {
    // Try current directory if unable to open /var/log
    audit_log_file = fopen("./mysql_audit.log", "a");
  }

  if (audit_log_file) {
    char timestamp[20];
    get_timestamp(timestamp, sizeof(timestamp));
    fprintf(audit_log_file, "[%s] [SERVER_STARTUP] Audit plugin initialized\n", timestamp);
    fflush(audit_log_file);
  }

  g_plugin_installed = true;
  return 0;
}

/**
  @brief Terminate the plugin at server shutdown or plugin deinstallation.

  @param [in] arg Plugin argument.

  @retval 0 success
  @retval 1 failure
*/
static int my_audit_plugin_deinit(void *arg [[maybe_unused]]) {
  if (g_plugin_installed) {
    if (audit_log_file) {
      char timestamp[20];
      get_timestamp(timestamp, sizeof(timestamp));
      fprintf(audit_log_file, "[%s] [SERVER_SHUTDOWN] Audit plugin terminated\n", timestamp);
      fclose(audit_log_file);
      audit_log_file = nullptr;
    }

    mysql_mutex_destroy(&g_audit_mutex);
    g_plugin_installed = false;
  }
  return 0;
}

/**
  @brief Plugin function handler.

  @param [in] thd         Connection context.
  @param [in] event_class Event class value.
  @param [in] event       Event data.

  @retval Value indicating, whether the server should abort continuation
          of the current operation.
*/
static int my_audit_notify(MYSQL_THD thd, mysql_event_class_t event_class, const void *event) {
  char buffer[4096] = {0};
  int buffer_data = 0;
  const unsigned long event_subclass = static_cast<unsigned long>(*static_cast<const int *>(event));
  const LEX_CSTRING event_name = event_to_str(event_class, event_subclass);

  if (event_class == MYSQL_AUDIT_GENERAL_CLASS) {
    const struct mysql_event_general *event_general = (const struct mysql_event_general *)event;

    switch (event_general->event_subclass) {
      case MYSQL_AUDIT_GENERAL_LOG:
      case MYSQL_AUDIT_GENERAL_ERROR:
      case MYSQL_AUDIT_GENERAL_RESULT:
      case MYSQL_AUDIT_GENERAL_STATUS:
        buffer_data = snprintf(buffer, sizeof(buffer), "message=\"%.*s\"", 
                              (int)event_general->message.length, event_general->message.str);
        break;
      default:
        break;
    }
  } else if (event_class == MYSQL_AUDIT_CONNECTION_CLASS) {
    const struct mysql_event_connection *event_connection = (const struct mysql_event_connection *)event;

    switch (event_connection->event_subclass) {
      case MYSQL_AUDIT_CONNECTION_CONNECT:
      case MYSQL_AUDIT_CONNECTION_DISCONNECT:
      case MYSQL_AUDIT_CONNECTION_CHANGE_USER:
        buffer_data = snprintf(buffer, sizeof(buffer), "user=\"%.*s\" host=\"%.*s\" db=\"%.*s\"", 
                              (int)event_connection->user.length, event_connection->user.str,
                              (int)event_connection->host.length, event_connection->host.str,
                              (int)event_connection->database.length, event_connection->database.str);
        break;
      case MYSQL_AUDIT_CONNECTION_PRE_AUTHENTICATE:
        buffer_data = snprintf(buffer, sizeof(buffer), "host=\"%.*s\"", 
                              (int)event_connection->host.length, event_connection->host.str);
        break;
      default:
        break;
    }
  } else if (event_class == MYSQL_AUDIT_QUERY_CLASS) {
    const struct mysql_event_query *event_query = (const struct mysql_event_query *)event;

    switch (event_query->event_subclass) {
      case MYSQL_AUDIT_QUERY_START:
      case MYSQL_AUDIT_QUERY_STATUS_END:
        buffer_data = snprintf(buffer, sizeof(buffer), "sql_command_id=\"%d\" query=\"%.*s\"", 
                              (int)event_query->sql_command_id,
                              (int)event_query->query.length, event_query->query.str);
        break;
      default:
        break;
    }
  } else if (event_class == MYSQL_AUDIT_TABLE_ACCESS_CLASS) {
    const struct mysql_event_table_access *event_table = (const struct mysql_event_table_access *)event;

    buffer_data = snprintf(buffer, sizeof(buffer), "db=\"%.*s\" table=\"%.*s\"", 
                          (int)event_table->table_database.length, event_table->table_database.str,
                          (int)event_table->table_name.length, event_table->table_name.str);
  } else if (event_class == MYSQL_AUDIT_GLOBAL_VARIABLE_CLASS) {
    const struct mysql_event_global_variable *event_gvar = (const struct mysql_event_global_variable *)event;

    buffer_data = snprintf(buffer, sizeof(buffer), "name=\"%.*s\" value=\"%.*s\"", 
                          (int)event_gvar->variable_name.length, event_gvar->variable_name.str,
                          (int)event_gvar->variable_value.length, event_gvar->variable_value.str);
  } else if (event_class == MYSQL_AUDIT_SERVER_STARTUP_CLASS) {
    buffer_data = snprintf(buffer, sizeof(buffer), "Server startup");
  } else if (event_class == MYSQL_AUDIT_SERVER_SHUTDOWN_CLASS) {
    buffer_data = snprintf(buffer, sizeof(buffer), "Server shutdown");
  }

  if (buffer_data > 0) {
    write_audit_log(thd, event_name, buffer);
  }

  return 0;
}

/**
  Plugin type-specific descriptor
*/
static struct st_mysql_audit my_audit_descriptor = {
    MYSQL_AUDIT_INTERFACE_VERSION, /* interface version    */
    nullptr,                       /* release_thd function */
    my_audit_notify,               /* notify function      */
    {(unsigned long)MYSQL_AUDIT_GENERAL_ALL,
     (unsigned long)MYSQL_AUDIT_CONNECTION_ALL,
     (unsigned long)MYSQL_AUDIT_PARSE_ALL,
     0, /* This event class is currently not supported. */
     (unsigned long)MYSQL_AUDIT_TABLE_ACCESS_ALL,
     (unsigned long)MYSQL_AUDIT_GLOBAL_VARIABLE_ALL,
     (unsigned long)MYSQL_AUDIT_SERVER_STARTUP_ALL,
     (unsigned long)MYSQL_AUDIT_SERVER_SHUTDOWN_ALL,
     (unsigned long)MYSQL_AUDIT_COMMAND_ALL,
     (unsigned long)MYSQL_AUDIT_QUERY_ALL,
     (unsigned long)MYSQL_AUDIT_STORED_PROGRAM_ALL,
     (unsigned long)MYSQL_AUDIT_AUTHENTICATION_ALL,
     (unsigned long)MYSQL_AUDIT_MESSAGE_ALL}
};

static SYS_VAR *system_variables[] = {
    MYSQL_SYSVAR(log_file),
    MYSQL_SYSVAR(log_level),
    nullptr
};

/**
  Plugin library descriptor
*/
mysql_declare_plugin(my_audit){
    MYSQL_AUDIT_PLUGIN,     /* type                            */
    &my_audit_descriptor,   /* descriptor                      */
    "MY_AUDIT",             /* name                            */
    "MySQL Server Team",    /* author                          */
    "Detailed audit logging plugin", /* description                     */
    PLUGIN_LICENSE_GPL,
    my_audit_plugin_init,   /* init function (when loaded)     */
    nullptr,                /* check uninstall function        */
    my_audit_plugin_deinit, /* deinit function (when unloaded) */
    0x0001,                 /* version                         */
    nullptr,                /* status variables                */
    system_variables,       /* system variables                */
    nullptr,
    0,
} mysql_declare_plugin_end;