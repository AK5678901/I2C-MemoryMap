@echo off
chcp 65001 > nul

set "AppName=I2C_MemoryMap"

set "RootDir=%~dp0"
set "BuildExePath=%RootDir%build\ninja-release\i2c_memorymap.exe"
set "ResourcesDir=%RootDir%resources"
set "LiveBridgeDir=%RootDir%saleae-live"
set "ReadmePath=%RootDir%README.md"
set "LicensePath=%RootDir%LICENSE"
set "DistBaseDir=%RootDir%dist"
set "PackageDir=%DistBaseDir%\%AppName%"
set "ZipPath=%DistBaseDir%\%AppName%.zip"

echo === Packaging Started ===

:: 1. 実行ファイルの存在確認
if not exist "%BuildExePath%" (
    echo [ERROR] Executable not found: %BuildExePath%
    goto :error
)

:: 2. 古い出力のクリーンアップ
if exist "%PackageDir%" rmdir /s /q "%PackageDir%"
if exist "%ZipPath%" del /f /q "%ZipPath%"

:: 3. 配布用フォルダの作成
mkdir "%PackageDir%"

:: 4. 実行ファイルのコピー
echo -^> Copying executable...
copy /y "%BuildExePath%" "%PackageDir%\" > nul

:: 5. resources フォルダの中身をコピー
if exist "%ResourcesDir%" (
    echo -^> Copying resources...
    xcopy /s /e /y "%ResourcesDir%\*" "%PackageDir%\" > nul
) else (
    echo [WARNING] Resources folder not found: %ResourcesDir%
)

if exist "%LiveBridgeDir%" (
    echo -^> Copying Logic 2 live bridge...
    xcopy /s /e /y "%LiveBridgeDir%\*" "%PackageDir%\saleae-live\" > nul
)

:: 6. README.md のコピー
if exist "%ReadmePath%" (
    echo -^> Copying README.md...
    copy /y "%ReadmePath%" "%PackageDir%\" > nul
) else (
    echo [WARNING] README.md not found: %ReadmePath%
)

:: 7. LICENSE のコピー
if exist "%LicensePath%" (
    echo -^> Copying LICENSE...
    copy /y "%LicensePath%" "%PackageDir%\" > nul
) else (
    echo [WARNING] LICENSE not found: %LicensePath%
)

:: 8. ZIP圧縮
echo -^> Creating ZIP archive...
powershell -Command "Compress-Archive -Path '%PackageDir%\*' -DestinationPath '%ZipPath%' -Force" > nul

:: 9. コピー先の実行ファイルをその場所（カレントディレクトリ）で起動
echo -^> Launching copied executable...
start "" /d "%PackageDir%" "i2c_memorymap.exe"

echo.
echo === Packaging Completed ===
echo Output: %ZipPath%
goto :end

:error
echo.
echo === Packaging Failed ===
pause
exit /b 1

:end
pause