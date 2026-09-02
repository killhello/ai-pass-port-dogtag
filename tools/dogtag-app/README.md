# 电子狗牌配对 APK

## 功能
- Web Bluetooth 配对
- 前台服务保持 BLE 连接
- WakeLock 防止休眠
- 历史记录保存

## 构建

### 方法 1：Android Studio
1. 用 Android Studio 打开 `dogtag-app` 目录
2. 等待 Gradle 同步完成
3. 点击 Build → Build Bundle(s) / APK(s) → Build APK(s)
4. APK 在 `app/build/outputs/apk/debug/app-debug.apk`

### 方法 2：命令行
```bash
cd dogtag-app
./gradlew assembleDebug
```

## 安装
1. 在手机上启用"开发者选项"
2. 启用"USB 调试"
3. 连接电脑，运行 `adb install app/build/outputs/apk/debug/app-debug.apk`

## 权限
- 蓝牙扫描/连接
- 位置权限（BLE 扫描需要）
- 前台服务
- 通知权限
