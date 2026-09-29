@echo off
rem 只移除 Pi Desktop 为当前用户注册的文件夹右键菜单。
"%~dp0pi_Desktop.exe" --uninstall-folder-menu
if errorlevel 1 (
    echo Folder menu removal failed.
) else (
    echo Folder menu removed.
)
pause
