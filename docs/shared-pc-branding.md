# 共用 PC 包与品牌安装向导

原助理、云助理及后续渠道共用 `product=iyw-claw` 的 PC 制品。每个系统和架构只构建一次 PC 包；名称、Logo 和本机隔离身份由安装向导配置提供。不创建 `iyw-claw-yun` PC 发布，不为每个品牌增加后端接口。

## 配置与构建

现有配置为 `brands/iyw/brand.json` 和 `brands/yun/brand.json`。名称使用 `displayName`，PNG 文件使用相对于配置文件的 `logo` 路径。`brandId` 是稳定的本机隔离标识，发布后不得随名称变化而修改。

```sh
python scripts/prepare-brand.py --config brands/yun/brand.json --version 0.1.372 --output .cache/yun
python scripts/build-candidate.py --kind installer --repository . --brand-directory .cache/yun -- <现有平台构建命令>
```

生成图标需要 Pillow。`prepare-brand.py` 也接受后台已有品牌修订导出的配置；本地配置方式不依赖品牌专用接口。

CI 的 `candidate_release_id` 可绑定同一个已登记的公共 PC 测试发布。向导检查已验证主程序中的 `IYW_INSTALLED_BRAND_V1` 能力标记，不用版本号猜测是否兼容。缺少能力时在部署前明确提示，避免安装后仍显示原助理。Windows 渠道安装使用同一公共发布的 `app_payload_zip`，不另建品牌产品。

指定 `--release-id` 后，向导使用现有版本查询接口的 beta 通道，并核对返回的发布 ID。正式向导使用 stable。后台将同一发布晋级后，候选向导可取得同一 ID 的 stable 制品。若该候选已被其他测试版替代且不在当前 stable 中，向导明确要求取得当前配置。

## 安装与更新

- Windows/Linux：配置和图标位于安装根目录的 `config/branding.json`、`config/branding/`。
- macOS：签名 `.app` 旁边保存 `<品牌名称>.app.branding/branding.json` 与图标。
- PC 启动时读取配置，设置界面名称、窗口和托盘图标、协议、凭据服务及本地目录；无配置时继续作为原助理运行。
- 各品牌使用独立的安装登记、协议、单实例标识和用户目录。原助理沿用 `.iyw-claw`，云助理使用 `.iyw-claw-yun`。
- 通用 Windows 更新向导从目标目录读取原有配置，保留名称和图标。应用更新只替换 `app`，不覆盖配置与数据。
- 固定签名的 Windows/macOS 系统分享入口仍使用原助理身份；渠道实例的手动导入收件箱独立，避免跨品牌读取分享内容。

## 候选发布

PC 候选使用最终 SemVer，GitHub Release 保持 draft/prerelease，测试通过后在现有后台晋级同一发布。不得通过重新构建、改版本或替换签名文件来冒充原制品晋级。

PC 复用现有 stable 查询识别已晋级的发布或更新的正式版本。`promotionChannel` 可设为 `stable` 或 `beta`；用户手动选择的更新通道优先。

CI 为原助理和云助理分别构建 Windows、macOS universal、Linux x64/arm64 向导。Windows 候选输入交给 PC 仓库的签名机，macOS 使用现有 Developer ID 证书。未提供 Apple 公证凭据时，只声明签名完成。

## 验证边界

已执行定向 C++ 语法检查、Python 语法与图标生成检查；PC 只做静态审查和定向 lint，按仓库规则禁止本机构建或启动。

三端 CI、签名产物、双品牌实际安装与升级均以对应流水线结果为准。候选版本尚未登记环境绑定时跳过依赖线上环境的完整安装验收；不能把编译通过等同于完整安装通过。现有线上旧 PC 包不因向导更换 Logo 就自动具备运行时品牌能力。
