# peony-copy-path

Peony 右键菜单插件：复制选中文件、文件夹或当前目录的完整本地路径。

## 行为

- 选中一个文件/文件夹：复制该项目完整路径。
- 多选：逐行复制所有路径。
- 目录空白处右键：复制当前目录路径。

## 构建

当前系统需要 Peony Qt 开发头文件和 Qt5 构建工具：

```bash
sudo apt install libpeony-dev qtbase5-dev qtbase5-dev-tools qt5-qmake g++
./build.sh
```

产物路径：

```text
plugins/peony-copy-path/build/libpeony-copy-path.so
```

构建完成后，可在 Peony Manager 中打开“复制完整路径”，点击“加载 .so”安装到系统插件目录。
