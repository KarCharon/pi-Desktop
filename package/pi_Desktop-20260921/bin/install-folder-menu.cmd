@echo off
rem 为当前用户安装文件夹右键菜单，使用脚本所在目录的程序。
"%~dp0pi_Desktop.exe" --install-folder-menu
if errorlevel 1 (
    echo Folder menu installation failed.
) else (
    echo Folder menu installed. On Windows 11, check Show more options.
)
pause
