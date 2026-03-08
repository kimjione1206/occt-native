# OCCT-Native 호환성 수정 계획서

## 개요
CLI / UI / Engine 간 호환성 분석 결과, 총 15건의 문제를 3단계로 나눠 수정합니다.

---

## Phase 1: Quick Fixes (단순 수정 7건)

### 1-1. RAM 패널 enum 매핑 오류 (CRITICAL)
- **파일**: `src/gui/panels/ram_panel.cpp`
- **문제**: UI 콤보 5개 항목, 엔진 enum 6개 → `WALKING_ZEROS` 누락
- **수정**: 콤보 항목 6개로 확장 + switch/case 매핑 업데이트
- **복잡도**: Simple

### 1-2. CPU ALL 모드 CLI 추가
- **파일**: `src/cli/cli_runner.cpp`
- **문제**: `CpuStressMode::ALL` 매핑 없음
- **수정**: `else if (opts.mode == "all") mode = CpuStressMode::ALL;` 1줄 추가
- **복잡도**: Simple

### 1-3. CPU LoadPattern CLI 노출
- **파일**: `cli_args.h` (+1 필드), `cli_args.cpp` (파서), `cli_runner.cpp`
- **문제**: VARIABLE 패턴 접근 불가, 항상 STEADY 기본값
- **수정**: `--load-pattern <steady|variable>` 플래그 추가, `engine.start()` 4번째 인자 전달
- **복잡도**: Simple

### 1-4. RAM WALKING_ZEROS CLI 추가
- **파일**: `src/cli/cli_runner.cpp`
- **문제**: `WALKING_ZEROS` 패턴 CLI에서 접근 불가
- **수정**: `else if (opts.mode == "walking_zeros") pattern = RamPattern::WALKING_ZEROS;` 추가
- **복잡도**: Simple

### 1-5. RAM passes 파라미터 CLI 노출
- **파일**: `cli_args.h` (+1 필드), `cli_args.cpp`, `cli_runner.cpp`
- **문제**: `passes` 하드코딩 1
- **수정**: `--passes <N>` 플래그 추가, `engine.start(pattern, mem_pct, opts.passes)` 전달
- **복잡도**: Simple

### 1-6. Storage 파라미터 CLI 노출
- **파일**: `cli_args.h` (+3 필드), `cli_args.cpp`, `cli_runner.cpp`
- **문제**: `file_size_mb`, `queue_depth`, `path` 전부 하드코딩
- **수정**: `--file-size`, `--queue-depth`, `--storage-path` 플래그 추가
- **복잡도**: Simple

### 1-7. CPU 에러 검출 수정
- **파일**: `src/cli/cli_runner.cpp`
- **문제**: CPU 테스트 항상 `passed=true` 반환, `error_count` 무시
- **수정**: `result.passed = (metrics.error_count == 0);` 으로 변경
- **복잡도**: Simple

---

## Phase 2: Engine Integration (엔진 연동 4건)

### 2-1. GPU CLI 구현 (NEW)
- **파일**: `cli_args.h` (+3 필드), `cli_args.cpp`, `cli_runner.cpp`
- **문제**: GPU 엔진 8개 모드 전부 CLI 접근 불가
- **수정 내용**:
  - `cli_args.h`: `gpu_index`, `shader_complexity`, `adaptive_mode` 필드 추가
  - `cli_args.cpp`: `--gpu-index`, `--shader-complexity`, `--adaptive-mode` 파서 추가
  - `cli_runner.cpp`: `#include "engines/gpu_engine.h"` + `else if (opts.test == "gpu")` 분기 추가
    - 8개 모드 문자열→enum 매핑
    - Vulkan 모드 시 shader_complexity, adaptive_mode 설정
    - 메트릭 콜백 (gflops, temp, gpu_usage, vram_usage, fps, artifacts)
    - 에러 판정: `passed = (vram_errors == 0 && artifact_count == 0)`
- **참조**: `src/gui/panels/gpu_panel.cpp` lines 280-308
- **복잡도**: Complex (~80줄)

### 2-2. PSU CLI 구현 (NEW)
- **파일**: `cli_args.h` (+1 필드), `cli_args.cpp`, `cli_runner.cpp`
- **문제**: PSU 엔진 3개 패턴 전부 CLI 접근 불가 (헬프텍스트에만 존재)
- **수정 내용**:
  - `cli_args.h`: `use_all_gpus` 플래그 추가
  - `cli_runner.cpp`: `#include "engines/psu_engine.h"` + `else if (opts.test == "psu")` 분기
    - 3개 패턴 매핑 (steady/spike/ramp)
    - `engine.set_use_all_gpus()` 호출
    - 에러 판정: `passed = (errors_cpu == 0 && errors_gpu == 0)`
- **복잡도**: Medium (~50줄)

### 2-3. Benchmark CLI 구현 (NEW)
- **파일**: `cli_runner.cpp`
- **문제**: CacheBenchmark, MemoryBenchmark 전부 CLI 접근 불가
- **수정 내용**:
  - `else if (opts.test == "benchmark")` 분기 추가
  - 모드: `cache`, `memory`, `all` (기본값)
  - `bench.run()` 동기 호출 (wait loop 불필요)
  - 결과 JSON: latency, bandwidth 값 출력
- **복잡도**: Medium (~40줄)

### 2-4. PSU 패널 에러 메트릭 표시
- **파일**: `src/gui/panels/psu_panel.h` (+2 멤버), `psu_panel.cpp`
- **문제**: `PsuMetrics.errors_cpu`, `errors_gpu` UI 미표시
- **수정**: 에러 카운트 라벨 2개 추가 + `updateMonitoring()`에서 업데이트
- **복잡도**: Simple

---

## Phase 3: Advanced Features (고급 기능 4건)

### 3-1. Schedule CLI 구현
- **파일**: `cli/CMakeLists.txt`, `cli_runner.h` (+1 메서드), `cli_runner.cpp`
- **문제**: `--schedule` 파싱만, 핸들러 없음 (dead code)
- **수정 내용**:
  - CMakeLists: `occt_scheduler` 링크 추가
  - `run_schedule()` 메서드 신규:
    - `TestScheduler::load_from_json()` → `start()` → 진행 콜백 → 결과 수집
  - `--stop-on-error` 플래그 추가
- **복잡도**: Medium (~60줄)

### 3-2. Certificate CLI 구현
- **파일**: `cli/CMakeLists.txt`, `cli_args.h/cpp`, `cli_runner.h/cpp`
- **문제**: 4티어 인증서 기능 CLI 전무
- **수정 내용**:
  - `--cert-tier <bronze|silver|gold|platinum>` 플래그
  - `run_certificate()` 메서드: 프리셋 스케줄 로드 → 실행 → 인증서 생성
  - 출력: HTML/PNG/JSON 인증서
- **의존성**: Fix 3-1 (Schedule) 선행 필요
- **복잡도**: Complex (~80줄)

### 3-3. Storage 블록사이즈 / Direct I/O 연동
- **파일**: `storage_engine.h/cpp`, `storage_panel.cpp`
- **문제**: UI에 블록사이즈 콤보, Direct I/O 체크박스 존재하나 엔진에 미전달
- **수정 내용**:
  - `StorageEngine::start()` 시그니처에 `block_size` 추가
  - `set_direct_io(bool)` 세터 추가
  - UI에서 값 전달
- **복잡도**: Medium (엔진 API 변경)

### 3-4. Dashboard 하드코딩 정보 동적화
- **파일**: `src/gui/panels/dashboard_panel.cpp`
- **문제**: GPU "OpenCL Device", RAM "System Memory" 하드코딩
- **수정**: `collect_system_info()` 호출하여 실제 하드웨어 정보 표시
- **참조**: `sysinfo_panel.cpp` 동일 패턴 이미 사용 중
- **복잡도**: Simple

---

## 수정 영향 요약

| 파일 | Phase 1 | Phase 2 | Phase 3 | 예상 변경 |
|------|---------|---------|---------|----------|
| `cli_args.h` | 3건 | 2건 | 1건 | +10 필드 |
| `cli_args.cpp` | 3건 | 2건 | 1건 | +70줄 |
| `cli_runner.h` | - | - | 2건 | +2 메서드 |
| `cli_runner.cpp` | 4건 | 3건 | 2건 | +300줄 |
| `cli/CMakeLists.txt` | - | - | 2건 | +2 라이브러리 |
| `ram_panel.cpp` | 1건 | - | - | ~10줄 |
| `psu_panel.h/cpp` | - | 1건 | - | ~15줄 |
| `storage_engine.h/cpp` | - | - | 1건 | ~20줄 |
| `storage_panel.cpp` | - | - | 1건 | ~5줄 |
| `dashboard_panel.cpp` | - | - | 1건 | ~10줄 |

## 총 예상 변경량
- **Phase 1**: ~30줄 (30분)
- **Phase 2**: ~200줄 (1-2시간)
- **Phase 3**: ~250줄 (2-3시간)
- **합계**: ~480줄

## 실행 순서
```
Phase 1 (독립적, 병렬 가능)
  ├── 1-1: RAM 패널 enum (UI)
  ├── 1-2: CPU ALL 모드 (CLI)
  ├── 1-3: CPU LoadPattern (CLI)
  ├── 1-4: RAM WALKING_ZEROS (CLI)
  ├── 1-5: RAM passes (CLI)
  ├── 1-6: Storage 파라미터 (CLI)
  └── 1-7: CPU 에러검출 (CLI)

Phase 2 (Phase 1 완료 후)
  ├── 2-1: GPU CLI (독립)
  ├── 2-2: PSU CLI (독립)
  ├── 2-3: Benchmark CLI (독립)
  └── 2-4: PSU 에러표시 (UI, 독립)

Phase 3 (Phase 2 완료 후)
  ├── 3-1: Schedule CLI (독립)
  ├── 3-2: Certificate CLI (3-1 선행)
  ├── 3-3: Storage 블록사이즈 (독립)
  └── 3-4: Dashboard 동적화 (독립)
```
