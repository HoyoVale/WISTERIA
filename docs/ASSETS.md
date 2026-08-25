# 演示资产说明（Demo Assets）

WISTERIA 仓库**不分发** `assets/models/` 与 `assets/motions/` 下的 PMX 模型和 VMD 动作。原因：

1. 这些文件的版权通常属于模型/动作作者，不能由本仓库默认再分发。
2. 完整资产体积很大（当前本地工作区约 10GB 以上），不适合直接进入 Git。

因此 v1.0.0 采用“代码在仓库、资产由用户提供”的方式。请按下面的说明把资产放到约定路径。

## 快速准备（推荐）

```powershell
.\script\setup_demo_assets.ps1 -SourceRoot <包含 models 和 motions 的目录>
```

脚本会把默认 Demo 需要的目录复制到 `assets/` 下。源目录可以是以下两种形态之一：

```text
<SourceRoot>/models/...        ← 优先
<SourceRoot>/motions/...

或者

<SourceRoot>/assets/models/...
<SourceRoot>/assets/motions/...
```

如果源目录不包含某个可选模型/场景，脚本会跳过并提示，不影响默认 Demo。

## 默认 Demo 需要的文件

### 角色模型（必需）

```text
assets/models/mmd/蕾米埃尔-黑/蕾米埃尔-黑.pmx
assets/models/mmd/蕾米埃尔-黑/        ← PMX 引用的纹理、toon、spa 等同目录资源
```

### 动作与相机（动作必需，相机可选）

```text
assets/motions/梦的翅膀/梦的翅膀motion.vmd
assets/motions/梦的翅膀/梦的翅膀camera.vmd   ← 缺失时只影响相机轨，程序仍可运行
```

### 场景模式（可选，使用 `--scene` 时）

```text
assets/models/mmd/随便观/随便观.pmx
```

### 备用模型（可选，使用 `--alternate-model` 时）

```text
assets/models/mmd/叶瞬光皮肤_pmx/蕾米埃尔-黑.pmx
```

## 手工放置

不执行脚本也可以，按上表创建目录并复制文件。角色 PMX 所在目录中的贴图
（`*.png` / `*.bmp` / `*.tga` 等）通常必须与 PMX 保持相对位置，建议整个目录复制。

## 资产缺失时的行为

- 默认角色 PMX 缺失：程序启动时给出明确错误，提示运行
  `setup_demo_assets.ps1` 或改用 `--ground-lab`。
- 默认 VMD 动作缺失：打印警告并以无动作状态继续，不中断程序。
- 默认 VMD 相机缺失：打印警告并继续，用户可自由控制相机。

不依赖外部资产的运行方式：

```powershell
.\run.ps1 run -ApplicationArguments '--ground-lab'
```

## 授权提醒

将任何 PMX/VMD 放入本仓库工作区或发布包之前，请确认：

- 模型/动作作者的使用条款允许该用途；
- 是否允许再分发、是否要求署名、是否禁止商业使用；
- 若不允许再分发，请保持文件在本地、不入库、不打入发布压缩包。

`WISTERIA.zip` 与 `assets/models/` 已在 `.gitignore` 中忽略。
