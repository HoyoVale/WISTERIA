# R2.0 Phase 0C — CPU Asset / GPU Realization Split 基线（2026-08-10）

> 状态：**IN PROGRESS — Step 1..5 IMPLEMENTED / VALIDATED**
> 前置：R2.0 0B CLOSED（`d05f2a6`）。
> 范围：Mesh / Texture / Material / EnvironmentMap 的 CPU asset 与 GPU
> realization 分离；per-device RenderResourceCache；动态 geometry 实例隔离。

## 1. Step 1 — Mesh CPU/GPU 物理拆分（已完成）

### 改动

```text
src/rendering/backend/opengl/mesh_gpu_resource.hpp/.cpp（新增）
  MeshGpuResource：
  - VBO/EBO 所有权（per-instance realization）
  - Attach / ConfigureVertexArray / Draw / UploadDynamicFrame 的 GL 逻辑
  - device 可空（stable render 组合路径延迟 attach，旧 VBO 行为）

include/wisteria/rendering/mesh.hpp + src/rendering/mesh.cpp
  Mesh = CPU semantic asset：
  - 保留 DefaultModelData / morph targets / source indices / bounds /
    skinning debug 数据 / RebuildInterleavedVertices（纯 CPU）
  - GPU 成员（vbo/ebo/attached）移除；
    持有 unique_ptr<MeshGpuResource>（前向声明，不 include backend 头）
  - Attach/Configure/Draw/Upload 转发到 realization
  - CloneForInstance：每个实例 clone 创建独立 realization
    （runtime-deformed geometry 永不跨实例共享）
  - device 引用保留仅用于 clone 时创建新 realization（0C 过渡）

CMakeLists.txt：+mesh_gpu_resource.cpp
tests/integration_tests.cpp：+TestR2MeshGpuRealizationSplit
```

### 验证

```text
TestR2MeshGpuRealizationSplit：
  - asset 数据构造后不变
  - 两个 clone 有独立 lifetime token（实例隔离）
  - 各自 Attach 成功
  - 各自 UploadDynamicFrame（不同位置）后 CPU asset 数据不变
  - vertex count 正确

四矩阵回归（R1 像素行为不变）：
  Windows CORE 11/11、Windows FULL 12/12
  WSL CORE 13/13、WSL FULL 14/14
  ABI 94 legacy + 30 stable
```

## 2. 0C 剩余子步（规划）

```text
Step 6  RenderResourceCache：
        per RenderDevice 的 static/shared realization 归并；
        dynamic 仍按 ModelInstance 隔离（0B/0C 契约）

Step 7  ShaderStageDesc 再审（0C/0D watchpoint）：
        backend 负责 shader/pipeline realization，
        neutral 层不得出现 if (OpenGL) GLSL else SPIR-V
```

## 3. 复审注意事项

1. Mesh 公共头不再持有 VBO/EBO（GPU 细节移出 CPU asset）；
   MeshGpuResource 属于 OpenGL backend 路径（Gate B）。
2. 动态 geometry 隔离由设计保证：clone 各自 unique_ptr realization；
   未来 RenderResourceCache 归并 static 时不得合并 dynamic。
3. Mesh 仍保留 device 引用（clone 需要）——0C 后期由
   RenderResourceCache 替代，属已记录的过渡债。
4. 每步都保持 R1 像素回归全绿 + ABI 30 stable。

## 4. Step 2 — IndexFormat 语义化（已完成）

```text
include/wisteria/rendering/model.hpp
  - ModelData::IndexGLType()（GLenum）删除
  - 新增 ModelData::IndexFormatValue()：
    sizeof(IndexType)==1 → IndexFormat::Uint8
    ==2 → Uint16；==4 → Uint32
  - include 清理：移除 ebo.hpp/vao.hpp（CPU asset 不再需要）；
    render_device.hpp 提供 IndexFormat（neutral）
  - model.hpp 零直接 GL 符号（仅注释提及）

include/wisteria/rendering/render_device.hpp
  - IndexFormat 枚举补回（0B Final Fix 重写时意外丢失，
    本轮发现并修复：GCC 编译暴露，Windows 增量构建未触发）

src/rendering/backend/opengl/mesh_gpu_resource.cpp
  - MapIndexFormat()：IndexFormat → GL_UNSIGNED_BYTE/SHORT/INT
  - Draw() 使用 IndexFormatValue() + MapIndexFormat()
```

验证：

```text
IndexGLType 全仓库移除（0 引用）
model.hpp 零直接 GL 类型/函数
四矩阵：Windows CORE 11/11、FULL 12/12、
        WSL CORE 13/13、FULL 14/14（全部重新构建后）
ABI：94 legacy + 30 stable
```

## 5. Step 3 — Texture split（已完成）

```text
src/rendering/backend/opengl/texture_gpu_resource.hpp/.cpp（新增）
  TextureGpuResource：
  - GL texture 对象所有权 + Bind/Unbind/UploadDecodedPixels/
    MaxUnits/ValidateUnit/Configure（GL 全部逻辑）
  - device 可空（与旧 Texture 析构行为一致）

include/wisteria/rendering/texture.hpp + src/rendering/texture.cpp
  Texture = CPU asset + 转发：
  - 保留 TextureData（file/encoded/RGBA8）+ stb 解码（CPU）
  - GL 成员删除；持有 unique_ptr<TextureGpuResource>
  - Upload*/Attach/Bind/Unbind/GetTexture 转发
```

## 6. Step 4 — Material split（已完成）

```text
src/rendering/backend/opengl/material_gpu_resource.hpp/.cpp（新增）
  MaterialGpuResource：
  - programCache/program/texture bindings 所有权
  - Attach（program acquire + texture attach）/Bind/Unbind/HasTexture

include/wisteria/rendering/material.hpp + src/rendering/material.cpp
  Material = semantic MaterialData + 转发：
  - 全部 getter 保留（读 data）
  - GPU 成员（programCache/program/textures）删除；
    持有 unique_ptr<MaterialGpuResource>
  - 4 个构造重载委托到 gpu 创建
  - ProgramCache 共享 ownership 保持（R1.9 修复不变）
```

## 7. Step 5 — EnvironmentMap split（已完成）

```text
src/rendering/backend/opengl/environment_gpu_resource.hpp/.cpp（新增）
  EnvironmentMapGpuResource：
  - IBL cubemap/BRDF LUT/capture FBO/RBO/skybox program/geometry 所有权
  - Attach（含 GL 限制验证 + ScopedOpenGlState 事务）/
    BindIrradiance/BindPrefilter/BindBrdfLut/ConfigureSkyboxVertexArray/
    DrawSkybox（接收 live data：intensity/drawSkybox 实时值）

include/wisteria/rendering/environment.hpp + src/rendering/environment.cpp
  EnvironmentMap = EnvironmentMapData + 转发：
  - 保留分辨率/power-of-two/mip/intensity 验证（CPU）
  - 10 个 GLuint + shader/program 成员删除；
    持有 unique_ptr<EnvironmentMapGpuResource>
  - getter/setter（Intensity/DrawSkybox/Data/MaxReflectionLod）保留
```

## 8. Step 3-5 验证

```text
四矩阵（全部重新构建后）：
  Windows CORE 11/11、Windows FULL 12/12
  WSL CORE 13/13、WSL FULL 14/14
ABI：94 legacy + 30 stable
R1 像素回归：stable render / engine==stable / IBL 路径全绿

Gate B 检查：
  Texture/Material/EnvironmentMap 公共头不再持有 GL 资源成员；
  全部 GPU realization 位于 backend/opengl/
```
