# SQLite 앰알가메이션 (외부 코드 — 수정하지 말 것)

`sqlite3.c` / `sqlite3.h` 는 **SQLite 3.47.1** 원본이다.

## 출처

언리얼 엔진 5.8 이 동봉한 사본을 그대로 복사했다.

| 이 저장소 | 원본 |
|---|---|
| `sqlite3.c` | `Engine/Plugins/Runtime/Database/SQLiteCore/Source/SQLiteCore/Private/sqlite/sqlite3.inl` |
| `sqlite3.h` | `Engine/Plugins/Runtime/Database/SQLiteCore/Source/ThirdParty/sqlite/sqlite3.h` |

원본의 확장자가 `.inl` 인 이유는 언리얼 빌드툴이 `.c` 를 자동으로 잡아 컴파일하지 않게 하려는 것뿐이고,
내용은 sqlite.org 가 배포하는 앰알가메이션과 동일하다.

## 왜 엔진 경로를 참조하지 않고 복사했는가

언리얼 설치 경로가 PC 마다 다르다 (실제로 이 프로젝트의 `.slnx` 에도 `F:` 와 `C:` 가 섞여 나타난다).
채팅 서버는 언리얼 없이도 단독으로 빌드되어야 하므로 저장소 안에 두는 편이 낫다.

## 라이선스

퍼블릭 도메인이다. 저작권 표시나 라이선스 파일 동봉 의무가 없다.
<https://www.sqlite.org/copyright.html>

## 갱신 방법

버전을 올릴 일이 생기면 sqlite.org 의 amalgamation zip 을 받아 두 파일만 교체하면 된다.
로컬 수정은 하지 않았으므로 그냥 덮어쓰면 된다.
