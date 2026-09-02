# Nvidia App Cleaner

一款简单的 Windows NVIDIA App 缓存清理工具。

A simple NVIDIA App cache cleaner for Windows.

## 中文

- 扫描 NVIDIA App 遗留的驱动安装包和更新缓存。
- 按驱动版本选择需要删除的回滚包。
- 可选清理 `Installer2` 和 NGX 模型。
- 可修复安装包丢失后 NVIDIA App 下载卡在 100% 的问题。
- 扫描不需要管理员权限；只有清理和修复时请求 UAC。
- 单文件 EXE，不常驻后台，不创建服务或计划任务。
- 支持中英文界面，并跟随 Windows 深浅色主题。

扫描可以直接执行；清理或修复前请完全退出 NVIDIA App。普通清理不会保留备份；“下载修复”只备份被修改的状态 JSON。

## English

- Scans driver installers and update caches left by NVIDIA App.
- Lets you select rollback packages by driver version.
- Optionally cleans `Installer2` and downloaded NGX models.
- Repairs downloads stuck at 100% after their installer was deleted.
- Scanning runs without administrator rights; cleanup and repair request UAC.
- Ships as one EXE with no background process, service, or scheduled task.
- Supports English and Simplified Chinese and follows the Windows app theme.

Scanning can run at any time. Exit NVIDIA App completely before cleanup or repair. Normal cleanup keeps no backup; Download Repair only backs up the status JSON it modifies.

## License

MIT License. This project is not affiliated with or endorsed by NVIDIA Corporation.
