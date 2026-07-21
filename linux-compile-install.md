# av — Mac → Linux 복사 · 컴파일 · 설치 런북

> 목적: 맥에서 코드를 수정한 뒤 리눅스 빌드 머신(`alexws`, 192.168.2.2)으로 옮겨
> 오프라인 컴파일하고 번들로 설치하는 전 과정을 재현 가능하게 기록.
> (최초 검증: 2026-07-21, av `v0.22-29-g385badc` 설치 성공)

---

## 0. 대상 환경 (2026-07-21 확인)

| 항목 | 값 |
|---|---|
| 호스트 | `192.168.2.2` (hostname `alexws`), Ubuntu, Linux 6.8, x86_64 |
| 사용자 / 홈 | `alex` / **`/user/alex`** (표준 `/home` 아님 — 경로 하드코딩 주의) |
| 소스 경로 | `/user/alex/claude_code/av` |
| **설치 경로(번들)** | **`/user/alex/local/bin/av-bundle/av`** — `av` 는 래퍼 스크립트 |
| 인터넷 | **없음 → 오프라인 빌드 필수** |
| 툴체인 | cmake 3.28.3, ninja 1.11.1, gcc/g++ 13.2.0, 8코어 |
| 의존성 | `build/_deps/`(sdl3·glad·imgui·stb) 전개됨 + `av-deps.tar.gz`(88MB) 백업 |
| **원격 로그인 셸** | **csh/tcsh** → ssh 원격 명령은 반드시 `bash -l` 로 감쌀 것 (아래 함정 참조) |

---

## 1. 사전 준비 (최초 1회): 무비번 SSH

맥 `~/.ssh/config` 의 `Host 192.168.2.2` 블록:
```
Host 192.168.2.2
    HostName 192.168.2.2
    User alex
    RemoteForward 9000 127.0.0.1:44554
    IdentityFile ~/.ssh/id_ed25519_av     # 암호 없는 전용 키
    IdentitiesOnly yes                     # 이 키만 사용 (암호 걸린 기본키 회피)
```
서버에 공개키 등록(서버 비번 1회):
```bash
ssh-copy-id -o PubkeyAuthentication=no -i ~/.ssh/id_ed25519_av.pub 192.168.2.2
```
확인: `ssh 192.168.2.2 true` 가 무프롬프트면 완료.

> `Warning: remote port forwarding failed for listen port 9000` 는 RemoteForward 포트가
> 이미 물려 있을 때 뜨는 **무해한 경고**. 빌드용 ssh 엔 `-o ClearAllForwardings=yes` 로 끈다.

---

## 2. 맥에서 소스 동기화

```bash
bash script/sync-linux.sh
```
- `git describe` 로 `VERSION.txt` / `VERSION_DATE.txt` 생성 → 함께 전송(오프라인 GenVersion 용).
- `rsync -avz --delete`, 제외: `.git/ build/ bin/ test/ deps* .planning/ .claude/`.
- 대상: `192.168.2.2:/user/alex/claude_code/av/` (env `AV_SYNC_REMOTE` 로 변경 가능).
- 완료 후 맥의 VERSION 파일은 자동 삭제(깃이 진실의 원천).

---

## 3. 원격 오프라인 빌드

```bash
ssh -o ClearAllForwardings=yes 192.168.2.2 'bash -l' <<'EOF'
set -eo pipefail
cd /user/alex/claude_code/av
# 스테일 .git 이 git describe 로 옛 버전을 씌우지 않게 제거 → VERSION.txt 권위화
[ -d .git ] && rm -rf .git || true
bash script/build-offline.sh          # FETCHCONTENT_FULLY_DISCONNECTED=ON, _deps 재사용, 네트워크 미사용
./bin/av --version                    # 기대: av v0.22-XX-gXXXXXXX (updated YYYY-MM-DD)
EOF
```
- `build/_deps` 가 이미 있으면 증분 컴파일(변경 파일만). 캐시가 다른 머신 것이면 build-offline.sh 가 감지해 재configure.
- 완전 초기화가 필요하면: `bash script/build-offline.sh clean` (deps 는 `av-deps.tar.gz` 에서 재전개).

---

## 4. 번들 생성 + 설치

```bash
ssh -o ClearAllForwardings=yes 192.168.2.2 'bash -l' <<'EOF'
set -eo pipefail
cd /user/alex/claude_code/av
bash script/bundle-av.sh                                   # bin/av-bundle/ 생성: av(래퍼)+av.bin+lib/+ld-linux
mkdir -p /user/alex/local/bin
rsync -a --delete bin/av-bundle/ /user/alex/local/bin/av-bundle/   # 디렉토리 통째로 배포(단일 파일 X)
/user/alex/local/bin/av-bundle/av --version                # 설치본 검증
EOF
```
- `av` 래퍼는 `readlink -f $0` 로 자기 위치를 찾아 `그 폴더/lib/ld-linux-x86-64.so.2 --library-path 그 폴더/lib av.bin` 를
  실행 → **디렉토리째 옮기면 어디서든 동작**. 반드시 `av-bundle/` 전체를 배포한다.

---

## 5. 검증 체크리스트

```bash
ssh -o ClearAllForwardings=yes 192.168.2.2 'bash -l' <<'EOF'
/user/alex/local/bin/av-bundle/av --version                       # 맥 현재 버전과 일치?
timeout 10 /user/alex/local/bin/av-bundle/av --help | grep -i pair # 신규 기능 존재?
ldd /user/alex/local/bin/av-bundle/av.bin | grep -i "not found"    # 출력 없어야 정상
EOF
```

---

## 6. 함정 / 주의사항 (실전 기록)

- **원격 셸이 tcsh** → `ssh host '... 2>&1 ...'` 처럼 직접 명령을 주면 `Ambiguous output redirect` 발생.
  **항상** `ssh host 'bash -l' <<'EOF' ... EOF` (또는 `ssh host bash -lc '...'`) 로 bash 를 명시.
  `bash -l` 은 로그인 프로필을 읽어 cmake/ninja PATH 도 잡아줌.
- **오프라인** → 온라인용 `build-linux.sh` 말고 `build-offline.sh`. 후자가 `FETCHCONTENT_FULLY_DISCONNECTED=ON`
  으로 네트워크를 아예 안 탄다. (`_deps` 만 있으면 `ninja -C build` 증분도 가능.)
- **스테일 .git**: 원격에 오래된 `.git` 이 있으면 `git describe` 가 옛 태그로 버전을 씌운다 → 빌드 전 `rm -rf .git`.
- **폰트 경로**: 바이너리에 `IMGUI_FONT_DIR=build/_deps/imgui-src/misc/fonts` **절대경로가 컴파일 타임에 박힌다**.
  `build/` 트리를 지우면 폰트가 깨지므로 빌드 트리를 보존한다.
- **설치는 "번들 디렉토리"**: `av` 는 래퍼 스크립트이므로 `av` 파일 하나만 복사하면 안 됨. `av-bundle/` 전체를 배포.
- **HOME=/user/alex** (비표준). 스크립트/경로에서 `~` 대신 절대경로가 안전.
- **RemoteForward 9000 경고**: 무해. 빌드 ssh 엔 `-o ClearAllForwardings=yes`.

---

## 7. 원클릭 요약

```bash
# ── 맥 ──
bash script/sync-linux.sh

# ── 원격 (빌드→설치, 한 번에) ──
ssh -o ClearAllForwardings=yes 192.168.2.2 'bash -l' <<'EOF'
set -eo pipefail
cd /user/alex/claude_code/av
[ -d .git ] && rm -rf .git || true
bash script/build-offline.sh
bash script/bundle-av.sh
rsync -a --delete bin/av-bundle/ /user/alex/local/bin/av-bundle/
/user/alex/local/bin/av-bundle/av --version
EOF
```
