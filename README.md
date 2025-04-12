# DRCA 项目安装指南

## 1. 安装 SystemC

```bash
wget https://github.com/accellera-official/systemc/archive/refs/tags/2.3.3.tar.gz
tar -zxvf 2.3.3.tar.gz
cd systemc-2.3.3/
mkdir tmp && cd tmp
../configure --prefix=/path/to/install/systemc-2.3.3 CXXFLAGS="-std=c++17"
sudo make -j8
make install
```

### 配置环境变量
在 `~/.bashrc` 文件中添加：
```bash
export SYSTEMC_HOME=/path/to/install/systemc-2.3.3/
export LD_LIBRARY_PATH=/path/to/install/systemc-2.3.3/lib-linux64/:$LD_LIBRARY_PATH
```

## 2. 安装依赖项

### JSON 库
```bash
git clone --branch=v3.11.3 --single-branch --depth=1 https://github.com/nlohmann/json.git
cd json
mkdir build && cd build
cmake ..
make
sudo make install
```

### 多媒体库
```bash
# 安装 SFML
sudo apt-get install libsfml-dev

# 安装 CAIRO
sudo apt install libcairo2-dev

# 安装 X11（服务器环境可能需要）
sudo apt install xorg

# 安装字体（源文件已包含必要的 ttf 文件）
sudo apt install ttf-mscorefonts-installer  # 需要在弹出界面选择 OK
```

## 3. 编译项目
```bash
cd /root/fdh/drca/src
mkdir build && cd build
cmake ..
make -j8
```

## 4. 运行
```bash
./train_gpt2 --config-file /root/fdh/drca/src/llm/test/config_gpt2_small_tp_24_new.json --use-dramsys true
```

