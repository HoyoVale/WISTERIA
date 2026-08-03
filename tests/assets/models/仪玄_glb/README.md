# 仪玄本地测试模型

这些 GLB 由用户提供的官方 MMD 压缩包在本地转换得到，仅用于本项目的私有、非商业学习和测试。
请保留原压缩包中的 `readme【一定要看】.txt`，不要提交或二次分发这些模型文件。

## 文件

- `仪玄.glb`：默认服装，静态模型，21 个材质分部
- `仪玄皮肤.glb`：皮肤服装，静态模型，25 个材质分部
- `鸟.glb`：独立静态模型，1 个材质分部
- `../仪玄_obj/仪玄.obj`：由默认服装 GLB 导出的静态 OBJ 测试模型
- `../仪玄_obj/仪玄.mtl`：OBJ 的 21 个材质定义
- `../仪玄_obj/*.png`：MTL 通过相对路径引用的 6 张外部纹理

三个 GLB 都内嵌了基础颜色贴图，不依赖外部纹理文件。原 PMX 中的骨骼、蒙皮、形态键和 MMD
物理没有包含在这些静态版本中；完整数据仍保留在原 ZIP 中。

## 重新转换

当前 WSL Blender 3.0.1 可配合 MMD Tools v2.0.0：

```bash
git clone --depth 1 --branch v2.0.0 \
  https://github.com/MMD-Blender/blender_mmd_tools.git \
  /tmp/blender_mmd_tools

blender --background --python script/convert_mmd.py -- \
  --addon-dir /tmp/blender_mmd_tools \
  --input /path/to/model.pmx \
  --output /path/to/model.glb
```

若以后需要保留骨骼、蒙皮和形态键，在命令末尾添加 `--rigged`。这种 GLB 会明显更大，且当前
FGGP 静态模型管线暂时不会使用这些数据。

## 生成 OBJ 外部纹理测试资源

OBJ 不保存骨骼、蒙皮和节点变换层级。下面的命令只转换静态网格、法线、UV、材质和外部纹理：

```bash
blender --background --python script/convert_glb_to_obj.py -- \
  --input tests/assets/models/仪玄/仪玄.glb \
  --output tests/assets/models/仪玄_obj/仪玄.obj
```
