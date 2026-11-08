@echo off
rem ============================================================
rem 重新打包 WF280 测试工具.exe
rem 需要：Python 3.8+（Windows 官方版，自带 tkinter）
rem       pip install pyserial pyinstaller
rem ============================================================
chcp 65001 >nul
cd /d "%~dp0"

python -m PyInstaller --noconfirm --clean --onefile --windowed ^
  --name "WF280测试工具" ^
  --distpath "." --workpath "build" --specpath "build" ^
  "src\wf280_tool.py"

if errorlevel 1 (
  echo.
  echo [失败] 打包没成功，检查 Python 环境里是否装了 pyinstaller。
  pause
  exit /b 1
)

echo.
echo [完成] 已生成 WF280测试工具.exe
pause
