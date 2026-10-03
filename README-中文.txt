AC8 MouseFlight 0.2.28：演出相机让出与后台开销优化

安装方式：
1. 双击install.cmd
2. 选择AC8的根目录
3. 在steam的AC8属性中，为启动选项复制粘贴以下参数：

cmd /d /c "set EOS_USE_ANTICHEATCLIENTNULL=1&& %command% -anticheat_settings=AC8MouseAim_Offline.json"

并确保“已选启动选项”改为“玩Ace Combat 8”

卸载方式：
1. 双击Quick-Uninstall.cmd、选择游戏根目录。
2. 去掉steam属性中的启动项参数


特殊操作：
F8-切换原生操作/摄像机与鼠标飞控

常见问题：
提示An unrelated UE4SS/dwmapi installation was found.
移动或卸载已安装的其他mod和UE4SS，再进行安装。