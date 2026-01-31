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

/* Query cache plugin descriptor */
typedef struct {
  int (*get)(void *ctx, const char *query, int query_len, char **result, int *result_len);
  int (*put)(void *ctx, const char *query, int query_len, const char *result, int result_len);
  int (*invalidate)(void *ctx, const char *table);
  int (*clear)(void *ctx);
  void *(*create_context)(void);
  void (*destroy_context)(void *ctx);
} st_mysql_query_cache_descriptor;

/* Cache entry structure */
typedef struct CacheEntry {
  char *query;
  int query_len;
  char *result;
  int result_len;
  time_t created_time;
  time_t last_access_time;
  int access_count;
  struct CacheEntry *next;
  struct CacheEntry *prev;
} CacheEntry;

/* Query cache context structure */
typedef struct {
  CacheEntry *head;
  CacheEntry *tail;
  int entry_count;
  int max_entries;
  size_t max_size;
  size_t current_size;
  time_t default_ttl;
} QueryCacheContext;

/* Plugin type definitions */
#define MYSQL_QUERY_CACHE_PLUGIN 13
#define PLUGIN_LICENSE_GPL "GPL"

/* Default cache settings */
#define DEFAULT_MAX_ENTRIES 1000
#define DEFAULT_MAX_SIZE (1024 * 1024 * 64) /* 64MB */
#define DEFAULT_TTL 3600 /* 1 hour */

/**
  @brief Create query cache context.

  @retval Query cache context pointer, or NULL on failure.
*/
static void *query_cache_create_context(void) {
  QueryCacheContext *ctx = (QueryCacheContext *)malloc(sizeof(QueryCacheContext));
  if (!ctx) {
    return NULL;
  }

  /* Initialize cache structure */
  ctx->head = NULL;
  ctx->tail = NULL;
  ctx->entry_count = 0;
  ctx->max_entries = DEFAULT_MAX_ENTRIES;
  ctx->max_size = DEFAULT_MAX_SIZE;
  ctx->current_size = 0;
  ctx->default_ttl = DEFAULT_TTL;

  return ctx;
}

/**
  @brief Destroy query cache context.

  @param [in] ctx Query cache context to destroy.
*/
static void query_cache_destroy_context(void *ctx) {
  if (ctx) {
    QueryCacheContext *cache_ctx = (QueryCacheContext *)ctx;
    
    /* Clear all cache entries */
    CacheEntry *entry = cache_ctx->head;
    while (entry) {
      CacheEntry *next = entry->next;
      if (entry->query) free(entry->query);
      if (entry->result) free(entry->result);
      free(entry);
      entry = next;
    }
    
    free(cache_ctx);
  }
}

/**
  @brief Remove cache entry from the list.

  @param [in] ctx   Query cache context.
  @param [in] entry Cache entry to remove.
*/
static void remove_cache_entry(QueryCacheContext *ctx, CacheEntry *entry) {
  if (entry->prev) {
    entry->prev->next = entry->next;
  } else {
    ctx->head = entry->next;
  }
  
  if (entry->next) {
    entry->next->prev = entry->prev;
  } else {
    ctx->tail = entry->prev;
  }
  
  ctx->entry_count--;
  ctx->current_size -= (entry->query_len + entry->result_len + sizeof(CacheEntry));
  
  if (entry->query) free(entry->query);
  if (entry->result) free(entry->result);
  free(entry);
}

/**
  @brief Add cache entry to the head of the list.

  @param [in] ctx   Query cache context.
  @param [in] entry Cache entry to add.
*/
static void add_cache_entry(QueryCacheContext *ctx, CacheEntry *entry) {
  /* Make room if necessary */
  while (ctx->entry_count >= ctx->max_entries || 
         ctx->current_size + entry->query_len + entry->result_len + sizeof(CacheEntry) > ctx->max_size) {
    if (ctx->tail) {
      remove_cache_entry(ctx, ctx->tail);
    } else {
      /* No entries to remove, but still no space */
      if (entry->query) free(entry->query);
      if (entry->result) free(entry->result);
      free(entry);
      return;
    }
  }
  
  /* Add to head */
  entry->next = ctx->head;
  entry->prev = NULL;
  
  if (ctx->head) {
    ctx->head->prev = entry;
  } else {
    ctx->tail = entry;
  }
  
  ctx->head = entry;
  ctx->entry_count++;
  ctx->current_size += (entry->query_len + entry->result_len + sizeof(CacheEntry));
}

/**
  @brief Find cache entry by query.

  @param [in] ctx       Query cache context.
  @param [in] query     Query string.
  @param [in] query_len Query length.

  @retval Cache entry if found, NULL otherwise.
*/
static CacheEntry *find_cache_entry(QueryCacheContext *ctx, const char *query, int query_len) {
  CacheEntry *entry = ctx->head;
  time_t now = time(NULL);
  
  while (entry) {
    /* Check if entry has expired */
    if (now - entry->created_time > ctx->default_ttl) {
      CacheEntry *to_remove = entry;
      entry = entry->next;
      remove_cache_entry(ctx, to_remove);
      continue;
    }
    
    /* Check if query matches */
    if (entry->query_len == query_len && 
        memcmp(entry->query, query, query_len) == 0) {
      /* Update access information */
      entry->last_access_time = now;
      entry->access_count++;
      
      /* Move to head (LRU) */
      if (entry != ctx->head) {
        /* Remove from current position */
        if (entry->prev) {
          entry->prev->next = entry->next;
        }
        if (entry->next) {
          entry->next->prev = entry->prev;
        } else {
          ctx->tail = entry->prev;
        }
        
        /* Add to head */
        entry->next = ctx->head;
        entry->prev = NULL;
        ctx->head->prev = entry;
        ctx->head = entry;
      }
      
      return entry;
    }
    
    entry = entry->next;
  }
  
  return NULL;
}

/**
  @brief Get cached query result.

  @param [in]  ctx       Query cache context.
  @param [in]  query     Query string.
  @param [in]  query_len Query length.
  @param [out] result    Pointer to store result.
  @param [out] result_len Pointer to store result length.

  @retval 0 success, 1 failure (not found).
*/
static int query_cache_get(void *ctx, const char *query, int query_len, char **result, int *result_len) {
  QueryCacheContext *cache_ctx = (QueryCacheContext *)ctx;
  CacheEntry *entry = find_cache_entry(cache_ctx, query, query_len);
  
  if (entry) {
    *result = entry->result;
    *result_len = entry->result_len;
    return 0;
  }
  
  return 1;
}

/**
  @brief Put query result into cache.

  @param [in] ctx       Query cache context.
  @param [in] query     Query string.
  @param [in] query_len Query length.
  @param [in] result    Result string.
  @param [in] result_len Result length.

  @retval 0 success, 1 failure.
*/
static int query_cache_put(void *ctx, const char *query, int query_len, const char *result, int result_len) {
  QueryCacheContext *cache_ctx = (QueryCacheContext *)ctx;
  CacheEntry *entry = (CacheEntry *)malloc(sizeof(CacheEntry));
  if (!entry) {
    return 1;
  }
  
  /* Allocate memory for query and result */
  entry->query = (char *)malloc(query_len + 1);
  if (!entry->query) {
    free(entry);
    return 1;
  }
  
  entry->result = (char *)malloc(result_len + 1);
  if (!entry->result) {
    free(entry->query);
    free(entry);
    return 1;
  }
  
  /* Copy query and result */
  memcpy(entry->query, query, query_len);
  entry->query[query_len] = '\0';
  entry->query_len = query_len;
  
  memcpy(entry->result, result, result_len);
  entry->result[result_len] = '\0';
  entry->result_len = result_len;
  
  /* Set metadata */
  entry->created_time = time(NULL);
  entry->last_access_time = entry->created_time;
  entry->access_count = 1;
  entry->next = NULL;
  entry->prev = NULL;
  
  /* Add to cache */
  add_cache_entry(cache_ctx, entry);
  
  return 0;
}

/**
  @brief Invalidate cache entries related to a table.

  @param [in] ctx   Query cache context.
  @param [in] table Table name.

  @retval 0 success, 1 failure.
*/
static int query_cache_invalidate(void *ctx, const char *table) {
  QueryCacheContext *cache_ctx = (QueryCacheContext *)ctx;
  CacheEntry *entry = cache_ctx->head;
  int invalidated = 0;
  
  while (entry) {
    /* Simple implementation: check if table name appears in query */
    /* In a real-world scenario, you would parse the query to find table references */
    if (strstr(entry->query, table)) {
      CacheEntry *to_remove = entry;
      entry = entry->next;
      remove_cache_entry(cache_ctx, to_remove);
      invalidated++;
    } else {
      entry = entry->next;
    }
  }
  
  return 0;
}

/**
  @brief Clear all cache entries.

  @param [in] ctx Query cache context.

  @retval 0 success, 1 failure.
*/
static int query_cache_clear(void *ctx) {
  QueryCacheContext *cache_ctx = (QueryCacheContext *)ctx;
  CacheEntry *entry = cache_ctx->head;
  
  while (entry) {
    CacheEntry *next = entry->next;
    remove_cache_entry(cache_ctx, entry);
    entry = next;
  }
  
  return 0;
}

/**
  @brief Initialize the query cache plugin.

  @param [in] arg Plugin argument.

  @retval 0 success, 1 failure.
*/
static int query_cache_plugin_init(void *arg) {
  /* Initialize any necessary resources */
  return 0;
}

/**
  @brief Deinitialize the query cache plugin.

  @param [in] arg Plugin argument.

  @retval 0 success, 1 failure.
*/
static int query_cache_plugin_deinit(void *arg) {
  /* Cleanup any resources */
  return 0;
}

/* Query cache plugin descriptor */
static st_mysql_query_cache_descriptor query_cache_descriptor = {
  query_cache_get,
  query_cache_put,
  query_cache_invalidate,
  query_cache_clear,
  query_cache_create_context,
  query_cache_destroy_context
};

/* Plugin declaration */
extern "C" {
struct st_mysql_plugin my_query_cache_plugin = {
  MYSQL_QUERY_CACHE_PLUGIN,
  &query_cache_descriptor,
  "MY_QUERY_CACHE",
  "MySQL Server Team",
  "Enhanced query cache plugin",
  PLUGIN_LICENSE_GPL,
  query_cache_plugin_init,
  nullptr,
  query_cache_plugin_deinit,
  0x0001,
  nullptr,
  nullptr,
  nullptr,
  0,
};
};