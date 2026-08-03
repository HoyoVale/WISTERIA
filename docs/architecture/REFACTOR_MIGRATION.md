# 模块目录重构迁移说明

完整源码包不需要迁移操作。

从 `WISTERIA(11)` 使用增量补丁时：

```powershell
# 1. 将补丁 ZIP 解压并覆盖到原项目根目录
# 2. 在项目根目录执行
.\script\migrate_module_layout.ps1

# 3. 清理旧构建并验证
Remove-Item .\build -Recurse -Force -ErrorAction SilentlyContinue
.\run.ps1 test
```

迁移脚本只删除 `script/module_layout_removed_paths.txt` 中列出的旧平铺头文件和源文件，不扫描或删除用户资产。

如果新模块文件尚未覆盖到项目，脚本会拒绝执行。
