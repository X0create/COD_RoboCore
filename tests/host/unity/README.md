# Unity（第三方代码，不修改）

- 来源：<https://github.com/ThrowTheSwitch/Unity>，标签 `v2.7.0`，只取 `src/` 下三个文件和 `LICENSE.txt`
- 许可证：MIT，见 `LICENSE.txt`
- 引入日期：2026-09-27

升级时整体替换这几个文件，并更新下面的 SHA-256：

```text
a6cc4b143075a03317d72c760b5ed67a4a12eeda1242f575464ad23f34042275  unity.c
b30ba4db1e0be1a1f6c862d359d73c91025a114977e41d3dad17103363804334  unity.h
35bffad23ebc533977291e7a848c646027513572268fbfa9bd03d5ec21ee818a  unity_internals.h
ec6cf55f05ba2aa538b9677b2481b9ac14a87c63594fce8a0677d4f71c583980  LICENSE.txt
```

校验：在本目录执行 `sha256sum -c`，把上面四行粘进去（或保存成文件后 `sha256sum -c 文件`）。
