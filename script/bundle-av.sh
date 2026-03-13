#!/usr/bin/env bash
# ─── av 바이너리 번들 스크립트 ────────────────────────────────────────────────────
# 용도: bin/av와 모든 동적 라이브러리 + ld-linux를 하나의 디렉토리로 묶는다.
#       CentOS 6.x 등 오래된 glibc 환경에서도 실행 가능.
#       sudo 불필요.
#
# 사용법 (빌드 머신에서):
#   bash script/bundle-av.sh
#
# 결과:
#   bin/av-bundle/av          ← 래퍼 스크립트 (이것을 실행)
#   bin/av-bundle/av.bin      ← 실제 바이너리
#   bin/av-bundle/lib/        ← 번들된 라이브러리 + ld-linux
#
# 다른 머신에서 실행:
#   /path/to/av-bundle/av image.png
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BINARY="$PROJECT_ROOT/bin/av"
BUNDLE_DIR="$PROJECT_ROOT/bin/av-bundle"

if [[ ! -f "$BINARY" ]]; then
    echo "오류: $BINARY 를 찾을 수 없습니다. 먼저 빌드하세요."
    exit 1
fi

echo "==> av 번들 생성 시작"

# 클린 시작
rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR/lib"

# 바이너리 복사
cp "$BINARY" "$BUNDLE_DIR/av.bin"
chmod +x "$BUNDLE_DIR/av.bin"

# 동적 링커 (ld-linux) 찾기
INTERP=$(readelf -l "$BINARY" 2>/dev/null | grep "interpreter:" | sed 's/.*: \(.*\)\]/\1/')
if [[ -z "$INTERP" ]]; then
    echo "오류: 동적 링커를 찾을 수 없습니다."
    exit 1
fi
echo "    동적 링커: $INTERP"
cp "$INTERP" "$BUNDLE_DIR/lib/"
INTERP_NAME=$(basename "$INTERP")

# 모든 동적 라이브러리 복사
echo "    라이브러리 복사 중..."
ldd "$BINARY" 2>/dev/null | awk '/=>/ && /\// {print $3}' | while IFS= read -r lib_path; do
    if [[ -n "$lib_path" && -f "$lib_path" ]]; then
        lib_name=$(basename "$lib_path")
        cp "$lib_path" "$BUNDLE_DIR/lib/$lib_name"
        echo "      $lib_name"
    fi
done

# 래퍼 스크립트 생성
cat > "$BUNDLE_DIR/av" << 'WRAPPER'
#!/bin/bash
# av launcher — 번들된 라이브러리와 동적 링커를 사용하여 실행
SELF_DIR="$(cd "$(dirname "$(readlink -f "$0" 2>/dev/null || echo "$0")")" && pwd)"
exec "$SELF_DIR/lib/ld-linux-x86-64.so.2" \
    --library-path "$SELF_DIR/lib" \
    "$SELF_DIR/av.bin" "$@"
WRAPPER
chmod +x "$BUNDLE_DIR/av"

# 결과 확인
echo ""
echo "==> 번들 생성 완료!"
echo "    위치: $BUNDLE_DIR/"
echo ""
ls -lh "$BUNDLE_DIR/"
echo ""
echo "── lib/ 내용:"
ls -lh "$BUNDLE_DIR/lib/" | head -20
echo ""
TOTAL=$(du -sh "$BUNDLE_DIR" | cut -f1)
echo "    총 크기: $TOTAL"
echo ""
echo "── 사용법:"
echo "    $BUNDLE_DIR/av image.png"
echo ""
echo "── 다른 머신으로 복사:"
echo "    tar czf av-bundle.tar.gz -C $PROJECT_ROOT/bin av-bundle"
echo "    scp av-bundle.tar.gz user@target:~/"
echo "    # target에서: tar xzf av-bundle.tar.gz && av-bundle/av image.png"
