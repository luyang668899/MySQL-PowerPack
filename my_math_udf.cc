/* Copyright (c) 2024, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is designed to work with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have either included with
   the program or referenced in the documentation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <string.h>
#include <math.h>
#include <mysql.h>
#include <mysql/udf_registration_types.h>

/*
** 平方函数: square(x)
** 返回 x 的平方
*/
extern "C" bool square_init(UDF_INIT *initid, UDF_ARGS *args, char *message) {
  if (args->arg_count != 1) {
    strcpy(message, "square() requires exactly one argument");
    return true;
  }
  if (args->arg_type[0] != REAL_RESULT && args->arg_type[0] != INT_RESULT) {
    strcpy(message, "square() requires a numeric argument");
    return true;
  }
  initid->maybe_null = false;
  initid->decimals = 4;
  initid->max_length = 20;
  return false;
}

extern "C" void square_deinit(UDF_INIT *) {
  /* 无需清理资源 */
}

extern "C" double square(UDF_INIT *, UDF_ARGS *args, unsigned char *is_null, unsigned char *error) {
  double value;
  if (args->arg_type[0] == REAL_RESULT) {
    value = *((double *)args->args[0]);
  } else {
    value = (double)*((long long *)args->args[0]);
  }
  return value * value;
}

/*
** 立方函数: cube(x)
** 返回 x 的立方
*/
extern "C" bool cube_init(UDF_INIT *initid, UDF_ARGS *args, char *message) {
  if (args->arg_count != 1) {
    strcpy(message, "cube() requires exactly one argument");
    return true;
  }
  if (args->arg_type[0] != REAL_RESULT && args->arg_type[0] != INT_RESULT) {
    strcpy(message, "cube() requires a numeric argument");
    return true;
  }
  initid->maybe_null = false;
  initid->decimals = 4;
  initid->max_length = 20;
  return false;
}

extern "C" void cube_deinit(UDF_INIT *) {
  /* 无需清理资源 */
}

extern "C" double cube(UDF_INIT *, UDF_ARGS *args, unsigned char *is_null, unsigned char *error) {
  double value;
  if (args->arg_type[0] == REAL_RESULT) {
    value = *((double *)args->args[0]);
  } else {
    value = (double)*((long long *)args->args[0]);
  }
  return value * value * value;
}

/*
** 平方根函数: my_sqrt(x)
** 返回 x 的平方根
*/
extern "C" bool my_sqrt_init(UDF_INIT *initid, UDF_ARGS *args, char *message) {
  if (args->arg_count != 1) {
    strcpy(message, "my_sqrt() requires exactly one argument");
    return true;
  }
  if (args->arg_type[0] != REAL_RESULT && args->arg_type[0] != INT_RESULT) {
    strcpy(message, "my_sqrt() requires a numeric argument");
    return true;
  }
  initid->maybe_null = true;
  initid->decimals = 4;
  initid->max_length = 20;
  return false;
}

extern "C" void my_sqrt_deinit(UDF_INIT *) {
  /* 无需清理资源 */
}

extern "C" double my_sqrt(UDF_INIT *, UDF_ARGS *args, unsigned char *is_null, unsigned char *error) {
  double value;
  if (args->arg_type[0] == REAL_RESULT) {
    value = *((double *)args->args[0]);
  } else {
    value = (double)*((long long *)args->args[0]);
  }
  if (value < 0) {
    *is_null = 1;
    return 0.0;
  }
  return sqrt(value);
}

/*
** 阶乘函数: factorial(x)
** 返回 x 的阶乘
*/
extern "C" bool factorial_init(UDF_INIT *initid, UDF_ARGS *args, char *message) {
  if (args->arg_count != 1) {
    strcpy(message, "factorial() requires exactly one argument");
    return true;
  }
  if (args->arg_type[0] != INT_RESULT) {
    strcpy(message, "factorial() requires an integer argument");
    return true;
  }
  initid->maybe_null = true;
  initid->max_length = 20;
  return false;
}

extern "C" void factorial_deinit(UDF_INIT *) {
  /* 无需清理资源 */
}

extern "C" long long factorial(UDF_INIT *, UDF_ARGS *args, unsigned char *is_null, unsigned char *error) {
  long long n = *((long long *)args->args[0]);
  if (n < 0) {
    *is_null = 1;
    return 0;
  }
  if (n > 20) {
    /* 防止溢出，20! 是最大的可以用 64 位整数表示的阶乘 */
    *is_null = 1;
    return 0;
  }
  long long result = 1;
  for (long long i = 2; i <= n; i++) {
    result *= i;
  }
  return result;
}
