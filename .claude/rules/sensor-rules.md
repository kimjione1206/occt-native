---
paths:
  - "src/monitor/**/*"
---

# 센서 수정 규칙

- `#ifdef Q_OS_WIN` / `Q_OS_MACOS` / `Q_OS_LINUX` 가드 필수
- 3개 플랫폼 모두 컴파일 가능 확인
- Windows: WMI COM 캐싱 (wmi_svc_root_wmi_, wmi_svc_cimv2_), GetSystemTimes, NVML/ADL
- macOS: NSProcessInfo, IOKit Battery, Mach host_processor_info
- Linux: sysfs hwmon, /proc/stat
