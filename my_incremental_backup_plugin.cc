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

/* Incremental backup plugin descriptor */
typedef struct {
  int (*init_backup)(void *ctx, const char *backup_dir, const char *backup_name);
  int (*perform_backup)(void *ctx, int incremental);
  int (*restore_backup)(void *ctx, const char *backup_dir, const char *backup_name);
  int (*list_backups)(void *ctx, const char *backup_dir, char ***backups, int *backup_count);
  int (*cleanup_backup)(void *ctx, const char *backup_dir, const char *backup_name);
  int (*validate_backup)(void *ctx, const char *backup_dir, const char *backup_name);
  void *(*create_context)(void);
  void (*destroy_context)(void *ctx);
} st_mysql_incremental_backup_descriptor;

/* Backup context structure */
typedef struct {
  char *backup_dir;
  char *backup_name;
  time_t backup_time;
  char *full_backup_name;
  int backup_level;
  char *backup_metadata;
  size_t metadata_size;
} BackupContext;

/* Plugin type definitions */
#define MYSQL_INCREMENTAL_BACKUP_PLUGIN 14
#define PLUGIN_LICENSE_GPL "GPL"

/* Backup constants */
#define BACKUP_METADATA_FILE "backup_metadata.json"
#define BACKUP_DATA_DIR "data"
#define BACKUP_LOG_DIR "logs"
#define BACKUP_LEVEL_FULL 0
#define BACKUP_LEVEL_INCREMENTAL 1

/**
  @brief Create backup context.

  @retval Backup context pointer, or NULL on failure.
*/
static void *backup_create_context(void) {
  BackupContext *ctx = (BackupContext *)malloc(sizeof(BackupContext));
  if (!ctx) {
    return NULL;
  }

  /* Initialize context */
  ctx->backup_dir = NULL;
  ctx->backup_name = NULL;
  ctx->backup_time = 0;
  ctx->full_backup_name = NULL;
  ctx->backup_level = BACKUP_LEVEL_FULL;
  ctx->backup_metadata = NULL;
  ctx->metadata_size = 0;

  return ctx;
}

/**
  @brief Destroy backup context.

  @param [in] ctx Backup context to destroy.
*/
static void backup_destroy_context(void *ctx) {
  if (ctx) {
    BackupContext *backup_ctx = (BackupContext *)ctx;
    
    if (backup_ctx->backup_dir) {
      free(backup_ctx->backup_dir);
    }
    if (backup_ctx->backup_name) {
      free(backup_ctx->backup_name);
    }
    if (backup_ctx->full_backup_name) {
      free(backup_ctx->full_backup_name);
    }
    if (backup_ctx->backup_metadata) {
      free(backup_ctx->backup_metadata);
    }
    
    free(backup_ctx);
  }
}

/**
  @brief Create directory recursively.

  @param [in] path Directory path to create.

  @retval 0 success, 1 failure.
*/
static int create_directory(const char *path) {
  char *dir = strdup(path);
  if (!dir) {
    return 1;
  }

  char *p = dir;
  while (*p) {
    if (*p == '/') {
      *p = '\0';
      if (*dir != '\0' && mkdir(dir, 0755) != 0 && errno != EEXIST) {
        free(dir);
        return 1;
      }
      *p = '/';
    }
    p++;
  }

  if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
    free(dir);
    return 1;
  }

  free(dir);
  return 0;
}

/**
  @brief Initialize backup context.

  @param [in] ctx          Backup context.
  @param [in] backup_dir   Backup directory.
  @param [in] backup_name  Backup name.

  @retval 0 success, 1 failure.
*/
static int backup_init_backup(void *ctx, const char *backup_dir, const char *backup_name) {
  BackupContext *backup_ctx = (BackupContext *)ctx;
  char full_path[1024];
  
  /* Set backup directory and name */
  if (backup_ctx->backup_dir) {
    free(backup_ctx->backup_dir);
  }
  backup_ctx->backup_dir = strdup(backup_dir);
  
  if (backup_ctx->backup_name) {
    free(backup_ctx->backup_name);
  }
  backup_ctx->backup_name = strdup(backup_name);
  
  if (!backup_ctx->backup_dir || !backup_ctx->backup_name) {
    return 1;
  }
  
  /* Create backup directory structure */
  snprintf(full_path, sizeof(full_path), "%s/%s", backup_dir, backup_name);
  if (create_directory(full_path) != 0) {
    return 1;
  }
  
  snprintf(full_path, sizeof(full_path), "%s/%s/%s", backup_dir, backup_name, BACKUP_DATA_DIR);
  if (create_directory(full_path) != 0) {
    return 1;
  }
  
  snprintf(full_path, sizeof(full_path), "%s/%s/%s", backup_dir, backup_name, BACKUP_LOG_DIR);
  if (create_directory(full_path) != 0) {
    return 1;
  }
  
  /* Set backup time */
  backup_ctx->backup_time = time(NULL);
  
  return 0;
}

/**
  @brief Perform incremental backup.

  @param [in] ctx         Backup context.
  @param [in] incremental 1 for incremental backup, 0 for full backup.

  @retval 0 success, 1 failure.
*/
static int backup_perform_backup(void *ctx, int incremental) {
  BackupContext *backup_ctx = (BackupContext *)ctx;
  char metadata_file[1024];
  FILE *fp;
  
  if (!backup_ctx->backup_dir || !backup_ctx->backup_name) {
    return 1;
  }
  
  /* Set backup level */
  backup_ctx->backup_level = incremental ? BACKUP_LEVEL_INCREMENTAL : BACKUP_LEVEL_FULL;
  
  /* Create metadata file */
  snprintf(metadata_file, sizeof(metadata_file), "%s/%s/%s", 
           backup_ctx->backup_dir, backup_ctx->backup_name, BACKUP_METADATA_FILE);
  
  fp = fopen(metadata_file, "w");
  if (!fp) {
    return 1;
  }
  
  /* Write metadata */
  fprintf(fp, "{");
  fprintf(fp, "\"backup_name\": \"%s\",", backup_ctx->backup_name);
  fprintf(fp, "\"backup_time\": %ld,", backup_ctx->backup_time);
  fprintf(fp, "\"backup_level\": %d,", backup_ctx->backup_level);
  
  if (incremental && backup_ctx->full_backup_name) {
    fprintf(fp, "\"full_backup\": \"%s\",", backup_ctx->full_backup_name);
  }
  
  fprintf(fp, "\"backup_size\": 0,");
  fprintf(fp, "\"status\": \"completed\"");
  fprintf(fp, "}\n");
  
  fclose(fp);
  
  /* In a real-world scenario, you would:
   * 1. For full backup: Copy all data files
   * 2. For incremental backup: Copy only changed files since last backup
   * 3. Create backup logs
   * 4. Verify backup integrity
   */
  
  return 0;
}

/**
  @brief Restore backup.

  @param [in] ctx          Backup context.
  @param [in] backup_dir   Backup directory.
  @param [in] backup_name  Backup name.

  @retval 0 success, 1 failure.
*/
static int backup_restore_backup(void *ctx, const char *backup_dir, const char *backup_name) {
  BackupContext *backup_ctx = (BackupContext *)ctx;
  char metadata_file[1024];
  FILE *fp;
  
  /* Check if backup exists */
  snprintf(metadata_file, sizeof(metadata_file), "%s/%s/%s", 
           backup_dir, backup_name, BACKUP_METADATA_FILE);
  
  fp = fopen(metadata_file, "r");
  if (!fp) {
    return 1;
  }
  fclose(fp);
  
  /* In a real-world scenario, you would:
   * 1. For full backup: Restore all data files
   * 2. For incremental backup: Restore full backup first, then apply incremental changes
   * 3. Apply transaction logs
   * 4. Verify restore integrity
   */
  
  return 0;
}

/**
  @brief List available backups.

  @param [in]  ctx           Backup context.
  @param [in]  backup_dir    Backup directory.
  @param [out] backups       Pointer to store backup names.
  @param [out] backup_count  Pointer to store backup count.

  @retval 0 success, 1 failure.
*/
static int backup_list_backups(void *ctx, const char *backup_dir, char ***backups, int *backup_count) {
  DIR *dir;
  struct dirent *entry;
  int count = 0;
  char **backup_list = NULL;
  
  /* Open backup directory */
  dir = opendir(backup_dir);
  if (!dir) {
    return 1;
  }
  
  /* Count backup directories */
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
      char metadata_file[1024];
      snprintf(metadata_file, sizeof(metadata_file), "%s/%s/%s", 
               backup_dir, entry->d_name, BACKUP_METADATA_FILE);
      
      if (access(metadata_file, F_OK) == 0) {
        count++;
      }
    }
  }
  
  /* Rewind directory */
  rewinddir(dir);
  
  /* Allocate backup list */
  if (count > 0) {
    backup_list = (char **)malloc(count * sizeof(char *));
    if (!backup_list) {
      closedir(dir);
      return 1;
    }
    
    /* Fill backup list */
    count = 0;
    while ((entry = readdir(dir)) != NULL) {
      if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
        char metadata_file[1024];
        snprintf(metadata_file, sizeof(metadata_file), "%s/%s/%s", 
                 backup_dir, entry->d_name, BACKUP_METADATA_FILE);
        
        if (access(metadata_file, F_OK) == 0) {
          backup_list[count] = strdup(entry->d_name);
          count++;
        }
      }
    }
  }
  
  closedir(dir);
  
  *backups = backup_list;
  *backup_count = count;
  
  return 0;
}

/**
  @brief Cleanup backup.

  @param [in] ctx          Backup context.
  @param [in] backup_dir   Backup directory.
  @param [in] backup_name  Backup name.

  @retval 0 success, 1 failure.
*/
static int backup_cleanup_backup(void *ctx, const char *backup_dir, const char *backup_name) {
  char backup_path[1024];
  char command[1024];
  
  /* Build backup path */
  snprintf(backup_path, sizeof(backup_path), "%s/%s", backup_dir, backup_name);
  
  /* In a real-world scenario, you would:
   * 1. Verify backup exists
   * 2. Remove backup files and directories
   * 3. Clean up metadata
   */
  
  /* For demonstration, just print the command that would be executed */
  snprintf(command, sizeof(command), "rm -rf %s", backup_path);
  printf("Would execute: %s\n", command);
  
  return 0;
}

/**
  @brief Validate backup.

  @param [in] ctx          Backup context.
  @param [in] backup_dir   Backup directory.
  @param [in] backup_name  Backup name.

  @retval 0 success, 1 failure.
*/
static int backup_validate_backup(void *ctx, const char *backup_dir, const char *backup_name) {
  char metadata_file[1024];
  FILE *fp;
  
  /* Check if backup exists */
  snprintf(metadata_file, sizeof(metadata_file), "%s/%s/%s", 
           backup_dir, backup_name, BACKUP_METADATA_FILE);
  
  fp = fopen(metadata_file, "r");
  if (!fp) {
    return 1;
  }
  fclose(fp);
  
  /* In a real-world scenario, you would:
   * 1. Check backup files integrity
   * 2. Verify metadata consistency
   * 3. Test restore process
   */
  
  return 0;
}

/**
  @brief Initialize the backup plugin.

  @param [in] arg Plugin argument.

  @retval 0 success, 1 failure.
*/
static int backup_plugin_init(void *arg) {
  /* Initialize any necessary resources */
  return 0;
}

/**
  @brief Deinitialize the backup plugin.

  @param [in] arg Plugin argument.

  @retval 0 success, 1 failure.
*/
static int backup_plugin_deinit(void *arg) {
  /* Cleanup any resources */
  return 0;
}

/* Backup plugin descriptor */
static st_mysql_incremental_backup_descriptor backup_descriptor = {
  backup_init_backup,
  backup_perform_backup,
  backup_restore_backup,
  backup_list_backups,
  backup_cleanup_backup,
  backup_validate_backup,
  backup_create_context,
  backup_destroy_context
};

/* Plugin declaration */
extern "C" {
struct st_mysql_plugin my_incremental_backup_plugin = {
  MYSQL_INCREMENTAL_BACKUP_PLUGIN,
  &backup_descriptor,
  "MY_INCREMENTAL_BACKUP",
  "MySQL Server Team",
  "Incremental backup plugin",
  PLUGIN_LICENSE_GPL,
  backup_plugin_init,
  nullptr,
  backup_plugin_deinit,
  0x0001,
  nullptr,
  nullptr,
  nullptr,
  0,
};
};