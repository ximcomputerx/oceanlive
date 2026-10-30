; OceanLive 的 Inno Setup 安装脚本。
; 先用 build.bat 编出 Release。按当前用户安装，不需要管理员。

#define MyAppName "OceanLive" ; 安装界面上的产品名
#define MyAppVersion "1.0.0" ; 安装包版本保持 1.0.0
#define MyAppExe "OceanLive.exe" ; 要启动的主程序
#define BuildDir "..\build\Release" ; 已编好的 Release 目录

[Setup] ; 安装程序属性
AppId={{8B5E6A1E-4C7A-4E0E-9B1A-0E6F4A8D2C11} ; 应用标识保持不变
AppName={#MyAppName} ; 使用上面的产品名
AppVersion={#MyAppVersion} ; 使用上面的版本号
DefaultDirName={localappdata}\OceanLive ; 装到当前用户的本地目录
PrivilegesRequired=lowest ; 不请求管理员权限
OutputBaseFilename=OceanLive-Setup ; 生成的安装包文件名
Compression=lzma2 ; 使用 lzma2 压缩
SolidCompression=yes ; 打开固实压缩
ArchitecturesAllowed=x64compatible ; 只允许 64 位系统
ArchitecturesInstallIn64BitMode=x64compatible ; 按 64 位模式安装
WizardStyle=modern ; 使用现代安装向导
SetupIconFile=..\resources\icons\app.ico ; 安装包和卸载程序的图标

[Files] ; 要复制的文件
Source: "{#BuildDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion ; 递归复制整个 Release 目录

[Icons] ; 快捷方式
Name: "{autoprograms}\OceanLive"; Filename: "{app}\{#MyAppExe}" ; 开始菜单快捷方式
Name: "{autodesktop}\OceanLive"; Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon ; 仅在勾选任务时创建桌面快捷方式

[Tasks] ; 可选任务
Name: "desktopicon"; Description: "创建桌面快捷方式"; Flags: unchecked ; 默认不勾选，说明文字保持原文

[Run] ; 安装结束时
Filename: "{app}\{#MyAppExe}"; Description: "启动 OceanLive"; Flags: nowait postinstall skipifsilent ; 不卡住向导，静默安装时不启动
