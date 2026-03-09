---
name: occt-dev
description: OCCT Native 프로젝트 개발 규칙. 코드 수정, 기능 추가, 버그 수정 등 모든 개발 작업 시 자동 적용.
allowed-tools: Read, Grep, Glob, Bash, Edit, Write
argument-hint: [작업 내용]
---

# OCCT Native 개발 규칙

## 현재 코드 구조 (자동 주입)
!`cat docs/CODE_STRUCTURE.md`

## 현재 변경 상태
!`git diff --stat 2>/dev/null || echo "git 미초기화"`

---

## 작업: $ARGUMENTS

위 코드 구조를 참고하여 작업을 수행하세요. ultrathink 모드로 영향 범위를 분석한 후 진행하세요.

## 핵심 원칙

| 원칙 | 내용 |
|------|------|
| **Windows 우선** | 주 타겟. macOS/Linux는 `#ifdef Q_OS_WIN/MACOS/LINUX` 분기 |
| **오류 검출 목적** | 단순 벤치마크 X → CPU: FMA 비트비교, GPU: Artifact, RAM: 패턴비교, Storage: CRC32C |
| **CI가 빌드/테스트** | GitHub 푸시 → Actions가 자동 빌드+46개 테스트 → Artifact로 실행파일 다운로드 |

## 수정 규칙

자세한 체크리스트는 [checklist.md](checklist.md)를 참조하세요.

**모드/기능 추가 시 반드시 5곳 수정:**
1. **enum** — `src/engines/<type>_engine.h`
2. **CLI** — `src/cli/cli_args.cpp`
3. **GUI** — `src/gui/panels/<type>_panel.cpp`
4. **스케줄러** — `src/scheduler/test_scheduler.cpp`
5. **프리셋** — `src/scheduler/preset_schedules.cpp`

## 작업 완료 시 (필수)

1. 파일 추가/삭제/구조 변경 → `docs/CODE_STRUCTURE.md` 업데이트
2. CI yml과의 정합성 확인
