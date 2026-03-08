# OCCT Native vs Real OCCT - 기능 비교 분석

## 종합 평가

| 항목 | 실제 OCCT | occt-native | 구현율 |
|------|-----------|-------------|--------|
| CPU 테스트 | ★★★★★ | ★★★★☆ | **85%** |
| GPU 테스트 | ★★★★★ | ★★★☆☆ | **65%** |
| RAM 테스트 | ★★★★★ | ★★★★☆ | **80%** |
| Storage 테스트 | ★★★★☆ | ★★★☆☆ | **70%** |
| PSU 테스트 | ★★★★☆ | ★★★☆☆ | **70%** |
| 모니터링 | ★★★★★ | ★★★☆☆ | **55%** |
| 스케줄링 | ★★★★☆ | ★★★★☆ | **80%** |
| 인증서 | ★★★★★ | ★★★☆☆ | **60%** |
| 리포트 | ★★★★★ | ★★★★☆ | **75%** |
| UI/UX | ★★★★★ | ★★★☆☆ | **60%** |
| CLI | ★★★★☆ | ★★★★☆ | **80%** |
| 플랫폼 | ★★★★☆ | ★★★★☆ | **85%** |
| **전체** | | | **~72%** |

---

## 1. CPU 테스트 비교

### 실제 OCCT
- **CPU:OCCT 테스트**: Normal/Extreme 모드, Small/Medium/Large 데이터셋
- **CPU:Linpack**: AMD64, 2012, Large 버전 (AVX2 지원)
- **명령어셋**: Auto/SSE/AVX/AVX2/AVX-512
- **부하 패턴**: Variable (10분마다 오퍼랜드 변경), Steady
- **스레드**: Auto (코어별 고정 어피니티), Fixed (사용자 지정), Core Cycling
- **에러 검출**: 코어별 에러 추적, ThreadX.txt 파일 생성
- **사이클 제어**: 시작 사이클 선택 가능 (~10분/사이클)
- **벤치마크**: SSE Single/Multi, AVX Single/Multi → 온라인 리더보드 업로드

### occt-native
- **스트레스 모드**: AVX2_FMA, AVX512_FMA, SSE_FLOAT, LINPACK, PRIME, ALL ✅
- **명령어셋**: SSE/AVX2/AVX-512/NEON (Apple Silicon) ✅
- **부하 패턴**: Variable/Steady ✅
- **스레드**: Auto + 사용자 지정 + 코어 어피니티 (Win/Linux/macOS) ✅
- **에러 검출**: 결정론적 FMA 체인, 비트 단위 비교, 코어별 추적 ✅
- **Linpack**: 타일드 naive DGEMM + 잔차 검증 ✅ (OpenBLAS 옵션 있으나 미활성)
- **Prime**: Miller-Rabin + Lucas-Lehmer ✅

### 차이점
| 기능 | 실제 OCCT | occt-native | 상태 |
|------|-----------|-------------|------|
| Normal/Extreme 모드 구분 | ✅ | ❌ | 미구현 |
| Small/Medium/Large 데이터셋 | ✅ | ❌ | 미구현 |
| Core Cycling 스레드 모드 | ✅ | ❌ | 미구현 |
| 사이클 제어 (시작 사이클) | ✅ | ❌ | 미구현 |
| 온라인 벤치마크 리더보드 | ✅ | ❌ | 미구현 |
| ThreadX.txt 에러 로그 파일 | ✅ | ❌ (메모리에만 저장) | 부분 구현 |
| Apple Silicon NEON 지원 | ❌ | ✅ | 우리가 우위 |
| Linux 지원 | ✅ (v14+) | ✅ | 동등 |
| AVX2/512 실제 SIMD | ✅ | ✅ | 동등 |
| Variable/Steady 패턴 | ✅ | ✅ | 동등 |
| 코어별 에러 추적 | ✅ | ✅ | 동등 |

---

## 2. GPU 테스트 비교

### 실제 OCCT
- **GPU:3D Standard**: Vulkan 기반 커스텀 3D 엔진, 셰이더 복잡도 조절, 아티팩트 자동 감지
- **GPU:3D Adaptive**: Unreal Engine 기반, Variable/Switch 모드, 전압-주파수 커브 전체 테스트
- **GPU:VRAM**: CUDA Memtest 기반, VRAM 95% 할당, 비트 에러 검출
- **GPU Compute**: OpenCL 워크로드 (Enterprise+)
- **멀티 GPU**: 다른 브랜드 GPU 동시 테스트
- **코일 울림 감지**: v15 실험적 기능
- **해상도/FPS**: 해상도, 전체화면, FPS 제한 설정 가능

### occt-native
- **OpenCL 백엔드**: FP32/FP64 행렬 곱셈, FMA, Trig, VRAM 테스트 ✅ (조건부 컴파일)
- **Vulkan 백엔드**: 오프스크린 렌더링, 5단계 셰이더 복잡도 ✅ (조건부 컴파일)
- **Vulkan Adaptive**: Variable (+5%/20s), Switch (20%↔80%) ✅
- **아티팩트 감지**: 픽셀 비교 기반 ✅ (Vulkan 경로만)
- **VRAM 테스트**: Walking ones/zeros, 주소 테스트, 교번 패턴 ✅
- **멀티 GPU**: MultiGpuManager 존재 ✅

### 차이점
| 기능 | 실제 OCCT | occt-native | 상태 |
|------|-----------|-------------|------|
| Unreal Engine 기반 Adaptive | ✅ | ❌ (자체 구현) | 다른 접근 |
| 해상도/전체화면 설정 | ✅ | ❌ | 미구현 |
| FPS 제한 설정 | ✅ | ❌ | 미구현 |
| 코일 울림 감지 | ✅ | ❌ | 미구현 |
| GPU 사용량 제한 설정 | ✅ | ❌ | 미구현 |
| 디스플레이 없는 GPU 테스트 | ✅ | ✅ (오프스크린) | 동등 |
| OpenCL compute 커널 | ✅ (Enterprise+) | ✅ | 동등 |
| VRAM 비트 에러 검출 | ✅ | ✅ | 동등 |
| Vulkan 3D 렌더링 | ✅ | ✅ (조건부) | 동등 |
| 셰이더 복잡도 조절 | ✅ | ✅ (5단계) | 동등 |

---

## 3. RAM 테스트 비교

### 실제 OCCT
- **메모리 할당**: 70-95% 설정 가능
- **명령어셋**: Auto/SSE/AVX/AVX2
- **에러 검출**: 통합 에러 체크 알고리즘

### occt-native
- **패턴 6종**: March C-, Walking Ones/Zeros, Checkerboard, Random (xoshiro256**), Bandwidth ✅
- **메모리 할당**: 1-95% 설정, VirtualLock/mlock 페이지 잠금 ✅
- **대역폭 측정**: AVX2 non-temporal streaming stores ✅

### 차이점
| 기능 | 실제 OCCT | occt-native | 상태 |
|------|-----------|-------------|------|
| 메모리 테스트 명령어셋 선택 | ✅ (SSE/AVX/AVX2) | ❌ (자동만) | 미구현 |
| March C- 알고리즘 | ✅ | ✅ | 동등 |
| 페이지 잠금 | ✅ | ✅ (3개 OS) | 동등 |
| 에러 검출 | ✅ | ✅ (64비트 비교) | 동등 |
| 대역폭 측정 | ✅ | ✅ (AVX2 스트리밍) | 동등 |

---

## 4. Storage 테스트 비교

### 실제 OCCT
- v15에서 안정 릴리스
- CrystalDiskMark 스타일 벤치마크
- 비교 데이터베이스 구축 중

### occt-native
- SEQ_READ/WRITE, RAND_READ/WRITE, MIXED (70/30) ✅
- Direct I/O (O_DIRECT, F_NOCACHE, FILE_FLAG_NO_BUFFERING) + 폴백 ✅
- 멀티스레드 큐 깊이 ✅

### 차이점
| 기능 | 실제 OCCT | occt-native | 상태 |
|------|-----------|-------------|------|
| CrystalDiskMark 호환 벤치마크 | ✅ | ❌ | 미구현 |
| 온라인 비교 데이터베이스 | ✅ | ❌ | 미구현 |
| Direct I/O 폴백 | ✅ | ✅ | 동등 |
| 큐 깊이 설정 | ✅ | ✅ | 동등 |

---

## 5. PSU 테스트 비교

### 차이점
| 기능 | 실제 OCCT | occt-native | 상태 |
|------|-----------|-------------|------|
| CPU+GPU 동시 부하 | ✅ | ✅ | 동등 |
| Steady/Spike/Ramp 패턴 | ✅ (Steady/Variable/Spike) | ✅ (Steady/Spike/Ramp) | 동등 |
| 전압 안정성 커브 표시 | ✅ | ❌ | 미구현 |
| PSU 보호 모드 감지 | ✅ | ✅ (급정지 감지) | 동등 |
| 모든 GPU 사용 옵션 | ✅ | ✅ | 동등 |

---

## 6. 모니터링 시스템 비교

### 실제 OCCT
- **HwInfo 엔진**: 200+ 센서 접근
- **업데이트 주기**: 2초 기본
- **값 표시**: 현재/최소/평균/최대
- **알람**: 센서별 커스텀 임계값
- **자동 중지**: 임계값 초과 시 테스트 중단
- **WHEA 에러**: 드라이버 레벨 하드웨어 에러 감지
- **데이터 로깅**: 날짜별 폴더 구조, PNG 그래프 저장

### occt-native
- **센서 백엔드**: sysfs (Linux), IOKit (macOS), WMI (Windows), NVML ✅
- **ADL (AMD)**: 라이브러리 로드만, 실제 데이터 읽기 없음 ⚠️ STUB
- **WHEA**: Windows 이벤트 로그 구독 ✅ (Windows 전용)
- **SafetyGuardian**: CPU 95°C, GPU 90°C, CPU 300W 초과 시 자동 중지 ✅
- **센서 모델**: 계층적 HardwareNode 트리 ✅

### 차이점
| 기능 | 실제 OCCT | occt-native | 상태 |
|------|-----------|-------------|------|
| HwInfo 통합 (200+ 센서) | ✅ | ❌ (자체 구현) | 큰 차이 |
| 센서별 커스텀 알람 | ✅ | ❌ (고정 임계값만) | 미구현 |
| 현재/최소/평균/최대 표시 | ✅ | ✅ (min/max만) | 부분 구현 |
| 테스트 후 PNG 그래프 저장 | ✅ | ❌ | 미구현 |
| 날짜별 데이터 저장 구조 | ✅ | ❌ | 미구현 |
| WHEA 에러 감지 | ✅ | ✅ (Windows) | 동등 |
| 안전 자동 중지 | ✅ | ✅ (SafetyGuardian) | 동등 |
| AMD ADL 센서 | ✅ | ⚠️ STUB | 미완성 |
| macOS SMC 직접 접근 | N/A | ❌ (IOKit만) | 제한적 |

---

## 7. 인증서 비교

### 실제 OCCT
- **4단계 티어**: Bronze (~1-2시간), Silver (~3-4시간), Gold (~6-8시간), Platinum (~12시간)
- **인증서 유형**: System, CPU, GPU, Memory 별도
- **온라인 공유**: ocbase.com에서 고유 URL로 공유 가능
- **커스텀 인증서**: Enterprise+ 전용
- **에러 없이 완료해야 인증 발급**

### occt-native
- **4단계 티어**: Bronze, Silver, Gold, Platinum ✅
- **생성 형식**: JSON, HTML, PNG ✅
- **SHA-256 해시**: 위변조 감지 ✅
- **프리셋 스케줄**: 각 티어별 사전 정의 ✅

### 차이점
| 기능 | 실제 OCCT | occt-native | 상태 |
|------|-----------|-------------|------|
| 4단계 티어 | ✅ | ✅ | 동등 |
| 온라인 공유 URL | ✅ | ❌ | 미구현 (서버 필요) |
| CPU/GPU/Memory 별도 인증서 | ✅ | ❌ (시스템만) | 미구현 |
| 커스텀 인증서 | ✅ (Enterprise+) | ❌ | 미구현 |
| HTML/PNG 인증서 생성 | ✅ | ✅ | 동등 |
| 해시 검증 | ✅ | ✅ (SHA-256) | 동등 |

---

## 8. 리포트/데이터 내보내기 비교

| 기능 | 실제 OCCT | occt-native | 상태 |
|------|-----------|-------------|------|
| PNG 그래프 | ✅ (Patreon/Steam) | ✅ | 동등 |
| CSV 데이터 | ✅ (Pro+) | ✅ | 동등 |
| JSON 전체 데이터 | ✅ (Enterprise) | ✅ | 동등 |
| HTML 대화형 리포트 | ✅ (Enterprise) | ✅ | 동등 |
| 리포트 비교 도구 | ✅ (Enterprise) | ❌ | 미구현 |

---

## 9. UI/UX 비교

| 기능 | 실제 OCCT | occt-native | 상태 |
|------|-----------|-------------|------|
| 3섹션 레이아웃 | ✅ (Testing/Monitoring/SysInfo) | ✅ (사이드바 3그룹) | 동등 |
| 다크 테마 | ✅ | ✅ (GitHub 스타일) | 동등 |
| 커스텀 스킨 | ✅ (LTT, Corsair 등) | ❌ | 미구현 |
| Material Design | ✅ (v5.0+) | ❌ | 미구현 |
| 동시 테스트 실행 | ✅ (여러 테스트 병렬) | ❌ (단일 테스트만) | 미구현 |
| Monitoring Only 모드 | ✅ | ❌ (UI만 있고 독립 실행 불가) | 부분 구현 |
| 크기 조절 가능 창 | ✅ | ✅ | 동등 |
| 메뉴바 | ✅ | ✅ (File/Test/Help) | 동등 |

---

## 10. CLI / 자동화 비교

| 기능 | 실제 OCCT | occt-native | 상태 |
|------|-----------|-------------|------|
| CLI 헤드리스 모드 | ✅ (Enterprise) | ✅ (--cli) | 동등 |
| JSON 스케줄 파일 | ✅ (Enterprise) | ✅ | 동등 |
| 커스텀 테스트 스케줄 | ✅ (Enterprise) | ✅ (8개 프리셋 + 커스텀) | 동등 |
| 종료 코드 | ✅ | ✅ (0/1/2) | 동등 |
| 원격 진단 | ✅ (Enterprise+) | ❌ | 미구현 |
| 디바이스 히스토리 | ✅ (Enterprise+) | ❌ | 미구현 |

---

## 11. 플랫폼 지원 비교

| 기능 | 실제 OCCT | occt-native | 상태 |
|------|-----------|-------------|------|
| Windows | ✅ (주력) | ✅ | 동등 |
| Linux | ✅ (v14+) | ✅ | 동등 |
| macOS | ❌ | ✅ (Apple Silicon 포함) | **우리가 우위** |
| USB 포터블 모드 | ✅ | ✅ (CPack ZIP) | 동등 |
| Steam 배포 | ✅ | ❌ | 미구현 |
| 자동 업데이트 | ✅ | ❌ | 미구현 |

---

## 12. 라이선스 모델 비교

### 실제 OCCT
| 에디션 | 가격 | 주요 제한 |
|--------|------|-----------|
| Free | 무료 | 1시간 제한, 10초 지연, CSV/JSON 없음 |
| Patreon/Steam | 후원/구매 | 무제한, 인증서, PNG |
| Pro | 유료 | CSV, 상업 사용 |
| Enterprise | 유료 | CLI, HTML/JSON, 리포트 비교 |
| Enterprise+ | 유료 | 원격 진단, 클라우드 대시보드 |

### occt-native
- **완전 무료, 오픈 소스 구조**
- 모든 기능 제한 없음 (CLI, 리포트, 인증서 포함)
- 테스트 시간 제한 없음
- 시작 지연 없음

---

## 핵심 차이 요약

### occt-native이 실제 OCCT보다 우수한 점
1. **macOS/Apple Silicon 지원** - 실제 OCCT는 macOS 미지원
2. **NEON SIMD 구현** - ARM 네이티브 스트레스 테스트
3. **무료 & 제한 없음** - 실제 OCCT는 Free 버전에 1시간 제한 + 10초 지연
4. **CLI 모드 무료** - 실제 OCCT는 Enterprise에서만
5. **HTML/JSON 리포트 무료** - 실제 OCCT는 Enterprise에서만

### 실제 OCCT가 우수한 점
1. **HwInfo 통합** - 200+ 센서 접근 (우리는 자체 구현으로 제한적)
2. **Unreal Engine 기반 GPU Adaptive** - 실제 게임 워크로드 시뮬레이션
3. **온라인 벤치마크 데이터베이스** - 커뮤니티 비교
4. **온라인 인증서 공유** - ocbase.com URL
5. **동시 테스트 실행** - CPU+RAM+GPU 병렬
6. **리포트 비교 도구** - 두 리포트 자동 비교
7. **커스텀 UI 스킨** - LTT, Corsair 파트너십
8. **코일 울림 감지** - v15 실험적 기능
9. **원격 진단 도구** - Enterprise+
10. **20년 개발 이력** - 안정성과 최적화

### 향후 우선 개선 사항
1. **동시 테스트 실행** - 가장 큰 기능 차이
2. **센서 커버리지 강화** - AMD ADL 구현 완성, macOS SMC 직접 접근
3. **Normal/Extreme 모드** + **Small/Medium/Large 데이터셋** 추가
4. **테스트 후 그래프 자동 저장** - 날짜별 구조
5. **센서별 커스텀 알람** - 임계값 설정 UI
