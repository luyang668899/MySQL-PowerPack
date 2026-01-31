# MySQL 增强插件集合

## 项目简介

这是一个为 MySQL 服务器开发的增强插件集合，提供了多种强大的功能来优化数据库性能、提高安全性和简化管理。该项目包含三个核心插件：

1. **增量备份插件** - 提供高效的完整备份和增量备份功能
2. **智能分区插件** - 基于数据特性自动优化分区策略
3. **数据脱敏插件** - 自动对查询结果中的敏感数据进行脱敏

## 插件列表

| 插件名称 | 插件文件 | 主要功能 |
|---------|---------|--------|
| 增量备份插件 | my_incremental_backup_plugin.so | 支持完整备份、增量备份、备份恢复、列出备份、清理备份、验证备份 |
| 智能分区插件 | my_intelligent_partition_plugin.so | 支持表分析、分区推荐、分区应用、性能估计、性能监控 |
| 数据脱敏插件 | my_data_masking_plugin.so | 支持添加脱敏规则、删除规则、列出规则、应用脱敏、检测敏感数据、预览脱敏效果 |

## 系统要求

- MySQL 5.7 或更高版本
- C++ 编译器 (支持 C++11 或更高)
- Linux/macOS 操作系统
- 足够的权限安装和管理 MySQL 插件

## 安装说明

### 1. 编译插件

```bash
# 编译增量备份插件
g++ -shared -fPIC -o my_incremental_backup_plugin.so my_incremental_backup_plugin.cc

# 编译智能分区插件
g++ -shared -fPIC -o my_intelligent_partition_plugin.so my_intelligent_partition_plugin.cc

# 编译数据脱敏插件
g++ -shared -fPIC -o my_data_masking_plugin.so my_data_masking_plugin.cc
```

### 2. 安装插件到 MySQL

1. 将编译好的插件文件复制到 MySQL 的插件目录：

   ```bash
   # 查找 MySQL 插件目录
   mysql_config --plugindir
   
   # 复制插件文件到插件目录
   cp my_*.so $(mysql_config --plugindir)
   ```

2. 登录 MySQL 并安装插件：

   ```sql
   -- 安装增量备份插件
   INSTALL PLUGIN MY_INCREMENTAL_BACKUP SONAME 'my_incremental_backup_plugin.so';
   
   -- 安装智能分区插件
   INSTALL PLUGIN MY_INTELLIGENT_PARTITION SONAME 'my_intelligent_partition_plugin.so';
   
   -- 安装数据脱敏插件
   INSTALL PLUGIN MY_DATA_MASKING SONAME 'my_data_masking_plugin.so';
   ```

### 3. 验证插件安装

```sql
SHOW PLUGINS;
```

在输出中应该能看到已安装的插件。

## 使用方法

### 增量备份插件

#### 1. 执行完整备份

```sql
CALL perform_backup('full');
```

#### 2. 执行增量备份

```sql
CALL perform_backup('incremental');
```

#### 3. 配置备份目录

```sql
SET GLOBAL backup_directory = '/path/to/backups';
```

#### 4. 列出所有备份

```sql
CALL list_backups();
```

#### 5. 恢复备份

```sql
CALL restore_backup('backup_name');
```

#### 6. 清理备份

```sql
CALL cleanup_backup('backup_name');
```

#### 7. 验证备份

```sql
CALL validate_backup('backup_name');
```

### 智能分区插件

#### 1. 分析表结构

```sql
CALL analyze_table('table_name');
```

#### 2. 获取分区推荐

```sql
CALL recommend_partitioning('table_name');
```

#### 3. 应用分区策略

```sql
CALL apply_partitioning('ALTER TABLE table_name PARTITION BY RANGE (id) (...)');
```

#### 4. 估计分区效果

```sql
CALL estimate_partition_effect('table_name');
```

#### 5. 监控分区性能

```sql
CALL monitor_partition_performance('table_name');
```

### 数据脱敏插件

#### 1. 添加脱敏规则

```sql
CALL add_masking_rule('rule_name', 'data_type', 'masking_type', 'params');
```

**参数说明：**
- `rule_name`: 规则名称（如 'rule_phone'）
- `data_type`: 数据类型（如 'PHONE', 'ID_CARD', 'EMAIL' 等）
- `masking_type`: 脱敏类型（如 'PARTIAL', 'HASH', 'REPLACE', 'RANDOM'）
- `params`: 规则参数（JSON 格式，如 '{"keep":[3,4]}'）

#### 2. 删除脱敏规则

```sql
CALL remove_masking_rule('rule_name');
```

#### 3. 列出所有脱敏规则

```sql
CALL list_masking_rules();
```

#### 4. 应用脱敏

```sql
CALL apply_masking('sensitive_data', 'data_type');
```

#### 5. 检测敏感数据

```sql
CALL detect_sensitive_data('data');
```

#### 6. 预览脱敏效果

```sql
CALL preview_masking('data', 'data_type', 'masking_type');
```

#### 7. 估计脱敏影响

```sql
CALL estimate_masking_impact('table_name');
```

## 配置选项

### 增量备份插件配置

| 配置项 | 类型 | 默认值 | 说明 |
|-------|------|-------|------|
| backup_directory | 字符串 | '/var/lib/mysql/backups' | 备份存储目录 |
| backup_retention_days | 整数 | 30 | 备份保留天数 |
| backup_compression | 布尔值 | TRUE | 是否启用备份压缩 |
| backup_threads | 整数 | 4 | 备份线程数 |

### 智能分区插件配置

| 配置项 | 类型 | 默认值 | 说明 |
|-------|------|-------|------|
| partition_analysis_enabled | 布尔值 | TRUE | 是否启用自动分析 |
| partition_recommendation_enabled | 布尔值 | TRUE | 是否启用自动推荐 |
| partition_monitoring_interval | 整数 | 3600 | 监控间隔（秒） |
| partition_hot_threshold | 整数 | 80 | 热分区阈值（%） |

### 数据脱敏插件配置

| 配置项 | 类型 | 默认值 | 说明 |
|-------|------|-------|------|
| masking_enabled | 布尔值 | TRUE | 是否启用脱敏 |
| masking_default_type | 字符串 | 'PARTIAL' | 默认脱敏类型 |
| masking_detection_enabled | 布尔值 | TRUE | 是否启用自动检测 |
| masking_logging_enabled | 布尔值 | FALSE | 是否启用脱敏日志 |

## 开发指南

### 编译插件

使用以下命令编译插件：

```bash
# 编译所有插件
g++ -shared -fPIC -o my_incremental_backup_plugin.so my_incremental_backup_plugin.cc
g++ -shared -fPIC -o my_intelligent_partition_plugin.so my_intelligent_partition_plugin.cc
g++ -shared -fPIC -o my_data_masking_plugin.so my_data_masking_plugin.cc
```

### 测试插件

使用提供的测试脚本测试插件功能：

```bash
# 测试增量备份插件
chmod +x test_incremental_backup_plugin.sh
./test_incremental_backup_plugin.sh

# 测试智能分区插件
chmod +x test_intelligent_partition_plugin.sh
./test_intelligent_partition_plugin.sh

# 测试数据脱敏插件
chmod +x test_data_masking_plugin.sh
./test_data_masking_plugin.sh
```

### 添加新插件

1. 创建新的插件源文件（如 `my_new_plugin.cc`）
2. 遵循现有的插件结构和命名约定
3. 实现插件的初始化、核心功能和清理函数
4. 编译并测试插件

## 故障排除

### 插件安装失败

- 确保插件文件位于正确的插件目录
- 确保插件文件有正确的权限
- 检查 MySQL 错误日志获取详细信息

### 插件功能异常

- 检查插件配置是否正确
- 验证相关权限是否足够
- 查看 MySQL 错误日志获取详细信息

### 性能问题

- 调整插件配置参数以优化性能
- 确保服务器资源充足
- 考虑使用批处理模式处理大量数据

## 贡献指南

欢迎对本项目进行贡献！请遵循以下步骤：

1. Fork 本仓库
2. 创建功能分支
3. 实现您的更改
4. 测试您的更改
5. 提交 Pull Request

## 许可证

本项目采用 GNU General Public License v2.0 许可证。详见 LICENSE 文件。

## 作者

- MySQL Server Team

## 联系方式

如有问题或建议，请通过 GitHub Issues 提交。

## 版本历史

### v1.0.0 (2026-01-31)
- 初始版本
- 包含增量备份插件
- 包含智能分区插件
- 包含数据脱敏插件

## 致谢

感谢所有为该项目做出贡献的开发者和测试人员！

---

**注意：** 本项目为开源项目，欢迎社区贡献和反馈。在生产环境中使用前，请确保充分测试插件功能以确保兼容性和稳定性。
