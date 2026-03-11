---
paths:
  - ".github/**/*"
  - "CMakeLists.txt"
---

# CI/빌드 규칙

- CMake 옵션 변경 → CI yml 양쪽 반영
- `windows-test.yml`: 테스트 스텝 의존성 확인
- `build-windows.yml`: 빌드 옵션 확인
- 필수 옵션: `OCCT_PORTABLE=ON`, `OCCT_CONSOLE=ON` (CI용)
- GPU/PSU 테스트: exit code 2 허용 (하드웨어 없음)
