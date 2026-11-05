# d3d9_probe — D3D9 API 표면 실측 프록시

아즈로크가 **실제로 호출하는** D3D9 메서드와 횟수를 재는 도구.
웹 이식 타당성 판단(게이트 1)의 근거를 만든다: "구현해야 할 메서드가 몇 개인가".

## 왜 이 방식인가

`acknex.dll` 은 `d3d9.dll` 에서 **`Direct3DCreate9` 하나만** 정적 임포트한다(임포트 1건).
그 뒤는 전부 반환된 COM 인터페이스의 vtable 호출이다.
→ `d3d9.dll` 을 갈아끼우면 렌더링 전체를 가로챌 수 있다.

객체를 **감싸지 않고 vtable 을 제자리에서 패치**한다. vtable 은 클래스 단위로 공유되므로
한 번 패치하면 모든 인스턴스에 적용되고, COM 수명주기(QueryInterface/AddRef/Release)를
흉내낼 필요가 없다 — 래핑 방식의 가장 흔한 버그원을 통째로 회피한다.

각 슬롯은 32바이트 썽크로 대체된다:
```
pushfd / pushad / push <id> / call LogCall / add esp,4 / popad / popfd / jmp [원본]
```
`jmp` 시점의 스택·레지스터가 진입 시와 **완전히 동일**하므로 thiscall/stdcall 어느 규약이든
인자 전달이 깨지지 않는다. `CreateDevice`(IDirect3D9 슬롯 16)만 전용 훅으로 처리해
반환된 디바이스의 vtable 도 잡는다.

계측 대상: `IDirect3D9` 17개 + `IDirect3DDevice9` 119개 = **136개 전부**.

## 빌드

```
tools\d3d9_probe\build.bat
```
VS BuildTools 의 **vcvars32**(32비트 — `Azrok.exe` 가 32비트다) 를 쓴다.
소스가 UTF-8 한글 주석을 포함하므로 `/utf-8` 필수(없으면 CP949 로 읽어 파싱이 깨진다).

산출: `d3d9.dll` (126,464 B, PE machine `0x014c`=x86, export = `Direct3DCreate9` 1개)

## 사용

1. `d3d9.dll` 을 `Azrok.exe` 와 **같은 폴더**에 둔다.
   Windows 는 실행파일 폴더를 시스템 폴더보다 먼저 탐색하므로 자동으로 이게 로드된다.
2. **게임을 새로 띄운다.** 이미 떠 있는 프로세스는 진짜 `d3d9.dll` 을 이미 물고 있어 효과가 없다.
3. 한 세션을 완주한다: 로그인 → PatHouse → Plaza → OX → 요리
4. 게임을 종료한다 — **정상 종료가 아니어도 데이터가 남는다** (아래 "주기적 flush" 참고).
5. `Azrok.exe` 옆에 `d3d9_probe.log` 생성:
   - 첫 줄: `# flush: frame=N status=...` — 이 로그가 몇 프레임까지 살아있었는지 /
     정상 종료로 완결된 데이터인지 표시
   - 호출된 메서드 · 슬롯 인덱스 · 호출 횟수
   - **미호출 목록** (= 구현 불필요 목록)
   - 요약: 고유 메서드 N개 / 총 호출 M회
   - 인자 분포 (SetRenderState 등) — 이 섹션까지 포함해 **기존 계측 항목의 출력 형식은
     그대로**다. 이전 로그와 줄 단위로 대조 가능하다(첫 줄만 새로 추가됨).

제거: `d3d9.dll` 만 지우면 원상복구.

## 주기적 flush (비정상 종료 대비)

2026-07-19, 확장 인자 프로브 세션에서 **정상 detach 실패로 세션 하나 분량이 통째로 날아간
사고**가 있었다(`https://github.com/onpe5679/azrok-server/docs/research/gate1/148_GATE1_ARGPROBE_LOAD_FAILURE_20260719.md`). 원인: 로그를
`DLL_PROCESS_DETACH` 에서만 썼기 때문. 이를 막기 위해 `Present` 훅에 프레임 카운터를 붙였다.

- **트리거**: `Present` 는 세션당 약 2,000회 호출된다. `FLUSH_INTERVAL_FRAMES`(기본 600,
  ~60fps 기준 약 10초) 배수째 프레임마다 `d3d9_probe.log` 를 **통째로 다시 쓴다**(덮어쓰기 —
  파일이 계속 커지지 않고, 항상 "마지막 flush 시점까지의" 완전한 스냅샷 하나만 존재).
  세션당 flush 는 3~4회뿐이라 비용은 무시할 만하다.
- **정상 종료와의 차이**: 기존 `DLL_PROCESS_DETACH` 덤프는 그대로 남아 있다 — 게임을 정상
  종료하면 마지막에 완전한 최종 데이터로 다시 한번 덮어써진다. 파일 첫 줄로 구분한다:
  - `status=clean-exit(complete)` — 정상 종료, 이 로그가 최종/완전 데이터
  - `status=partial(mid-session, will be overwritten by next flush)` — 세션 도중의
    주기적 flush. 이 상태로 로그가 멈춰 있다면 그 이후 강제종료/크래시가 났다는 뜻이고,
    `frame=N` 이 어디까지 살아있었는지 알려준다.
- **핫패스 영향**: 카운터 증가(`InterlockedIncrement`)는 락 없이 처리되어 나머지 ~1,996회의
  `Present` 호출은 정수 연산 하나만 추가로 탄다. 실제 파일 I/O(덤프)는 전용 임계구역
  (`g_flushcs`)에서만 수행되고, 이는 상태/드로우 인자 기록용 핫패스 락(`g_argcs`)과
  분리되어 있어 `SetRenderState` 등 프레임당 수백~수천회 호출되는 경로는 이 락을 기다리지
  않는다. 덤프 자체(테이블 정렬 + 파일 쓰기)가 걸리는 동안에는 그 프레임의 `Present` 반환이
  지연되므로 관측 시 짧은 끊김이 있을 수 있으나, 세션당 3~4회로 한정된다.
- **잔여 위험**: 만약 D3D9 메서드가 렌더 스레드가 아닌 다른 스레드에서도 호출된다면(이 엔진은
  지금까지 단일 렌더 스레드로 관측됨), 주기적 flush 도중 인자 조합 테이블(`ComboRecord`)에
  대한 스냅샷이 그 순간 진행 중인 갱신과 겹쳐 극히 드물게 부정확할 수 있다 — 단 이는 로그
  품질 저하일 뿐 크래시나 메모리 손상으로 이어지지 않으며, 최종 `DLL_PROCESS_DETACH` 덤프는
  이 위험과 무관하다(그 시점엔 다른 스레드가 이미 정리된 상태).

## ⚠ 주의 — 이 폴더에 원래 dgVoodoo2 가 있었다

2026-03-29 부터 게임 폴더에 **dgVoodoo2 의 `D3D9.dll`(483,840 B)** 이 있었고,
2026-07-19 게이트 1 투입 때 이 프록시가 그것을 덮었다. 원본은
`dgVoodoo2_86_5/MS/x86/D3D9.dll` 에 그대로 있으므로 언제든 복구 가능하다.

**사용자 확인: dgVoodoo2 없이도 게임은 정상 동작한다(2026-07-19).**
따라서 현재 체인 `acknex.dll → 이 프록시 → System32\d3d9.dll` 로 진행해도 무방하다.

만약 나중에 dgVoodoo 경유가 필요해지면 `Direct3DCreate9()` 안의
`GetSystemDirectoryA` 로드 대상을 `dgVoodoo2_86_5\MS\x86\D3D9.dll` 로 바꾸면 체이닝된다.

## ⚠ 주의 — 계측은 관측 대상을 망가뜨릴 수 있다

2026-07-18 하루에 계측 때문에 세 번 깨졌다:
1. **Frida** → 오븐 클릭 접근위반 크래시. WER 모듈 목록에 `frida-agent.dll` 기록. 제거하니 정상
2. **UI 워킹 자동화** → 목표를 욕실로 오인, 시간 낭비. 사용자 수동 클릭으로 돌파
3. **x32dbg + Automate ZMQ** → GPU TLS 예외 뒤 격리 클라이언트 2회 종료

→ **이 프록시로 크래시·이상이 나면 프록시를 먼저 의심하라.** `d3d9.dll` 을 지우고 재현되는지
확인하는 게 1번 절차다. 게임 로직·패킷 경로는 안 건드리므로 상태가 오염되지는 않지만,
렌더링 경로에는 확실히 개입한다.

## ⚠ 한계 — Wine/BoxedWine 에서는 쓸 수 없다 (VERIFIED 2026-07-19, 문서 139)

이 프록시는 **Windows 전용**이다. Wine 안에서는 체이닝이 성립하지 않는다.

`Direct3DCreate9()` 안에서 `GetSystemDirectoryA` + `LoadLibraryA("d3d9.dll")` 로 진짜
d3d9 를 불러 넘기는데, Wine 에서 프록시를 태우려면 `WINEDLLOVERRIDES=d3d9=n` 이 필요하고,
**그 override 는 우리 프록시 안의 두 번째 `LoadLibrary` 에도 똑같이 적용된다.**
= native 를 강제한 순간 Wine builtin D3D9 로 갈 길이 끊긴다.

실측: Wine 이 `D:\d3d9.dll` 을 native 모듈로 적재한 로그는 남았으나
`d3d9_probe.log` 도, D3D 디바이스 트레이스도, 서버 접속도 뒤따르지 않았다.

**Wine 에서는 프록시 대신 `WINEDEBUG=+d3d9,+d3d` 를 써라.** 같은 데이터를 얻고
원본도 안 건드린다. 단 로그가 `Present` 당 약 484KB(347 프레임 = 168MB)이므로
**호출 표면 분류에는 유효하지만 성능 측정에는 못 쓴다.**

## 한계 (Windows 에서도)

- `IDirect3D9` / `IDirect3DDevice9` 만 계측한다. 리소스 인터페이스
  (`IDirect3DTexture9`, `IDirect3DVertexBuffer9`, `IDirect3DSurface9` 등)의
  `Lock`/`Unlock` 같은 호출은 **안 잡힌다.** 표면 산정 시 이 점을 감안할 것.
- ~~인자값은 기록하지 않는다(호출 여부·횟수만).~~ **2026-07-19 해소** — `SetRenderState`,
  `SetTextureStageState`, `SetSamplerState`, `SetFVF`, `CreateTexture`, primitive type,
  `SetTransform` 슬롯의 **고유 인자 조합 + 등장 횟수**를 수집한다(위 "사용" 절의 인자 분포).
  다만 **행렬·정점 데이터 같은 벌크 값은 여전히 기록하지 않는다.**
- 멀티스레드 호출은 `InterlockedIncrement` 로 안전하나, 순서 정보는 없다.

## 산출물 3종 (혼동 주의)

| 파일 | 크기 | 내용 |
|---|---:|---|
| `d3d9.dll` | 128,512 B | 게이트 1 원본. 호출 횟수만. **기존 로그와 대조할 때 이것** |
| `d3d9_argprobe.dll` | 131,584 B | + 인자 분포. **정상 종료해야 로그가 남는다** |
| `d3d9_flushprobe.dll` | 빌드 산출물 | + 주기적 flush. **비정상 종료에도 남는다. 현재 권장** |

셋 다 export 는 `Direct3DCreate9` 1개, PE machine `0x014c`(x86)다.
게임 폴더에 넣을 때는 **`d3d9.dll` 이름으로 바꿔서** 넣어야 로드된다.

2026-07-19 캐릭터 선택 화면 부분 로그에서 `SetTransform` 고유 슬롯이 기존
64/64 테이블을 채웠으므로 `XF_CAP`을 512로 늘렸다. 기존 로그의 다른 인자
테이블은 가득 차지 않았으며, 변환 슬롯 64개는 완전 집합이 아닌 하한이다.
