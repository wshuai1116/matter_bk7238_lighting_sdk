# Sparkleiot BK7238 Matter 灯带 SDK

本仓库用于 Sparkleiot 的 BK7238 RGB 灯带固件开发和交付。灯带与插座 SDK 分仓维护；本仓库不包含插座产品工程。

## 技术栈

- 芯片平台：Beken BK7238
- 方案：移远通信（Quectel）方案
- 协议与框架：Matter / connectedhomeip
- 芯片 SDK 与 RTOS：Beken ARMINO / FreeRTOS
- Matter 应用：`connectedhomeip/examples/lighting-app/beken`

## 构建

在仓库根目录运行：

```bash
./build.sh r FGM842D R01 V01 LIGHT
```

`r` 会使用仓库内的工具链及依赖执行构建；全新检出时会从头生成缺失的构建产物。固件输出位于 `out/`。

构建依赖包括仓库内 ARM GCC 工具链、GN/Ninja、Beken SDK、Quectel 适配层、Matter 源码和 ZAP 代码生成工具。首次检出或 LFS 文件未下载完整时，安装 Git LFS 后执行：

```bash
git lfs install
git lfs pull
```

`connectedhomeip/` 下保留了上游 `LICENSE`、`NOTICE` 和各组件许可证；再分发时请一并保留。
